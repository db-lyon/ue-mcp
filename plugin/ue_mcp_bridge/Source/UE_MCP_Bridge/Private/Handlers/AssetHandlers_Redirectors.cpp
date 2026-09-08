// asset(fixup_redirectors) (#908).
//
// bulk_rename moved 29 referenced assets and left 19 ObjectRedirector packages
// at the old paths, still pointed at by unloaded maps and assets. Nothing
// native could load and resave exactly those referencers, re-query the
// registry and delete only the redirectors that came out clean: UE 5.8's
// Python AssetTools does not expose fixup_referencers, and the blunt
// instrument, ResavePackages -FixupRedirects -ProjectOnly, started rewriting
// unrelated old packages.
//
// The shape of this action follows from that. It preflights the exact
// redirectors, their destinations and their hard and soft referencers, reports
// the exact packages it would touch, rewrites only those, re-queries the
// registry, and deletes a redirector only when nothing references it any more.
// A whole content root is refused unless the caller asks for it by name, so
// "fix these 19" cannot turn into "resave the project" by accident.

#include "AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	const FTopLevelAssetPath MCPRedirectorClassPath(TEXT("/Script/CoreUObject"), TEXT("ObjectRedirector"));

	/** One redirector and everything the preflight learned about it. */
	struct FMCPRedirectorPlan
	{
		FString PackageName;
		FString ObjectPath;
		FString DestinationPath;
		TArray<FString> HardReferencers;
		TArray<FString> SoftReferencers;
		FString PreflightError;

		TArray<FString> AllReferencers() const
		{
			TArray<FString> All = HardReferencers;
			for (const FString& Soft : SoftReferencers) All.AddUnique(Soft);
			return All;
		}
	};

	/** The content roots. Naming one of these as a path is a project-wide
	 *  operation, which this action refuses without an explicit opt-in. */
	bool MCPIsWholeContentRoot(const FString& Path)
	{
		FString Trimmed = Path;
		Trimmed.TrimStartAndEndInline();
		while (Trimmed.EndsWith(TEXT("/")) && Trimmed.Len() > 1)
		{
			Trimmed.LeftChopInline(1);
		}
		if (Trimmed.IsEmpty() || Trimmed == TEXT("/")) return true;
		// "/Game" and "/MyPlugin" have exactly one slash: they name a mount
		// point rather than a folder inside one.
		int32 SlashCount = 0;
		for (TCHAR Char : Trimmed)
		{
			if (Char == TEXT('/')) ++SlashCount;
		}
		return SlashCount <= 1;
	}

	TArray<TSharedPtr<FJsonValue>> MCPRedirectorStringsToJson(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Reserve(Values.Num());
		for (const FString& Value : Values) Out.Add(MakeShared<FJsonValueString>(Value));
		return Out;
	}

	/** Package referencers of one package, split by whether the reference is
	 *  hard. A soft referencer is the case the manual workaround kept missing:
	 *  it does not force the package to load, so it never got rewritten. */
	void MCPGatherReferencers(
		IAssetRegistry& Registry,
		const FName PackageName,
		TArray<FString>& OutHard,
		TArray<FString>& OutSoft)
	{
		TArray<FName> Hard;
		Registry.GetReferencers(PackageName, Hard,
			UE::AssetRegistry::EDependencyCategory::Package,
			UE::AssetRegistry::EDependencyQuery::Hard);
		TArray<FName> Soft;
		Registry.GetReferencers(PackageName, Soft,
			UE::AssetRegistry::EDependencyCategory::Package,
			UE::AssetRegistry::EDependencyQuery::Soft);

		for (const FName& Referencer : Hard)
		{
			if (Referencer != PackageName) OutHard.AddUnique(Referencer.ToString());
		}
		for (const FName& Referencer : Soft)
		{
			if (Referencer != PackageName) OutSoft.AddUnique(Referencer.ToString());
		}
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::FixupRedirectors(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const TArray<TSharedPtr<FJsonValue>>* PathsField = nullptr;
	if (!Params->TryGetArrayField(TEXT("paths"), PathsField) || !PathsField)
	{
		return MCPError(TEXT("Missing 'paths' array: the redirector packages, or the folders holding them, to fix up."));
	}
	const TArray<FString> RequestedPaths = JsonArrayToStringList(PathsField);
	if (RequestedPaths.Num() == 0)
	{
		return MCPError(TEXT("'paths' is empty. Name the redirector packages, or the folders holding them."));
	}

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	const bool bAllowProjectWide = OptionalBool(Params, TEXT("allowProjectWide"), false);

	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegistryModule.Get();

	// ── Resolve the requested paths to an exact set of redirectors ──────────
	TArray<FAssetData> Redirectors;
	TArray<TSharedPtr<FJsonValue>> UnresolvedPaths;
	for (const FString& RequestedPath : RequestedPaths)
	{
		if (MCPIsWholeContentRoot(RequestedPath) && !bAllowProjectWide)
		{
			return MCPError(FString::Printf(
				TEXT("'%s' names a whole content root, which would resave the project. ")
				TEXT("Name the redirector packages or the folders holding them, or pass allowProjectWide=true to mean it."),
				*RequestedPath));
		}
		if (MCPIsProtectedAssetPath(RequestedPath))
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is on a protected mount (/Engine/, /Script/, /Memory/, /Temp/), which the bridge never rewrites or deletes."),
				*RequestedPath));
		}

		const FMCPAssetPathForms Forms = MCPAssetPathForms(RequestedPath);

		// A folder: every redirector under it, recursively.
		FARFilter FolderFilter;
		FolderFilter.PackagePaths.Add(FName(*Forms.PackagePath));
		FolderFilter.bRecursivePaths = true;
		FolderFilter.ClassPaths.Add(MCPRedirectorClassPath);
		TArray<FAssetData> InFolder;
		Registry.GetAssets(FolderFilter, InFolder);

		// A package: the redirector living in it.
		TArray<FAssetData> InPackage;
		Registry.GetAssetsByPackageName(FName(*Forms.PackagePath), InPackage);

		int32 FoundHere = 0;
		for (const FAssetData& Candidate : InFolder)
		{
			if (!Candidate.IsRedirector()) continue;
			Redirectors.AddUnique(Candidate);
			++FoundHere;
		}
		for (const FAssetData& Candidate : InPackage)
		{
			if (!Candidate.IsRedirector()) continue;
			Redirectors.AddUnique(Candidate);
			++FoundHere;
		}
		if (FoundHere == 0)
		{
			// Not an error: a path with nothing to fix is the state this action
			// is trying to reach. Reported so a typo is still visible.
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), RequestedPath);
			Entry->SetStringField(TEXT("reason"), TEXT("no_redirector_found"));
			UnresolvedPaths.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	// ── Preflight: destinations, referencers, and the packages to be written ─
	TArray<FMCPRedirectorPlan> Plans;
	TArray<UObjectRedirector*> RedirectorObjects;
	TArray<FString> PackagesToLoad;
	for (const FAssetData& Data : Redirectors)
	{
		FMCPRedirectorPlan Plan;
		Plan.PackageName = Data.PackageName.ToString();
		Plan.ObjectPath = Data.GetSoftObjectPath().ToString();

		if (MCPIsProtectedAssetPath(Plan.PackageName))
		{
			Plan.PreflightError = TEXT("on a protected mount");
			Plans.Add(MoveTemp(Plan));
			continue;
		}

		UObjectRedirector* Redirector = Cast<UObjectRedirector>(MCPLoadAssetObject(Plan.ObjectPath));
		if (!Redirector)
		{
			Plan.PreflightError = TEXT("the registry lists a redirector here but it would not load");
			Plans.Add(MoveTemp(Plan));
			continue;
		}
		Plan.DestinationPath = Redirector->DestinationObject
			? Redirector->DestinationObject->GetPathName()
			: FString();
		if (Plan.DestinationPath.IsEmpty())
		{
			// A redirector with no destination cannot be followed, so rewriting
			// its referencers would drop the reference rather than repoint it.
			Plan.PreflightError = TEXT("the redirector has no destination object, so its referencers cannot be repointed");
			Plans.Add(MoveTemp(Plan));
			continue;
		}

		MCPGatherReferencers(Registry, Data.PackageName, Plan.HardReferencers, Plan.SoftReferencers);
		for (const FString& Referencer : Plan.AllReferencers())
		{
			PackagesToLoad.AddUnique(Referencer);
		}

		RedirectorObjects.Add(Redirector);
		Plans.Add(MoveTemp(Plan));
	}

	// A referencer that is itself one of the redirectors being fixed is not a
	// package to rewrite; it is one to delete.
	for (const FMCPRedirectorPlan& Plan : Plans)
	{
		PackagesToLoad.Remove(Plan.PackageName);
	}
	PackagesToLoad.Sort();

	TArray<TSharedPtr<FJsonValue>> PlanJson;
	for (const FMCPRedirectorPlan& Plan : Plans)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("packageName"), Plan.PackageName);
		Entry->SetStringField(TEXT("objectPath"), Plan.ObjectPath);
		Entry->SetStringField(TEXT("destinationPath"), Plan.DestinationPath);
		Entry->SetArrayField(TEXT("hardReferencers"), MCPRedirectorStringsToJson(Plan.HardReferencers));
		Entry->SetArrayField(TEXT("softReferencers"), MCPRedirectorStringsToJson(Plan.SoftReferencers));
		Entry->SetNumberField(TEXT("referencerCount"), Plan.AllReferencers().Num());
		if (!Plan.PreflightError.IsEmpty())
		{
			Entry->SetStringField(TEXT("status"), TEXT("skipped"));
			Entry->SetStringField(TEXT("error"), Plan.PreflightError);
		}
		PlanJson.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("allowProjectWide"), bAllowProjectWide);
	Result->SetNumberField(TEXT("redirectorCount"), Plans.Num());
	Result->SetArrayField(TEXT("redirectors"), PlanJson);
	Result->SetArrayField(TEXT("packagesToLoad"), MCPRedirectorStringsToJson(PackagesToLoad));
	Result->SetArrayField(TEXT("packagesToSave"), MCPRedirectorStringsToJson(PackagesToLoad));
	if (UnresolvedPaths.Num() > 0)
	{
		Result->SetArrayField(TEXT("unresolvedPaths"), UnresolvedPaths);
	}

	if (bDryRun || RedirectorObjects.Num() == 0)
	{
		Result->SetNumberField(TEXT("rewrittenPackageCount"), 0);
		Result->SetNumberField(TEXT("deletedRedirectorCount"), 0);
		if (RedirectorObjects.Num() == 0 && !bDryRun)
		{
			Result->SetStringField(TEXT("note"),
				TEXT("No redirector needed fixing at the paths given, so nothing was loaded, saved or deleted."));
		}
		return MCPResult(Result);
	}

	// ── Rewrite exactly those packages ─────────────────────────────────────
	// The referencers are loaded first because a soft reference in an unloaded
	// package is invisible to the rewriter, which is how 19 stubs survived the
	// original rename.
	TArray<UPackage*> LoadedReferencers;
	TArray<TSharedPtr<FJsonValue>> PackageStatuses;
	for (const FString& PackageName : PackagesToLoad)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("packageName"), PackageName);

		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
			Entry->SetBoolField(TEXT("loadedByThisCall"), Package != nullptr);
		}
		else
		{
			Entry->SetBoolField(TEXT("loadedByThisCall"), false);
		}

		if (!Package)
		{
			Entry->SetStringField(TEXT("status"), TEXT("load_failed"));
			PackageStatuses.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}
		LoadedReferencers.Add(Package);
		Entry->SetStringField(TEXT("status"), TEXT("loaded"));
		PackageStatuses.Add(MakeShared<FJsonValueObject>(Entry));
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	// LeaveFixedUpRedirectors on purpose: the deletion below happens after the
	// registry has been re-queried, so a redirector something still points at
	// is never removed.
	AssetTools.FixupReferencers(
		RedirectorObjects,
		/*bCheckoutDialogPrompt=*/false,
		ERedirectFixupMode::LeaveFixedUpRedirectors);

	const bool bFixupStillRunning = AssetTools.IsFixupReferencersInProgress();
	Result->SetBoolField(TEXT("fixupInProgress"), bFixupStillRunning);

	// ── Save exactly those packages ────────────────────────────────────────
	// save=false skips this explicit pass only. The editor's own fix-up writes
	// what it can on its own, so this is not a way to preview the change:
	// dryRun is, and it returns before anything is loaded.
	int32 SavedCount = 0;
	if (!bSave)
	{
		Result->SetStringField(TEXT("saveNote"),
			TEXT("save=false skipped the explicit save pass. The editor's referencer fix-up writes what it can regardless, ")
			TEXT("so use dryRun=true to preview instead. Redirectors are still only deleted when the registry says nothing references them."));
	}
	if (bSave && !bFixupStillRunning)
	{
		TArray<UPackage*> ToSave;
		for (UPackage* Package : LoadedReferencers)
		{
			if (Package && Package->IsDirty()) ToSave.Add(Package);
		}
		if (ToSave.Num() > 0)
		{
			UEditorLoadingAndSavingUtils::SavePackages(ToSave, /*bOnlyDirty=*/true);
		}
		TArray<FString> ModifiedFiles;
		for (const TSharedPtr<FJsonValue>& Value : PackageStatuses)
		{
			const TSharedPtr<FJsonObject> Entry = Value->AsObject();
			if (!Entry.IsValid()) continue;
			FString PackageName;
			Entry->TryGetStringField(TEXT("packageName"), PackageName);
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package) continue;
			const bool bStillDirty = Package->IsDirty();
			Entry->SetBoolField(TEXT("stillDirty"), bStillDirty);
			Entry->SetStringField(TEXT("status"), bStillDirty ? TEXT("save_failed") : TEXT("saved"));
			if (!bStillDirty)
			{
				++SavedCount;
				FString FileName;
				if (FPackageName::DoesPackageExist(PackageName, &FileName))
				{
					ModifiedFiles.Add(FileName);
				}
			}
		}
		// Re-query: the registry's referencer graph is what decides which
		// redirectors are safe to delete, and it is stale until the rewritten
		// packages have been rescanned.
		if (ModifiedFiles.Num() > 0)
		{
			Registry.ScanModifiedAssetFiles(ModifiedFiles);
		}
	}
	Result->SetNumberField(TEXT("rewrittenPackageCount"), SavedCount);
	Result->SetArrayField(TEXT("packages"), PackageStatuses);

	// ── Delete only the redirectors nothing points at any more ─────────────
	TArray<UObject*> ToDelete;
	TArray<TSharedPtr<FJsonValue>> RedirectorStatuses;
	for (const FMCPRedirectorPlan& Plan : Plans)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("packageName"), Plan.PackageName);
		Entry->SetStringField(TEXT("objectPath"), Plan.ObjectPath);
		Entry->SetStringField(TEXT("destinationPath"), Plan.DestinationPath);
		Entry->SetArrayField(TEXT("hardReferencers"), MCPRedirectorStringsToJson(Plan.HardReferencers));
		Entry->SetArrayField(TEXT("softReferencers"), MCPRedirectorStringsToJson(Plan.SoftReferencers));

		if (!Plan.PreflightError.IsEmpty())
		{
			Entry->SetStringField(TEXT("status"), TEXT("skipped"));
			Entry->SetStringField(TEXT("error"), Plan.PreflightError);
			RedirectorStatuses.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		TArray<FString> RemainingHard;
		TArray<FString> RemainingSoft;
		MCPGatherReferencers(Registry, FName(*Plan.PackageName), RemainingHard, RemainingSoft);
		const int32 Remaining = RemainingHard.Num() + RemainingSoft.Num();
		Entry->SetNumberField(TEXT("referencersBefore"), Plan.AllReferencers().Num());
		Entry->SetNumberField(TEXT("referencersAfter"), Remaining);
		Entry->SetArrayField(TEXT("remainingHardReferencers"), MCPRedirectorStringsToJson(RemainingHard));
		Entry->SetArrayField(TEXT("remainingSoftReferencers"), MCPRedirectorStringsToJson(RemainingSoft));

		if (Remaining > 0 || bFixupStillRunning)
		{
			Entry->SetStringField(TEXT("status"), TEXT("kept"));
			Entry->SetStringField(TEXT("reason"), bFixupStillRunning
				? TEXT("the editor's referencer fix-up is still running, so nothing was deleted this call")
				: TEXT("something still references it; rerun once those packages are saved"));
			RedirectorStatuses.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		if (UObject* Redirector = MCPLoadAssetObject(Plan.ObjectPath))
		{
			ToDelete.Add(Redirector);
			Entry->SetStringField(TEXT("status"), TEXT("deleting"));
		}
		else
		{
			// Already gone, which is the state this action is trying to reach.
			Entry->SetStringField(TEXT("status"), TEXT("deleted"));
		}
		RedirectorStatuses.Add(MakeShared<FJsonValueObject>(Entry));
	}

	int32 DeletedCount = 0;
	if (ToDelete.Num() > 0)
	{
		DeletedCount = ObjectTools::DeleteObjects(
			ToDelete, /*bShowConfirmation=*/false, ObjectTools::EAllowCancelDuringDelete::CancelNotAllowed);
	}

	// The deletion is reported from what is on disk afterwards, not from the
	// return count, because a package the editor declined to remove is exactly
	// the case that must not read as cleaned up.
	int32 ConfirmedDeleted = 0;
	for (const TSharedPtr<FJsonValue>& Value : RedirectorStatuses)
	{
		const TSharedPtr<FJsonObject> Entry = Value->AsObject();
		if (!Entry.IsValid()) continue;
		FString Status;
		Entry->TryGetStringField(TEXT("status"), Status);
		if (Status != TEXT("deleting") && Status != TEXT("deleted")) continue;

		FString PackageName;
		Entry->TryGetStringField(TEXT("packageName"), PackageName);
		const bool bStillOnDisk = FPackageName::DoesPackageExist(PackageName);
		Entry->SetStringField(TEXT("status"), bStillOnDisk ? TEXT("delete_failed") : TEXT("deleted"));
		if (!bStillOnDisk) ++ConfirmedDeleted;
	}

	Result->SetNumberField(TEXT("deleteAttemptedCount"), ToDelete.Num());
	Result->SetNumberField(TEXT("deleteReportedCount"), DeletedCount);
	Result->SetNumberField(TEXT("deletedRedirectorCount"), ConfirmedDeleted);
	Result->SetArrayField(TEXT("redirectors"), RedirectorStatuses);
	MCPSetUpdated(Result);
	// Two irreversible writes, neither of which the bridge can undo. The
	// referencer packages were saved over their previous bytes, and no copy of
	// those was kept; the redirector packages were deleted from disk once
	// nothing pointed at them. Restoring the old state needs a call that
	// recreates an ObjectRedirector at a vacated path and a call that rewinds a
	// package to its pre-save contents, and neither exists.
	MCPSetNoRollback(Result, FString::Printf(
		TEXT("Rewrote and saved %d referencer package(s) over their previous contents and deleted %d redirector ")
		TEXT("package(s). Undoing this needs an action that recreates an ObjectRedirector at an old path and one that ")
		TEXT("restores a package's pre-save bytes; neither exists, and no copy of the overwritten packages was kept."),
		SavedCount, ConfirmedDeleted));
	return MCPResult(Result);
}
