#pragma once

// The one-file-per-actor packages that belong to a map, for deleting a map
// with them (#1851). A World Partition map keeps its actors in
// __ExternalActors__ and its other external objects in __ExternalObjects__,
// beside the content folder rather than under the map, so deleting the .umap
// alone left them on disk.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "ExternalPackageHelper.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "PackageSourceControlHelper.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "HandlerJsonConvert.h"

namespace MCPMapExternalPackages
{
	/** Whether the map is the open editor map, or a level loaded into it. */
	inline bool IsOpen(const FString& MapPackageName)
	{
		if (!GEditor) return false;
		for (const FWorldContext& Context : GEditor->GetWorldContexts())
		{
			const UWorld* World = Context.World();
			if (!World) continue;
			if (World->GetOutermost()->GetName() == MapPackageName) return true;
			for (const ULevel* Level : World->GetLevels())
			{
				if (Level && Level->GetOutermost()->GetName() == MapPackageName) return true;
			}
		}
		return false;
	}

	/** Refusal for deleting a map that is open, with the call that gets past it. Nothing has been touched. */
	inline TSharedPtr<FJsonValue> OpenMapError(const FString& AssetPath)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("success"), false);
		Out->SetStringField(TEXT("reason"), TEXT("map_open"));
		Out->SetStringField(TEXT("error"), FString::Printf(
			TEXT("'%s' is the open map, and deleting it would pull the level out from under the editor; nothing was deleted. ")
			TEXT("Open another map with level(load, levelPath=<other map>, discardUnsaved=true) or demo(go_home), then delete again."), *AssetPath));
		return MCPResult(Out);
	}

	/** The map's external actor and external object folders, as long package paths, from the engine's own helpers. */
	inline TArray<FString> Roots(const FString& MapPackageName)
	{
		return {
			ULevel::GetExternalActorsPath(MapPackageName),
			FExternalPackageHelper::GetExternalObjectsPath(MapPackageName),
		};
	}

	/** Whether a map file is on disk, asked of the file system because FPackageName::DoesPackageExist answers from a cache that lags a save or a delete. */
	inline bool MapFileExists(const FString& PackageName)
	{
		FString File;
		return FPackageName::TryConvertLongPackageNameToFilename(PackageName, File, FPackageName::GetMapPackageExtension())
			&& IFileManager::Get().FileExists(*File);
	}

	/**
	 * Whether a package under one of the map's external folders belongs to a
	 * different map. A map at <Map>/Sub keeps its packages at <Root>/Sub, which
	 * is inside this map's folder, so every directory between the root and the
	 * package is checked for being a map of its own.
	 */
	inline bool BelongsToAnotherMap(const FString& MapPackageName, const FString& Root, const FString& PackageName, TMap<FString, bool>& Cache)
	{
		const FString Prefix = Root + TEXT("/");
		if (!PackageName.StartsWith(Prefix)) return true;
		TArray<FString> Parts;
		PackageName.RightChop(Prefix.Len()).ParseIntoArray(Parts, TEXT("/"));
		FString Rel;
		for (int32 Index = 0; Index + 1 < Parts.Num(); ++Index)
		{
			Rel = Rel.IsEmpty() ? Parts[Index] : Rel + TEXT("/") + Parts[Index];
			const FString Candidate = MapPackageName + TEXT("/") + Rel;
			bool* Known = Cache.Find(Candidate);
			const bool bIsMap = Known ? *Known : Cache.Add(Candidate, MapFileExists(Candidate));
			if (bIsMap) return true;
		}
		return false;
	}

	/** Every package file on disk under the map's external folders, other maps' subtrees left out. Sorted. */
	inline TArray<FString> Find(const FString& MapPackageName)
	{
		TArray<FString> Out;
		TMap<FString, bool> Cache;
		for (const FString& Root : Roots(MapPackageName))
		{
			FString RootDir;
			if (!FPackageName::TryConvertLongPackageNameToFilename(Root + TEXT("/"), RootDir)) continue;
			if (!IFileManager::Get().DirectoryExists(*RootDir)) continue;
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *RootDir, TEXT("*.uasset"), /*Files*/ true, /*Directories*/ false);
			for (const FString& File : Files)
			{
				FString PackageName;
				if (!FPackageName::TryConvertFilenameToLongPackageName(File, PackageName)) continue;
				if (BelongsToAnotherMap(MapPackageName, Root, PackageName, Cache)) continue;
				Out.AddUnique(PackageName);
			}
		}
		Out.Sort();
		return Out;
	}

	struct FResult
	{
		TArray<FString> Deleted;
		/** Still loaded after a garbage collection, so the file was left alone. */
		TArray<FString> KeptLoaded;
		/** The delete was attempted and the file is still there. */
		TArray<FString> Failed;

		bool IsComplete() const { return KeptLoaded.Num() == 0 && Failed.Num() == 0; }
	};

	/**
	 * Delete the map's external packages, through FPackageSourceControlHelper:
	 * with revision control on, a controlled file is marked for delete and an
	 * added one reverted, as the editor's own asset delete does; with it off,
	 * the file is removed. Call after the map itself is gone, so nothing is
	 * left to load them.
	 */
	inline FResult Delete(const FString& MapPackageName, const TArray<FString>& PackageNames)
	{
		FResult Result;
		if (PackageNames.Num() == 0) return Result;

		const auto IsLoaded = [](const FString& Name) { return FindPackage(nullptr, *Name) != nullptr; };
		if (PackageNames.ContainsByPredicate(IsLoaded))
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
		TArray<FString> ToDelete;
		for (const FString& Name : PackageNames)
		{
			if (IsLoaded(Name)) Result.KeptLoaded.Add(Name);
			else ToDelete.Add(Name);
		}

		// Whether the file is on disk is asked of the file system. FPackageName::DoesPackageExist
		// answers from a cache that still lists a package for a while after its file is gone, so
		// it reported every delete as failed (#1851).
		const auto OnDisk = [](const FString& Name)
		{
			FString File;
			return FPackageName::TryConvertLongPackageNameToFilename(Name, File, FPackageName::GetAssetPackageExtension())
				&& IFileManager::Get().FileExists(*File);
		};
		// The engine's own map delete already removes the external packages it had loaded, so
		// only what is still on disk goes to the helper (it warns on a file that is already gone).
		TArray<FString> Pending = ToDelete.FilterByPredicate(OnDisk);
		if (Pending.Num() > 0)
		{
			FPackageSourceControlHelper Helper;
			Helper.Delete(Pending, /*bErrorsAsWarnings*/ true);
			Pending = Pending.FilterByPredicate(OnDisk);
		}
		for (const FString& Name : ToDelete)
		{
			if (Pending.Contains(Name)) Result.Failed.Add(Name);
			else Result.Deleted.Add(Name);
		}

		// The hashed folders the packages sat in, once nothing is left in them.
		for (const FString& Root : Roots(MapPackageName))
		{
			FString RootDir;
			if (!FPackageName::TryConvertLongPackageNameToFilename(Root + TEXT("/"), RootDir)) continue;
			if (!IFileManager::Get().DirectoryExists(*RootDir)) continue;
			TArray<FString> Remaining;
			IFileManager::Get().FindFilesRecursive(Remaining, *RootDir, TEXT("*.*"), /*Files*/ true, /*Directories*/ false);
			if (Remaining.Num() == 0)
			{
				IFileManager::Get().DeleteDirectory(*RootDir, /*RequireExists*/ false, /*Tree*/ true);
			}
		}
		return Result;
	}

	/** Report a delete on a response (or a batch entry). Nothing is written when the map had no external packages. */
	inline void Describe(const TSharedPtr<FJsonObject>& Out, const FResult& Result)
	{
		if (Result.Deleted.Num() == 0 && Result.IsComplete()) return;
		Out->SetNumberField(TEXT("externalPackagesDeleted"), Result.Deleted.Num());
		if (Result.KeptLoaded.Num() > 0 || Result.Failed.Num() > 0)
		{
			TArray<FString> Remaining = Result.KeptLoaded;
			Remaining.Append(Result.Failed);
			Remaining.Sort();
			Out->SetArrayField(TEXT("externalPackagesRemaining"), MCPStringListToJson(Remaining));
			Out->SetStringField(TEXT("externalPackagesNote"), Result.KeptLoaded.Num() > 0
				? TEXT("Some of the map's external packages are still loaded, so their files were left on disk. Open another map and delete the same path again to remove them.")
				: TEXT("Some of the map's external packages could not be deleted; the output log carries the revision control or file system reason. Deleting the same path again retries them."));
		}
	}
}
