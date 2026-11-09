#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerWorld.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

/**
 * #1488: a test world torn down with DestroyWorld alone stays on the editor's
 * dirty world list as /Temp/Untitled_N, which blocks level(load) and makes
 * shutdown prompt to save it.
 */

namespace
{
	/** A world in its own /Temp package with an actor in it, its package dirty. */
	UWorld* MCPMakeDirtyTempWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Editor, /*bInformEngineOfWorld=*/false);
		if (!World) return nullptr;
		World->SpawnActor<AActor>();
		World->GetPackage()->SetDirtyFlag(true);
		return World;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPDisposableWorldLeavesNothingDirtyTest,
	"UE.MCP.World.Disposable.LeavesNoDirtyTempPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPDisposableWorldLeavesNothingDirtyTest::RunTest(const FString& Parameters)
{
	UWorld* World = MCPMakeDirtyTempWorld();
	if (!TestNotNull(TEXT("a test world is created"), World)) return false;
	UPackage* Package = World->GetPackage();
	const FName PackageName = Package->GetFName();
	TestTrue(TEXT("the world lives in a /Temp package"), Package->GetName().StartsWith(TEXT("/Temp/")));
	TestTrue(TEXT("the dirty test world is on the editor's dirty world list"), MCPDirtyWorldPackageNames().Contains(PackageName));

	MCPDestroyDisposableWorld(World);

	TestFalse(TEXT("the package is clean after disposal"), Package->IsDirty());
	TestFalse(TEXT("the test world is off the editor's dirty world list"), MCPDirtyWorldPackageNames().Contains(PackageName));
	TestFalse(TEXT("the world is marked garbage"), IsValid(World));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPSweepAbandonedTempWorldsTest,
	"UE.MCP.World.Disposable.SweepClearsOnlyAbandonedTempWorlds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSweepAbandonedTempWorldsTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("the engine is up"), GEngine)) return false;
	const TSet<FName> Before = MCPDirtyWorldPackageNames();

	// What a fixture that only calls DestroyWorld leaves behind.
	UWorld* Abandoned = MCPMakeDirtyTempWorld();
	// A world a context still uses: the sweep must not touch it.
	UWorld* Live = MCPMakeDirtyTempWorld();
	if (!TestTrue(TEXT("both test worlds are created"), Abandoned && Live)) return false;
	UPackage* AbandonedPackage = Abandoned->GetPackage();
	UPackage* LivePackage = Live->GetPackage();
	GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(Live);
	Abandoned->DestroyWorld(false);

	const TArray<FString> Cleared = MCPSweepAbandonedTempWorlds(Before);

	TestTrue(TEXT("the abandoned world is reported"), Cleared.Contains(AbandonedPackage->GetName()));
	TestFalse(TEXT("the abandoned world's package is clean"), AbandonedPackage->IsDirty());
	TestFalse(TEXT("the live world is not reported"), Cleared.Contains(LivePackage->GetName()));
	TestTrue(TEXT("the live world's package is still dirty"), LivePackage->IsDirty());
	for (const FString& Name : Cleared)
	{
		TestTrue(FString::Printf(TEXT("%s was dirtied during the test"), *Name), !Before.Contains(FName(*Name)));
	}

	if (Abandoned->IsRooted()) Abandoned->RemoveFromRoot();
	Abandoned->MarkObjectsPendingKill();
	GEngine->DestroyWorldContext(Live);
	MCPDestroyDisposableWorld(Live);
	TestFalse(TEXT("the live world is clean once disposed"), LivePackage->IsDirty());
	return true;
}

#endif
