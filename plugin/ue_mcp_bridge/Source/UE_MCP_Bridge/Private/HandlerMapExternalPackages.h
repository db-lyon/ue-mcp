#pragma once

// The one-file-per-actor packages that belong to a map, for deleting a map
// with them (#1851). A World Partition map keeps its actors in
// __ExternalActors__ and its other external objects in __ExternalObjects__,
// beside the content folder rather than under the map, so deleting the .umap
// alone left them on disk.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Engine/Level.h"
#include "ExternalPackageHelper.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "PackageSourceControlHelper.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "HandlerJsonConvert.h"

namespace MCPMapExternalPackages
{
	/** The map's external actor and external object folders, as long package paths, from the engine's own helpers. */
	inline TArray<FString> Roots(const FString& MapPackageName)
	{
		return {
			ULevel::GetExternalActorsPath(MapPackageName),
			FExternalPackageHelper::GetExternalObjectsPath(MapPackageName),
		};
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
			const bool bIsMap = Known ? *Known : Cache.Add(Candidate, FPackageName::DoesPackageExist(Candidate));
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
		// Deleting the map loads it to check its references, and a package that load
		// touched keeps its file open through its linker until the loader is reset,
		// which makes the file delete fail on Windows. Close them before collecting.
		FlushAsyncLoading();
		for (const FString& Name : PackageNames)
		{
			if (UPackage* Loaded = FindPackage(nullptr, *Name)) ResetLoaders(Loaded);
		}
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

		if (ToDelete.Num() > 0)
		{
			FPackageSourceControlHelper Helper;
			Helper.Delete(ToDelete, /*bErrorsAsWarnings*/ true);
		}
		// A file still held after the first pass (a package the collection above
		// released late): collect once more and try those again.
		TArray<FString> Retry = ToDelete.FilterByPredicate([](const FString& Name) { return FPackageName::DoesPackageExist(Name); });
		if (Retry.Num() > 0)
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			FPackageSourceControlHelper Helper;
			Helper.Delete(Retry, /*bErrorsAsWarnings*/ true);
		}
		for (const FString& Name : ToDelete)
		{
			if (FPackageName::DoesPackageExist(Name)) Result.Failed.Add(Name);
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
