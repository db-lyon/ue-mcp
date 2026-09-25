#pragma once

// A private content root for one automation test, shared by every test that
// writes packages. Tests must be able to run twice in one editor process
// (#1080), so the mount evicts what was loaded under it before it comes down.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

namespace UEMCPTests
{
	/** Every in-memory package whose name starts with RootPath. */
	inline TArray<UPackage*> PackagesUnderRoot(const FString& RootPath)
	{
		TArray<UPackage*> Packages;
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			UPackage* Package = *It;
			if (Package && Package->GetName().StartsWith(RootPath, ESearchCase::IgnoreCase))
			{
				Packages.Add(Package);
			}
		}
		return Packages;
	}

	/**
	 * Unload every package under RootPath. Objects are released and collected;
	 * a package something else still holds (the undo buffer, say) is renamed off
	 * the root so the next CreatePackage of the same name makes a fresh one.
	 */
	inline void EvictPackagesUnderRoot(const FString& RootPath)
	{
		const TArray<UPackage*> Packages = PackagesUnderRoot(RootPath);
		if (Packages.Num() == 0)
		{
			return;
		}

		for (UPackage* Package : Packages)
		{
			TArray<UObject*> Objects;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			GetObjectsWithPackage(Package, Objects, EGetObjectsFlags::IncludeNestedObjects);
#else
			GetObjectsWithPackage(Package, Objects, /*bIncludeNestedObjects=*/true);
#endif
			for (UObject* Object : Objects)
			{
				if (Object->IsRooted())
				{
					Object->RemoveFromRoot();
				}
				Object->ClearFlags(RF_Standalone);
			}
			if (Package->IsRooted())
			{
				Package->RemoveFromRoot();
			}
			Package->ClearFlags(RF_Standalone);
			Package->SetDirtyFlag(false);
			ResetLoaders(Package);
		}

		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

		for (UPackage* Survivor : PackagesUnderRoot(RootPath))
		{
			const FString Parked = FString::Printf(
				TEXT("/Temp/UEMCPEvictedTestPackage_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Survivor->Rename(*Parked, nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		}
	}
}

/**
 * Mounts RootPath (e.g. TEXT("/UEMCPFooTest/")) over a fresh directory in the
 * system temp area, and on the way out evicts everything loaded under it,
 * unmounts it and deletes the directory. Declare it before anything the test
 * roots, so it is destroyed last.
 */
struct FMCPScopedTestMount
{
	FString RootPath;
	FString ContentPath;

	FMCPScopedTestMount(const FString& InRootPath, const TCHAR* TempFolderName)
		: RootPath(InRootPath)
		, ContentPath(FPaths::Combine(
			FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::UserTempDir())),
			FString(TempFolderName),
			FGuid::NewGuid().ToString(EGuidFormats::Digits)))
	{
		// A run that died before its destructor can leave packages behind.
		UEMCPTests::EvictPackagesUnderRoot(RootPath);
		IFileManager::Get().MakeDirectory(*ContentPath, /*Tree=*/true);
		FPackageName::RegisterMountPoint(RootPath, ContentPath);
	}

	~FMCPScopedTestMount()
	{
		UEMCPTests::EvictPackagesUnderRoot(RootPath);
		FPackageName::UnRegisterMountPoint(RootPath, ContentPath);
		IFileManager::Get().DeleteDirectory(*ContentPath, /*RequireExists=*/false, /*Tree=*/true);
	}

	FMCPScopedTestMount(const FMCPScopedTestMount&) = delete;
	FMCPScopedTestMount& operator=(const FMCPScopedTestMount&) = delete;
};

#endif // WITH_DEV_AUTOMATION_TESTS
