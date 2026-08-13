// Native coverage for snap_instances_to_surface. All mutation coverage uses a
// transient test world and transient actors, never project packages or maps.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/LevelHandlers.h"
#include "Handlers/LevelHandlers_InstanceProjection_Internal.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace
{
	class FScopedProjectionTestWorld
	{
	public:
		FScopedProjectionTestWorld()
		{
			check(GEngine);
			const FName WorldName = MakeUniqueObjectName(
				nullptr,
				UWorld::StaticClass(),
				TEXT("UEMCPInstanceProjectionTestWorld"),
				EUniqueObjectNameOptions::GloballyUnique);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			check(World->GetPhysicsScene());
		}

		~FScopedProjectionTestWorld()
		{
			if (!World) return;
			if (World->AreActorsInitialized())
			{
				for (AActor* Actor : FActorRange(World))
				{
					if (Actor) Actor->RouteEndPlay(EEndPlayReason::LevelTransition);
				}
			}
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(true);
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
			World = nullptr;
		}

		UWorld* Get() const { return World; }

	private:
		UWorld* World = nullptr;
	};

	AActor* SpawnTransientActor(UWorld* World, const FString& Label)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(
			World->PersistentLevel,
			AActor::StaticClass(),
			FName(*Label));
		SpawnParameters.ObjectFlags |= RF_Transient;
		AActor* Actor = World->SpawnActor<AActor>(
			AActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
		if (Actor) Actor->SetActorLabel(Label, false);
		return Actor;
	}

	UInstancedStaticMeshComponent* AddInstanceComponent(
		AActor* Actor,
		const TArray<FTransform>& WorldTransforms)
	{
		UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(
			Actor,
			TEXT("ProjectedInstances"),
			RF_Transient | RF_Transactional);
		Actor->SetRootComponent(Instances);
		Actor->AddInstanceComponent(Instances);
		Instances->SetMobility(EComponentMobility::Movable);
		Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Instances->RegisterComponent();
		for (const FTransform& Transform : WorldTransforms)
		{
			Instances->AddInstance(Transform, true);
		}
		return Instances;
	}

	UBoxComponent* AddSurfaceBox(AActor* Actor, const FVector& Extent)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(
			Actor,
			TEXT("ProjectionSurface"),
			RF_Transient);
		Actor->SetRootComponent(Box);
		Actor->AddInstanceComponent(Box);
		Box->SetBoxExtent(Extent, false);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Box->RegisterComponent();
		return Box;
	}

	TSharedPtr<FJsonObject> MakeProjectionRequest(
		const FString& ActorLabel,
		const FString& SurfaceLabel)
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorLabel"), ActorLabel);
		Request->SetStringField(TEXT("componentName"), TEXT("ProjectedInstances"));
		Request->SetArrayField(
			TEXT("surfaceActorLabels"),
			{ MakeShared<FJsonValueString>(SurfaceLabel) });
		Request->SetNumberField(TEXT("surfaceOffset"), 10.0);
		return Request;
	}

	bool ReadWorldTransform(
		UInstancedStaticMeshComponent* Instances,
		int32 Index,
		FTransform& OutTransform)
	{
		return Instances && Instances->GetInstanceTransform(Index, OutTransform, true);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelInstanceProjectionRegistrationTest,
	"UE.MCP.Level.InstanceProjection.RegistrationAndPureValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelInstanceProjectionRegistrationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	TestTrue(
		TEXT("native projection handler is registered"),
		Registry.HasHandler(TEXT("snap_instances_to_surface")));
	TestTrue(
		TEXT("bounded batch handler has an explicit timeout"),
		Registry.GetHandlerTimeout(TEXT("snap_instances_to_surface")) >= 300.0f);

	using namespace UEMCPInstanceProjection;
	EMissPolicy Policy = EMissPolicy::Error;
	TestTrue(TEXT("miss policy is case insensitive"), ParseMissPolicy(TEXT(" SKIP "), Policy));
	TestTrue(TEXT("skip policy is selected"), Policy == EMissPolicy::Skip);
	TestFalse(TEXT("unknown miss policy is rejected"), ParseMissPolicy(TEXT("continue"), Policy));
	TestTrue(
		TEXT("surface offset follows the normalized impact normal"),
		ApplySurfaceOffset(FVector(1.0, 2.0, 3.0), FVector(0.0, 0.0, 4.0), 7.0)
			.Equals(FVector(1.0, 2.0, 10.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelInstanceProjectionDryRunAndCommitTest,
	"UE.MCP.Level.InstanceProjection.DryRunBoundsAndAtomicCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelInstanceProjectionDryRunAndCommitTest::RunTest(const FString& Parameters)
{
	FScopedProjectionTestWorld TestWorld;
	const FString TargetLabel = TEXT("UEMCP_ProjectionTarget");
	const FString SurfaceLabel = TEXT("UEMCP_ProjectionSurface");
	const FString BlockerLabel = TEXT("UEMCP_ProjectionBlocker");
	AActor* Target = SpawnTransientActor(TestWorld.Get(), TargetLabel);
	AActor* Surface = SpawnTransientActor(TestWorld.Get(), SurfaceLabel);
	AActor* Blocker = SpawnTransientActor(TestWorld.Get(), BlockerLabel);
	TestNotNull(TEXT("target actor exists"), Target);
	TestNotNull(TEXT("surface actor exists"), Surface);
	TestNotNull(TEXT("non-matching blocker exists"), Blocker);
	if (!Target || !Surface || !Blocker) return false;

	const FQuat OriginalRotation = FRotator(0.0, 37.0, 0.0).Quaternion();
	const FVector OriginalScale(1.2, 0.8, 1.5);
	const TArray<FTransform> OriginalTransforms = {
		FTransform(OriginalRotation, FVector(0.0, 0.0, 500.0), OriginalScale),
		FTransform(FQuat::Identity, FVector(1000.0, 0.0, 500.0), FVector::OneVector),
	};
	UInstancedStaticMeshComponent* Instances = AddInstanceComponent(Target, OriginalTransforms);
	UBoxComponent* SurfaceBox = AddSurfaceBox(Surface, FVector(150.0, 150.0, 100.0));
	UBoxComponent* BlockerBox = AddSurfaceBox(Blocker, FVector(150.0, 150.0, 50.0));
	TestNotNull(TEXT("instance component exists"), Instances);
	TestNotNull(TEXT("surface collision exists"), SurfaceBox);
	TestNotNull(TEXT("blocker collision exists"), BlockerBox);
	if (!Instances || !SurfaceBox || !BlockerBox) return false;
	BlockerBox->SetWorldLocation(FVector(0.0, 0.0, 250.0));

	TSharedPtr<FJsonObject> DryRunRequest = MakeProjectionRequest(TargetLabel, SurfaceLabel);
	DryRunRequest->SetArrayField(TEXT("instanceIndices"), { MakeShared<FJsonValueNumber>(0) });
	const TSharedPtr<FJsonValue> DryRunResponse =
		UEMCPInstanceProjection::SnapInstancesToSurfaceInWorld(TestWorld.Get(), DryRunRequest);
	TestTrue(TEXT("dry run returns an object"), DryRunResponse.IsValid() && DryRunResponse->Type == EJson::Object);
	if (!DryRunResponse.IsValid() || DryRunResponse->Type != EJson::Object) return false;
	const TSharedPtr<FJsonObject> DryRunResult = DryRunResponse->AsObject();
	TestTrue(TEXT("dry run succeeds"), DryRunResult->GetBoolField(TEXT("success")));
	TestTrue(TEXT("dry run is the default"), DryRunResult->GetBoolField(TEXT("dryRun")));
	TestTrue(TEXT("dry-run preflight passes"), DryRunResult->GetBoolField(TEXT("preflightPassed")));
	TestFalse(TEXT("dry run performs no mutation"), DryRunResult->GetBoolField(TEXT("mutationPerformed")));
	TestEqual(TEXT("dry run reaches the filtered surface behind a blocker"), DryRunResult->GetIntegerField(TEXT("hitCount")), 1);

	FTransform AfterDryRun;
	TestTrue(TEXT("instance remains readable after dry run"), ReadWorldTransform(Instances, 0, AfterDryRun));
	TestTrue(TEXT("dry run preserves the original transform"), AfterDryRun.Equals(OriginalTransforms[0], 0.01));

	TSharedPtr<FJsonObject> BoundedRequest = MakeProjectionRequest(TargetLabel, SurfaceLabel);
	BoundedRequest->SetNumberField(TEXT("maxInstances"), 1);
	const TSharedPtr<FJsonValue> BoundedResponse =
		UEMCPInstanceProjection::SnapInstancesToSurfaceInWorld(TestWorld.Get(), BoundedRequest);
	TestFalse(TEXT("oversized request is rejected"), BoundedResponse->AsObject()->GetBoolField(TEXT("success")));
	TestTrue(
		TEXT("oversized rejection reports the bound"),
		BoundedResponse->AsObject()->GetStringField(TEXT("error")).Contains(TEXT("maxInstances")));

	TSharedPtr<FJsonObject> AtomicRequest = MakeProjectionRequest(TargetLabel, SurfaceLabel);
	AtomicRequest->SetBoolField(TEXT("dryRun"), false);
	AtomicRequest->SetStringField(TEXT("onMiss"), TEXT("error"));
	const TSharedPtr<FJsonValue> AtomicResponse =
		UEMCPInstanceProjection::SnapInstancesToSurfaceInWorld(TestWorld.Get(), AtomicRequest);
	const TSharedPtr<FJsonObject> AtomicResult = AtomicResponse->AsObject();
	TestFalse(TEXT("a preflight miss rejects the commit"), AtomicResult->GetBoolField(TEXT("success")));
	TestFalse(TEXT("failed preflight reports no mutation"), AtomicResult->GetBoolField(TEXT("mutationPerformed")));
	TestEqual(TEXT("one instance misses"), AtomicResult->GetIntegerField(TEXT("missCount")), 1);

	FTransform AfterAtomicFailure;
	TestTrue(TEXT("instance remains readable after rejected commit"), ReadWorldTransform(Instances, 0, AfterAtomicFailure));
	TestTrue(
		TEXT("onMiss=error leaves every instance unchanged"),
		AfterAtomicFailure.Equals(OriginalTransforms[0], 0.01));

	TSharedPtr<FJsonObject> CommitRequest = MakeProjectionRequest(TargetLabel, SurfaceLabel);
	CommitRequest->SetBoolField(TEXT("dryRun"), false);
	CommitRequest->SetStringField(TEXT("onMiss"), TEXT("skip"));
	const TSharedPtr<FJsonValue> CommitResponse =
		UEMCPInstanceProjection::SnapInstancesToSurfaceInWorld(TestWorld.Get(), CommitRequest);
	const TSharedPtr<FJsonObject> CommitResult = CommitResponse->AsObject();
	TestTrue(TEXT("skip commit succeeds"), CommitResult->GetBoolField(TEXT("success")));
	TestTrue(TEXT("commit performs a mutation"), CommitResult->GetBoolField(TEXT("mutationPerformed")));
	TestEqual(TEXT("commit updates one instance"), CommitResult->GetIntegerField(TEXT("updatedCount")), 1);

	FTransform UpdatedHit;
	FTransform UnchangedMiss;
	TestTrue(TEXT("updated instance remains readable"), ReadWorldTransform(Instances, 0, UpdatedHit));
	TestTrue(TEXT("skipped instance remains readable"), ReadWorldTransform(Instances, 1, UnchangedMiss));
	TestTrue(TEXT("projected location uses the surface offset"), FMath::IsNearlyEqual(UpdatedHit.GetLocation().Z, 110.0, 0.1));
	TestTrue(TEXT("projection preserves rotation"), UpdatedHit.GetRotation().Equals(OriginalRotation, 0.001));
	TestTrue(TEXT("projection preserves scale"), UpdatedHit.GetScale3D().Equals(OriginalScale, 0.001));
	TestTrue(TEXT("onMiss=skip leaves the miss unchanged"), UnchangedMiss.Equals(OriginalTransforms[1], 0.01));
	return true;
}

#endif
