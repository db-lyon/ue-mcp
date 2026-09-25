// Regression coverage for #949.
//
// SaveAssetPackage built its target filename with the asset extension
// unconditionally, so every handler that mutated an actor or a component in the
// open level wrote Content/Maps/<Level>.uasset alongside the existing
// Content/Maps/<Level>.umap. Two package files then claimed the same long
// package name, level(save) kept writing one and the ~87 SaveAssetPackage call
// sites kept writing the other, and the two diverged in silence. Unreal resolves
// the ".uasset" first, so the fork is the copy that wins.
//
// The test writes nowhere near the attached project. run_automation_tests
// dispatches every EditorContext/EngineFilter test in the process against
// whatever project the bridge happens to be attached to, so this registers its
// own mount point over a fresh directory in the system temp area, does the whole
// exercise there, then unmounts and deletes it.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Tests/MCPScopedTestMount.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* const MCPExtensionTestRoot = TEXT("/UEMCPPackageExtensionTest/");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPackageSaveExtensionTest,
	"UE.MCP.Save.PackageExtension.WorldPackagesWriteUMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPackageSaveExtensionTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPExtensionTestRoot), TEXT("UEMCPPackageExtensionTest") };
	IFileManager& FM = IFileManager::Get();

	// A package with no world in it keeps the asset extension. This is the half
	// the old code got right, and it has to stay right: one-file-per-actor
	// packages under __ExternalActors__ hold an AActor and no UWorld, and OFPA
	// expects those on disk as ".uasset".
	const FString AssetPackageName = FString(MCPExtensionTestRoot) + TEXT("AssetPackage");
	UPackage* AssetPackage = CreatePackage(*AssetPackageName);
	TestNotNull(TEXT("asset package created"), AssetPackage);
	if (AssetPackage)
	{
		TestFalse(TEXT("a package with no world is not a map package"), IsMapPackage(AssetPackage));
		TestEqual(TEXT("a package with no world takes the asset extension"),
			PackageFileExtension(AssetPackage), FPackageName::GetAssetPackageExtension());
	}

	// The regression itself: an object whose outermost is a UWorld.
	const FString WorldPackageName = FString(MCPExtensionTestRoot) + TEXT("TestLevel");
	UPackage* WorldPackage = CreatePackage(*WorldPackageName);
	TestNotNull(TEXT("world package created"), WorldPackage);
	if (!WorldPackage)
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(
		EWorldType::Inactive,
		/*bInformEngineOfWorld=*/false,
		FName(TEXT("TestLevel")),
		WorldPackage,
		/*bAddToRoot=*/false);
	TestNotNull(TEXT("world created inside the package"), World);
	if (!World)
	{
		return false;
	}
	// Mirror what UWorldFactory stamps on a newly authored map, so the save has
	// a top-level asset to pick up under SaveAssetPackage's TopLevelFlags.
	World->SetFlags(RF_Public | RF_Standalone);
	const FGCRootScope KeepWorldAlive(World);

	TestTrue(TEXT("the world's outermost is the world package"), World->GetOutermost() == WorldPackage);
	TestTrue(TEXT("a package holding a world is a map package"), IsMapPackage(WorldPackage));
	TestEqual(TEXT("a world package takes the map extension"),
		PackageFileExtension(WorldPackage), FPackageName::GetMapPackageExtension());

	FString ResolvedFileName;
	TestTrue(TEXT("the world package resolves to a filename"),
		ResolvePackageFileName(WorldPackage, ResolvedFileName));
	TestTrue(TEXT("the resolved filename is a .umap"),
		ResolvedFileName.EndsWith(FPackageName::GetMapPackageExtension(), ESearchCase::IgnoreCase));
	TestFalse(TEXT("the resolved filename is not a .uasset"),
		ResolvedFileName.EndsWith(FPackageName::GetAssetPackageExtension(), ESearchCase::IgnoreCase));

	FString BaseFileName;
	TestTrue(TEXT("the package name maps into the test mount"),
		FPackageName::TryConvertLongPackageNameToFilename(WorldPackageName, BaseFileName));
	const FString MapFile = BaseFileName + FPackageName::GetMapPackageExtension();
	const FString AssetSibling = BaseFileName + FPackageName::GetAssetPackageExtension();

	TestFalse(TEXT("no map file on disk before the save"), FM.FileExists(*MapFile));
	TestFalse(TEXT("no asset sibling on disk before the save"), FM.FileExists(*AssetSibling));

	const bool bSaved = SaveAssetPackage(World);

	// This is the assertion that fails on a #949 regression, and it holds
	// whatever the save itself decided to do: the asset sibling must never be
	// created for a package that already lives on disk as a map.
	TestFalse(TEXT("saving a world package writes no .uasset sibling"), FM.FileExists(*AssetSibling));
	if (bSaved)
	{
		TestTrue(TEXT("saving a world package writes the .umap"), FM.FileExists(*MapFile));
	}
	else
	{
		AddInfo(TEXT("SavePackage declined to write the test world; the .uasset sibling assertion still stands."));
	}

	// Leave nothing dirty behind: a later editor(save_dirty) would otherwise
	// find these packages and try to flush them through an unmounted root.
	WorldPackage->SetDirtyFlag(false);
	if (AssetPackage)
	{
		AssetPackage->SetDirtyFlag(false);
	}
	World->DestroyWorld(/*bInformEngineOfWorld=*/false);

	return true;
}

#endif
