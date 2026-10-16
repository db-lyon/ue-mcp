// Coverage for snap_actor_to_floor on a World Partition map, where the editor
// shows HLOD actors in place of cells that are not loaded.
//
// The snap took the first thing its downward trace hit. An HLOD mesh is a
// simplified copy of the cell that sits above the real ground, so on a World
// Partition map about half the snapped actors landed on one and floated up to
// ~70 cm once the real cell streamed in, with nothing in the result saying so
// beyond the hit actor's name.
//
// The fixture builds the case in a disposable world: a floor, an HLOD actor
// whose mesh hovers above it, and an actor to snap from above both.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "WorldPartition/HLOD/HLODActor.h"

namespace
{
	const TCHAR* MCPSnapTestMover = TEXT("UEMCP_SnapMover");
	constexpr double MCPSnapFloorTop = 0.0;
	constexpr double MCPSnapHlodTop = 200.0;

	struct FMCPSnapFixture
	{
		FAutomationTestBase& Test;
		UWorld* OriginalWorld = nullptr;
		UWorld* World = nullptr;
		FMCPHandlerRegistry Registry;
		UStaticMesh* Cube = nullptr;
		AStaticMeshActor* Mover = nullptr;

		explicit FMCPSnapFixture(FAutomationTestBase& InTest) : Test(InTest)
		{
			if (!GEditor) return;
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			if (!Test.TestNotNull(TEXT("the engine cube loads"), Cube)) return;
			OriginalWorld = GEditor->GetEditorWorldContext().World();
			// The engine's default initialization: traces need its physics scene and
			// trace collision, which the trimmed-down fixtures elsewhere switch off.
			World = UWorld::CreateWorld(EWorldType::Editor, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("UEMCP_SnapToFloorTest")),
				GetTransientPackage());
			if (!Test.TestNotNull(TEXT("the disposable world has a physics scene"), World ? World->GetPhysicsScene() : nullptr)) return;
			GEditor->GetEditorWorldContext().SetCurrentWorld(World);
			FLevelHandlers::RegisterHandlers(Registry);

			// The mover: a half-metre cube well above everything, so its bounds have a bottom to snap.
			Mover = SpawnCube(FVector(0, 0, 1000), FVector(0.5));
			if (Mover) Mover->SetActorLabel(MCPSnapTestMover);
		}

		~FMCPSnapFixture()
		{
			if (!World) return;
			GEditor->GetEditorWorldContext().SetCurrentWorld(OriginalWorld);
			World->DestroyWorld(false);
		}

		bool Ready() const { return World && Mover; }

		AStaticMeshActor* SpawnCube(const FVector& Location, const FVector& Scale)
		{
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
			if (!Actor) return nullptr;
			UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Cube);
			BlockVisibility(Mesh);
			Actor->SetActorScale3D(Scale);
			return Actor;
		}

		/** Query collision that blocks the Visibility channel the snap traces on. */
		static void BlockVisibility(UPrimitiveComponent* Component)
		{
			Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Component->SetCollisionObjectType(ECC_WorldStatic);
			Component->SetCollisionResponseToAllChannels(ECR_Ignore);
			Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}

		/** A 10 m floor whose top face is at MCPSnapFloorTop (the engine cube is 100 cm, centred). */
		void AddFloor()
		{
			SpawnCube(FVector(0, 0, MCPSnapFloorTop - 50.0), FVector(10.0, 10.0, 1.0));
		}

		/** An HLOD actor whose simplified mesh is a thin slab with its top at MCPSnapHlodTop. */
		AWorldPartitionHLOD* AddHlodStandIn()
		{
			AWorldPartitionHLOD* Hlod = World->SpawnActor<AWorldPartitionHLOD>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!Test.TestNotNull(TEXT("an HLOD actor spawns in the disposable world"), Hlod)) return nullptr;
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Hlod, TEXT("UEMCP_HlodMesh"));
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Cube);
			if (USceneComponent* Root = Hlod->GetRootComponent()) Mesh->SetupAttachment(Root);
			else Hlod->SetRootComponent(Mesh);
			Mesh->SetWorldLocation(FVector(0, 0, MCPSnapHlodTop - 5.0));
			Mesh->SetWorldScale3D(FVector(10.0, 10.0, 0.1));
			BlockVisibility(Mesh);
			Mesh->RegisterComponent();
			Hlod->AddInstanceComponent(Mesh);
			// What AWorldPartitionHLOD::PreRegisterAllComponents does in an editor world
			// (so actor placement can land on HLODs), and the reason a Visibility trace
			// there hits them: query-only collision that blocks Visibility. PIE and the
			// game turn it off. The mesh is added after registration, so mirror it here.
			Hlod->SetActorEnableCollision(true);
			return Hlod;
		}

		TSharedPtr<FJsonObject> Snap()
		{
			auto Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("actorLabel"), MCPSnapTestMover);
			const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("snap_actor_to_floor"), Params);
			if (!Response.IsValid() || Response->Type != EJson::Object) return nullptr;
			return Response->AsObject();
		}

		double MoverBottom() const
		{
			FVector Origin, Extent;
			Mover->GetActorBounds(false, Origin, Extent);
			return Origin.Z - Extent.Z;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelSnapToFloorSkipsHlodStandInsTest,
	"UE.MCP.Level.SnapToFloor.SkipsHlodStandIns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelSnapToFloorSkipsHlodStandInsTest::RunTest(const FString& Parameters)
{
	FMCPSnapFixture F(*this);
	if (!F.Ready()) return false;
	F.AddFloor();
	if (!F.AddHlodStandIn()) return false;

	const TSharedPtr<FJsonObject> Response = F.Snap();
	if (!TestTrue(TEXT("snap_actor_to_floor answers"), Response.IsValid())) return false;
	bool bSuccess = false;
	FString Error;
	Response->TryGetStringField(TEXT("error"), Error);
	TestTrue(FString::Printf(TEXT("the snap succeeds%s%s"), Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error),
		Response->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess);
	TestTrue(FString::Printf(TEXT("the actor lands on the floor at %.0f, not the HLOD mesh at %.0f (bottom at %.1f)"),
		MCPSnapFloorTop, MCPSnapHlodTop, F.MoverBottom()),
		FMath::Abs(F.MoverBottom() - MCPSnapFloorTop) <= 1.0);
	const TArray<TSharedPtr<FJsonValue>>* Skipped = nullptr;
	TestTrue(TEXT("the result names the HLOD actor it skipped"),
		Response->TryGetArrayField(TEXT("skippedHlodActors"), Skipped) && Skipped && Skipped->Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelSnapToFloorRefusesOnlyHlodTest,
	"UE.MCP.Level.SnapToFloor.RefusesWhenOnlyHlodIsBelow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelSnapToFloorRefusesOnlyHlodTest::RunTest(const FString& Parameters)
{
	// No floor: the real ground's cell is "not loaded", and only its HLOD stand-in
	// is there. Snapping onto it would be the floating case, so the call refuses
	// and leaves the actor where it was.
	FMCPSnapFixture F(*this);
	if (!F.Ready()) return false;
	if (!F.AddHlodStandIn()) return false;
	const double Before = F.MoverBottom();

	const TSharedPtr<FJsonObject> Response = F.Snap();
	if (!TestTrue(TEXT("snap_actor_to_floor answers"), Response.IsValid())) return false;
	bool bSuccess = true;
	Response->TryGetBoolField(TEXT("success"), bSuccess);
	FString Error;
	Response->TryGetStringField(TEXT("error"), Error);
	TestFalse(TEXT("the snap refuses"), bSuccess);
	TestTrue(FString::Printf(TEXT("and points at load_actor_descs (error: %s)"), *Error), Error.Contains(TEXT("load_actor_descs")));
	TestEqual(TEXT("the actor has not moved"), F.MoverBottom(), Before, 0.01);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
