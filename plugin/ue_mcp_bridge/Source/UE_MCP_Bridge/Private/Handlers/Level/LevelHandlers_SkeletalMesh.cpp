#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Families/MCPRuntime.h"
#include "Families/MCPWorldSpawn.h"
#include "HandlerSkinnedAsset.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

// Spawning a skeletal mesh actor and setting the mesh on a skeletal mesh
// component. Registration stays in LevelHandlers.cpp.

// #679/#677: spawn_skeletal_mesh_actor is place_actor of a SkeletalMeshActor
// with a mesh, optional per-slot materials and a single-node animation, for
// visual and deform verification.
TSharedPtr<FJsonValue> FLevelHandlers::SpawnSkeletalMeshActor(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("transform"), TEXT("materials"), TEXT("loop") });
	FString MeshPath;
	if (auto Err = RequireStringAlt(Params, TEXT("skeletalMesh"), TEXT("meshPath"), MeshPath)) return Err;
	const FString AnimPath = OptionalString(Params, TEXT("animSequence"));
	FMCPSpawn Spawn(Params, { TEXT("SkeletalMeshActor") });

	REQUIRE_ASSET(USkeletalMesh, Mesh, MeshPath);
	UAnimSequence* Anim = nullptr;
	if (!AnimPath.IsEmpty())
	{
		Anim = LoadAssetByPath<UAnimSequence>(AnimPath);
		if (!Anim) return MCPAssetLoadError(AnimPath, TEXT("AnimSequence"));
	}

	// transform supplies defaults that location, rotation and scale override.
	const FTransform Base = OptionalTransform(Params, TEXT("transform"));
	const FTransform At(
		OptionalRotator(Params, TEXT("rotation"), Base.Rotator()),
		OptionalVec3(Params, TEXT("location"), Base.GetLocation()),
		OptionalVec3(Params, TEXT("scale"), Base.GetScale3D()));

	if (auto Early = Spawn.Open()) return Early;
	ASkeletalMeshActor* Actor = Spawn.SpawnAt<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), At);
	if (!Actor) return Spawn.Failed();

	// #946: per-slot COMPONENT material overrides, reported rather than
	// applied silently. These write the component's OverrideMaterials, not the
	// mesh asset's own slots. For an actor already placed, or one material on
	// every slot, use level(set_component_materials).
	TArray<TSharedPtr<FJsonValue>> MaterialResults;
	int32 FailedMaterials = 0;
	USkeletalMeshComponent* Comp = Actor->GetSkeletalMeshComponent();
	if (Comp)
	{
		Comp->SetSkeletalMeshAsset(Mesh);

		const TArray<TSharedPtr<FJsonValue>>* Mats = nullptr;
		if (TryGetArrayParam(Params, TEXT("materials"), Mats) && Mats)
		{
			const int32 SlotCount = Comp->GetNumMaterials();
			for (int32 i = 0; i < Mats->Num(); ++i)
			{
				FString MatPath;
				const bool bHasPath =
					(*Mats)[i].IsValid() && (*Mats)[i]->TryGetString(MatPath) && !MatPath.IsEmpty();
				if (!bHasPath) continue;

				TSharedPtr<FJsonObject> MatRow = MakeShared<FJsonObject>();
				MatRow->SetNumberField(TEXT("slotIndex"), i);
				MatRow->SetStringField(TEXT("materialPath"), MatPath);
				if (i >= SlotCount)
				{
					MatRow->SetBoolField(TEXT("ok"), false);
					MatRow->SetStringField(TEXT("error"), FString::Printf(
						TEXT("slot %d is past the mesh's %d material slots"), i, SlotCount));
					++FailedMaterials;
				}
				else if (UMaterialInterface* Mat = LoadAssetByPath<UMaterialInterface>(MatPath))
				{
					Comp->SetMaterial(i, Mat);
					MatRow->SetBoolField(TEXT("ok"), true);
					MatRow->SetStringField(TEXT("source"), TEXT("componentOverride"));
				}
				else
				{
					MatRow->SetBoolField(TEXT("ok"), false);
					MatRow->SetStringField(TEXT("error"), TEXT("material not found"));
					++FailedMaterials;
				}
				MaterialResults.Add(MakeShared<FJsonValueObject>(MatRow));
			}
		}

		// Optional single-node animation preview (visual deform check).
		if (Anim)
		{
			const bool bLoop = OptionalBool(Params, TEXT("loop"), true);
			Comp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Comp->SetAnimation(Anim);
			Comp->Play(bLoop);

			// #766/#790: SetAnimation() only drives the RUNTIME single-node
			// player. AnimationData is what the level serialises, so it is
			// written too or the actor comes back in A-pose after a reload.
			Comp->AnimationData.AnimToPlay = Anim;
			Comp->AnimationData.bSavedLooping = bLoop;
			Comp->AnimationData.bSavedPlaying = true;
			Comp->AnimationData.SavedPosition = 0.0f;
			Comp->AnimationData.SavedPlayRate = 1.0f;
			Comp->Modify();
		}
	}

	TSharedPtr<FJsonObject> Result = Spawn.Finish();
	if (FailedMaterials > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The actor was spawned but %d material override(s) did not apply; see materials[]."),
			FailedMaterials));
	}
	Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
	Result->SetNumberField(TEXT("materialSlotCount"), Comp ? Comp->GetNumMaterials() : 0);
	Result->SetArrayField(TEXT("materials"), MaterialResults);
	Result->SetObjectField(TEXT("boxExtent"), MCPVec3ToJsonObject(Actor->GetComponentsBoundingBox(true).GetExtent()));
	return MCPResult(Result);
}

// #1099: swap the mesh on a skinned mesh component that is already placed.
// Goes through SetSkinnedAssetAndUpdate, the only write that resizes the pose
// buffers for the new skeleton.
TSharedPtr<FJsonValue> FLevelHandlers::SetComponentSkeletalMesh(const TSharedPtr<FJsonObject>& Params)
{
	if (!HasParam(Params, TEXT("skeletalMesh")))
	{
		return MCPError(TEXT("Missing 'skeletalMesh': a SkeletalMesh asset path, or null to clear the mesh"));
	}
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor")).ToLower();
	UWorld* World = FMCPRuntimeWorld::Resolve(Params, *WorldScope).World;
	if (!World)
	{
		return MCPError(WorldScope == TEXT("pie")
			? TEXT("PIE not running (or no such pieInstance). See editor(list_pie_instances).")
			: TEXT("Editor world not available"));
	}

	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelNameOrPath;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;

	USkinnedMeshComponent* Comp = nullptr;
	if (ComponentName.IsEmpty())
	{
		Comp = Actor->FindComponentByClass<USkinnedMeshComponent>();
	}
	else
	{
		Comp = Cast<USkinnedMeshComponent>(MCPFindComponentByName(Actor, ComponentName));
	}
	if (!Comp)
	{
		TArray<FString> Available;
		for (UActorComponent* Candidate : Actor->GetComponents())
		{
			if (Cast<USkinnedMeshComponent>(Candidate)) Available.Add(Candidate->GetName());
		}
		const FString Named = ComponentName.IsEmpty()
			? FString()
			: FString::Printf(TEXT(" named '%s'"), *ComponentName);
		return MCPError(FString::Printf(
			TEXT("No skinned mesh component%s on '%s'. Skinned mesh components: [%s]"),
			*Named, *Actor->GetActorLabel(), *FString::Join(Available, TEXT(", "))));
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("MCP set component skeletal mesh")));
	Actor->Modify();
	FString PreviousMesh;
	FString MeshErr;
	if (!MCPSkinnedAsset::AssignFromJson(Comp, TryGetParam(Params, TEXT("skeletalMesh")), PreviousMesh, MeshErr))
	{
		Transaction.Cancel();
		return MCPError(MeshErr);
	}
	Comp->MarkRenderStateDirty();
	Actor->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), Comp->GetName());
	Result->SetStringField(TEXT("componentClass"), Comp->GetClass()->GetName());
	MCPSkinnedAsset::Report(Result, Comp, PreviousMesh);
	TArray<TSharedPtr<FJsonValue>> SlotNames;
	for (const FName& SlotName : Comp->GetMaterialSlotNames())
	{
		SlotNames.Add(MakeShared<FJsonValueString>(SlotName.ToString()));
	}
	Result->SetArrayField(TEXT("materialSlotNames"), SlotNames);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetStringField(TEXT("componentName"), Comp->GetName());
	if (PreviousMesh.IsEmpty()) Payload->SetField(TEXT("skeletalMesh"), MakeShared<FJsonValueNull>());
	else Payload->SetStringField(TEXT("skeletalMesh"), PreviousMesh);
	if (World->IsGameWorld()) Payload->SetStringField(TEXT("world"), TEXT("pie"));
	FMCPRuntimeWorld::Resolve(Params, TEXT("editor")).Echo(Payload);
	MCPSetRollback(Result, TEXT("set_component_skeletal_mesh"), Payload);
	return MCPResult(Result);
}
