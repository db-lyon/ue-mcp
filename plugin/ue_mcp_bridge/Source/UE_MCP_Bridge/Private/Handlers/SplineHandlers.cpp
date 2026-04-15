#include "SplineHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

void FSplineHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("create_spline_actor"), &CreateSplineActor);
	Registry.RegisterHandler(TEXT("read_spline"), &ReadSpline);
	Registry.RegisterHandler(TEXT("get_spline_info"), &ReadSpline);
	Registry.RegisterHandler(TEXT("set_spline_points"), &SetSplinePoints);
}

TSharedPtr<FJsonValue> FSplineHandlers::CreateSplineActor(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FString Label = OptionalString(Params, TEXT("label"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (!Label.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Spline actor '%s' already exists"), *Label));
				}
				auto Existing = MCPSuccess();
				MCPSetExisted(Existing);
				Existing->SetStringField(TEXT("actorLabel"), Label);
				return MCPResult(Existing);
			}
		}
	}

	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj))
	{
		(*LocationObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocationObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocationObj)->TryGetNumberField(TEXT("z"), Location.Z);
	}

	FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnTransform);
	if (!NewActor)
	{
		return MCPError(TEXT("Failed to spawn actor"));
	}

	// Add a root scene component if none exists
	USceneComponent* RootComp = NewObject<USceneComponent>(NewActor, TEXT("DefaultSceneRoot"));
	RootComp->RegisterComponent();
	NewActor->SetRootComponent(RootComp);

	// Add the spline component
	USplineComponent* SplineComp = NewObject<USplineComponent>(NewActor, TEXT("SplineComponent"));
	SplineComp->SetupAttachment(RootComp);
	SplineComp->RegisterComponent();
	NewActor->AddInstanceComponent(SplineComp);

	if (!Label.IsEmpty())
	{
		NewActor->SetActorLabel(Label);
	}

	// Optionally set closed loop
	bool bClosedLoop = OptionalBool(Params, TEXT("closedLoop"), false);
	if (Params->HasField(TEXT("closedLoop")))
	{
		SplineComp->SetClosedLoop(bClosedLoop);
	}

	// Optionally set initial points from array
	const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("points"), PointsArray) && PointsArray->Num() > 0)
	{
		SplineComp->ClearSplinePoints(false);
		for (int32 i = 0; i < PointsArray->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* PointObj = nullptr;
			if ((*PointsArray)[i]->TryGetObject(PointObj))
			{
				FVector Point = FVector::ZeroVector;
				(*PointObj)->TryGetNumberField(TEXT("x"), Point.X);
				(*PointObj)->TryGetNumberField(TEXT("y"), Point.Y);
				(*PointObj)->TryGetNumberField(TEXT("z"), Point.Z);
				SplineComp->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
			}
		}
		SplineComp->UpdateSpline();
	}

	const FString FinalLabel = NewActor->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), FinalLabel);
	Result->SetStringField(TEXT("actorName"), NewActor->GetName());
	Result->SetNumberField(TEXT("splinePointCount"), SplineComp->GetNumberOfSplinePoints());
	Result->SetBoolField(TEXT("closedLoop"), SplineComp->IsClosedLoop());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), FinalLabel);
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSplineHandlers::ReadSpline(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// Find actor by label
	AActor* Actor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			Actor = *ActorIt;
			break;
		}
	}

	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	// Find spline component
	USplineComponent* SplineComp = Actor->FindComponentByClass<USplineComponent>();
	if (!SplineComp)
	{
		return MCPError(FString::Printf(TEXT("Actor '%s' does not have a SplineComponent"), *ActorLabel));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetNumberField(TEXT("splinePointCount"), SplineComp->GetNumberOfSplinePoints());
	Result->SetBoolField(TEXT("closedLoop"), SplineComp->IsClosedLoop());
	Result->SetNumberField(TEXT("splineLength"), SplineComp->GetSplineLength());

	// Return all control points with world-space locations
	TArray<TSharedPtr<FJsonValue>> PointsArray;
	for (int32 i = 0; i < SplineComp->GetNumberOfSplinePoints(); ++i)
	{
		TSharedPtr<FJsonObject> PointObj = MakeShared<FJsonObject>();
		PointObj->SetNumberField(TEXT("index"), i);

		FVector WorldPos = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
		LocationObj->SetNumberField(TEXT("x"), WorldPos.X);
		LocationObj->SetNumberField(TEXT("y"), WorldPos.Y);
		LocationObj->SetNumberField(TEXT("z"), WorldPos.Z);
		PointObj->SetObjectField(TEXT("worldLocation"), LocationObj);

		FVector LocalPos = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
		TSharedPtr<FJsonObject> LocalObj = MakeShared<FJsonObject>();
		LocalObj->SetNumberField(TEXT("x"), LocalPos.X);
		LocalObj->SetNumberField(TEXT("y"), LocalPos.Y);
		LocalObj->SetNumberField(TEXT("z"), LocalPos.Z);
		PointObj->SetObjectField(TEXT("localLocation"), LocalObj);

		FVector ArriveTangent = SplineComp->GetArriveTangentAtSplinePoint(i, ESplineCoordinateSpace::World);
		TSharedPtr<FJsonObject> ArriveObj = MakeShared<FJsonObject>();
		ArriveObj->SetNumberField(TEXT("x"), ArriveTangent.X);
		ArriveObj->SetNumberField(TEXT("y"), ArriveTangent.Y);
		ArriveObj->SetNumberField(TEXT("z"), ArriveTangent.Z);
		PointObj->SetObjectField(TEXT("arriveTangent"), ArriveObj);

		FVector LeaveTangent = SplineComp->GetLeaveTangentAtSplinePoint(i, ESplineCoordinateSpace::World);
		TSharedPtr<FJsonObject> LeaveObj = MakeShared<FJsonObject>();
		LeaveObj->SetNumberField(TEXT("x"), LeaveTangent.X);
		LeaveObj->SetNumberField(TEXT("y"), LeaveTangent.Y);
		LeaveObj->SetNumberField(TEXT("z"), LeaveTangent.Z);
		PointObj->SetObjectField(TEXT("leaveTangent"), LeaveObj);

		// Point type
		ESplinePointType::Type PointType = SplineComp->GetSplinePointType(i);
		FString PointTypeStr;
		switch (PointType)
		{
		case ESplinePointType::Linear: PointTypeStr = TEXT("Linear"); break;
		case ESplinePointType::Curve: PointTypeStr = TEXT("Curve"); break;
		case ESplinePointType::Constant: PointTypeStr = TEXT("Constant"); break;
		case ESplinePointType::CurveClamped: PointTypeStr = TEXT("CurveClamped"); break;
		case ESplinePointType::CurveCustomTangent: PointTypeStr = TEXT("CurveCustomTangent"); break;
		default: PointTypeStr = TEXT("Unknown"); break;
		}
		PointObj->SetStringField(TEXT("pointType"), PointTypeStr);

		PointsArray.Add(MakeShared<FJsonValueObject>(PointObj));
	}

	Result->SetArrayField(TEXT("points"), PointsArray);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSplineHandlers::SetSplinePoints(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("points"), PointsArray))
	{
		return MCPError(TEXT("Missing 'points' parameter (array of {x, y, z} objects)"));
	}

	REQUIRE_EDITOR_WORLD(World);

	// Find actor by label
	AActor* Actor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			Actor = *ActorIt;
			break;
		}
	}

	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	// Find spline component
	USplineComponent* SplineComp = Actor->FindComponentByClass<USplineComponent>();
	if (!SplineComp)
	{
		return MCPError(FString::Printf(TEXT("Actor '%s' does not have a SplineComponent"), *ActorLabel));
	}

	// Capture previous spline points for rollback
	TArray<TSharedPtr<FJsonValue>> PrevPoints;
	for (int32 i = 0; i < SplineComp->GetNumberOfSplinePoints(); ++i)
	{
		FVector P = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
		PObj->SetNumberField(TEXT("x"), P.X);
		PObj->SetNumberField(TEXT("y"), P.Y);
		PObj->SetNumberField(TEXT("z"), P.Z);
		PrevPoints.Add(MakeShared<FJsonValueObject>(PObj));
	}
	const bool bPrevClosedLoop = SplineComp->IsClosedLoop();

	// Clear existing points and add new ones
	SplineComp->ClearSplinePoints(false);

	for (int32 i = 0; i < PointsArray->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* PointObj = nullptr;
		if ((*PointsArray)[i]->TryGetObject(PointObj))
		{
			FVector Point = FVector::ZeroVector;
			(*PointObj)->TryGetNumberField(TEXT("x"), Point.X);
			(*PointObj)->TryGetNumberField(TEXT("y"), Point.Y);
			(*PointObj)->TryGetNumberField(TEXT("z"), Point.Z);
			SplineComp->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
		}
	}

	// Optionally set closed loop
	bool bClosedLoop = false;
	if (Params->TryGetBoolField(TEXT("closedLoop"), bClosedLoop))
	{
		SplineComp->SetClosedLoop(bClosedLoop);
	}

	SplineComp->UpdateSpline();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetNumberField(TEXT("splinePointCount"), SplineComp->GetNumberOfSplinePoints());
	Result->SetBoolField(TEXT("closedLoop"), SplineComp->IsClosedLoop());
	Result->SetNumberField(TEXT("splineLength"), SplineComp->GetSplineLength());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetArrayField(TEXT("points"), PrevPoints);
	Payload->SetBoolField(TEXT("closedLoop"), bPrevClosedLoop);
	MCPSetRollback(Result, TEXT("set_spline_points"), Payload);

	return MCPResult(Result);
}
