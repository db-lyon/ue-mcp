// #1119: exercise place_actor in a disposable world, never the open map.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Handlers/VolumeHelpers_Internal.h"
#include "Components/BrushComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/BlockingVolume.h"
#include "Engine/Polys.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Volume.h"
#include "Misc/AutomationTest.h"
#include "Model.h"
#include "AI/NavigationSystemBase.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"

namespace LevelPlaceVolumeTests
{
	struct FFixture
	{
		FAutomationTestBase& Test;
		UWorld* OriginalWorld = nullptr;
		UWorld* World = nullptr;
		FVector OriginalPivot = FVector::ZeroVector;
		FMCPHandlerRegistry Registry;

		explicit FFixture(FAutomationTestBase& InTest, bool bNavigation = false) : Test(InTest)
		{
			if (!GEditor) return;
			OriginalWorld = GEditor->GetEditorWorldContext().World();
			OriginalPivot = GEditor->GetPivotLocation();
			const UWorld::InitializationValues Initialization = UWorld::InitializationValues()
				.InitializeScenes(false).AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreatePhysicsScene(false).CreateNavigation(bNavigation).CreateAISystem(false)
				.ShouldSimulatePhysics(false).EnableTraceCollision(false).SetTransactional(false)
				.CreateFXSystem(false).CreateWorldPartition(false);
			World = UWorld::CreateWorld(EWorldType::Editor, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("UEMCP_PlaceVolumeTest")),
				GetTransientPackage(), true, ERHIFeatureLevel::Num, &Initialization);
			if (World) GEditor->GetEditorWorldContext().SetCurrentWorld(World);
			if (World && bNavigation)
			{
				FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode);
			}
			FLevelHandlers::RegisterHandlers(Registry);
		}

		~FFixture()
		{
			if (!World) return;
			GEditor->GetEditorWorldContext().SetCurrentWorld(OriginalWorld);
			GEditor->SetPivot(OriginalPivot, false, true);
			World->DestroyWorld(false);
		}

		AActor* Place(const TCHAR* ClassName, const FTransform& Transform, bool bStaticMesh = false)
		{
			auto Params = MakeShared<FJsonObject>();
			const FString Label = FString(TEXT("UEMCP_")) + ClassName;
			Params->SetStringField(TEXT("actorClass"), ClassName);
			Params->SetStringField(TEXT("label"), Label);
			Params->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Transform.GetLocation()));
			Params->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Transform.Rotator()));
			Params->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Transform.GetScale3D()));
			if (bStaticMesh) Params->SetStringField(TEXT("staticMesh"), TEXT("/Engine/BasicShapes/Cube.Cube"));
			const auto Response = Registry.ExecuteHandler(TEXT("place_actor"), Params);
			if (!Test.TestTrue(TEXT("place_actor returns an object"), Response.IsValid() && Response->Type == EJson::Object)) return nullptr;
			bool bSuccess = false;
			if (!Test.TestTrue(TEXT("place_actor succeeds"), Response->AsObject()->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess)) return nullptr;
			TSharedPtr<FJsonValue> LookupError;
			AActor* Actor = MCPResolveActorToken(World, Label, LookupError);
			Test.TestNotNull(TEXT("placed actor is in the disposable world"), Actor);
			return Actor;
		}

		void CheckTransform(AActor* Actor, const FTransform& Expected)
		{
			Test.TestTrue(TEXT("requested location is preserved"), Actor->GetActorLocation().Equals(Expected.GetLocation(), 0.01));
			Test.TestTrue(TEXT("requested rotation is preserved"), Actor->GetActorQuat().Equals(Expected.GetRotation(), 0.0001));
			Test.TestTrue(TEXT("requested scale is preserved"), Actor->GetActorScale3D().Equals(Expected.GetScale3D(), 0.0001));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPPlaceVolumeGeometryTest,
	"UE.MCP.Level.PlaceVolume.NativeGeometryAndTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPlaceVolumeGeometryTest::RunTest(const FString& Parameters)
{
	LevelPlaceVolumeTests::FFixture F(*this);
	if (!TestNotNull(TEXT("disposable editor world exists"), F.World)) return false;
	const FTransform Requested(FRotator(17.0, 31.0, 9.0), FVector(123.25, -456.75, 789.5), FVector(2.0, 3.0, 0.5));
	for (const TCHAR* ClassName : { TEXT("BlockingVolume"), TEXT("TriggerVolume"), TEXT("NavMeshBoundsVolume"), TEXT("AudioVolume"), TEXT("PostProcessVolume") })
	{
		AVolume* Volume = Cast<AVolume>(F.Place(ClassName, Requested));
		if (!TestNotNull(FString::Printf(TEXT("%s is a volume"), ClassName), Volume)) return false;
		F.CheckTransform(Volume, Requested);
		if (!TestNotNull(TEXT("volume has a brush model"), Volume->Brush.Get())) return false;
		if (!TestNotNull(TEXT("brush has polygons"), Volume->Brush->Polys.Get())) return false;
		TestEqual(TEXT("default cube has six faces"), Volume->Brush->Polys->Element.Num(), 6);
		TestTrue(TEXT("brush has native 100 cm half-extents"), Volume->Brush->Bounds.BoxExtent.Equals(FVector(100.0), 0.01));
		UBrushComponent* Component = Volume->GetBrushComponent();
		if (!TestNotNull(TEXT("volume has a brush component"), Component)) return false;
		TestTrue(TEXT("component uses the volume model"), Component->Brush == Volume->Brush);
		UBodySetup* Body = Component->GetBodySetup();
		if (!TestNotNull(TEXT("brush collision body exists"), Body)) return false;
		TestTrue(TEXT("brush collision has convex geometry"), Body->AggGeom.ConvexElems.Num() > 0);
		FVector Origin, Extent;
		Volume->GetActorBounds(false, Origin, Extent);
		TestTrue(TEXT("volume has nonzero bounds on every axis"), Extent.X > 0 && Extent.Y > 0 && Extent.Z > 0);
		TestTrue(TEXT("volume bounds follow requested location"), Origin.Equals(Requested.GetLocation(), 0.01));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPPlaceVolumeExistingBrushTest,
	"UE.MCP.Level.PlaceVolume.PreservesExistingBrush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPlaceVolumeExistingBrushTest::RunTest(const FString& Parameters)
{
	LevelPlaceVolumeTests::FFixture F(*this);
	if (!TestNotNull(TEXT("disposable editor world exists"), F.World)) return false;
	const FVector CustomExtent(37.0, 53.0, 71.0);
	UModel* SuppliedBrush = nullptr;
	// Supply custom geometry before SpawnActor returns, as an authored class
	// can do. place_actor must not replace it with its fallback cube.
	const FDelegateHandle Handle = F.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
	{
		if (ABlockingVolume* Volume = Cast<ABlockingVolume>(Actor))
		{
			const FTransform BeforeBuild = Volume->GetActorTransform();
			UEMCP::BuildVolumeAsCube(F.World, Volume, CustomExtent);
			Volume->SetActorTransform(BeforeBuild);
			SuppliedBrush = Volume->Brush;
		}
	}));
	const FTransform Requested(FRotator(11.0, 23.0, 7.0), FVector(411.25, 222.5, -87.75), FVector(1.5, 0.75, 2.0));
	AVolume* Volume = Cast<AVolume>(F.Place(TEXT("BlockingVolume"), Requested));
	F.World->RemoveOnActorSpawnedHandler(Handle);
	if (!TestNotNull(TEXT("custom volume was placed"), Volume)) return false;
	if (!TestNotNull(TEXT("spawn supplied a brush"), SuppliedBrush)) return false;
	TestTrue(TEXT("place_actor preserves the supplied model"), Volume->Brush == SuppliedBrush);
	TestTrue(TEXT("place_actor preserves custom brush dimensions"), Volume->Brush->Bounds.BoxExtent.Equals(CustomExtent, 0.01));
	F.CheckTransform(Volume, Requested);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPPlaceVolumeOrdinaryActorTest,
	"UE.MCP.Level.PlaceVolume.OrdinaryActorUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPlaceVolumeOrdinaryActorTest::RunTest(const FString& Parameters)
{
	LevelPlaceVolumeTests::FFixture F(*this);
	if (!TestNotNull(TEXT("disposable editor world exists"), F.World)) return false;
	const FTransform Requested(FRotator(10.0, 20.0, 30.0), FVector(10.25, -20.5, 30.75), FVector(0.5, 2.0, 3.0));
	AStaticMeshActor* Actor = Cast<AStaticMeshActor>(F.Place(TEXT("StaticMeshActor"), Requested, true));
	if (!TestNotNull(TEXT("ordinary static mesh actor was placed"), Actor)) return false;
	F.CheckTransform(Actor, Requested);
	TestNull(TEXT("ordinary actor has no brush component"), Actor->FindComponentByClass<UBrushComponent>());
	TestTrue(TEXT("static mesh shorthand still loads the mesh"), Actor->GetStaticMeshComponent()->GetStaticMesh() != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPPlaceVolumeNavBoundsTest,
	"UE.MCP.Level.PlaceVolume.NavBoundsFollowScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPlaceVolumeNavBoundsTest::RunTest(const FString& Parameters)
{
	LevelPlaceVolumeTests::FFixture F(*this, /*bNavigation=*/true);
	if (!TestNotNull(TEXT("disposable editor world exists"), F.World)) return false;
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(F.World);
	if (!TestNotNull(TEXT("disposable world has a navigation system"), NavSys)) return false;
	const FTransform Requested(FRotator::ZeroRotator, FVector(100.0, 200.0, 300.0), FVector(50.0, 50.0, 5.0));
	ANavMeshBoundsVolume* Volume = Cast<ANavMeshBoundsVolume>(F.Place(TEXT("NavMeshBoundsVolume"), Requested));
	if (!TestNotNull(TEXT("nav mesh bounds volume was placed"), Volume)) return false;
	// Pending bounds requests are applied on the navigation system's tick.
	NavSys->Tick(0.f);
	const FNavigationBounds* Registered = nullptr;
	for (const FNavigationBounds& Bounds : NavSys->GetNavigationBounds())
	{
		if (Bounds.UniqueID == Volume->GetUniqueID()) { Registered = &Bounds; break; }
	}
	if (!TestNotNull(TEXT("volume registered navigation bounds"), Registered)) return false;
	TestTrue(TEXT("registered nav bounds are valid"), Registered->AreaBox.IsValid != 0);
	TestTrue(TEXT("registered nav bounds use the scaled extent"), Registered->AreaBox.GetExtent().Equals(FVector(5000.0, 5000.0, 500.0), 1.0));
	TestTrue(TEXT("registered nav bounds follow requested location"), Registered->AreaBox.GetCenter().Equals(Requested.GetLocation(), 1.0));
	return true;
}

#endif
