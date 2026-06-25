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
	Registry.RegisterHandler(TEXT("get_spline_info"), &ReadSpline);
	Registry.RegisterHandler(TEXT("set_spline_points"), &SetSplinePoints);
}

TSharedPtr<FJsonValue> FSplineHandlers::ReadSpline(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	// #553: support editor or PIE world so runtime spline state is readable.
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	AActor* Actor = FindActorByLabelOrName(World, ActorLabel);
	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	// #553: optional componentName to target a specific (custom) spline component
	// when an actor has more than one. Custom spline subclasses derive from
	// USplineComponent, so FindComponentByClass still resolves them.
	USplineComponent* SplineComp = nullptr;
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	if (!ComponentName.IsEmpty())
	{
		TArray<USplineComponent*> Comps;
		Actor->GetComponents<USplineComponent>(Comps);
		for (USplineComponent* C : Comps)
		{
			if (C && C->GetName() == ComponentName) { SplineComp = C; break; }
		}
	}
	else
	{
		SplineComp = Actor->FindComponentByClass<USplineComponent>();
	}
	if (!SplineComp)
	{
		return MCPError(FString::Printf(TEXT("Actor '%s' has no matching SplineComponent"), *ActorLabel));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), SplineComp->GetName());
	Result->SetStringField(TEXT("componentClass"), SplineComp->GetClass()->GetName());
	Result->SetNumberField(TEXT("splinePointCount"), SplineComp->GetNumberOfSplinePoints());
	Result->SetBoolField(TEXT("closedLoop"), SplineComp->IsClosedLoop());
	Result->SetNumberField(TEXT("splineLength"), SplineComp->GetSplineLength());

	// #555: project a world point onto the spline. Returns the closest location,
	// the input key, distance along the spline, and the perpendicular distance.
	if (Params->HasField(TEXT("projectPoint")))
	{
		const FVector P = OptionalVec3(Params, TEXT("projectPoint"), FVector::ZeroVector);
		const FVector Closest = SplineComp->FindLocationClosestToWorldLocation(P, ESplineCoordinateSpace::World);
		const float InputKey = SplineComp->FindInputKeyClosestToWorldLocation(P);
		const float DistAlong = SplineComp->GetDistanceAlongSplineAtSplineInputKey(InputKey);
		TSharedPtr<FJsonObject> Proj = MakeShared<FJsonObject>();
		Proj->SetObjectField(TEXT("closestLocation"), MCPVec3ToJsonObject(Closest));
		Proj->SetNumberField(TEXT("inputKey"), InputKey);
		Proj->SetNumberField(TEXT("distanceAlongSpline"), DistAlong);
		Proj->SetNumberField(TEXT("distanceToSpline"), FVector::Dist(P, Closest));
		Proj->SetObjectField(TEXT("tangent"), MCPVec3ToJsonObject(SplineComp->GetTangentAtDistanceAlongSpline(DistAlong, ESplineCoordinateSpace::World)));
		Result->SetObjectField(TEXT("projection"), Proj);
	}

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

	AActor* Actor = FindActorByLabel(World, ActorLabel);
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
