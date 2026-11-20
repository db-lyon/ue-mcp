// Saving the current level, reconciled with editor(save_dirty) (#964).
//
// level(save) used to be ULevelEditorSubsystem::SaveCurrentLevel() and a bare
// boolean: on false it answered {"success": false, "error": "Failed to save
// current level"} and said nothing else. It returned that twice in a row on a
// genuinely dirty map that editor(save_dirty, includeMaps=true) then wrote
// seconds later without complaint, and the engine log showed SavePackage work
// running for the package the whole time.
//
// A false failure is the dangerous half. An agent that believes a save did not
// happen redoes the work that was in fact written, and the second pass can undo
// or duplicate it. So this action now takes the same path save_dirty takes -
// resolve the package's on-disk filename, call UPackage::Save, read the dirty
// flag back - and the two can no longer disagree about the same package.
//
// The second half is the diagnostic. The old error named neither the package it
// tried nor why it stopped, which is why the report could not say whether the
// cause was source control, a read-only file, an I/O error or something that
// was not the save at all. Every package now comes back with its name, its
// resolved file, whether that file exists and is read-only, the
// ESavePackageResult the engine returned, and the error and warning lines the
// save itself emitted.
//
// On a World Partition or one-file-per-actor map the level's own package is
// only part of the answer: the actors live in their own packages and are what
// is usually dirty. Those are saved too, each reported separately, which is
// also why a caller can see that the map package was already clean rather than
// reading that as a failure.
//
// Translation-unit partition of FLevelHandlers - registration lives in
// LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerCheckpoint.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerCommitSave.h"
#include "HandlerLevelSwitch.h"

#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "HAL/FileManager.h"
#include "Misc/OutputDevice.h"
// GError and GLog are declared as pointers in CoreGlobals; forwarding a line to
// either one needs the complete type.
#include "Misc/OutputDeviceError.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	/**
	 * Collects the error and warning lines one SavePackage emits, and passes
	 * them on to the normal log so nothing is swallowed. Passed as
	 * FSavePackageArgs::Error rather than hooked onto GLog, so only this save's
	 * own output is captured and unrelated engine chatter cannot be reported as
	 * the reason a level would not write.
	 */
	class FMCPSavePackageDiagnostics : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const ELogVerbosity::Type Masked =
				static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			// Pass everything on so capturing the save's output does not
			// silence the log. Fatal keeps going to GError, which is where the
			// engine's own error handling lives.
			if (Masked == ELogVerbosity::Fatal)
			{
				if (GError) GError->Serialize(Message, Verbosity, Category);
			}
			else if (GLog)
			{
				GLog->Serialize(Message, Verbosity, Category);
			}
			if (Masked > ELogVerbosity::Warning) return;
			if (Lines.Num() >= 16) return;
			Lines.Add(FString::Printf(TEXT("%s: %s"), *Category.ToString(), Message));
		}

		TArray<TSharedPtr<FJsonValue>> ToJson() const
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const FString& Line : Lines) Out.Add(MakeShared<FJsonValueString>(Line));
			return Out;
		}

		TArray<FString> Lines;
	};

	/** ESavePackageResult as a name a caller can act on. */
	FString MCPDescribeSavePackageResult(ESavePackageResult Result)
	{
		// Deliberately not an exhaustive switch: several enumerators are
		// deprecated and naming them would compile with warnings for no gain.
		switch (Result)
		{
			case ESavePackageResult::Success:                return TEXT("Success");
			case ESavePackageResult::Error:                  return TEXT("Error");
			case ESavePackageResult::Canceled:               return TEXT("Canceled");
			case ESavePackageResult::ContainsEditorOnlyData: return TEXT("ContainsEditorOnlyData");
			case ESavePackageResult::MissingFile:            return TEXT("MissingFile");
			case ESavePackageResult::ValidatorError:         return TEXT("ValidatorError");
			case ESavePackageResult::ValidatorSuppress:      return TEXT("ValidatorSuppress");
			case ESavePackageResult::Timeout:                return TEXT("Timeout");
			default:                                         return TEXT("Other");
		}
	}

	/** Plain-language cause for a failed save, from what can be observed. */
	FString MCPExplainLevelSaveFailure(
		const FString& FileName,
		bool bFileExists,
		bool bReadOnly,
		ESavePackageResult Result)
	{
		if (bReadOnly)
		{
			return FString::Printf(
				TEXT("'%s' is read-only on disk. Under source control this means the file is not checked out: check it out with source_control(checkout), or set source_control(set_checkout_mode, mode=auto). Otherwise clear the read-only attribute."),
				*FileName);
		}
		if (Result == ESavePackageResult::Canceled)
		{
			return TEXT("The save was cancelled, which in the editor usually means a checkout or overwrite prompt was declined.");
		}
		if (Result == ESavePackageResult::ValidatorError || Result == ESavePackageResult::ValidatorSuppress)
		{
			return TEXT("A save validator rejected the package. Its reason is in the captured log lines.");
		}
		if (!bFileExists)
		{
			return FString::Printf(
				TEXT("'%s' does not exist yet and could not be created. Check the directory exists and is writable."),
				*FileName);
		}
		return TEXT("SavePackage reported a failure. The captured log lines carry the engine's own reason.");
	}

	/** Save one package and report exactly what happened to it. */
	TSharedPtr<FJsonObject> MCPSaveLevelPackage(UPackage* Package, bool bForce, bool& bOutFailed, bool& bOutWrote)
	{
		bOutFailed = false;
		bOutWrote = false;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (!Package)
		{
			Entry->SetBoolField(TEXT("saved"), false);
			Entry->SetStringField(TEXT("error"), TEXT("null package"));
			bOutFailed = true;
			return Entry;
		}

		const FString PackageName = Package->GetName();
		Entry->SetStringField(TEXT("package"), PackageName);
		Entry->SetBoolField(TEXT("isMap"), IsMapPackage(Package));
		const bool bWasDirty = Package->IsDirty();
		Entry->SetBoolField(TEXT("wasDirty"), bWasDirty);

		// A clean package is not a failed save. Reporting it as one is how the
		// old boolean turned "nothing to do" into "redo your work".
		if (!bWasDirty && !bForce)
		{
			Entry->SetBoolField(TEXT("saved"), false);
			Entry->SetBoolField(TEXT("skipped"), true);
			Entry->SetStringField(TEXT("reason"), TEXT("already clean; pass force=true to write it anyway"));
			return Entry;
		}

		FString FileName;
		if (!ResolvePackageFileName(Package, FileName))
		{
			Entry->SetBoolField(TEXT("saved"), false);
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("'%s' has no mounted root, so no on-disk filename could be resolved for it."), *PackageName));
			bOutFailed = true;
			return Entry;
		}
		Entry->SetStringField(TEXT("file"), FileName);

		IFileManager& FileManager = IFileManager::Get();
		const bool bFileExists = FileManager.FileExists(*FileName);
		bool bReadOnly = bFileExists && FileManager.IsReadOnly(*FileName);
		if (bReadOnly)
		{
			FString CheckoutNote;
			if (MCPSourceControl::TryAutoCheckout(FileName, CheckoutNote))
			{
				bReadOnly = false;
				Entry->SetBoolField(TEXT("autoCheckedOut"), true);
			}
			else if (!CheckoutNote.IsEmpty())
			{
				Entry->SetStringField(TEXT("checkoutNote"), CheckoutNote);
			}
		}
		Entry->SetBoolField(TEXT("fileExists"), bFileExists);
		Entry->SetBoolField(TEXT("readOnly"), bReadOnly);

		FMCPSavePackageDiagnostics Diagnostics;
		FSavePackageArgs SaveArgs;
		// The same flags editor(save_dirty) passes, so the two paths cannot
		// write the same package differently.
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = &Diagnostics;
		MCPCheckpointRecordBeforeWrite(FileName);
		const FSavePackageResultStruct SaveResult =
			UPackage::Save(Package, nullptr, *FileName, SaveArgs);

		const FString ResultName = MCPDescribeSavePackageResult(SaveResult.Result);
		Entry->SetStringField(TEXT("result"), ResultName);
		Entry->SetNumberField(TEXT("bytesWritten"), static_cast<double>(SaveResult.TotalFileSize));
		// Read the dirty flag back rather than trusting the return value. This
		// is the reconciliation the issue asked for: the response says what the
		// package's state IS, not what the call claimed.
		const bool bStillDirty = Package->IsDirty();
		Entry->SetBoolField(TEXT("dirtyAfter"), bStillDirty);

		// FSavePackageResultStruct::IsSuccessful() exists on every supported
		// engine; the free IsSuccessful(ESavePackageResult) does not.
		const bool bOk = SaveResult.IsSuccessful();
		Entry->SetBoolField(TEXT("saved"), bOk);
		if (Diagnostics.Lines.Num() > 0)
		{
			Entry->SetArrayField(TEXT("log"), Diagnostics.ToJson());
		}
		if (!bOk)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Could not save '%s' to '%s' (%s). %s"),
				*PackageName, *FileName, *ResultName,
				*MCPExplainLevelSaveFailure(FileName, bFileExists, bReadOnly, SaveResult.Result)));
			bOutFailed = true;
			return Entry;
		}

		bOutWrote = true;
		if (bStillDirty)
		{
			// Written, but something dirtied it again during the save. Worth
			// saying: it is not a failure and it is not a clean finish either.
			Entry->SetStringField(TEXT("note"),
				TEXT("The package was written but is dirty again; something modified it during the save."));
		}
		return Entry;
	}
}


// level(save): save the level currently being edited, and say what happened.
TSharedPtr<FJsonValue> FLevelHandlers::SaveLevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	// A save during PIE is one of the ways the old action produced a bare
	// false. Naming it is the difference between "stop play and try again" and
	// an agent redoing work it never lost.
	if (GEditor && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor))
	{
		return MCPError(TEXT("Play In Editor is running; stop it before saving the level. Saving during PIE is what produced an unexplained failure before. ") + MCPStopPlayInEditorStep());
	}

	// Landscape edits land in edit layers and reach the saved heightmaps and weightmaps through a merge that runs on
	// editor ticks. A throttled, backgrounded editor can save before it runs, persisting stale weights (#1239).
	int32 LandscapesMerged = 0;
	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		// From 5.7 every landscape uses edit layers and HasLayersContent is deprecated.
#if !UE_MCP_HAS_5_7_API
		if (!It->HasLayersContent()) continue;
#endif
		It->ForceUpdateLayersContent();
		++LandscapesMerged;
	}

	// commitDeletes: the editor's own save path, which removes the external
	// package of a deleted World Partition actor instead of failing on it.
	if (OptionalBool(Params, TEXT("commitDeletes"), false))
	{
		// This branch writes every dirty package, not only the level's.
		if (auto Refused = MCPRefuseSweepWithUnknownParams(Params, { TEXT("force"), TEXT("includeExternalActors"), TEXT("commitDeletes") }, TEXT("level(save, commitDeletes=true)"))) return Refused;
		TSharedPtr<FJsonObject> Committed = MCPSaveDirtyCommittingDeletes(true, true);
		Committed->SetStringField(TEXT("levelName"), World->GetName());
		Committed->SetStringField(TEXT("levelPath"), World->GetPathName());
		Committed->SetNumberField(TEXT("landscapesMerged"), LandscapesMerged);
		return MCPResult(Committed);
	}

	ULevel* Level = World->GetCurrentLevel();
	if (!Level) Level = World->PersistentLevel;
	if (!Level) return MCPError(TEXT("The editor world has no current level to save"));

	UPackage* LevelPackage = Level->GetOutermost();
	if (!LevelPackage) return MCPError(TEXT("The current level has no package"));

	// A never-saved Untitled map lives under /Temp, which no project file
	// backs: a write there is not a save anyone can reopen (#2007).
	if (MCPLevelSwitch::IsUnsavedMapPackage(World->GetOutermost()->GetName()))
	{
		TSharedPtr<FJsonObject> Refusal = MCPErrorObject(FString::Printf(
			TEXT("'%s' is an untitled map with no file to save to. Save it under a path with level(save_as, levelPath=\"/Game/Maps/MyLevel\"). Nothing was written."),
			*World->GetOutermost()->GetName()));
		Refusal->SetStringField(TEXT("reason"), TEXT("untitled_map"));
		return MCPResult(Refusal);
	}

	const bool bForce = OptionalBool(Params, TEXT("force"), false);
	const bool bIncludeExternalActors = OptionalBool(Params, TEXT("includeExternalActors"), true);

	TArray<UPackage*> Packages;
	Packages.Add(LevelPackage);

	// One-file-per-actor and World Partition maps keep their actors in their own
	// packages. Saving only the map package there writes almost nothing and
	// leaves the actual edits unsaved, which reads as a successful save.
	int32 ExternalConsidered = 0;
	if (bIncludeExternalActors && Level->IsUsingExternalObjects())
	{
		for (UPackage* External : Level->GetLoadedExternalObjectPackages())
		{
			if (!External) continue;
			++ExternalConsidered;
			if (!External->IsDirty() && !bForce) continue;
			Packages.AddUnique(External);
		}
	}

	TArray<TSharedPtr<FJsonValue>> Saved;
	TArray<TSharedPtr<FJsonValue>> Failed;
	TArray<TSharedPtr<FJsonValue>> Skipped;
	TArray<TSharedPtr<FJsonValue>> Deleted;
	MCPCommitEmptyExternalPackages(Packages, Deleted, Failed);
	for (UPackage* Package : Packages)
	{
		bool bFailed = false;
		bool bWrote = false;
		TSharedPtr<FJsonObject> Entry = MCPSaveLevelPackage(Package, bForce, bFailed, bWrote);
		TSharedPtr<FJsonValue> Value = MakeShared<FJsonValueObject>(Entry);
		if (bFailed)      Failed.Add(Value);
		else if (bWrote)  Saved.Add(Value);
		else              Skipped.Add(Value);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("levelName"), World->GetName());
	Result->SetStringField(TEXT("levelPath"), World->GetPathName());
	Result->SetStringField(TEXT("levelPackage"), LevelPackage->GetName());
	Result->SetBoolField(TEXT("usesExternalActors"), Level->IsUsingExternalObjects());
	Result->SetNumberField(TEXT("externalPackagesLoaded"), ExternalConsidered);
	Result->SetNumberField(TEXT("landscapesMerged"), LandscapesMerged);
	Result->SetNumberField(TEXT("savedCount"), Saved.Num());
	Result->SetNumberField(TEXT("failedCount"), Failed.Num());
	Result->SetNumberField(TEXT("skippedCount"), Skipped.Num());
	Result->SetNumberField(TEXT("deletedCount"), Deleted.Num());
	Result->SetArrayField(TEXT("saved"), Saved);
	if (Deleted.Num() > 0) Result->SetArrayField(TEXT("deleted"), Deleted);
	if (Skipped.Num() > 0) Result->SetArrayField(TEXT("skipped"), Skipped);

	// Stated before the failure branch, because a partial save has already
	// overwritten the packages it did write.
	if (Saved.Num() > 0 || Deleted.Num() > 0)
	{
		MCPSetNoRollback(Result,
			TEXT("The level package, and every external actor package that was dirty, were overwritten on disk with the editor's in-memory state, and the packages of deleted actors were removed from it. ")
			TEXT("Undoing that needs the previous file contents back, and the bridge has no action that snapshots or restores a package file; source control is what holds the old revision."));
	}

	if (Failed.Num() > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetArrayField(TEXT("failed"), Failed);
		// The top-level error names the first package that actually failed,
		// rather than the generic sentence the old action returned.
		const TSharedPtr<FJsonObject> First = Failed[0]->AsObject();
		Result->SetStringField(TEXT("error"), First.IsValid()
			? First->GetStringField(TEXT("error"))
			: TEXT("One or more level packages could not be saved"));
		return MCPResult(Result);
	}

	// Nothing failed. Say plainly whether anything needed writing, because
	// "already saved" and "just saved" are both successes and only one of them
	// means work was done.
	Result->SetStringField(TEXT("message"), Saved.Num() + Deleted.Num() > 0
		? FString::Printf(TEXT("Saved %d and deleted %d package(s) for level '%s'"), Saved.Num(), Deleted.Num(), *World->GetName())
		: FString::Printf(TEXT("Level '%s' was already saved; nothing needed writing"), *World->GetName()));
	return MCPResult(Result);
}


// level(save_as): save the open map under a new package path (#2007).
//
// level(save) writes the package the map already has, and a never-saved
// Untitled map has none, so an agent that built a level in one had to reach
// for execute_python and EditorLoadingAndSavingUtils.save_map. This is that
// call, made safe to drive unattended: every case in which the engine's
// SaveWorld would open a message box (a name in use, a path outside the
// content roots, a read-only file) is refused here first, and the engine runs
// as an unattended script so nothing it reaches can raise a modal either.
TSharedPtr<FJsonValue> FLevelHandlers::SaveLevelAs(const TSharedPtr<FJsonObject>& Params)
{
	FString LevelPath;
	if (auto Err = RequireString(Params, TEXT("levelPath"), LevelPath)) return Err;
	REQUIRE_EDITOR_WORLD(World);

	if (MCPLevelSwitch::IsPlaySessionActive())
	{
		return MCPError(FString::Printf(TEXT("A play session (%s) is running, and a map cannot be saved while it is up. %s"),
			*MCPLevelSwitch::PlaySessionMode(), *MCPStopPlayInEditorStep()));
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(LevelPath);
	FText InvalidReason;
	if (!FPackageName::IsValidLongPackageName(PackageName, /*bIncludeReadOnlyRoots*/ false, &InvalidReason) || FPackageName::IsTempPackage(PackageName))
	{
		return MCPError(FString::Printf(TEXT("levelPath '%s' is not a package path a map can be saved to%s%s. Use a mounted content root, e.g. \"/Game/Maps/MyLevel\". Nothing was written."),
			*LevelPath, InvalidReason.IsEmpty() ? TEXT("") : TEXT(": "), *InvalidReason.ToString()));
	}
	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, FPackageName::GetMapPackageExtension()))
	{
		return MCPError(FString::Printf(TEXT("levelPath '%s' is not under a mounted content root. Use one such as \"/Game/Maps/MyLevel\". Nothing was written."), *LevelPath));
	}

	UPackage* PreviousPackage = World->GetOutermost();
	const FString PreviousName = PreviousPackage ? PreviousPackage->GetName() : FString();
	if (PreviousName == PackageName)
	{
		return MCPError(FString::Printf(TEXT("'%s' is already the open map's path. Save it in place with level(save). Nothing was written."), *PackageName));
	}
	if (FPackageName::DoesPackageExist(PackageName))
	{
		return MCPError(FString::Printf(TEXT("An asset already exists at '%s'. Pass another levelPath, or delete that asset first with asset(delete, assetPath=\"%s\"). Nothing was written."),
			*PackageName, *PackageName));
	}
	if (FindPackage(nullptr, *PackageName))
	{
		return MCPError(FString::Printf(TEXT("A package named '%s' is loaded in memory without a file: an unsaved asset, or one deleted this session that the editor still holds. Pass another levelPath. Nothing was written."),
			*PackageName));
	}
	if (IFileManager::Get().FileExists(*Filename) && IFileManager::Get().IsReadOnly(*Filename))
	{
		return MCPError(FString::Printf(TEXT("'%s' is read-only. Make it writable, check it out, or pass another levelPath. Nothing was written."), *Filename));
	}

	const bool bWasUntitled = MCPLevelSwitch::IsUnsavedMapPackage(PreviousName);
	bool bEngineSaved = false;
	{
		TGuardValue<bool> Unattended(GIsRunningUnattendedScript, true);
		bEngineSaved = UEditorLoadingAndSavingUtils::SaveMap(World, PackageName);
	}

	// A map that already has a file is not renamed by the engine: SaveWorld
	// writes a duplicate at the new path, so the objects in the two maps get
	// their own guids, and leaves the original open with its edits still
	// unsaved. Save As means the editor carries on in the copy, so the copy is
	// opened from disk. The original's unsaved edits are in the copy, and its
	// own file keeps its last saved state, so its dirty flags are cleared
	// rather than offered for saving.
	bool bReopened = false;
	if (bEngineSaved && !bWasUntitled && MCPLevelSwitch::OpenMapPackageName() == PreviousName && FPackageName::DoesPackageExist(PackageName))
	{
		ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		if (!LevelEditorSubsystem) return MCPError(TEXT("LevelEditorSubsystem not available"));
		MCPLevelSwitch::DiscardChanges(MCPLevelSwitch::CollectDirtyPackages(World));
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
		MCPLevelSwitch::LoadLevel(*LevelEditorSubsystem, PackageName, /*bDiscardUnsaved*/ true);
		bReopened = true;
	}

	// The open world under the new name, and a file on disk, are the proof.
	// The world is read again rather than trusted from before the save.
	UWorld* Saved = GetEditorWorld();
	const FString OpenMap = MCPLevelSwitch::OpenMapPackageName();
	const bool bFileExists = FPackageName::DoesPackageExist(PackageName);
	if (!bEngineSaved || !Saved || OpenMap != PackageName || !bFileExists)
	{
		TSharedPtr<FJsonObject> Failure = MCPErrorObject(FString::Printf(
			TEXT("Saving the open map as '%s' did not complete: the engine reported %s, the open map is '%s' and the file %s. The output log carries the engine's reason."),
			*PackageName, bEngineSaved ? TEXT("success") : TEXT("failure"), *OpenMap, bFileExists ? TEXT("exists") : TEXT("does not exist")));
		Failure->SetStringField(TEXT("openMap"), OpenMap);
		Failure->SetBoolField(TEXT("fileExists"), bFileExists);
		return MCPResult(Failure);
	}

	// SaveWorld writes the map and the external packages it renamed. Anything
	// of the map still dirty afterwards (an actor package it left alone) is
	// written now, so the map on disk is the map in the editor.
	TArray<UPackage*> StillDirty = MCPLevelSwitch::CollectDirtyPackages(Saved);
	int32 ExtraSaved = 0;
	if (StillDirty.Num() > 0)
	{
		ExtraSaved = StillDirty.Num();
		UEditorLoadingAndSavingUtils::SavePackages(StillDirty, /*bOnlyDirty*/ true);
		StillDirty = MCPLevelSwitch::CollectDirtyPackages(Saved);
		ExtraSaved -= StillDirty.Num();
	}

	ULevel* Persistent = Saved->PersistentLevel;
	const bool bExternal = Persistent && Persistent->IsUsingExternalObjects();
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("levelPath"), PackageName);
	Result->SetStringField(TEXT("previousLevelPath"), PreviousName);
	Result->SetBoolField(TEXT("wasUntitled"), bWasUntitled);
	Result->SetBoolField(TEXT("reopenedFromDisk"), bReopened);
	Result->SetStringField(TEXT("worldName"), Saved->GetName());
	Result->SetStringField(TEXT("worldPath"), Saved->GetPathName());
	Result->SetStringField(TEXT("filename"), FPaths::ConvertRelativePathToFull(Filename));
	Result->SetBoolField(TEXT("worldPartition"), Saved->IsPartitionedWorld());
	Result->SetBoolField(TEXT("externalActors"), bExternal);
	if (bExternal) Result->SetNumberField(TEXT("externalPackages"), Persistent->GetLoadedExternalObjectPackages().Num());
	Result->SetNumberField(TEXT("additionalPackagesSaved"), ExtraSaved);
	MCPSetNoRollback(Result, bWasUntitled
		? TEXT("The Untitled map now lives at levelPath; there is no file to return it to. Delete the saved map with asset(delete) if it should not exist.")
		: TEXT("The editor now has the map open under levelPath. The previous map's file on disk is untouched and holds its last saved state; reopen it with level(load) and delete the copy with asset(delete) to go back."));

	if (StillDirty.Num() > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetArrayField(TEXT("stillDirty"), MCPStringListToJson(MCPLevelSwitch::PackageNames(StillDirty)));
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The map was saved as '%s', but %d of its package(s) are still unsaved; the output log carries the reason. Run level(save) to retry them."),
			*PackageName, StillDirty.Num()));
	}
	return MCPResult(Result);
}
