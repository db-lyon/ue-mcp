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
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

namespace
{
	static bool ReadFiniteNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& OutValue)
	{
		return Object.IsValid() && Object->TryGetNumberField(Field, OutValue) && FMath::IsFinite(OutValue);
	}

	static bool ReadPointVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FVector& OutValue)
	{
		const TSharedPtr<FJsonObject>* VectorObject = nullptr;
		if (!Object.IsValid() || !Object->TryGetObjectField(Field, VectorObject) || !VectorObject || !VectorObject->IsValid())
		{
			return false;
		}
		double X = 0.0, Y = 0.0, Z = 0.0;
		if (!ReadFiniteNumber(*VectorObject, TEXT("x"), X) || !ReadFiniteNumber(*VectorObject, TEXT("y"), Y) || !ReadFiniteNumber(*VectorObject, TEXT("z"), Z))
		{
			return false;
		}
		OutValue = FVector(X, Y, Z);
		return !OutValue.ContainsNaN();
	}

	static bool ParseSplinePointType(const FString& TypeName, ESplinePointType::Type& OutType)
	{
		if (TypeName == TEXT("Linear")) OutType = ESplinePointType::Linear;
		else if (TypeName == TEXT("Curve")) OutType = ESplinePointType::Curve;
		else if (TypeName == TEXT("CurveClamped")) OutType = ESplinePointType::CurveClamped;
		else if (TypeName == TEXT("Constant")) OutType = ESplinePointType::Constant;
		else if (TypeName == TEXT("CurveCustomTangent")) OutType = ESplinePointType::CurveCustomTangent;
		else return false;
		return true;
	}

	static FString SplinePointTypeName(ESplinePointType::Type Type)
	{
		switch (Type)
		{
		case ESplinePointType::Linear: return TEXT("Linear");
		case ESplinePointType::Curve: return TEXT("Curve");
		case ESplinePointType::CurveClamped: return TEXT("CurveClamped");
		case ESplinePointType::Constant: return TEXT("Constant");
		case ESplinePointType::CurveCustomTangent: return TEXT("CurveCustomTangent");
		default: return TEXT("Unknown");
		}
	}
}

void FSplineHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("get_spline_info"), &ReadSpline);
	Registry.RegisterHandler(TEXT("set_spline_points"), &SetSplinePoints);
}

TSharedPtr<FJsonValue> FSplineHandlers::ReadSpline(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	// #553: support editor or PIE world so runtime spline state is readable.
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	// #983: this is the action the report came in on. A level with several
	// copy-pasted road Blueprints all labelled the same thing answered with
	// whichever one the actor iterator reached first.
	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelOrName;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

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
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
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
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("points"), PointsArray))
	{
		return MCPError(TEXT("Missing 'points' parameter (array of {x, y, z} objects)"));
	}

	REQUIRE_EDITOR_WORLD(World);
	if (Params->HasField(TEXT("componentName")) && (!Params->TryGetField(TEXT("componentName")).IsValid() || Params->TryGetField(TEXT("componentName"))->Type != EJson::String))
		return MCPError(TEXT("componentName must be a string"));

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	// A named component is an exact component-name lookup; never silently fall back
	// to another spline when the caller has identified one.
	USplineComponent* SplineComp = nullptr;
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	if (Params->HasField(TEXT("componentName")) && ComponentName.IsEmpty()) return MCPError(TEXT("componentName cannot be empty"));
	if (!ComponentName.IsEmpty())
	{
		TArray<USplineComponent*> Components;
		Actor->GetComponents<USplineComponent>(Components);
		for (USplineComponent* Candidate : Components)
		{
			if (Candidate && Candidate->GetName() == ComponentName) { SplineComp = Candidate; break; }
		}
	}
	else
	{
		SplineComp = Actor->FindComponentByClass<USplineComponent>();
	}
	if (!SplineComp)
	{
		return MCPError(FString::Printf(TEXT("Actor '%s' has no matching SplineComponent%s"), *ActorLabel,
			ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *ComponentName)));
	}
	if (SplineComp->GetSplinePointsMetadata())
		return MCPError(TEXT("This spline has subclass metadata and requires its specialized authoring API"));

	struct FValidatedPoint
	{
		FVector Location = FVector::ZeroVector;
		FVector ArriveTangent = FVector::ZeroVector;
		FVector LeaveTangent = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector Scale = FVector::OneVector;
		float InputKey = 0.0f;
		bool bHasInputKey = false;
		bool bHasRotation = false;
		bool bHasTangents = false;
		ESplinePointType::Type Type = ESplinePointType::Curve;
	};
	TArray<FValidatedPoint> NewPoints;
	NewPoints.Reserve(PointsArray->Num());
	for (int32 Index = 0; Index < PointsArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* PointObject = nullptr;
		if (!(*PointsArray)[Index].IsValid() || !(*PointsArray)[Index]->TryGetObject(PointObject) || !PointObject || !PointObject->IsValid())
		{
			return MCPError(FString::Printf(TEXT("points[%d] must be an object with finite x, y, z"), Index));
		}
		FValidatedPoint Point;
		int32 CoordinateForms = 0;
		for (const TCHAR* Form : { TEXT("location"), TEXT("position"), TEXT("worldLocation") })
		{
			if (!(*PointObject)->HasField(Form)) continue;
			++CoordinateForms; FVector Checked;
			if (!ReadPointVector(*PointObject, Form, Checked)) return MCPError(FString::Printf(TEXT("points[%d] has malformed coordinate object"), Index));
		}
		if (CoordinateForms > 1 || (CoordinateForms && ((*PointObject)->HasField(TEXT("x")) || (*PointObject)->HasField(TEXT("y")) || (*PointObject)->HasField(TEXT("z")))))
			return MCPError(FString::Printf(TEXT("points[%d] must use one coordinate representation"), Index));
		if (!ReadPointVector(*PointObject, TEXT("location"), Point.Location))
		{
			if (!ReadPointVector(*PointObject, TEXT("position"), Point.Location) && !ReadPointVector(*PointObject, TEXT("worldLocation"), Point.Location))
			{
				double X = 0.0, Y = 0.0, Z = 0.0;
				if (!ReadFiniteNumber(*PointObject, TEXT("x"), X) || !ReadFiniteNumber(*PointObject, TEXT("y"), Y) || !ReadFiniteNumber(*PointObject, TEXT("z"), Z))
					return MCPError(FString::Printf(TEXT("points[%d] must contain finite x, y, z"), Index));
				Point.Location = FVector(X, Y, Z);
				if (Point.Location.ContainsNaN())
					return MCPError(FString::Printf(TEXT("points[%d] coordinates are outside the finite float range"), Index));
			}
		}
		double InputKey = 0.0;
		if ((*PointObject)->HasField(TEXT("inputKey")))
		{
			if (!ReadFiniteNumber(*PointObject, TEXT("inputKey"), InputKey) || !FMath::IsFinite(static_cast<float>(InputKey)))
				return MCPError(FString::Printf(TEXT("points[%d] inputKey must be finite"), Index));
			Point.InputKey = static_cast<float>(InputKey);
			Point.bHasInputKey = true;
		}
		FVector RotationVector;
		if ((*PointObject)->HasField(TEXT("rotation")))
		{
			Point.bHasRotation = true;
			if (!ReadPointVector(*PointObject, TEXT("rotation"), RotationVector))
				return MCPError(FString::Printf(TEXT("points[%d] rotation must contain finite x, y, z"), Index));
			Point.Rotation = FRotator(RotationVector.X, RotationVector.Y, RotationVector.Z);
		}
		if ((*PointObject)->HasField(TEXT("scale")) && !ReadPointVector(*PointObject, TEXT("scale"), Point.Scale))
			return MCPError(FString::Printf(TEXT("points[%d] scale must contain finite x, y, z"), Index));
		FString TypeName = TEXT("Curve");
		if ((*PointObject)->HasField(TEXT("pointType")) && (!(*PointObject)->TryGetField(TEXT("pointType")).IsValid() || (*PointObject)->TryGetField(TEXT("pointType"))->Type != EJson::String))
			return MCPError(FString::Printf(TEXT("points[%d] pointType must be a string"), Index));
		(*PointObject)->TryGetStringField(TEXT("pointType"), TypeName);
		if (!ParseSplinePointType(TypeName, Point.Type))
			return MCPError(FString::Printf(TEXT("points[%d] has unsupported pointType '%s'"), Index, *TypeName));
		const bool bHasArrive = ReadPointVector(*PointObject, TEXT("arriveTangent"), Point.ArriveTangent);
		const bool bHasLeave = ReadPointVector(*PointObject, TEXT("leaveTangent"), Point.LeaveTangent);
		if ((*PointObject)->HasField(TEXT("arriveTangent")) != (*PointObject)->HasField(TEXT("leaveTangent")))
			return MCPError(FString::Printf(TEXT("points[%d] must provide both arriveTangent and leaveTangent"), Index));
		if (((*PointObject)->HasField(TEXT("arriveTangent")) && !bHasArrive) || ((*PointObject)->HasField(TEXT("leaveTangent")) && !bHasLeave))
			return MCPError(FString::Printf(TEXT("points[%d] tangents must contain finite x, y, z"), Index));
		Point.bHasTangents = bHasArrive && bHasLeave;
		if (Point.Type == ESplinePointType::CurveCustomTangent && (!bHasArrive || !bHasLeave))
			return MCPError(FString::Printf(TEXT("points[%d] CurveCustomTangent requires arriveTangent and leaveTangent"), Index));
		NewPoints.Add(Point);
	}
	bool bClosedLoop = false;
	const bool bHasClosedLoop = Params->HasField(TEXT("closedLoop"));
	if (bHasClosedLoop)
	{
		if (!Params->TryGetField(TEXT("closedLoop")).IsValid() || Params->TryGetField(TEXT("closedLoop"))->Type != EJson::Boolean)
			return MCPError(TEXT("closedLoop must be a boolean"));
		Params->TryGetBoolField(TEXT("closedLoop"), bClosedLoop);
	}
	float PreviousInputKey = -TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < NewPoints.Num(); ++Index)
	{
		const float EffectiveInputKey = NewPoints[Index].bHasInputKey ? NewPoints[Index].InputKey : static_cast<float>(Index);
		if (!FMath::IsFinite(EffectiveInputKey) || (Index > 0 && (EffectiveInputKey <= PreviousInputKey || FMath::IsNearlyEqual(EffectiveInputKey, PreviousInputKey))))
			return MCPError(FString::Printf(TEXT("points[%d] inputKey values must be finite and strictly increasing"), Index));
		PreviousInputKey = EffectiveInputKey;
	}

	const FBoolProperty* LoopOverrideProperty = FindFProperty<FBoolProperty>(USplineComponent::StaticClass(), TEXT("bLoopPositionOverride"));
	const FFloatProperty* LoopPositionProperty = FindFProperty<FFloatProperty>(USplineComponent::StaticClass(), TEXT("LoopPosition"));
	const bool PreviousLoopOverride = LoopOverrideProperty && LoopOverrideProperty->GetPropertyValue_InContainer(SplineComp);
	const float PreviousLoopPosition = LoopPositionProperty ? LoopPositionProperty->GetPropertyValue_InContainer(SplineComp) : 0.f;
	bool RequestedLoopOverride = bHasClosedLoop ? false : PreviousLoopOverride;
	double RequestedLoopPosition = PreviousLoopPosition;
	if (Params->HasField(TEXT("loopPositionOverride")) && !Params->TryGetBoolField(TEXT("loopPositionOverride"), RequestedLoopOverride))
		return MCPError(TEXT("loopPositionOverride must be a boolean"));
	if (Params->HasField(TEXT("loopPosition")) && (!Params->TryGetNumberField(TEXT("loopPosition"), RequestedLoopPosition) || !FMath::IsFinite(RequestedLoopPosition) || !FMath::IsFinite(static_cast<float>(RequestedLoopPosition))))
		return MCPError(TEXT("loopPosition must be a finite float"));
	if (RequestedLoopOverride && (!((bHasClosedLoop && bClosedLoop) || (!bHasClosedLoop && SplineComp->IsClosedLoop())) || NewPoints.IsEmpty() || static_cast<float>(RequestedLoopPosition) <= PreviousInputKey || FMath::IsNearlyEqual(static_cast<float>(RequestedLoopPosition), PreviousInputKey)))
		return MCPError(TEXT("An overridden loop position requires a closed nonempty spline and a key after the final point"));

	// Point state is portable through MCP; Undo additionally captures complete component state.
	TArray<TSharedPtr<FJsonValue>> PrevPoints;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
	const FSplineCurves PreviousCurves = SplineComp->GetSplineCurves();
#else
	const FSplineCurves PreviousCurves = SplineComp->SplineCurves;
#endif
	for (int32 i = 0; i < SplineComp->GetNumberOfSplinePoints(); ++i)
	{
		FVector P = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
		PObj->SetNumberField(TEXT("x"), P.X);
		PObj->SetNumberField(TEXT("y"), P.Y);
		PObj->SetNumberField(TEXT("z"), P.Z);
		PObj->SetObjectField(TEXT("arriveTangent"), MCPVec3ToJsonObject(SplineComp->GetArriveTangentAtSplinePoint(i, ESplineCoordinateSpace::World)));
		PObj->SetObjectField(TEXT("leaveTangent"), MCPVec3ToJsonObject(SplineComp->GetLeaveTangentAtSplinePoint(i, ESplineCoordinateSpace::World)));
		PObj->SetStringField(TEXT("pointType"), SplinePointTypeName(SplineComp->GetSplinePointType(i)));
		PObj->SetNumberField(TEXT("inputKey"), PreviousCurves.Position.Points[i].InVal);
		const FRotator StoredWorldRotation = (SplineComp->GetComponentQuat() * PreviousCurves.Rotation.Points[i].OutVal).Rotator();
		PObj->SetObjectField(TEXT("rotation"), MCPVec3ToJsonObject(FVector(StoredWorldRotation.Pitch, StoredWorldRotation.Yaw, StoredWorldRotation.Roll)));
		PObj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(PreviousCurves.Scale.Points[i].OutVal));
		PrevPoints.Add(MakeShared<FJsonValueObject>(PObj));
	}
	const bool bPrevClosedLoop = SplineComp->IsClosedLoop();

	FScopedTransaction Transaction(FText::FromString(TEXT("Set Spline Points")));
	Actor->SetFlags(RF_Transactional);
	SplineComp->SetFlags(RF_Transactional);
	Actor->Modify();
	SplineComp->Modify();
	SplineComp->ClearSplinePoints(false);
	for (int32 i = 0; i < NewPoints.Num(); ++i)
	{
		const FTransform ComponentTransform = SplineComp->GetComponentTransform();
		const FVector LocalLocation = ComponentTransform.InverseTransformPosition(NewPoints[i].Location);
		const FVector LocalArrive = ComponentTransform.InverseTransformVector(NewPoints[i].ArriveTangent);
		const FVector LocalLeave = ComponentTransform.InverseTransformVector(NewPoints[i].LeaveTangent);
		const FQuat LocalRotationQuat = NewPoints[i].bHasRotation ? ComponentTransform.GetRotation().Inverse() * NewPoints[i].Rotation.Quaternion() : FQuat::Identity;
		FSplinePoint NewPoint(NewPoints[i].bHasInputKey ? NewPoints[i].InputKey : static_cast<float>(i), LocalLocation,
			LocalArrive, LocalLeave, LocalRotationQuat.Rotator(), NewPoints[i].Scale, NewPoints[i].Type);
		SplineComp->AddPoint(NewPoint, false);
		if (NewPoints[i].bHasTangents)
			SplineComp->SetTangentsAtSplinePoint(i, NewPoints[i].ArriveTangent, NewPoints[i].LeaveTangent, ESplineCoordinateSpace::World, false);
		// SetTangentsAtSplinePoint promotes the point to CurveCustomTangent;
		// apply the requested type last so the public result is exact.
		SplineComp->SetSplinePointType(i, NewPoints[i].Type, false);
	}

	// Optionally set closed loop
	if (RequestedLoopOverride)
	{
		SplineComp->SetClosedLoopAtPosition(true, static_cast<float>(RequestedLoopPosition), false);
	}
	else if (bHasClosedLoop || Params->HasField(TEXT("loopPositionOverride")))
	{
		SplineComp->SetClosedLoop(bHasClosedLoop ? bClosedLoop : bPrevClosedLoop, false);
	}

	SplineComp->UpdateSpline();
	SplineComp->GetPackage()->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), SplineComp->GetName());
	Result->SetNumberField(TEXT("splinePointCount"), SplineComp->GetNumberOfSplinePoints());
	Result->SetBoolField(TEXT("closedLoop"), SplineComp->IsClosedLoop());
	Result->SetNumberField(TEXT("splineLength"), SplineComp->GetSplineLength());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetArrayField(TEXT("points"), PrevPoints);
	Payload->SetBoolField(TEXT("closedLoop"), bPrevClosedLoop);
	Payload->SetBoolField(TEXT("loopPositionOverride"), PreviousLoopOverride);
	Payload->SetNumberField(TEXT("loopPosition"), PreviousLoopPosition);
	Payload->SetStringField(TEXT("componentName"), SplineComp->GetName());
	MCPSetRollback(Result, TEXT("set_spline_points"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), TEXT("MCP rollback preserves authored point positions, types, tangents, rotations, scales, keys and loop endpoint. Use editor Undo to restore any custom rotation/scale-curve interpolation and ancillary component state."));

	return MCPResult(Result);
}
