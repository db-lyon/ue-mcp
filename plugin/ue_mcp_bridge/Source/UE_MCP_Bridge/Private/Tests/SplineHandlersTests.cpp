// Focused native coverage for exact component targeting and validate-before-mutate.
#if WITH_DEV_AUTOMATION_TESTS
#include "HandlerRegistry.h"
#include "Handlers/SplineHandlers.h"
#include "Components/SplineComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPNativeSplineAuthoringTest, "UE.MCP.Spline.NativeAuthoringContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNativeSplineAuthoringTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("an editor world is available"), GEditor != nullptr)) return false;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!TestTrue(TEXT("the editor world is valid"), World != nullptr)) return false;
	const bool WasDirty = World->GetOutermost()->IsDirty();
	FActorSpawnParameters SpawnParams; SpawnParams.ObjectFlags = RF_Transient | RF_Transactional;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TestTrue(TEXT("a transient actor can be spawned"), Actor != nullptr)) return false;
	Actor->SetActorLabel(TEXT("MCP_SplineContractTest"));
	USplineComponent* First = NewObject<USplineComponent>(Actor, TEXT("PrimarySpline"));
	USplineComponent* Second = NewObject<USplineComponent>(Actor, TEXT("SecondarySpline"));
	Actor->AddInstanceComponent(First); Actor->AddInstanceComponent(Second);
	Actor->SetRootComponent(First);
	Second->SetupAttachment(First);
	First->RegisterComponent(); Second->RegisterComponent();
	Actor->SetActorTransform(FTransform(FRotator(0.0, 45.0, 0.0), FVector(100.0, 200.0, 50.0), FVector(2.0, 3.0, 1.5)));
	TestFalse(TEXT("component transform is nonidentity"), Second->GetComponentTransform().Equals(FTransform::Identity));
	First->AddSplinePoint(FVector(10.0, 0.0, 0.0), ESplineCoordinateSpace::World, false);
	Second->AddSplinePoint(FVector(20.0, 0.0, 0.0), ESplineCoordinateSpace::World, false);
	First->UpdateSpline(); Second->UpdateSpline();
	Second->SetRotationAtSplinePoint(0, FRotator(12,31,7), ESplineCoordinateSpace::Local, false);
	Second->SetScaleAtSplinePoint(1, FVector(1.2,.8,2.1), false);
	Second->SetClosedLoopAtPosition(true, 5.f);
	const FVector OriginalPoint = Second->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
	const FQuat OriginalRotation = Second->GetSplinePointsRotation().Points[0].OutVal;
	const FVector OriginalScale = Second->GetScaleAtSplinePoint(1);
	const float OriginalLength = Second->GetSplineLength();
	const int32 OriginalCount = Second->GetNumberOfSplinePoints();

	FMCPHandlerRegistry Registry; FSplineHandlers::RegisterHandlers(Registry);
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("actorLabel"), TEXT("MCP_SplineContractTest"));
	Params->SetStringField(TEXT("componentName"), TEXT("SecondarySpline"));
	TArray<TSharedPtr<FJsonValue>> Points;
	TSharedPtr<FJsonObject> Linear = MakeShared<FJsonObject>();
	Linear->SetNumberField(TEXT("x"), 30.0); Linear->SetNumberField(TEXT("y"), 0.0); Linear->SetNumberField(TEXT("z"), 0.0); Linear->SetStringField(TEXT("pointType"), TEXT("Linear"));
	Points.Add(MakeShared<FJsonValueObject>(Linear));
	TSharedPtr<FJsonObject> Custom = MakeShared<FJsonObject>();
	Custom->SetNumberField(TEXT("x"), 40.0); Custom->SetNumberField(TEXT("y"), 0.0); Custom->SetNumberField(TEXT("z"), 0.0); Custom->SetStringField(TEXT("pointType"), TEXT("CurveCustomTangent"));
	for (const TCHAR* Name : {TEXT("arriveTangent"), TEXT("leaveTangent")}) { TSharedPtr<FJsonObject> Tangent = MakeShared<FJsonObject>(); Tangent->SetNumberField(TEXT("x"), Name[0] == 'a' ? 1.0 : 2.0); Tangent->SetNumberField(TEXT("y"), 0.0); Tangent->SetNumberField(TEXT("z"), 0.0); Custom->SetObjectField(Name, Tangent); }
	Points.Add(MakeShared<FJsonValueObject>(Custom)); Params->SetArrayField(TEXT("points"), Points);
	const int32 FirstBefore = First->GetNumberOfSplinePoints();
	const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("set_spline_points"), Params);
	TestTrue(TEXT("named spline update succeeds"), Response.IsValid() && Response->AsObject()->GetBoolField(TEXT("success")));
	TestEqual(TEXT("only the exact named component is changed"), First->GetNumberOfSplinePoints(), FirstBefore);
	TestEqual(TEXT("the target receives both typed points"), Second->GetNumberOfSplinePoints(), 2);
	TestEqual(TEXT("the first target point is linear"), Second->GetSplinePointType(0), ESplinePointType::Linear);
	TestEqual(TEXT("the custom tangent point keeps its type"), Second->GetSplinePointType(1), ESplinePointType::CurveCustomTangent);
	TestTrue(TEXT("world-space input is not transformed twice"), Second->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World).Equals(FVector(30.0, 0.0, 0.0), 0.01));

	const FVector BeforeMalformed = Second->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
	TSharedPtr<FJsonObject> Malformed = MakeShared<FJsonObject>(); Malformed->SetStringField(TEXT("actorLabel"), TEXT("MCP_SplineContractTest")); Malformed->SetStringField(TEXT("componentName"), TEXT("SecondarySpline"));
	TArray<TSharedPtr<FJsonValue>> BadPoints; BadPoints.Add(MakeShared<FJsonValueString>(TEXT("not-a-point"))); Malformed->SetArrayField(TEXT("points"), BadPoints);
	const TSharedPtr<FJsonValue> MalformedResponse = Registry.ExecuteHandler(TEXT("set_spline_points"), Malformed);
	TestFalse(TEXT("malformed input is rejected"), MalformedResponse.IsValid() && MalformedResponse->AsObject()->GetBoolField(TEXT("success")));
	TestEqual(TEXT("rejected input leaves spline unchanged"), Second->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World), BeforeMalformed);
	Linear->SetNumberField(TEXT("inputKey"), 0); Custom->SetNumberField(TEXT("inputKey"), 1e-9);
	const auto NearKeys = Registry.ExecuteHandler(TEXT("set_spline_points"), Params);
	TestFalse(TEXT("engine-near-equal keys are rejected"), NearKeys->AsObject()->GetBoolField(TEXT("success")));
	TestEqual(TEXT("near-equal keys leave points intact"), Second->GetNumberOfSplinePoints(), 2);
	const auto Rollback = Response->AsObject()->GetObjectField(TEXT("rollback"))->GetObjectField(TEXT("payload"));
	const auto Restored = Registry.ExecuteHandler(TEXT("set_spline_points"), Rollback);
	TestTrue(TEXT("MCP rollback succeeds"), Restored->AsObject()->GetBoolField(TEXT("success")));
	TestEqual(TEXT("rollback restores count"), Second->GetNumberOfSplinePoints(), OriginalCount);
	TestTrue(TEXT("rollback restores world position"), Second->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World).Equals(OriginalPoint,.01));
	TestTrue(TEXT("rollback restores stored rotation"), Second->GetSplinePointsRotation().Points[0].OutVal.Equals(OriginalRotation,1e-5));
	TestTrue(TEXT("rollback restores scale"), Second->GetScaleAtSplinePoint(1).Equals(OriginalScale,1e-5));
	TestTrue(TEXT("rollback restores loop parameterization"), FMath::IsNearlyEqual(Second->GetSplineLength(),OriginalLength,.01f));
	TestTrue(TEXT("successful mutation marks package dirty"), World->GetOutermost()->IsDirty());
	GEditor->UndoTransaction();
	TestEqual(TEXT("Undo restores the state before rollback"), Second->GetNumberOfSplinePoints(), 2);
	GEditor->RedoTransaction();
	TestEqual(TEXT("Redo restores original point count"), Second->GetNumberOfSplinePoints(), OriginalCount);
	Actor->Destroy();
	World->GetOutermost()->SetDirtyFlag(WasDirty);
	return true;
}
#endif
