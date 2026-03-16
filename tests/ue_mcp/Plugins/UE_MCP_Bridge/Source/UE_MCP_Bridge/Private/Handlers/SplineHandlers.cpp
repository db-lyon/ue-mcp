#include "SplineHandlers.h"
#include "HandlerRegistry.h"
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
	Registry.RegisterHandler(TEXT("set_spline_points"), &SetSplinePoints);
}

TSharedPtr<FJsonValue> FSplineHandlers::CreateSplineActor(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get location
	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj))
	{
		(*LocationObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocationObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocationObj)->TryGetNumberField(TEXT("z"), Location.Z);
	}

	// Spawn an empty actor
	FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnTransform);
	if (!NewActor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn actor"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	// Set label if provided
	FString Label;
	if (Params->TryGetStringField(TEXT("label"), Label))
	{
		NewActor->SetActorLabel(Label);
	}

	// Optionally set closed loop
	bool bClosedLoop = false;
	if (Params->TryGetBoolField(TEXT("closedLoop"), bClosedLoop))
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

	Result->SetStringField(TEXT("actorLabel"), NewActor->GetActorLabel());
	Result->SetStringField(TEXT("actorName"), NewActor->GetName());
	Result->SetNumberField(TEXT("splinePointCount"), SplineComp->GetNumberOfSplinePoints());
	Result->SetBoolField(TEXT("closedLoop"), SplineComp->IsClosedLoop());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FSplineHandlers::ReadSpline(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find spline component
	USplineComponent* SplineComp = Actor->FindComponentByClass<USplineComponent>();
	if (!SplineComp)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' does not have a SplineComponent"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FSplineHandlers::SetSplinePoints(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	const TArray<TSharedPtr<FJsonValue>>* PointsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("points"), PointsArray))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'points' parameter (array of {x, y, z} objects)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find spline component
	USplineComponent* SplineComp = Actor->FindComponentByClass<USplineComponent>();
	if (!SplineComp)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' does not have a SplineComponent"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetNumberField(TEXT("splinePointCount"), SplineComp->GetNumberOfSplinePoints());
	Result->SetBoolField(TEXT("closedLoop"), SplineComp->IsClosedLoop());
	Result->SetNumberField(TEXT("splineLength"), SplineComp->GetSplineLength());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}
