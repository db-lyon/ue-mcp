// World-space line trace + actor floor snap.
// Translation-unit partition of FLevelHandlers - registration stays in
// LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	static void EmitHitFields(TSharedPtr<FJsonObject> Result, const FHitResult& Hit)
	{
		AActor* HitActor = Hit.GetActor();
		UPrimitiveComponent* HitComp = Hit.GetComponent();
		if (HitActor)
		{
			Result->SetStringField(TEXT("actorLabel"), HitActor->GetActorLabel());
			Result->SetStringField(TEXT("actorClass"), HitActor->GetClass()->GetName());
		}
		if (HitComp)
		{
			Result->SetStringField(TEXT("componentName"), HitComp->GetName());
			Result->SetStringField(TEXT("componentClass"), HitComp->GetClass()->GetName());
		}
		Result->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Hit.Location));
		Result->SetObjectField(TEXT("impactPoint"), MCPVec3ToJsonObject(Hit.ImpactPoint));
		Result->SetObjectField(TEXT("normal"), MCPVec3ToJsonObject(Hit.Normal));
		Result->SetObjectField(TEXT("impactNormal"), MCPVec3ToJsonObject(Hit.ImpactNormal));
		Result->SetNumberField(TEXT("distance"), Hit.Distance);
		Result->SetNumberField(TEXT("faceIndex"), Hit.FaceIndex);
		if (Hit.BoneName != NAME_None) Result->SetStringField(TEXT("boneName"), Hit.BoneName.ToString());
		if (Hit.PhysMaterial.IsValid()) Result->SetStringField(TEXT("physicalMaterial"), Hit.PhysMaterial->GetPathName());
	}
}


TSharedPtr<FJsonValue> FLevelHandlers::LineTrace(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FVector Start = OptionalVec3(Params, TEXT("start"));
	FVector End;
	if (Params->HasField(TEXT("end")))
	{
		End = OptionalVec3(Params, TEXT("end"));
	}
	else if (Params->HasField(TEXT("direction")))
	{
		FVector Dir = OptionalVec3(Params, TEXT("direction"));
		if (!Dir.Normalize())
		{
			return MCPError(TEXT("'direction' must be a non-zero vector"));
		}
		const double Distance = OptionalNumber(Params, TEXT("distance"), 200000.0);
		End = Start + Dir * Distance;
	}
	else
	{
		return MCPError(TEXT("Pass either 'end' (Vec3) or 'direction' (Vec3) + 'distance?'"));
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPLineTrace), /*bTraceComplex*/ true);
	Query.bReturnPhysicalMaterial = true;
	Query.bReturnFaceIndex = true;

	const TArray<TSharedPtr<FJsonValue>>* IgnoreArr = nullptr;
	if (Params->TryGetArrayField(TEXT("ignoreActors"), IgnoreArr) && IgnoreArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *IgnoreArr)
		{
			FString Label;
			if (!V->TryGetString(Label)) continue;
			if (AActor* A = FindActorByLabel(World, Label)) Query.AddIgnoredActor(A);
		}
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("hit"), bHit);
	Result->SetObjectField(TEXT("start"), MCPVec3ToJsonObject(Start));
	Result->SetObjectField(TEXT("end"), MCPVec3ToJsonObject(End));
	if (bHit) EmitHitFields(Result, Hit);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FLevelHandlers::SnapActorToFloor(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

	const double Offset = OptionalNumber(Params, TEXT("floorOffset"), 0.0);
	const double MaxDistance = OptionalNumber(Params, TEXT("maxDistance"), 100000.0);

	FVector Origin, Extent;
	Actor->GetActorBounds(/*bOnlyCollidingComponents*/ false, Origin, Extent);
	const FVector Top = Origin + FVector(0, 0, Extent.Z + 10.0);
	const FVector End = Top - FVector(0, 0, MaxDistance);

	FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPSnapToFloor), /*bTraceComplex*/ true);
	Query.AddIgnoredActor(Actor);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Top, End, ECC_Visibility, Query))
	{
		return MCPError(FString::Printf(TEXT("No floor hit within %.1f cm below '%s'"), MaxDistance, *ActorLabel));
	}

	const FVector ActorLoc = Actor->GetActorLocation();
	const double BoundsBottomZ = (Origin.Z - Extent.Z);
	const double DeltaZ = (Hit.ImpactPoint.Z + Offset) - BoundsBottomZ;
	const FVector NewLoc = ActorLoc + FVector(0, 0, DeltaZ);

	const FVector PrevLoc = ActorLoc;
	Actor->Modify();
	Actor->SetActorLocation(NewLoc, /*bSweep*/ false, /*OutSweepHitResult*/ nullptr, ETeleportType::TeleportPhysics);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetObjectField(TEXT("from"), MCPVec3ToJsonObject(PrevLoc));
	Result->SetObjectField(TEXT("to"), MCPVec3ToJsonObject(NewLoc));
	Result->SetObjectField(TEXT("impactPoint"), MCPVec3ToJsonObject(Hit.ImpactPoint));
	if (AActor* HitActor = Hit.GetActor()) Result->SetStringField(TEXT("hitActor"), HitActor->GetActorLabel());
	Result->SetNumberField(TEXT("dropDistance"), Hit.Distance);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
	Loc->SetNumberField(TEXT("x"), PrevLoc.X);
	Loc->SetNumberField(TEXT("y"), PrevLoc.Y);
	Loc->SetNumberField(TEXT("z"), PrevLoc.Z);
	Payload->SetObjectField(TEXT("location"), Loc);
	MCPSetRollback(Result, TEXT("move_actor"), Payload);
	return MCPResult(Result);
}
