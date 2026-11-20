#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "HAL/FileManager.h"
#include "HandlerAssetResolve.h"
#include "HandlerResult.h"
#include "Misc/PackageName.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

/** Deletes an asset and verifies its package file left the disk.
 *
 *  #1243: UEditorAssetLibrary::DeleteAsset can force-delete a loaded package's objects and return true while the
 *  .uasset stays on disk, because the package is still resident when the file cleanup runs. A second call then
 *  deletes it. This collects garbage and removes the orphaned file once nothing holds the package, so "deleted"
 *  means gone from disk. bOutRemovedOrphanFile reports that the file needed that second step. */
inline bool MCPDeleteAssetFromDisk(const FString& AssetPath, bool& bOutRemovedOrphanFile)
{
	bOutRemovedOrphanFile = false;

	FString ObjectPath = FPackageName::ExportTextPathToObjectPath(AssetPath);
	ObjectPath.TrimStartAndEndInline();
	const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
	FString Filename;
	const bool bHadFile = FPackageName::DoesPackageExist(PackageName, &Filename);

	if (!UEditorAssetLibrary::DeleteAsset(AssetPath))
	{
		// Deleting the open map first swaps the editor to a blank world, and the old world is only
		// released by the next garbage collection, so that first attempt fails with the swap already
		// done. One retry after the collection finishes the delete (#1223).
		if (!bHadFile || FPaths::GetExtension(Filename, /*bIncludeDot*/ true) != FPackageName::GetMapPackageExtension())
		{
			return false;
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		if (UEditorAssetLibrary::DoesAssetExist(AssetPath) && !UEditorAssetLibrary::DeleteAsset(AssetPath))
		{
			return false;
		}
	}
	if (!bHadFile || !IFileManager::Get().FileExists(*Filename))
	{
		return true;
	}

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	if (FindPackage(nullptr, *PackageName))
	{
		return false;
	}
	bOutRemovedOrphanFile = IFileManager::Get().Delete(*Filename, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
	return !IFileManager::Get().FileExists(*Filename);
}

// ─── #278: structured delete diagnostics ────────────────────────────
// UEditorAssetLibrary::DeleteAsset returns a bare bool with no reason on failure. Every handler that deletes
// an asset reports why a delete did not finish through these, so asset(delete) and the deletes other categories
// own (pie(script_delete), #1815) give the same reasons.
struct FMCPDeleteDiagnostics
{
	bool bOpenInEditor = false;
	TArray<FString> Referencers;         // on-disk (AssetRegistry) referencers
	TArray<FString> InMemoryReferencers; // live UObject referencers (#601)
	bool bInMemoryReferenced = false;    // #601
	bool bPackageReadOnly = false;       // #601 - file read-only on disk
	bool bPackageDirty = false;          // #601 - unsaved changes block delete
	FString Reason; // open_in_editor | has_referencers | in_memory_referenced | package_read_only | package_dirty | unknown
};

inline FMCPDeleteDiagnostics MCPDiagnoseDeleteFailure(const FString& AssetPath)
{
	FMCPDeleteDiagnostics Diag;

	if (GEditor)
	{
		if (UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			if (UObject* Asset = MCPLoadAssetObject(AssetPath))
			{
				Diag.bOpenInEditor = AES->FindEditorsForAsset(Asset).Num() > 0;
			}
		}
	}

	// AssetRegistry referencers - filtered to non-self.
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	const FName PackageFName = *FPackageName::ObjectPathToPackageName(AssetPath);
	TArray<FName> Refs;
	ARM.Get().GetReferencers(PackageFName, Refs);
	for (const FName& R : Refs)
	{
		if (R != PackageFName)
		{
			Diag.Referencers.Add(R.ToString());
		}
	}

	// #601: when there are no editors/on-disk referencers the delete still fails for non-obvious reasons.
	// Gather the common culprits so callers get something actionable instead of a bare "unknown".
	if (UObject* Asset = MCPLoadAssetObject(AssetPath))
	{
		// Live (in-memory) references beyond the asset's own package.
		FReferencerInformationList RefInfo;
		if (IsReferenced(Asset, RF_Public | RF_Standalone, EInternalObjectFlags::Garbage, /*bCheckSubObjects=*/true, &RefInfo))
		{
			for (const FReferencerInformation& Ext : RefInfo.ExternalReferences)
			{
				if (Ext.Referencer && Ext.Referencer->GetOutermost() != Asset->GetOutermost())
				{
					Diag.bInMemoryReferenced = true;
					if (Diag.InMemoryReferencers.Num() < 20)
					{
						Diag.InMemoryReferencers.Add(Ext.Referencer->GetPathName());
					}
				}
			}
		}

		if (UPackage* Pkg = Asset->GetOutermost())
		{
			Diag.bPackageDirty = Pkg->IsDirty();
			FString PkgFilename;
			if (FPackageName::DoesPackageExist(Pkg->GetName(), &PkgFilename))
			{
				Diag.bPackageReadOnly = IFileManager::Get().IsReadOnly(*PkgFilename);
			}
		}
	}

	if (Diag.bOpenInEditor)               Diag.Reason = TEXT("open_in_editor");
	else if (Diag.Referencers.Num())      Diag.Reason = TEXT("has_referencers");
	else if (Diag.bInMemoryReferenced)    Diag.Reason = TEXT("in_memory_referenced");
	else if (Diag.bPackageReadOnly)       Diag.Reason = TEXT("package_read_only");
	else if (Diag.bPackageDirty)          Diag.Reason = TEXT("package_dirty");
	else                                  Diag.Reason = TEXT("unknown");
	return Diag;
}

inline void MCPApplyDeleteDiagnosticsToJson(const TSharedPtr<FJsonObject>& Out, const FMCPDeleteDiagnostics& Diag)
{
	Out->SetStringField(TEXT("reason"), Diag.Reason);
	Out->SetBoolField(TEXT("openInEditor"), Diag.bOpenInEditor);
	TArray<TSharedPtr<FJsonValue>> RefsJson;
	for (const FString& R : Diag.Referencers)
	{
		RefsJson.Add(MakeShared<FJsonValueString>(R));
	}
	Out->SetArrayField(TEXT("referencers"), RefsJson);

	// #601 richer diagnostics for the formerly-"unknown" cases.
	Out->SetBoolField(TEXT("inMemoryReferenced"), Diag.bInMemoryReferenced);
	if (Diag.InMemoryReferencers.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> InMemJson;
		for (const FString& R : Diag.InMemoryReferencers)
		{
			InMemJson.Add(MakeShared<FJsonValueString>(R));
		}
		Out->SetArrayField(TEXT("inMemoryReferencers"), InMemJson);
	}
	Out->SetBoolField(TEXT("packageReadOnly"), Diag.bPackageReadOnly);
	Out->SetBoolField(TEXT("packageDirty"), Diag.bPackageDirty);
}

/** The editor asset library answers "does not exist" and refuses every delete while PIE runs, so a delete
 *  then would report alreadyDeleted for an asset that is still there. Refused up front instead. */
inline TSharedPtr<FJsonValue> MCPDeleteDuringPlayError()
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), false);
	Out->SetStringField(TEXT("reason"), TEXT("play_in_editor"));
	Out->SetStringField(TEXT("error"), TEXT("Assets cannot be deleted while a play-in-editor session is running; nothing was deleted. ")
		TEXT("Stop it with editor(play_in_editor, pieAction=stop) and delete again."));
	return MCPResult(Out);
}
