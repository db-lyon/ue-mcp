// All geometry lives in a transient world; the open map is never changed.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Handlers/PIE/PIEHandlers.h"
#include "Scripting/MCPTraceLibrary.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"

namespace
{
	class FScopedTraceTestWorld
	{
	public:
		UWorld* World = nullptr;
		int32 Instance = 1000000;
		FScopedTraceTestWorld()
		{
			check(GEngine);
			// A distinct instance keeps explicit requests independent of live PIE.
			for (const FWorldContext& Existing : GEngine->GetWorldContexts())
				Instance = FMath::Max(Instance, Existing.PIEInstance + 1);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.PIEInstance = Instance;
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("MCPTraceTestWorld")), GetTransientPackage());
			check(World);
			World->AddToRoot();
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedTraceTestWorld()
		{
			for (AActor* Actor : FActorRange(World))
				if (Actor) Actor->RouteEndPlay(EEndPlayReason::LevelTransition);
			GEngine->ShutdownWorldNetDriver(World);
			MCPDestroyDisposableWorld(World, /*bInformEngineOfWorld=*/true);
			World->SetPhysicsScene(nullptr);
			GEngine->DestroyWorldContext(World);
		}

		UBoxComponent* Box(const FVector& Location, ECollisionResponse Response = ECR_Block)
		{
			FActorSpawnParameters Spawn;
			Spawn.ObjectFlags |= RF_Transient;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
			check(Actor);
			Actor->SetActorLabel(TEXT("MCPTraceBox"), false);
			auto* Box = NewObject<UBoxComponent>(Actor, NAME_None, RF_Transient);
			Actor->SetRootComponent(Box);
			Actor->AddInstanceComponent(Box);
			Box->SetBoxExtent(FVector(10), false);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionObjectType(ECC_WorldDynamic);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_Visibility, Response);
			Box->RegisterComponent();
			Box->SetWorldLocation(Location);
			return Box;
		}

		TSharedPtr<FJsonObject> Request() const
		{
			auto Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("world"), TEXT("pie"));
			Params->SetNumberField(TEXT("pieInstance"), Instance);
			Params->SetObjectField(TEXT("start"), MCPVec3ToJsonObject(FVector::ZeroVector));
			Params->SetObjectField(TEXT("end"), MCPVec3ToJsonObject(FVector(1000, 0, 0)));
			Params->SetStringField(TEXT("shape"), TEXT("sphere"));
			Params->SetNumberField(TEXT("radius"), 20);
			return Params;
		}
	};

	TSharedPtr<FJsonObject> TraceTestRun(FMCPHandlerRegistry& Registry, const TCHAR* Method,
		const TSharedPtr<FJsonObject>& Params)
	{
		return Registry.ExecuteHandler(Method, Params)->AsObject();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShapeTraceGeometryTest, "UE.MCP.Level.ShapeTrace.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShapeTraceGeometryTest::RunTest(const FString& Parameters)
{
	FScopedTraceTestWorld Fixture;
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	UBoxComponent* Target = Fixture.Box(FVector(300, 25, 0));
	auto Params = Fixture.Request();
	const auto Line = TraceTestRun(Registry, TEXT("line_trace"), Params);
	TestFalse(TEXT("thin ray misses offset box"), Line->GetBoolField(TEXT("hit")));
	for (const TCHAR* Shape : { TEXT("sphere"), TEXT("box"), TEXT("capsule") })
	{
		Params->SetStringField(TEXT("shape"), Shape);
		Params->SetObjectField(TEXT("halfExtent"), MCPVec3ToJsonObject(FVector(20)));
		Params->SetNumberField(TEXT("halfHeight"), 40);
		const auto Hit = TraceTestRun(Registry, TEXT("shape_trace"), Params);
		TestTrue(TEXT("shape succeeds"), Hit->GetBoolField(TEXT("success")));
		TestTrue(TEXT("shape reaches offset box"), Hit->GetBoolField(TEXT("hit")));
		TestEqual(TEXT("precise actor path"), Hit->GetStringField(TEXT("actorPath")), Target->GetOwner()->GetPathName());
		TestFalse(TEXT("starts outside geometry"), Hit->GetBoolField(TEXT("bStartPenetrating")));
		TestEqual(TEXT("world echo"), Hit->GetStringField(TEXT("world")), FString(TEXT("game")));
	}
	Params->SetStringField(TEXT("shape"), TEXT("sphere"));
	Params->SetArrayField(TEXT("ignoreActors"), { MakeShared<FJsonValueString>(Target->GetOwner()->GetPathName()) });
	TestFalse(TEXT("path ignore removes target"), TraceTestRun(Registry, TEXT("shape_trace"), Params)->GetBoolField(TEXT("hit")));
	Params->SetArrayField(TEXT("ignoreActors"), { MakeShared<FJsonValueString>(Target->GetOwner()->GetActorLabel()) });
	TestFalse(TEXT("label ignore removes target"), TraceTestRun(Registry, TEXT("shape_trace"), Params)->GetBoolField(TEXT("hit")));
	Params->RemoveField(TEXT("ignoreActors"));
	Params->SetObjectField(TEXT("start"), MCPVec3ToJsonObject(Target->GetComponentLocation()));
	TestTrue(TEXT("initial penetration is reported"), TraceTestRun(Registry, TEXT("shape_trace"), Params)->GetBoolField(TEXT("bStartPenetrating")));
	Params = Fixture.Request();
	Fixture.Box(FVector(100, 0, 0), ECR_Overlap);
	Params->SetBoolField(TEXT("multi"), true);
	const auto Multi = TraceTestRun(Registry, TEXT("shape_trace"), Params);
	const auto& Hits = Multi->GetArrayField(TEXT("hits"));
	TestEqual(TEXT("multi returns touch then blocker"), Hits.Num(), 2);
	if (Hits.Num() == 2)
	{
		TestFalse(TEXT("first hit overlaps"), Hits[0]->AsObject()->GetBoolField(TEXT("blockingHit")));
		TestTrue(TEXT("last hit blocks"), Hits[1]->AsObject()->GetBoolField(TEXT("blockingHit")));
	}
	for (int32 Index = 0; Index < 70; ++Index) Fixture.Box(FVector(40 + Index * 3, 0, 0), ECR_Overlap);
	const auto Capped = TraceTestRun(Registry, TEXT("shape_trace"), Params);
	TestEqual(TEXT("multi payload capped"), Capped->GetArrayField(TEXT("hits")).Num(), 64);
	TestTrue(TEXT("truncation explicit"), Capped->GetBoolField(TEXT("truncated")));
	const auto PythonHits = UMCPTraceLibrary::ShapeTrace(FVector::ZeroVector, FVector(1000, 0, 0), TEXT("sphere"),
		{}, 20, FVector::ZeroVector, 0, TEXT("pie"), Fixture.Instance);
	TestEqual(TEXT("Python single result"), PythonHits.Num(), 1);
	if (PythonHits.Num() == 1) TestTrue(TEXT("Python uses same geometry"), PythonHits[0].bHit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShapeTraceValidationTest, "UE.MCP.Level.ShapeTrace.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShapeTraceValidationTest::RunTest(const FString& Parameters)
{
	FScopedTraceTestWorld Fixture;
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	for (const TCHAR* Shape : { TEXT("sphere"), TEXT("capsule"), TEXT("box"), TEXT("unknown") })
	{
		auto Params = Fixture.Request();
		Params->SetStringField(TEXT("shape"), Shape);
		Params->SetNumberField(TEXT("radius"), -1);
		TestFalse(TEXT("invalid dimensions fail"), TraceTestRun(Registry, TEXT("shape_trace"), Params)->GetBoolField(TEXT("success")));
	}
	auto Params = Fixture.Request();
	Params->SetStringField(TEXT("channel"), TEXT("MCPNoSuchChannel"));
	TestTrue(TEXT("unknown channel is named"), TraceTestRun(Registry, TEXT("shape_trace"), Params)->GetStringField(TEXT("error")).Contains(TEXT("MCPNoSuchChannel")));
	Params = Fixture.Request();
	Params->RemoveField(TEXT("end"));
	Params->SetObjectField(TEXT("direction"), MCPVec3ToJsonObject(FVector::ZeroVector));
	TestFalse(TEXT("zero direction fails"), TraceTestRun(Registry, TEXT("shape_trace"), Params)->GetBoolField(TEXT("success")));
	Params->SetNumberField(TEXT("pieInstance"), Fixture.Instance + 1);
	TestFalse(TEXT("missing PIE instance never traces editor world"), TraceTestRun(Registry, TEXT("shape_trace"), Params)->GetBoolField(TEXT("success")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FViewTraceGeometryTest, "UE.MCP.PIE.ViewTrace.GeometryAndCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FViewTraceGeometryTest::RunTest(const FString& Parameters)
{
	FScopedTraceTestWorld Fixture;
	FMCPHandlerRegistry Registry;
	FPIEHandlers::RegisterHandlers(Registry);
	FLevelHandlers::RegisterHandlers(Registry);
	UGameInstance* GameInstance = NewObject<UGameInstance>(Fixture.World);
	Fixture.World->SetGameInstance(GameInstance);
	// ULocalPlayer is ClassWithin UEngine; UGameInstance::CreateLocalPlayer uses the engine as its outer too.
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	GameInstance->AddLocalPlayer(Player, FPlatformUserId::CreateFromInternalId(0));
	APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("controller"), Controller)) return false;
	Player->PlayerController = Controller;
	FVector View;
	FRotator Rotation;
	Controller->GetPlayerViewPoint(View, Rotation);
	UBoxComponent* Target = Fixture.Box(View + Rotation.Vector() * 300);
	auto Params = MakeShared<FJsonObject>();
	Params->SetNumberField(TEXT("pieInstance"), Fixture.Instance);
	Params->SetNumberField(TEXT("sweepRadius"), 20);
	const auto Result = TraceTestRun(Registry, TEXT("pie_view_trace"), Params);
	if (!TestTrue(TEXT("view trace succeeds"), Result->GetBoolField(TEXT("success")))) return false;
	TestTrue(TEXT("live view position"), OptionalVec3(Result, TEXT("viewLocation")).Equals(View));
	TestTrue(TEXT("live view direction"), OptionalVec3(Result, TEXT("viewDirection")).Equals(Rotation.Vector()));
	for (const TCHAR* Key : { TEXT("line"), TEXT("sweep") })
	{
		const auto Hit = Result->GetObjectField(Key);
		TestTrue(TEXT("view ray hits fixture"), Hit->GetBoolField(TEXT("hit")));
		TestEqual(TEXT("collision enabled"), Hit->GetStringField(TEXT("collisionEnabled")), FString(TEXT("QueryOnly")));
		TestEqual(TEXT("channel response"), Hit->GetStringField(TEXT("responseOnChannel")), FString(TEXT("Block")));
		TestTrue(TEXT("visibility response"), Hit->GetBoolField(TEXT("blocksVisibility")));
		TestEqual(TEXT("owner identity"), Hit->GetStringField(TEXT("ownerActorLabel")), Target->GetOwner()->GetActorLabel());
	}
	auto Query = MakeShared<FJsonObject>();
	Query->SetStringField(TEXT("world"), TEXT("pie"));
	Query->SetNumberField(TEXT("pieInstance"), Fixture.Instance);
	Query->SetStringField(TEXT("componentClass"), TEXT("BoxComponent"));
	Query->SetBoolField(TEXT("includeCollision"), true);
	Query->SetStringField(TEXT("collisionChannel"), TEXT("Visibility"));
	const auto Components = TraceTestRun(Registry, TEXT("query_components"), Query);
	const auto& Rows = Components->GetArrayField(TEXT("rows"));
	TestEqual(TEXT("one box component"), Rows.Num(), 1);
	if (Rows.Num() == 1)
	{
		TestEqual(TEXT("query collision response"), Rows[0]->AsObject()->GetStringField(TEXT("responseOnChannel")), FString(TEXT("Block")));
		TestEqual(TEXT("query object type"), Rows[0]->AsObject()->GetStringField(TEXT("objectType")), FString(TEXT("WorldDynamic")));
	}
	Query->RemoveField(TEXT("collisionChannel"));
	const auto Full = TraceTestRun(Registry, TEXT("query_components"), Query);
	if (Full->GetArrayField(TEXT("rows")).Num() == 1)
	{
		const auto Row = Full->GetArrayField(TEXT("rows"))[0]->AsObject();
		TestEqual(TEXT("default response baseline"), Row->GetStringField(TEXT("defaultResponse")), FString(TEXT("Block")));
		TestTrue(TEXT("response list bounded"), Row->GetArrayField(TEXT("responses")).Num() <= 32);
	}
	// Put a collision component owned by the viewing pawn in front of its
	// camera. The root stays at the viewpoint, so self-collision can be tested
	// without relying on a project's pawn or camera manager configuration.
	APawn* Pawn = Fixture.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("viewing pawn"), Pawn)) return false;
	auto* Root = NewObject<USceneComponent>(Pawn, NAME_None, RF_Transient);
	Pawn->SetRootComponent(Root);
	Pawn->AddInstanceComponent(Root);
	Root->RegisterComponent();
	Controller->Possess(Pawn);
	Controller->GetPlayerViewPoint(View, Rotation);
	auto* PawnBox = NewObject<UBoxComponent>(Pawn, NAME_None, RF_Transient);
	Pawn->AddInstanceComponent(PawnBox);
	PawnBox->SetupAttachment(Root);
	PawnBox->SetBoxExtent(FVector(10), false);
	PawnBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PawnBox->SetCollisionResponseToAllChannels(ECR_Block);
	PawnBox->RegisterComponent();
	PawnBox->SetWorldLocation(View + Rotation.Vector() * 100);
	Target->SetWorldLocation(View + Rotation.Vector() * 300);
	const auto IgnoringPawn = TraceTestRun(Registry, TEXT("pie_view_trace"), Params);
	TestEqual(TEXT("default ignores viewing pawn"), IgnoringPawn->GetObjectField(TEXT("line"))->GetStringField(TEXT("actorPath")), Target->GetOwner()->GetPathName());
	Params->SetBoolField(TEXT("ignorePawn"), false);
	const auto IncludingPawn = TraceTestRun(Registry, TEXT("pie_view_trace"), Params);
	TestEqual(TEXT("opt-out hits viewing pawn"), IncludingPawn->GetObjectField(TEXT("line"))->GetStringField(TEXT("actorPath")), Pawn->GetPathName());
	Params->SetBoolField(TEXT("ignorePawn"), true);
	Params->SetStringField(TEXT("channel"), TEXT("Camera"));
	const auto Camera = TraceTestRun(Registry, TEXT("pie_view_trace"), Params);
	TestFalse(TEXT("selected channel changes query"), Camera->GetObjectField(TEXT("line"))->GetBoolField(TEXT("hit")));
	Params->RemoveField(TEXT("channel"));
	Params->RemoveField(TEXT("sweepRadius"));
	const auto LineOnly = TraceTestRun(Registry, TEXT("pie_view_trace"), Params);
	TestFalse(TEXT("optional sweep omitted"), LineOnly->HasField(TEXT("sweep")));
	TestTrue(TEXT("default distance 1000 cm"), FMath::IsNearlyEqual(
		(OptionalVec3(LineOnly->GetObjectField(TEXT("line")), TEXT("end")) - View).Size(), 1000.0));
	Params->SetNumberField(TEXT("playerIndex"), 1);
	TestFalse(TEXT("missing local player never falls back"), TraceTestRun(Registry, TEXT("pie_view_trace"), Params)->GetBoolField(TEXT("success")));
	Params->RemoveField(TEXT("playerIndex"));
	Params->SetNumberField(TEXT("pieInstance"), Fixture.Instance + 1);
	TestFalse(TEXT("unavailable instance fails clearly"), TraceTestRun(Registry, TEXT("pie_view_trace"), Params)->GetBoolField(TEXT("success")));
	Params->SetNumberField(TEXT("playerIndex"), -1);
	TestTrue(TEXT("invalid player names parameter"), TraceTestRun(Registry, TEXT("pie_view_trace"), Params)->GetStringField(TEXT("error")).Contains(TEXT("playerIndex")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLineTraceMultiTest, "UE.MCP.Level.LineTrace.Multi",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLineTraceMultiTest::RunTest(const FString& Parameters)
{
	FScopedTraceTestWorld Fixture;
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	UBoxComponent* Touch = Fixture.Box(FVector(100, 0, 0), ECR_Overlap);
	UBoxComponent* Blocker = Fixture.Box(FVector(300, 0, 0));
	Fixture.Box(FVector(500, 0, 0));
	auto Params = Fixture.Request();
	Params->RemoveField(TEXT("shape"));
	Params->RemoveField(TEXT("radius"));
	const auto Single = TraceTestRun(Registry, TEXT("line_trace"), Params);
	TestTrue(TEXT("single trace blocks"), Single->GetBoolField(TEXT("hit")));
	TestEqual(TEXT("single skips the overlap"), Single->GetStringField(TEXT("actorPath")), Blocker->GetOwner()->GetPathName());
	TestFalse(TEXT("single has no hits array"), Single->HasField(TEXT("hits")));
	Params->SetBoolField(TEXT("multi"), true);
	const auto Multi = TraceTestRun(Registry, TEXT("line_trace"), Params);
	TestTrue(TEXT("multi line succeeds"), Multi->GetBoolField(TEXT("success")));
	TestTrue(TEXT("multi reports the blocker"), Multi->GetBoolField(TEXT("hit")));
	const auto& Hits = Multi->GetArrayField(TEXT("hits"));
	TestEqual(TEXT("overlap then blocker, nothing past it"), Hits.Num(), 2);
	if (Hits.Num() == 2)
	{
		TestEqual(TEXT("first row is the overlap"), Hits[0]->AsObject()->GetStringField(TEXT("actorPath")), Touch->GetOwner()->GetPathName());
		TestFalse(TEXT("overlap row is not blocking"), Hits[0]->AsObject()->GetBoolField(TEXT("blockingHit")));
		TestEqual(TEXT("second row is the blocker"), Hits[1]->AsObject()->GetStringField(TEXT("actorPath")), Blocker->GetOwner()->GetPathName());
		TestTrue(TEXT("blocker row is blocking"), Hits[1]->AsObject()->GetBoolField(TEXT("blockingHit")));
		TestTrue(TEXT("blocker impact is on its near face"), FMath::IsNearlyEqual(
			OptionalVec3(Hits[1]->AsObject(), TEXT("impactPoint")).X, 290.0, 0.5));
	}
	TestEqual(TEXT("totalHits matches"), static_cast<int32>(Multi->GetNumberField(TEXT("totalHits"))), 2);

	auto Bulk = MakeShared<FJsonObject>();
	Bulk->SetStringField(TEXT("world"), TEXT("pie"));
	Bulk->SetNumberField(TEXT("pieInstance"), Fixture.Instance);
	auto Item = MakeShared<FJsonObject>();
	Item->SetObjectField(TEXT("start"), MCPVec3ToJsonObject(FVector::ZeroVector));
	Item->SetObjectField(TEXT("end"), MCPVec3ToJsonObject(FVector(1000, 0, 0)));
	Item->SetBoolField(TEXT("multi"), true);
	Bulk->SetArrayField(TEXT("traces"), { MakeShared<FJsonValueObject>(Item) });
	const auto BulkResult = TraceTestRun(Registry, TEXT("bulk_line_trace"), Bulk);
	const auto& BulkRows = BulkResult->GetArrayField(TEXT("results"));
	TestEqual(TEXT("bulk returns one result"), BulkRows.Num(), 1);
	if (BulkRows.Num() == 1)
		TestEqual(TEXT("bulk item honours multi"), BulkRows[0]->AsObject()->GetArrayField(TEXT("hits")).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorsInBoxTest, "UE.MCP.Level.ActorsInBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FActorsInBoxTest::RunTest(const FString& Parameters)
{
	FScopedTraceTestWorld Fixture;
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	UBoxComponent* Inside = Fixture.Box(FVector(0, 0, 0));
	Inside->GetOwner()->SetActorLabel(TEXT("MCPBoxInside"), false);
	UBoxComponent* Straddling = Fixture.Box(FVector(100, 0, 0));
	Straddling->GetOwner()->SetActorLabel(TEXT("MCPBoxStraddling"), false);
	UBoxComponent* Outside = Fixture.Box(FVector(500, 0, 0));
	Outside->GetOwner()->SetActorLabel(TEXT("MCPBoxOutside"), false);
	UBoxComponent* Hidden = Fixture.Box(FVector(0, 50, 0));
	Hidden->GetOwner()->SetActorLabel(TEXT("MCPBoxHidden"), false);
	Hidden->GetOwner()->SetActorHiddenInGame(true);

	auto Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("world"), TEXT("pie"));
	Params->SetNumberField(TEXT("pieInstance"), Fixture.Instance);
	Params->SetObjectField(TEXT("min"), MCPVec3ToJsonObject(FVector(-100, -100, -100)));
	Params->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(FVector(95, 100, 100)));

	auto Labels = [](const TSharedPtr<FJsonObject>& Result)
	{
		TMap<FString, TSharedPtr<FJsonObject>> Out;
		for (const TSharedPtr<FJsonValue>& Row : Result->GetArrayField(TEXT("actors")))
			Out.Add(Row->AsObject()->GetStringField(TEXT("label")), Row->AsObject());
		return Out;
	};

	const auto Overlap = TraceTestRun(Registry, TEXT("actors_in_box"), Params);
	TestTrue(TEXT("overlap query succeeds"), Overlap->GetBoolField(TEXT("success")));
	const auto OverlapRows = Labels(Overlap);
	TestTrue(TEXT("inside actor listed"), OverlapRows.Contains(TEXT("MCPBoxInside")));
	TestTrue(TEXT("straddling actor listed in overlap mode"), OverlapRows.Contains(TEXT("MCPBoxStraddling")));
	TestFalse(TEXT("outside actor excluded"), OverlapRows.Contains(TEXT("MCPBoxOutside")));
	TestFalse(TEXT("hidden actor excluded by default"), OverlapRows.Contains(TEXT("MCPBoxHidden")));
	if (const TSharedPtr<FJsonObject>* Row = OverlapRows.Find(TEXT("MCPBoxInside")))
	{
		TestTrue(TEXT("inside row is contained"), (*Row)->GetBoolField(TEXT("contained")));
		TestEqual(TEXT("row carries the actor path"), (*Row)->GetStringField(TEXT("path")), Inside->GetOwner()->GetPathName());
		const auto Bounds = (*Row)->GetObjectField(TEXT("bounds"));
		TestTrue(TEXT("bounds extent is the box extent"), OptionalVec3(Bounds, TEXT("extent")).Equals(FVector(10), 0.01));
		TestTrue(TEXT("bounds min is origin minus extent"), OptionalVec3(Bounds, TEXT("min")).Equals(FVector(-10), 0.01));
		TestEqual(TEXT("bounds come from components"), Bounds->GetStringField(TEXT("source")), FString(TEXT("components")));
		TestTrue(TEXT("location reported"), OptionalVec3(*Row, TEXT("location")).Equals(FVector::ZeroVector, 0.01));
		TestFalse(TEXT("visible in game"), (*Row)->GetBoolField(TEXT("hiddenInGame")));
	}
	if (const TSharedPtr<FJsonObject>* Row = OverlapRows.Find(TEXT("MCPBoxStraddling")))
		TestFalse(TEXT("straddling row is not contained"), (*Row)->GetBoolField(TEXT("contained")));

	Params->SetStringField(TEXT("mode"), TEXT("contained"));
	const auto ContainedRows = Labels(TraceTestRun(Registry, TEXT("actors_in_box"), Params));
	TestTrue(TEXT("contained keeps inside actor"), ContainedRows.Contains(TEXT("MCPBoxInside")));
	TestFalse(TEXT("contained drops straddling actor"), ContainedRows.Contains(TEXT("MCPBoxStraddling")));

	Params->SetStringField(TEXT("mode"), TEXT("overlap"));
	Params->SetBoolField(TEXT("includeHidden"), true);
	const auto WithHidden = Labels(TraceTestRun(Registry, TEXT("actors_in_box"), Params));
	TestTrue(TEXT("includeHidden lists the hidden actor"), WithHidden.Contains(TEXT("MCPBoxHidden")));
	if (const TSharedPtr<FJsonObject>* Row = WithHidden.Find(TEXT("MCPBoxHidden")))
		TestTrue(TEXT("hidden row reports hiddenInGame"), (*Row)->GetBoolField(TEXT("hiddenInGame")));

	Params->SetStringField(TEXT("classFilter"), TEXT("NoSuchClassName"));
	TestEqual(TEXT("classFilter narrows"), TraceTestRun(Registry, TEXT("actors_in_box"), Params)->GetArrayField(TEXT("actors")).Num(), 0);
	Params->RemoveField(TEXT("classFilter"));

	// Only the four fixture boxes carry a primitive, so restrict to colliding bounds
	// to keep any engine-spawned helper actor out of the count.
	Params->SetBoolField(TEXT("onlyColliding"), true);
	Params->SetNumberField(TEXT("limit"), 1);
	const auto Limited = TraceTestRun(Registry, TEXT("actors_in_box"), Params);
	TestEqual(TEXT("limit cuts rows"), Limited->GetArrayField(TEXT("actors")).Num(), 1);
	TestTrue(TEXT("truncation explicit"), Limited->GetBoolField(TEXT("truncated")));
	TestEqual(TEXT("total counts every match"), static_cast<int32>(Limited->GetNumberField(TEXT("total"))), 3);
	Params->RemoveField(TEXT("limit"));

	Params->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(FVector(-200, 100, 100)));
	TestFalse(TEXT("inverted box refused"), TraceTestRun(Registry, TEXT("actors_in_box"), Params)->GetBoolField(TEXT("success")));
	Params->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(FVector(95, 100, 100)));
	Params->SetStringField(TEXT("mode"), TEXT("sideways"));
	TestFalse(TEXT("unknown mode refused"), TraceTestRun(Registry, TEXT("actors_in_box"), Params)->GetBoolField(TEXT("success")));
	return true;
}
#endif
