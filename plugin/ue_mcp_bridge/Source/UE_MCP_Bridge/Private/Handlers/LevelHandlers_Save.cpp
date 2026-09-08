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
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
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
				TEXT("'%s' is read-only on disk. Under source control this means the file is not checked out; otherwise clear the read-only attribute."),
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
		const bool bReadOnly = bFileExists && FileManager.IsReadOnly(*FileName);
		Entry->SetBoolField(TEXT("fileExists"), bFileExists);
		Entry->SetBoolField(TEXT("readOnly"), bReadOnly);

		FMCPSavePackageDiagnostics Diagnostics;
		FSavePackageArgs SaveArgs;
		// The same flags editor(save_dirty) passes, so the two paths cannot
		// write the same package differently.
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = &Diagnostics;
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

		const bool bOk = IsSuccessful(SaveResult.Result);
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
		return MCPError(TEXT("Play In Editor is running; stop it before saving the level. Saving during PIE is what produced an unexplained failure before."));
	}

	ULevel* Level = World->GetCurrentLevel();
	if (!Level) Level = World->PersistentLevel;
	if (!Level) return MCPError(TEXT("The editor world has no current level to save"));

	UPackage* LevelPackage = Level->GetOutermost();
	if (!LevelPackage) return MCPError(TEXT("The current level has no package"));

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
	Result->SetNumberField(TEXT("savedCount"), Saved.Num());
	Result->SetNumberField(TEXT("failedCount"), Failed.Num());
	Result->SetNumberField(TEXT("skippedCount"), Skipped.Num());
	Result->SetArrayField(TEXT("saved"), Saved);
	if (Skipped.Num() > 0) Result->SetArrayField(TEXT("skipped"), Skipped);

	// Stated before the failure branch, because a partial save has already
	// overwritten the packages it did write.
	if (Saved.Num() > 0)
	{
		MCPSetNoRollback(Result,
			TEXT("The level package, and every external actor package that was dirty, were overwritten on disk with the editor's in-memory state. ")
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
	Result->SetStringField(TEXT("message"), Saved.Num() > 0
		? FString::Printf(TEXT("Saved %d package(s) for level '%s'"), Saved.Num(), *World->GetName())
		: FString::Printf(TEXT("Level '%s' was already saved; nothing needed writing"), *World->GetName()));
	return MCPResult(Result);
}
