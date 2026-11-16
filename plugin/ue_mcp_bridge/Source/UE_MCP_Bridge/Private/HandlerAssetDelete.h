#pragma once

#include "CoreMinimal.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
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
