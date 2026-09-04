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
#include "HandlerPagination.h"
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
#include "Animation/AnimBlueprintGeneratedClass.h"
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

		// #983: actorPath wins over the label token, and a label naming more
		// than one actor in the world that answered is refused rather than
		// read off whichever the iterator reached first.
		const FString ActorPath = OptionalString(Params, TEXT("actorPath"));
		TArray<TPair<FString, UWorld*>> Candidates = BuildWorldCandidates(RequestedScope);
		for (const TPair<FString, UWorld*>& Candidate : Candidates)
		{
			if (!ActorPath.IsEmpty())
			{
				if (AActor* ByPath = MCPFindActorByPath(Candidate.Value, ActorPath))
				{
					OutWorld = Candidate.Value;
					OutActor = ByPath;
					OutResolvedScope = Candidate.Key;
					return nullptr;
				}
				continue;
			}
			TArray<AActor*> Matches;
			MCPCollectActorsByToken(Candidate.Value, ActorToken, EMCPActorMatch::LabelNameOrPath, Matches);
			if (Matches.Num() > 1)
			{
				return MCPAmbiguousActorError(
					ActorToken, TEXT("actorLabel"), TEXT("actorPath"),
					MCPDescribeActorMatchTier(ActorToken, Matches[0]), Matches);
			}
			if (Matches.Num() == 1)
			{
				OutWorld = Candidate.Value;
				OutActor = Matches[0];
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
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	FString BoneName;
	if (auto Err = RequireString(Params, TEXT("boneName"), BoneName)) return Err;
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	const FString Space = OptionalString(Params, TEXT("space"), TEXT("world")).ToLower();

	UWorld* World = nullptr;
	AActor* Actor = nullptr;
	FString ResolvedWorldScope;
	if (auto Err = ResolveSkeletalActorForQuery(Params, ActorLabel, TEXT("auto"), World, Actor, ResolvedWorldScope)) return Err;
	// A caller who passed only actorPath left ActorLabel holding the path, and
	// every message below reads better naming the actor that answered (#983).
	ActorLabel = Actor->GetActorLabel();
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
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));

	UWorld* World = nullptr;
	AActor* Actor = nullptr;
	FString ResolvedWorldScope;
	if (auto Err = ResolveSkeletalActorForQuery(Params, ActorLabel, TEXT("auto"), World, Actor, ResolvedWorldScope)) return Err;
	// A caller who passed only actorPath left ActorLabel holding the path, and
	// every message below reads better naming the actor that answered (#983).
	ActorLabel = Actor->GetActorLabel();
	USkeletalMeshComponent* SK = ResolveSkeletalMeshComp(Actor, ComponentName);
	if (!SK) return MakeSkeletalComponentNotFoundError(Actor, ActorLabel, ComponentName);
	if (!SK->GetSkeletalMeshAsset()) return MCPError(FString::Printf(TEXT("SkeletalMeshComponent '%s' on actor '%s' has no SkeletalMesh asset"), *SK->GetName(), *ActorLabel));

	const FReferenceSkeleton& Ref = SK->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 NumBones = Ref.GetNum();

	// T3: paged. A production skeleton runs to several hundred bones once
	// twist, cloth and facial joints are in, and the whole hierarchy arrived in
	// one response with no way to ask for part of it.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_bones|actor=%s|component=%s"), *Actor->GetPathName(), *SK->GetName()),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	// Deliberately NOT sorted. Reference-skeleton index order is a contract of
	// the asset: parents precede children, so the emitted sequence is the
	// hierarchy itself, and alphabetising it would destroy the one thing that
	// makes a bone list readable.
	TArray<MCPPagination::FPageRow> Bones;
	Bones.Reserve(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		const FString BoneName = Ref.GetBoneName(i).ToString();
		TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
		B->SetStringField(TEXT("name"), BoneName);
		B->SetNumberField(TEXT("index"), i);
		const int32 ParentIdx = Ref.GetParentIndex(i);
		B->SetNumberField(TEXT("parentIndex"), ParentIdx);
		if (ParentIdx != INDEX_NONE) B->SetStringField(TEXT("parentName"), Ref.GetBoneName(ParentIdx).ToString());
		// The bone NAME is the anchor, not its index: a skeleton edited between
		// two pages renumbers every bone after the change, and an index would
		// then resume at a different joint while claiming to be exact.
		Bones.Add({ BoneName, MakeShared<FJsonValueObject>(B) });
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorName"), Actor->GetName());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("world"), ResolvedWorldScope);
	Result->SetStringField(TEXT("worldName"), World ? World->GetName() : TEXT(""));
	AddSkeletalComponentMetadata(Result, SK);
	Result->SetNumberField(TEXT("boneCount"), NumBones);
	MCPPagination::EmitPage(Page, Bones, TEXT("bones"), Result);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::RebindLeaderPose(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

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
	int32 AlreadyBoundToBody = 0;
	TArray<TSharedPtr<FJsonValue>> Bound;
	// What each component was following before, reported so a caller can see
	// what the rebind displaced. It is not a rollback payload: no action points
	// a component at an arbitrary leader or clears one.
	TArray<TSharedPtr<FJsonValue>> PreviousLeaders;
	for (USkeletalMeshComponent* C : Comps)
	{
		if (C == Body) continue;
		const USkinnedMeshComponent* PrevLeader = C->LeaderPoseComponent.Get();
		if (PrevLeader == Body) ++AlreadyBoundToBody;
		TSharedPtr<FJsonObject> Prev = MakeShared<FJsonObject>();
		Prev->SetStringField(TEXT("component"), C->GetName());
		Prev->SetStringField(TEXT("previousLeader"), PrevLeader ? PrevLeader->GetName() : FString());
		PreviousLeaders.Add(MakeShared<FJsonValueObject>(Prev));

		C->SetLeaderPoseComponent(nullptr, /*bForceUpdate*/ true);
		C->SetLeaderPoseComponent(Body, /*bForceUpdate*/ true);
		Bound.Add(MakeShared<FJsonValueString>(C->GetName()));
		++Rebound;
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), Rebound > 0 && AlreadyBoundToBody == Rebound);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("body"), Body->GetName());
	Result->SetNumberField(TEXT("rebound"), Rebound);
	Result->SetNumberField(TEXT("alreadyBoundToBody"), AlreadyBoundToBody);
	Result->SetArrayField(TEXT("components"), Bound);
	Result->SetArrayField(TEXT("previousLeaders"), PreviousLeaders);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("This points every other skeletal mesh component at one body component. No action sets a component's leader pose to an arbitrary component or clears one, so the previous bindings listed in previousLeaders cannot be replayed. ")
		TEXT("The binding is live component state on the spawned actor rather than saved asset data, so it is rebuilt from the actor's construction when the world reloads."));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FAnimationHandlers::PreviewAnimation(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	bool bEnabled = true;
	Params->TryGetBoolField(TEXT("enabled"), bEnabled);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

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
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetNumberField(TEXT("componentsUpdated"), Updated);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetBoolField(TEXT("enabled"), !bEnabled);
	MCPSetRollback(Result, TEXT("preview_animation"), Payload);
	return MCPResult(Result);
}


// Set the transient component override rather than the skeletal mesh's
// PostProcessAnimBlueprint asset setting. A live actor may be an editor-world
// actor or a PIE copy; either way this is intentionally not saved into an asset
// or template and disappears when that component is reconstructed.
TSharedPtr<FJsonValue> FAnimationHandlers::SetLivePostProcessAnimBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	const FString AnimBlueprintClassPath = OptionalString(Params, TEXT("animBlueprintClassPath"));
	bool bClear = false;
	Params->TryGetBoolField(TEXT("clear"), bClear);
	if (bClear && !AnimBlueprintClassPath.IsEmpty())
	{
		return MCPError(TEXT("Pass either animBlueprintClassPath or clear=true, not both"));
	}
	if (!bClear && AnimBlueprintClassPath.IsEmpty())
	{
		return MCPError(TEXT("animBlueprintClassPath is required unless clear=true"));
	}

	UAnimBlueprintGeneratedClass* NewClass = nullptr;
	if (!bClear)
	{
		NewClass = LoadObject<UAnimBlueprintGeneratedClass>(nullptr, *AnimBlueprintClassPath);
		if (!NewClass)
		{
			return MCPError(FString::Printf(
				TEXT("AnimBlueprintGeneratedClass not found: %s. Pass the generated class object path (for example /Game/Animations/ABP_Name.ABP_Name_C), not the AnimBlueprint asset path."),
				*AnimBlueprintClassPath));
		}
		if (!NewClass->IsChildOf(UAnimInstance::StaticClass()))
		{
			return MCPError(FString::Printf(TEXT("Class is not an AnimInstance subclass: %s"), *AnimBlueprintClassPath));
		}
	}

	UWorld* World = nullptr;
	AActor* Actor = nullptr;
	FString ResolvedWorldScope;
	if (auto Err = ResolveSkeletalActorForQuery(Params, ActorLabel, TEXT("auto"), World, Actor, ResolvedWorldScope)) return Err;
	ActorLabel = Actor->GetActorLabel();
	USkeletalMeshComponent* SK = ResolveSkeletalMeshComp(Actor, ComponentName);
	if (!SK) return MakeSkeletalComponentNotFoundError(Actor, ActorLabel, ComponentName);
	const USkeletalMesh* MeshAsset = SK->GetSkeletalMeshAsset();
	if (!MeshAsset)
	{
		return MCPError(FString::Printf(TEXT("Skeletal mesh component '%s' has no skeletal mesh"), *SK->GetName()));
	}
	if (NewClass)
	{
		if (SK->GetAnimClass() == NewClass)
		{
			return MCPError(TEXT("The post-process AnimBP must differ from the component's main AnimBP"));
		}

		const USkeleton* MeshSkeleton = MeshAsset->GetSkeleton();
		const USkeleton* AnimSkeleton = NewClass->GetTargetSkeleton();
		if (MeshSkeleton && AnimSkeleton && MeshSkeleton != AnimSkeleton
			&& !MeshSkeleton->IsCompatibleForEditor(AnimSkeleton)
			&& !AnimSkeleton->IsCompatibleForEditor(MeshSkeleton))
		{
			return MCPError(FString::Printf(
				TEXT("Post-process AnimBP skeleton '%s' is not compatible with component mesh skeleton '%s'"),
				*AnimSkeleton->GetPathName(), *MeshSkeleton->GetPathName()));
		}
	}

	const TSubclassOf<UAnimInstance> PreviousOverride = SK->OverridePostProcessAnimBP;
	const bool bAlreadySet = PreviousOverride == NewClass;
	if (!bAlreadySet)
	{
		SK->SetOverridePostProcessAnimBP(NewClass, /*ReinitAnimInstances*/ true);
	}

	const TSubclassOf<UAnimInstance> OverrideAfter = SK->OverridePostProcessAnimBP;
	const TSubclassOf<UAnimInstance> EffectiveAfter = SK->GetPostProcessAnimBPClassToBeUsed();
	UAnimInstance* InstanceAfter = SK->GetPostProcessInstance();

	auto Result = MCPSuccess();
	if (bAlreadySet) MCPSetExisted(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("world"), ResolvedWorldScope);
	Result->SetStringField(TEXT("worldName"), World ? World->GetName() : TEXT(""));
	AddSkeletalComponentMetadata(Result, SK);
	Result->SetBoolField(TEXT("clear"), bClear);
	Result->SetBoolField(TEXT("alreadySet"), bAlreadySet);
	Result->SetBoolField(TEXT("transient"), true);
	Result->SetStringField(TEXT("persistence"), TEXT("live component override only; no asset or component template was modified"));
	Result->SetStringField(TEXT("previousOverrideClass"), PreviousOverride ? PreviousOverride->GetPathName() : TEXT(""));
	Result->SetStringField(TEXT("overrideClass"), OverrideAfter ? OverrideAfter->GetPathName() : TEXT(""));
	Result->SetStringField(TEXT("effectivePostProcessClass"), EffectiveAfter ? EffectiveAfter->GetPathName() : TEXT(""));
	Result->SetStringField(TEXT("postProcessInstancePath"), InstanceAfter ? InstanceAfter->GetPathName() : TEXT(""));
	Result->SetStringField(TEXT("postProcessInstanceClass"), InstanceAfter ? InstanceAfter->GetClass()->GetPathName() : TEXT(""));
	Result->SetBoolField(TEXT("postProcessEnabled"), !SK->GetDisablePostProcessBlueprint());

	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Rollback->SetStringField(TEXT("componentName"), SK->GetName());
	Rollback->SetStringField(TEXT("world"), ResolvedWorldScope);
	if (PreviousOverride)
	{
		Rollback->SetStringField(TEXT("animBlueprintClassPath"), PreviousOverride->GetPathName());
	}
	else
	{
		Rollback->SetBoolField(TEXT("clear"), true);
	}
	MCPSetRollback(Result, TEXT("set_live_post_process_anim_blueprint"), Rollback);
	return MCPResult(Result);
}


// #922/#926: the EVALUATED pose off a live SkeletalMeshComponent, in the editor
// world or in PIE.
//
// get_bone_transforms reads a skeleton's REFERENCE pose, which is the read that
// looks correct while the running instance is wrong: in #922 a character lay
// flat on the floor and every transform the bridge could show read clean,
// because none of them were the pose the component was actually holding.
// get_bone_transform answers for one bone at a time. This answers for a set, off
// the component's own evaluated arrays, and reports what is driving the
// evaluation next to the numbers, so a flat character and a stopped anim
// instance are distinguishable from one call.
TSharedPtr<FJsonValue> FAnimationHandlers::GetLiveBoneTransforms(const TSharedPtr<FJsonObject>& Params)
{
	// How many bones one response carries. A humanoid skeleton is a few hundred,
	// so the whole thing fits; a crowd-scale or facial rig can be far more.
	constexpr int32 LiveBoneTransformCap = 1000;

	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	const FString Space = OptionalString(Params, TEXT("space"), TEXT("world")).ToLower();
	if (!(Space == TEXT("world") || Space == TEXT("component") || Space == TEXT("local")))
	{
		return MCPError(TEXT("space must be 'world' (default), 'component', or 'local'"));
	}

	UWorld* World = nullptr;
	AActor* Actor = nullptr;
	FString ResolvedWorldScope;
	if (auto Err = ResolveSkeletalActorForQuery(Params, ActorLabel, TEXT("auto"), World, Actor, ResolvedWorldScope)) return Err;
	// A caller who passed only actorPath left ActorLabel holding the path, and
	// every message below reads better naming the actor that answered (#983).
	ActorLabel = Actor->GetActorLabel();
	USkeletalMeshComponent* SK = ResolveSkeletalMeshComp(Actor, ComponentName);
	if (!SK) return MakeSkeletalComponentNotFoundError(Actor, ActorLabel, ComponentName);
	USkeletalMesh* Mesh = SK->GetSkeletalMeshAsset();
	if (!Mesh)
	{
		return MCPError(FString::Printf(
			TEXT("SkeletalMeshComponent '%s' on actor '%s' has no SkeletalMesh asset"), *SK->GetName(), *ActorLabel));
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();

	// boneNames selects a subset; omitting it returns every bone, which is what a
	// caller diagnosing a pose wants and is a few hundred entries on a character.
	TArray<FName> RequestedBones;
	const TArray<TSharedPtr<FJsonValue>>* BonesArray = nullptr;
	if (Params->TryGetArrayField(TEXT("boneNames"), BonesArray) && BonesArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *BonesArray)
		{
			if (!Entry.IsValid()) continue;
			const FString Name = Entry->AsString();
			if (!Name.IsEmpty()) RequestedBones.Add(FName(*Name));
		}
	}
	if (RequestedBones.Num() == 0)
	{
		for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
		{
			RequestedBones.Add(RefSkeleton.GetBoneName(BoneIndex));
		}
	}
	if (RequestedBones.Num() > LiveBoneTransformCap)
	{
		return MCPError(FString::Printf(
			TEXT("%d bones requested, over the %d limit. Pass 'boneNames' with the bones you need."),
			RequestedBones.Num(), LiveBoneTransformCap));
	}

	const TArray<FTransform>& ComponentSpace = SK->GetComponentSpaceTransforms();
	const TArray<FTransform> BoneSpace = SK->GetBoneSpaceTransforms();

	TArray<TSharedPtr<FJsonValue>> Bones;
	TArray<TSharedPtr<FJsonValue>> Missing;
	Bones.Reserve(RequestedBones.Num());
	for (const FName& BoneName : RequestedBones)
	{
		const int32 BoneIndex = SK->GetBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			Missing.Add(MakeShared<FJsonValueString>(BoneName.ToString()));
			continue;
		}

		FTransform Transform;
		if (Space == TEXT("world"))
		{
			Transform = SK->GetBoneTransform(BoneIndex);
		}
		else if (Space == TEXT("component"))
		{
			if (!ComponentSpace.IsValidIndex(BoneIndex))
			{
				Missing.Add(MakeShared<FJsonValueString>(BoneName.ToString()));
				continue;
			}
			Transform = ComponentSpace[BoneIndex];
		}
		else
		{
			if (!BoneSpace.IsValidIndex(BoneIndex))
			{
				Missing.Add(MakeShared<FJsonValueString>(BoneName.ToString()));
				continue;
			}
			Transform = BoneSpace[BoneIndex];
		}

		TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
		BoneObj->SetStringField(TEXT("name"), BoneName.ToString());
		BoneObj->SetNumberField(TEXT("index"), BoneIndex);
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		BoneObj->SetNumberField(TEXT("parentIndex"), ParentIndex);
		if (ParentIndex != INDEX_NONE)
		{
			BoneObj->SetStringField(TEXT("parentName"), RefSkeleton.GetBoneName(ParentIndex).ToString());
		}
		BoneObj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Transform.GetLocation()));
		BoneObj->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Transform.GetRotation().Rotator()));
		BoneObj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Transform.GetScale3D()));
		Bones.Add(MakeShared<FJsonValueObject>(BoneObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorName"), Actor->GetName());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("world"), ResolvedWorldScope);
	Result->SetStringField(TEXT("worldName"), World ? World->GetName() : TEXT(""));
	AddSkeletalComponentMetadata(Result, SK);
	Result->SetStringField(TEXT("space"), Space);
	Result->SetNumberField(TEXT("boneCount"), Bones.Num());
	Result->SetArrayField(TEXT("bones"), Bones);
	if (Missing.Num() > 0)
	{
		Result->SetArrayField(TEXT("unresolvedBones"), Missing);
	}

	// The component's own placement, so a component-space read lifts to world
	// without a second call, and a component transform that reads clean while
	// the pose does not is visible side by side (#922).
	const FTransform ComponentToWorld = SK->GetComponentTransform();
	TSharedPtr<FJsonObject> ComponentTransform = MakeShared<FJsonObject>();
	ComponentTransform->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(ComponentToWorld.GetLocation()));
	ComponentTransform->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(ComponentToWorld.GetRotation().Rotator()));
	ComponentTransform->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(ComponentToWorld.GetScale3D()));
	Result->SetObjectField(TEXT("componentTransform"), ComponentTransform);

	// What is driving the pose. A stopped or absent anim instance is the usual
	// reason an evaluated read matches the reference pose exactly.
	TSharedPtr<FJsonObject> Evaluation = MakeShared<FJsonObject>();
	const EAnimationMode::Type AnimationMode = SK->GetAnimationMode();
	Evaluation->SetStringField(TEXT("animationMode"),
		AnimationMode == EAnimationMode::AnimationBlueprint ? TEXT("AnimationBlueprint")
		: AnimationMode == EAnimationMode::AnimationSingleNode ? TEXT("AnimationSingleNode")
		: TEXT("AnimationCustomMode"));
	if (UAnimInstance* AnimInstance = SK->GetAnimInstance())
	{
		Evaluation->SetStringField(TEXT("animInstanceClass"), AnimInstance->GetClass()->GetPathName());
	}
	Evaluation->SetNumberField(TEXT("componentSpaceTransformCount"), ComponentSpace.Num());
	Evaluation->SetNumberField(TEXT("refSkeletonBoneCount"), RefSkeleton.GetNum());
	Evaluation->SetBoolField(TEXT("componentVisible"), SK->IsVisible());
	Result->SetObjectField(TEXT("evaluation"), Evaluation);

	return MCPResult(Result);
}
