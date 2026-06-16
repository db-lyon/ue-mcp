// Live-actor skeletal reads + leader-pose rebind + preview-animation toggle.
// Originally co-located with FLevelHandlers; moved to FAnimationHandlers in
// the architecture cleanup because these operate on the animation domain
// (bones, leader-pose, anim tick) rather than placement / outliner state.
//
// Translation-unit partition of FAnimationHandlers - registration stays in
// AnimationHandlers.cpp::RegisterHandlers.

#include "AnimationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimInstance.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
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
				if (C->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
					C->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
					return C;
			}
			for (USkeletalMeshComponent* C : Comps)
			{
				if (C->GetName().StartsWith(ComponentName, ESearchCase::IgnoreCase)) return C;
			}
			return nullptr;
		}
		for (USkeletalMeshComponent* C : Comps)
		{
			if (C->GetName() == TEXT("CharacterMesh0") || C->GetName() == TEXT("Mesh")) return C;
		}
		return Comps[0];
	}

	static TArray<TSharedPtr<FJsonValue>> BuildSkeletalComponentList(AActor* Actor)
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		if (!Actor) return Items;

		TArray<USkeletalMeshComponent*> Comps;
		Actor->GetComponents<USkeletalMeshComponent>(Comps);
		for (USkeletalMeshComponent* C : Comps)
		{
			if (!C) continue;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), C->GetName());
			Obj->SetStringField(TEXT("class"), C->GetClass()->GetName());
			Obj->SetStringField(TEXT("path"), C->GetPathName());
			if (USkeletalMesh* Mesh = C->GetSkeletalMeshAsset())
			{
				Obj->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
				if (USkeleton* Skeleton = Mesh->GetSkeleton())
				{
					Obj->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
				}
			}
			Items.Add(MakeShared<FJsonValueObject>(Obj));
		}
		return Items;
	}

	static TSharedPtr<FJsonValue> MakeSkeletalComponentNotFoundError(
		AActor* Actor,
		const FString& ActorToken,
		const FString& ComponentName)
	{
		auto Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(
			TEXT("error"),
			ComponentName.IsEmpty()
				? FString::Printf(TEXT("No SkeletalMeshComponent on actor '%s'"), *ActorToken)
				: FString::Printf(TEXT("SkeletalMeshComponent '%s' not found on actor '%s'"), *ComponentName, *ActorToken));
		Result->SetStringField(TEXT("actorLabel"), ActorToken);
		if (Actor)
		{
			Result->SetStringField(TEXT("actorName"), Actor->GetName());
			Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		}
		Result->SetArrayField(TEXT("availableComponents"), BuildSkeletalComponentList(Actor));
		return MCPResult(Result);
	}

	static void AddSkeletalComponentMetadata(TSharedPtr<FJsonObject> Result, USkeletalMeshComponent* SK)
	{
		if (!Result || !SK) return;
		Result->SetStringField(TEXT("componentName"), SK->GetName());
		Result->SetStringField(TEXT("componentClass"), SK->GetClass()->GetName());
		Result->SetStringField(TEXT("componentPath"), SK->GetPathName());
		if (USkeletalMesh* Mesh = SK->GetSkeletalMeshAsset())
		{
			Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
			if (USkeleton* Skeleton = Mesh->GetSkeleton())
			{
				Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
			}
		}
	}

	static void AddWorldCandidate(TArray<TPair<FString, UWorld*>>& Candidates, const FString& Scope, UWorld* World)
	{
		if (!World) return;
		for (const TPair<FString, UWorld*>& Existing : Candidates)
		{
			if (Existing.Value == World) return;
		}
		Candidates.Add(TPair<FString, UWorld*>(Scope, World));
	}

	static TArray<TPair<FString, UWorld*>> BuildWorldCandidates(const FString& RequestedScope)
	{
		TArray<TPair<FString, UWorld*>> Candidates;
		if (RequestedScope == TEXT("auto"))
		{
			AddWorldCandidate(Candidates, TEXT("pie"), GetPIEWorld());
			AddWorldCandidate(Candidates, TEXT("editor"), GetEditorWorld());
			return Candidates;
		}
		if (RequestedScope == TEXT("pie") || RequestedScope == TEXT("game"))
		{
			AddWorldCandidate(Candidates, RequestedScope, GetPIEWorld());
			return Candidates;
		}
		if (RequestedScope == TEXT("editor"))
		{
			AddWorldCandidate(Candidates, RequestedScope, GetEditorWorld());
			return Candidates;
		}
		return Candidates;
	}

	static TSharedPtr<FJsonValue> MakeSkeletalActorNotFoundError(
		const FString& ActorToken,
		const FString& RequestedScope,
		const TArray<TPair<FString, UWorld*>>& Candidates)
	{
		auto Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found for live skeletal query: %s"), *ActorToken));
		Result->SetStringField(TEXT("requestedWorld"), RequestedScope);

		TArray<TSharedPtr<FJsonValue>> SearchedWorlds;
		TArray<TSharedPtr<FJsonValue>> AvailableActors;
		for (const TPair<FString, UWorld*>& Candidate : Candidates)
		{
			if (!Candidate.Value) continue;
			TSharedPtr<FJsonObject> WorldObj = MakeShared<FJsonObject>();
			WorldObj->SetStringField(TEXT("scope"), Candidate.Key);
			WorldObj->SetStringField(TEXT("name"), Candidate.Value->GetName());
			SearchedWorlds.Add(MakeShared<FJsonValueObject>(WorldObj));

			int32 Count = 0;
			for (TActorIterator<AActor> It(Candidate.Value); It && Count < 20; ++It, ++Count)
			{
				AActor* Actor = *It;
				TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
				ActorObj->SetStringField(TEXT("world"), Candidate.Key);
				ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
				ActorObj->SetStringField(TEXT("name"), Actor->GetName());
				ActorObj->SetStringField(TEXT("path"), Actor->GetPathName());
				ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
				AvailableActors.Add(MakeShared<FJsonValueObject>(ActorObj));
			}
		}
		Result->SetArrayField(TEXT("searchedWorlds"), SearchedWorlds);
		Result->SetArrayField(TEXT("availableActors"), AvailableActors);
		return MCPResult(Result);
	}

	static TSharedPtr<FJsonValue> ResolveSkeletalActorForQuery(
		const TSharedPtr<FJsonObject>& Params,
		const FString& ActorToken,
		const FString& DefaultScope,
		UWorld*& OutWorld,
		AActor*& OutActor,
		FString& OutResolvedScope)
	{
		const FString RequestedScope = OptionalString(Params, TEXT("world"), DefaultScope).ToLower();
		if (!(RequestedScope == TEXT("auto") ||
			  RequestedScope == TEXT("pie") ||
			  RequestedScope == TEXT("game") ||
			  RequestedScope == TEXT("editor")))
		{
			return MCPError(TEXT("world must be 'auto' (default), 'pie', 'game', or 'editor'"));
		}

		TArray<TPair<FString, UWorld*>> Candidates = BuildWorldCandidates(RequestedScope);
		for (const TPair<FString, UWorld*>& Candidate : Candidates)
		{
			AActor* Actor = FindActorByLabelNameOrPath(Candidate.Value, ActorToken);
			if (Actor)
			{
				OutWorld = Candidate.Value;
				OutActor = Actor;
				OutResolvedScope = Candidate.Key;
				return nullptr;
			}
		}
		if (Candidates.Num() == 0)
		{
			return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *RequestedScope));
		}
		return MakeSkeletalActorNotFoundError(ActorToken, RequestedScope, Candidates);
	}
}


TSharedPtr<FJsonValue> FAnimationHandlers::GetBoneTransform(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;
	FString BoneName;
	if (auto Err = RequireString(Params, TEXT("boneName"), BoneName)) return Err;
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	const FString Space = OptionalString(Params, TEXT("space"), TEXT("world")).ToLower();

	UWorld* World = nullptr;
	AActor* Actor = nullptr;
	FString ResolvedWorldScope;
	if (auto Err = ResolveSkeletalActorForQuery(Params, ActorLabel, TEXT("auto"), World, Actor, ResolvedWorldScope)) return Err;
	USkeletalMeshComponent* SK = ResolveSkeletalMeshComp(Actor, ComponentName);
	if (!SK) return MakeSkeletalComponentNotFoundError(Actor, ActorLabel, ComponentName);

	const FName TargetName(*BoneName);
	const int32 BoneIdx = SK->GetBoneIndex(TargetName);
	const bool bSocketExists = SK->DoesSocketExist(TargetName);
	if (BoneIdx == INDEX_NONE && !bSocketExists)
	{
		return MCPError(FString::Printf(TEXT("Bone or socket '%s' not found"), *BoneName));
	}

	FTransform Xf;
	if (Space == TEXT("world"))
	{
		Xf = (BoneIdx != INDEX_NONE) ? SK->GetBoneTransform(BoneIdx) : SK->GetSocketTransform(TargetName, RTS_World);
	}
	else if (Space == TEXT("component"))
	{
		Xf = SK->GetSocketTransform(TargetName, RTS_Component);
	}
	else if (Space == TEXT("local"))
	{
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
	Result->SetStringField(TEXT("actorName"), Actor->GetName());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("world"), ResolvedWorldScope);
	Result->SetStringField(TEXT("worldName"), World ? World->GetName() : TEXT(""));
	AddSkeletalComponentMetadata(Result, SK);
	Result->SetStringField(TEXT("boneName"), BoneName);
	Result->SetStringField(TEXT("targetType"), BoneIdx != INDEX_NONE ? TEXT("bone") : TEXT("socket"));
	Result->SetStringField(TEXT("space"), Space);
	Result->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Xf.GetLocation()));
	Result->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Xf.GetRotation().Rotator()));
	Result->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Xf.GetScale3D()));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::ListBones(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));

	UWorld* World = nullptr;
	AActor* Actor = nullptr;
	FString ResolvedWorldScope;
	if (auto Err = ResolveSkeletalActorForQuery(Params, ActorLabel, TEXT("auto"), World, Actor, ResolvedWorldScope)) return Err;
	USkeletalMeshComponent* SK = ResolveSkeletalMeshComp(Actor, ComponentName);
	if (!SK) return MakeSkeletalComponentNotFoundError(Actor, ActorLabel, ComponentName);
	if (!SK->GetSkeletalMeshAsset()) return MCPError(FString::Printf(TEXT("SkeletalMeshComponent '%s' on actor '%s' has no SkeletalMesh asset"), *SK->GetName(), *ActorLabel));

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
	Result->SetStringField(TEXT("actorName"), Actor->GetName());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("world"), ResolvedWorldScope);
	Result->SetStringField(TEXT("worldName"), World ? World->GetName() : TEXT(""));
	AddSkeletalComponentMetadata(Result, SK);
	Result->SetNumberField(TEXT("boneCount"), NumBones);
	Result->SetArrayField(TEXT("bones"), Bones);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::RebindLeaderPose(const TSharedPtr<FJsonObject>& Params)
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


TSharedPtr<FJsonValue> FAnimationHandlers::PreviewAnimation(const TSharedPtr<FJsonObject>& Params)
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
