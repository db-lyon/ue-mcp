// Split from LevelHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FLevelHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "JsonSerializer.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimInstance.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ── #420 / #419: raycast, bone reads, leader-pose, snap, anim preview ─────

namespace
{
	// Local copy of FindActorByLabel - the canonical version lives in
	// LevelHandlers.cpp's anonymous namespace. Both translation units need
	// it; keep them in lockstep when changing either.
	static AActor* FindActorByLabel(UWorld* World, const FString& Label)
	{
		if (!World) return nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label) return *It;
		}
		return nullptr;
	}

	// Resolve a SkeletalMeshComponent on an actor by name. If componentName is
	// empty, prefer "CharacterMesh0" / "Mesh" first (the canonical Character
	// body), then any SkeletalMeshComponent.
	static USkeletalMeshComponent* ResolveSkeletalMeshComp(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor) return nullptr;
		TArray<USkeletalMeshComponent*> Comps;
		Actor->GetComponents<USkeletalMeshComponent>(Comps);
		if (Comps.Num() == 0) return nullptr;
		if (!ComponentName.IsEmpty())
		{
			for (USkeletalMeshComponent* C : Comps)
			{
				if (C->GetName() == ComponentName || C->GetClass()->GetName() == ComponentName)
					return C;
			}
			for (USkeletalMeshComponent* C : Comps)
			{
				if (C->GetName().StartsWith(ComponentName)) return C;
			}
			return nullptr;
		}
		for (USkeletalMeshComponent* C : Comps)
		{
			if (C->GetName() == TEXT("CharacterMesh0") || C->GetName() == TEXT("Mesh")) return C;
		}
		return Comps[0];
	}

	static void WriteVec(TSharedPtr<FJsonObject> Parent, const TCHAR* Field, const FVector& V)
	{
		Parent->SetObjectField(Field, MCPVec3ToJsonObject(V));
	}

	static void WriteRot(TSharedPtr<FJsonObject> Parent, const TCHAR* Field, const FRotator& R)
	{
		Parent->SetObjectField(Field, MCPRotatorToJsonObject(R));
	}

	static FVector ReadVec(const TSharedPtr<FJsonObject>& In, const TCHAR* Field, const FVector& Fallback = FVector::ZeroVector)
	{
		return OptionalVec3(In, Field, Fallback);
	}

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
		WriteVec(Result, TEXT("location"), Hit.Location);
		WriteVec(Result, TEXT("impactPoint"), Hit.ImpactPoint);
		WriteVec(Result, TEXT("normal"), Hit.Normal);
		WriteVec(Result, TEXT("impactNormal"), Hit.ImpactNormal);
		Result->SetNumberField(TEXT("distance"), Hit.Distance);
		Result->SetNumberField(TEXT("faceIndex"), Hit.FaceIndex);
		if (Hit.BoneName != NAME_None) Result->SetStringField(TEXT("boneName"), Hit.BoneName.ToString());
		if (Hit.PhysMaterial.IsValid()) Result->SetStringField(TEXT("physicalMaterial"), Hit.PhysMaterial->GetPathName());
	}
}


TSharedPtr<FJsonValue> FLevelHandlers::LineTrace(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FVector Start = ReadVec(Params, TEXT("start"));
	FVector End;
	if (Params->HasField(TEXT("end")))
	{
		End = ReadVec(Params, TEXT("end"));
	}
	else if (Params->HasField(TEXT("direction")))
	{
		FVector Dir = ReadVec(Params, TEXT("direction"));
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
	WriteVec(Result, TEXT("start"), Start);
	WriteVec(Result, TEXT("end"), End);
	if (bHit) EmitHitFields(Result, Hit);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FLevelHandlers::GetBoneTransform(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;
	FString BoneName;
	if (auto Err = RequireString(Params, TEXT("boneName"), BoneName)) return Err;
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	const FString Space = OptionalString(Params, TEXT("space"), TEXT("world")).ToLower();

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	USkeletalMeshComponent* SK = ResolveSkeletalMeshComp(Actor, ComponentName);
	if (!SK) return MCPError(FString::Printf(TEXT("No SkeletalMeshComponent on actor '%s'"), *ActorLabel));

	FTransform Xf;
	if (Space == TEXT("world"))
	{
		const int32 BoneIdx = SK->GetBoneIndex(FName(*BoneName));
		if (BoneIdx == INDEX_NONE)
		{
			// Maybe it's a socket name. GetSocketTransform handles both.
			Xf = SK->GetSocketTransform(FName(*BoneName), RTS_World);
			if (Xf.Equals(FTransform::Identity)) return MCPError(FString::Printf(TEXT("Bone or socket '%s' not found"), *BoneName));
		}
		else
		{
			Xf = SK->GetBoneTransform(BoneIdx);
		}
	}
	else if (Space == TEXT("component"))
	{
		Xf = SK->GetSocketTransform(FName(*BoneName), RTS_Component);
	}
	else if (Space == TEXT("local"))
	{
		const int32 BoneIdx = SK->GetBoneIndex(FName(*BoneName));
		if (BoneIdx == INDEX_NONE) return MCPError(FString::Printf(TEXT("Bone '%s' not found"), *BoneName));
		const TArray<FTransform>& Local = SK->GetBoneSpaceTransforms();
		if (BoneIdx >= Local.Num()) return MCPError(FString::Printf(TEXT("Bone index %d out of range for local-space transforms"), BoneIdx));
		Xf = Local[BoneIdx];
	}
	else
	{
		return MCPError(TEXT("space must be 'world' (default), 'component', or 'local'"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), SK->GetName());
	Result->SetStringField(TEXT("boneName"), BoneName);
	Result->SetStringField(TEXT("space"), Space);
	WriteVec(Result, TEXT("location"), Xf.GetLocation());
	WriteRot(Result, TEXT("rotation"), Xf.GetRotation().Rotator());
	WriteVec(Result, TEXT("scale"), Xf.GetScale3D());
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FLevelHandlers::ListBones(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	USkeletalMeshComponent* SK = ResolveSkeletalMeshComp(Actor, ComponentName);
	if (!SK || !SK->GetSkeletalMeshAsset()) return MCPError(FString::Printf(TEXT("No SkeletalMesh on actor '%s'"), *ActorLabel));

	const FReferenceSkeleton& Ref = SK->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 NumBones = Ref.GetNum();

	TArray<TSharedPtr<FJsonValue>> Bones;
	for (int32 i = 0; i < NumBones; ++i)
	{
		TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
		B->SetStringField(TEXT("name"), Ref.GetBoneName(i).ToString());
		B->SetNumberField(TEXT("index"), i);
		const int32 ParentIdx = Ref.GetParentIndex(i);
		B->SetNumberField(TEXT("parentIndex"), ParentIdx);
		if (ParentIdx != INDEX_NONE) B->SetStringField(TEXT("parentName"), Ref.GetBoneName(ParentIdx).ToString());
		Bones.Add(MakeShared<FJsonValueObject>(B));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), SK->GetName());
	Result->SetNumberField(TEXT("boneCount"), NumBones);
	Result->SetArrayField(TEXT("bones"), Bones);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FLevelHandlers::RebindLeaderPose(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

	TArray<USkeletalMeshComponent*> Comps;
	Actor->GetComponents<USkeletalMeshComponent>(Comps);
	if (Comps.Num() < 2)
	{
		return MCPError(FString::Printf(TEXT("Actor '%s' has %d SkeletalMeshComponent(s); need >= 2 to rebind leader pose"), *ActorLabel, Comps.Num()));
	}

	USkeletalMeshComponent* Body = nullptr;
	const FString BodyHint = OptionalString(Params, TEXT("bodyComponent"));
	if (!BodyHint.IsEmpty())
	{
		Body = ResolveSkeletalMeshComp(Actor, BodyHint);
		if (!Body) return MCPError(FString::Printf(TEXT("bodyComponent '%s' not found"), *BodyHint));
	}
	else
	{
		Body = ResolveSkeletalMeshComp(Actor, FString());
	}
	if (!Body) return MCPError(TEXT("Could not resolve a body SkeletalMeshComponent"));

	int32 Rebound = 0;
	TArray<TSharedPtr<FJsonValue>> Bound;
	for (USkeletalMeshComponent* C : Comps)
	{
		if (C == Body) continue;
		C->SetLeaderPoseComponent(nullptr, /*bForceUpdate*/ true);
		C->SetLeaderPoseComponent(Body, /*bForceUpdate*/ true);
		Bound.Add(MakeShared<FJsonValueString>(C->GetName()));
		++Rebound;
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("body"), Body->GetName());
	Result->SetNumberField(TEXT("rebound"), Rebound);
	Result->SetArrayField(TEXT("components"), Bound);
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

	// Move so the bounds bottom rests at the impact point + offset.
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
	WriteVec(Result, TEXT("from"), PrevLoc);
	WriteVec(Result, TEXT("to"), NewLoc);
	WriteVec(Result, TEXT("impactPoint"), Hit.ImpactPoint);
	if (AActor* HitActor = Hit.GetActor()) Result->SetStringField(TEXT("hitActor"), HitActor->GetActorLabel());
	Result->SetNumberField(TEXT("dropDistance"), Hit.Distance);

	// Rollback: move back to previous location.
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


TSharedPtr<FJsonValue> FLevelHandlers::PreviewAnimation(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;
	bool bEnabled = true;
	Params->TryGetBoolField(TEXT("enabled"), bEnabled);

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

	TArray<USkeletalMeshComponent*> Comps;
	Actor->GetComponents<USkeletalMeshComponent>(Comps);
	if (Comps.Num() == 0) return MCPError(FString::Printf(TEXT("No SkeletalMeshComponents on '%s'"), *ActorLabel));

	const FString TickHint = OptionalString(Params, TEXT("visibilityBasedAnimTickOption"));
	const EVisibilityBasedAnimTickOption Tick = bEnabled
		? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
		: EVisibilityBasedAnimTickOption::OnlyTickMontagesAndRefreshBonesWhenPlayingMontages;

	int32 Updated = 0;
	for (USkeletalMeshComponent* C : Comps)
	{
		C->Modify();
		C->SetUpdateAnimationInEditor(bEnabled);
		C->VisibilityBasedAnimTickOption = Tick;
		C->MarkRenderStateDirty();
		++Updated;
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetNumberField(TEXT("componentsUpdated"), Updated);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetBoolField(TEXT("enabled"), !bEnabled);
	MCPSetRollback(Result, TEXT("preview_animation"), Payload);
	return MCPResult(Result);
}
