#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "VolumeHelpers_Internal.h"
#include "EditorScriptingUtilities/Public/EditorLevelLibrary.h"
#include "Editor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimInstance.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonSerializer.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/RectLight.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/BrushBuilder.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "Builders/CubeBuilder.h"
#include "BSPOps.h"
#include "Components/BrushComponent.h"
#include "GameFramework/Volume.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "Engine/BlockingVolume.h"
#include "Engine/TriggerVolume.h"
#include "Engine/PostProcessVolume.h"
#include "Sound/AudioVolume.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "GameFramework/PainCausingVolume.h"
#include "Selection.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "LevelEditorSubsystem.h"
#include "EditorLevelUtils.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "HandlerJsonProperty.h"

void FLevelHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("get_world_outliner"), &GetOutliner);
	Registry.RegisterHandler(TEXT("place_actor"), &PlaceActor);
	Registry.RegisterHandler(TEXT("delete_actor"), &DeleteActor);
	Registry.RegisterHandler(TEXT("get_actor_details"), &GetActorDetails);
	Registry.RegisterHandler(TEXT("get_component_tree"), &GetComponentTree);
	Registry.RegisterHandler(TEXT("get_relative_transform"), &GetRelativeTransform);
	Registry.RegisterHandler(TEXT("get_current_level"), &GetCurrentLevel);
	Registry.RegisterHandler(TEXT("list_levels"), &ListLevels);
	Registry.RegisterHandler(TEXT("get_selected_actors"), &GetSelectedActors);
	Registry.RegisterHandler(TEXT("list_volumes"), &ListVolumes);
	Registry.RegisterHandler(TEXT("move_actor"), &MoveActor);
	Registry.RegisterHandler(TEXT("select_actors"), &SelectActors);
	Registry.RegisterHandler(TEXT("spawn_light"), &SpawnLight);
	Registry.RegisterHandler(TEXT("set_light_properties"), &SetLightProperties);
	Registry.RegisterHandler(TEXT("spawn_volume"), &SpawnVolume);
	Registry.RegisterHandler(TEXT("add_component_to_actor"), &AddComponentToActor);
	Registry.RegisterHandler(TEXT("load_level"), &LoadLevel);
	Registry.RegisterHandler(TEXT("set_component_property"), &SetComponentProperty);
	Registry.RegisterHandler(TEXT("set_actor_material"), &SetActorMaterial);
	Registry.RegisterHandler(TEXT("set_volume_properties"), &SetVolumeProperties);
	Registry.RegisterHandler(TEXT("get_world_settings"), &GetWorldSettings);
	Registry.RegisterHandler(TEXT("set_world_settings"), &SetWorldSettings);
	Registry.RegisterHandler(TEXT("set_fog_properties"), &SetFogProperties);
	Registry.RegisterHandler(TEXT("get_actors_by_class"), &GetActorsByClass);
	Registry.RegisterHandler(TEXT("count_actors_by_class"), &CountActorsByClass);
	Registry.RegisterHandler(TEXT("get_runtime_virtual_texture_summary"), &GetRVTSummary);
	Registry.RegisterHandler(TEXT("set_water_body_property"), &SetWaterBodyProperty);
	Registry.RegisterHandler(TEXT("get_actor_bounds"), &GetActorBounds);
	Registry.RegisterHandler(TEXT("resolve_actor"), &ResolveActor);
	Registry.RegisterHandler(TEXT("set_actor_property"), &SetActorProperty);
	Registry.RegisterHandler(TEXT("line_trace"), &LineTrace);
	Registry.RegisterHandler(TEXT("get_bone_transform"), &GetBoneTransform);
	Registry.RegisterHandler(TEXT("list_bones"), &ListBones);
	Registry.RegisterHandler(TEXT("rebind_leader_pose"), &RebindLeaderPose);
	Registry.RegisterHandler(TEXT("snap_actor_to_floor"), &SnapActorToFloor);
	Registry.RegisterHandler(TEXT("preview_animation"), &PreviewAnimation);
	Registry.RegisterHandler(TEXT("delete_actors"), &DeleteActors);
	Registry.RegisterHandler(TEXT("add_actor_tag"), &AddActorTag);
	Registry.RegisterHandler(TEXT("remove_actor_tag"), &RemoveActorTag);
	Registry.RegisterHandler(TEXT("set_actor_tags"), &SetActorTags);
	Registry.RegisterHandler(TEXT("list_actor_tags"), &ListActorTags);
	Registry.RegisterHandler(TEXT("attach_actor"), &AttachActor);
	Registry.RegisterHandler(TEXT("detach_actor"), &DetachActor);
	Registry.RegisterHandler(TEXT("set_actor_mobility"), &SetActorMobility);
	Registry.RegisterHandler(TEXT("get_current_edit_level"), &GetCurrentEditLevel);
	Registry.RegisterHandler(TEXT("set_current_edit_level"), &SetCurrentEditLevel);
	Registry.RegisterHandler(TEXT("list_streaming_sublevels"), &ListStreamingSublevels);
	Registry.RegisterHandler(TEXT("add_streaming_sublevel"), &AddStreamingSublevel);
	Registry.RegisterHandler(TEXT("remove_streaming_sublevel"), &RemoveStreamingSublevel);
	Registry.RegisterHandler(TEXT("set_streaming_sublevel_properties"), &SetStreamingSublevelProperties);
	Registry.RegisterHandler(TEXT("spawn_grid"), &SpawnGrid);
	Registry.RegisterHandler(TEXT("batch_translate"), &BatchTranslate);
	Registry.RegisterHandler(TEXT("place_actors_batch"), &PlaceActorsBatch);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetOutliner(const TSharedPtr<FJsonObject>& Params)
{
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));

	FString ClassFilter = OptionalString(Params, TEXT("classFilter"));
	FString NameFilter = OptionalString(Params, TEXT("nameFilter"));
	// Default 50 keeps us snappy on World Partition projects whose levels
	// contain hundreds of streaming-proxy / HLOD actors. Callers who need the
	// full list can pass a larger limit explicitly.
	int32 Limit = OptionalInt(Params, TEXT("limit"), 50);
	bool bIncludeStreaming = OptionalBool(Params, TEXT("includeStreaming"), false);

	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	int32 TotalCount = 0;
	int32 StreamingSkipped = 0;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!Actor) continue;
		TotalCount++;

		FString ActorClass = Actor->GetClass()->GetName();
		FString ActorName = Actor->GetName();

		// World Partition spawns large numbers of LandscapeStreamingProxy and
		// WorldPartitionHLOD actors whose component graphs are expensive to
		// walk. Skip by default; callers can opt in via includeStreaming=true.
		if (!bIncludeStreaming &&
			(ActorClass == TEXT("LandscapeStreamingProxy") ||
			 ActorClass == TEXT("WorldPartitionHLOD")))
		{
			StreamingSkipped++;
			continue;
		}

		FString ActorLabel = Actor->GetActorLabel();

		if (!ClassFilter.IsEmpty() && !ActorClass.Contains(ClassFilter))
		{
			continue;
		}
		if (!NameFilter.IsEmpty() && !ActorName.Contains(NameFilter) && !ActorLabel.Contains(NameFilter))
		{
			continue;
		}
		if (ActorsArray.Num() >= Limit) break;

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), ActorName);
		ActorObj->SetStringField(TEXT("label"), ActorLabel);
		ActorObj->SetStringField(TEXT("class"), ActorClass);
		ActorObj->SetStringField(TEXT("path"), Actor->GetPathName());

		FVector Location = Actor->GetActorLocation();
		TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
		LocationObj->SetNumberField(TEXT("x"), Location.X);
		LocationObj->SetNumberField(TEXT("y"), Location.Y);
		LocationObj->SetNumberField(TEXT("z"), Location.Z);
		ActorObj->SetObjectField(TEXT("location"), LocationObj);

		FRotator Rotation = Actor->GetActorRotation();
		TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
		RotationObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
		RotationObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
		RotationObj->SetNumberField(TEXT("roll"), Rotation.Roll);
		ActorObj->SetObjectField(TEXT("rotation"), RotationObj);

		// Include child components
		TArray<TSharedPtr<FJsonValue>> ComponentsArray;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (!Comp) continue;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Comp->GetName());
			CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
		ActorObj->SetArrayField(TEXT("components"), ComponentsArray);

		ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetNumberField(TEXT("totalActors"), TotalCount);
	Result->SetNumberField(TEXT("returnedActors"), ActorsArray.Num());
	Result->SetNumberField(TEXT("streamingSkipped"), StreamingSkipped);
	Result->SetArrayField(TEXT("actors"), ActorsArray);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::PlaceActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorClass;
	if (auto Err = RequireString(Params, TEXT("actorClass"), ActorClass)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	const FString Label = OptionalString(Params, TEXT("label"));

	if (auto Existing = MCPCheckActorLabelExists(World, Label, OnConflict, TEXT("Actor")))
	{
		return Existing;
	}

	UClass* Class = FindClassByShortName(ActorClass);
	if (!Class)
	{
		Class = LoadObject<UClass>(nullptr, *ActorClass);
	}
	if (!Class)
	{
		return MCPError(FString::Printf(TEXT("Actor class not found: %s"), *ActorClass));
	}

	const FVector Location = OptionalVec3(Params, TEXT("location"));
	const FRotator Rotation = OptionalRotator(Params, TEXT("rotation"));

	FTransform SpawnTransform(Rotation, Location);
	AActor* NewActor = World->SpawnActor<AActor>(Class, SpawnTransform);
	if (!NewActor)
	{
		return MCPError(TEXT("Failed to spawn actor"));
	}

	if (!Label.IsEmpty())
	{
		NewActor->SetActorLabel(Label);
	}

	if (Params->HasField(TEXT("scale")))
	{
		NewActor->SetActorScale3D(OptionalVec3(Params, TEXT("scale"), FVector::OneVector));
	}

	// Static mesh shorthand
	FString StaticMeshPath = OptionalString(Params, TEXT("staticMesh"));
	if (!StaticMeshPath.IsEmpty())
	{
		AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(NewActor);
		if (MeshActor && MeshActor->GetStaticMeshComponent())
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath);
			if (Mesh)
			{
				MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			}
		}
	}

	// Material shorthand
	FString MaterialPath = OptionalString(Params, TEXT("material"));
	if (!MaterialPath.IsEmpty())
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
		if (Material)
		{
			UPrimitiveComponent* PrimComp = NewActor->FindComponentByClass<UPrimitiveComponent>();
			if (PrimComp)
			{
				PrimComp->SetMaterial(0, Material);
			}
		}
	}

	const FString FinalLabel = NewActor->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), FinalLabel);
	Result->SetStringField(TEXT("actorClass"), ActorClass);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), FinalLabel);
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::DeleteActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	AActor* ActorToDelete = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			ActorToDelete = *ActorIt;
			break;
		}
	}

	// Idempotent: deleting a non-existent actor is a no-op, not an error.
	if (!ActorToDelete)
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("actorLabel"), ActorLabel);
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Result);
	}

	World->DestroyActor(ActorToDelete);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("deleted"), true);
	// Delete is not reversible by default (would need snapshot-before-delete).

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetActorDetails(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	FString ActorPath;
	bool bHasLabel = Params->TryGetStringField(TEXT("actorLabel"), ActorLabel);
	bool bHasPath = Params->TryGetStringField(TEXT("actorPath"), ActorPath);
	if (!bHasLabel && !bHasPath)
	{
		return MCPError(TEXT("Missing 'actorLabel' or 'actorPath' parameter"));
	}

	// World selection: "editor" (default) or "pie" (#111)
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = nullptr;
	if (WorldScope.Equals(TEXT("pie"), ESearchCase::IgnoreCase) || WorldScope.Equals(TEXT("game"), ESearchCase::IgnoreCase))
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game)
			{
				World = Ctx.World();
				break;
			}
		}
		if (!World) return MCPError(TEXT("No PIE/Game world active"));
	}
	else
	{
		World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World) return MCPError(TEXT("No editor world available"));
	}

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if (bHasPath && (*ActorIt)->GetPathName() == ActorPath) { Actor = *ActorIt; break; }
		if (bHasLabel && (*ActorIt)->GetActorLabel() == ActorLabel) { Actor = *ActorIt; break; }
	}

	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), bHasPath ? *ActorPath : *ActorLabel));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("name"), Actor->GetName());
	Result->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	Result->SetStringField(TEXT("path"), Actor->GetPathName());

	FVector Location = Actor->GetActorLocation();
	TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);
	Result->SetObjectField(TEXT("location"), LocationObj);

	FRotator Rot = Actor->GetActorRotation();
	TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
	RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
	RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
	Result->SetObjectField(TEXT("rotation"), RotObj);

	FVector Scale = Actor->GetActorScale3D();
	TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
	ScaleObj->SetNumberField(TEXT("x"), Scale.X);
	ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
	ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
	Result->SetObjectField(TEXT("scale"), ScaleObj);

	if (AActor* Parent = Actor->GetAttachParentActor())
	{
		Result->SetStringField(TEXT("attachParent"), Parent->GetActorLabel());
	}

	// Components (always on) — name + class
	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	TArray<TSharedPtr<FJsonValue>> CompArr;
	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;
		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("name"), Comp->GetName());
		C->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		CompArr.Add(MakeShared<FJsonValueObject>(C));
	}
	Result->SetArrayField(TEXT("components"), CompArr);

	// #125: optional includeProperties=true dumps UPROPERTY name/type/value
	if (OptionalBool(Params, TEXT("includeProperties")))
	{
		FString PropFilter = OptionalString(Params, TEXT("propertyName"));
		TArray<TSharedPtr<FJsonValue>> PropsArr;
		for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop) continue;
			if (!PropFilter.IsEmpty() && Prop->GetName() != PropFilter) continue;

			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Prop->GetName());
			P->SetStringField(TEXT("type"), Prop->GetCPPType());

			FString ValueStr;
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
			Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, Actor, PPF_None);
			P->SetStringField(TEXT("value"), ValueStr);
			PropsArr.Add(MakeShared<FJsonValueObject>(P));
		}
		Result->SetArrayField(TEXT("properties"), PropsArr);
		Result->SetNumberField(TEXT("propertyCount"), PropsArr.Num());
	}

	return MCPResult(Result);
}

// #240/#241/#302/#320/#370/#353: deep component-tree introspection.
//
// Single call returns the actor's component list with all the inspection
// data that previously required either a tower of blueprint.get_component_property
// calls or a fall-back to execute_python with subclass-specific accessors.
// Covers:
//   - attach topology (parent + socket)
//   - relative + world transforms
//   - mobility + visibility
//   - collision profile + enabled state for PrimitiveComponents
//   - mesh path + override materials for StaticMesh / SkeletalMesh / SplineMesh
//   - bounds (origin + extent) for PrimitiveComponents
//   - tags
//   - reflected UPROPERTY name/type/value when includeProperties=true
TSharedPtr<FJsonValue> FLevelHandlers::GetComponentTree(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	FString ActorPath;
	const bool bHasLabel = Params->TryGetStringField(TEXT("actorLabel"), ActorLabel);
	const bool bHasPath = Params->TryGetStringField(TEXT("actorPath"), ActorPath);
	if (!bHasLabel && !bHasPath)
	{
		return MCPError(TEXT("Missing 'actorLabel' or 'actorPath' parameter"));
	}

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World '%s' not available"), *WorldScope));
	}

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (bHasPath && (*It)->GetPathName() == ActorPath) { Actor = *It; break; }
		if (bHasLabel && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	}
	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), bHasPath ? *ActorPath : *ActorLabel));
	}

	const bool bIncludeProperties = OptionalBool(Params, TEXT("includeProperties"));
	const FString PropertyFilter = OptionalString(Params, TEXT("componentClass"));

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	TArray<TSharedPtr<FJsonValue>> CompArr;
	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;
		if (!PropertyFilter.IsEmpty() && !Comp->GetClass()->GetName().Contains(PropertyFilter, ESearchCase::IgnoreCase)) continue;

		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("name"), Comp->GetName());
		C->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		C->SetBoolField(TEXT("isEditorOnly"), Comp->IsEditorOnly());

		// Tags array
		TArray<TSharedPtr<FJsonValue>> TagArr;
		for (FName Tag : Comp->ComponentTags) { TagArr.Add(MakeShared<FJsonValueString>(Tag.ToString())); }
		C->SetArrayField(TEXT("tags"), TagArr);

		if (USceneComponent* SC = Cast<USceneComponent>(Comp))
		{
			// Attach topology
			if (USceneComponent* AttachParent = SC->GetAttachParent())
			{
				C->SetStringField(TEXT("attachParent"), AttachParent->GetName());
			}
			const FName SocketName = SC->GetAttachSocketName();
			if (SocketName != NAME_None)
			{
				C->SetStringField(TEXT("attachSocket"), SocketName.ToString());
			}

			// Visibility + mobility
			C->SetBoolField(TEXT("bVisible"), SC->IsVisible());
			switch (SC->Mobility)
			{
			case EComponentMobility::Static:     C->SetStringField(TEXT("mobility"), TEXT("Static")); break;
			case EComponentMobility::Stationary: C->SetStringField(TEXT("mobility"), TEXT("Stationary")); break;
			case EComponentMobility::Movable:    C->SetStringField(TEXT("mobility"), TEXT("Movable")); break;
			default: break;
			}

			// Relative transform
			const FVector RelLoc = SC->GetRelativeLocation();
			const FRotator RelRot = SC->GetRelativeRotation();
			const FVector RelScale = SC->GetRelativeScale3D();
			auto MakeVec = [](const FVector& V) {
				auto O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("x"), V.X);
				O->SetNumberField(TEXT("y"), V.Y);
				O->SetNumberField(TEXT("z"), V.Z);
				return O;
			};
			auto MakeRot = [](const FRotator& R) {
				auto O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("pitch"), R.Pitch);
				O->SetNumberField(TEXT("yaw"), R.Yaw);
				O->SetNumberField(TEXT("roll"), R.Roll);
				return O;
			};
			C->SetObjectField(TEXT("relativeLocation"), MakeVec(RelLoc));
			C->SetObjectField(TEXT("relativeRotation"), MakeRot(RelRot));
			C->SetObjectField(TEXT("relativeScale"), MakeVec(RelScale));

			// World transform
			C->SetObjectField(TEXT("worldLocation"), MakeVec(SC->GetComponentLocation()));
			C->SetObjectField(TEXT("worldRotation"), MakeRot(SC->GetComponentRotation()));
			C->SetObjectField(TEXT("worldScale"), MakeVec(SC->GetComponentScale()));

			if (UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(SC))
			{
				// Collision profile
				const FName CollisionProfile = PC->GetCollisionProfileName();
				C->SetStringField(TEXT("collisionProfile"), CollisionProfile.ToString());
				switch (PC->GetCollisionEnabled())
				{
				case ECollisionEnabled::NoCollision:        C->SetStringField(TEXT("collisionEnabled"), TEXT("NoCollision")); break;
				case ECollisionEnabled::QueryOnly:          C->SetStringField(TEXT("collisionEnabled"), TEXT("QueryOnly")); break;
				case ECollisionEnabled::PhysicsOnly:        C->SetStringField(TEXT("collisionEnabled"), TEXT("PhysicsOnly")); break;
				case ECollisionEnabled::QueryAndPhysics:    C->SetStringField(TEXT("collisionEnabled"), TEXT("QueryAndPhysics")); break;
				case ECollisionEnabled::ProbeOnly:          C->SetStringField(TEXT("collisionEnabled"), TEXT("ProbeOnly")); break;
				case ECollisionEnabled::QueryAndProbe:      C->SetStringField(TEXT("collisionEnabled"), TEXT("QueryAndProbe")); break;
				default: break;
				}
				C->SetBoolField(TEXT("castShadow"), PC->CastShadow);

				// Bounds
				const FBoxSphereBounds Bounds = PC->Bounds;
				C->SetObjectField(TEXT("boundsOrigin"), MakeVec(Bounds.Origin));
				C->SetObjectField(TEXT("boundsBoxExtent"), MakeVec(Bounds.BoxExtent));
				C->SetNumberField(TEXT("boundsSphereRadius"), Bounds.SphereRadius);

				// Material slots + meshes (mesh-component subclasses)
				if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(PC))
				{
					if (UStaticMesh* Mesh = SMC->GetStaticMesh())
					{
						C->SetStringField(TEXT("staticMesh"), Mesh->GetPathName());
					}
					TArray<TSharedPtr<FJsonValue>> Mats;
					const int32 NumMats = SMC->GetNumMaterials();
					for (int32 i = 0; i < NumMats; i++)
					{
						UMaterialInterface* Mat = SMC->GetMaterial(i);
						Mats.Add(MakeShared<FJsonValueString>(Mat ? Mat->GetPathName() : TEXT("")));
					}
					C->SetArrayField(TEXT("materials"), Mats);
				}
				else if (USkeletalMeshComponent* SKMC = Cast<USkeletalMeshComponent>(PC))
				{
					if (USkeletalMesh* Mesh = SKMC->GetSkeletalMeshAsset())
					{
						C->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
					}
					TArray<TSharedPtr<FJsonValue>> Mats;
					const int32 NumMats = SKMC->GetNumMaterials();
					for (int32 i = 0; i < NumMats; i++)
					{
						UMaterialInterface* Mat = SKMC->GetMaterial(i);
						Mats.Add(MakeShared<FJsonValueString>(Mat ? Mat->GetPathName() : TEXT("")));
					}
					C->SetArrayField(TEXT("materials"), Mats);
					if (USkeleton* Skel = SKMC->GetSkeletalMeshAsset() ? SKMC->GetSkeletalMeshAsset()->GetSkeleton() : nullptr)
					{
						C->SetStringField(TEXT("skeleton"), Skel->GetPathName());
					}
				}
			}
		}

		if (bIncludeProperties)
		{
			TArray<TSharedPtr<FJsonValue>> Props;
			for (TFieldIterator<FProperty> PIt(Comp->GetClass()); PIt; ++PIt)
			{
				FProperty* Prop = *PIt;
				if (!Prop) continue;
				// Skip uneditable / hidden flagged fields to keep the payload focused
				// on values an agent would actually inspect.
				if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_DisableEditOnInstance)) continue;
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("name"), Prop->GetName());
				P->SetStringField(TEXT("type"), Prop->GetCPPType());
				FString ValueStr;
				const void* VP = Prop->ContainerPtrToValuePtr<void>(Comp);
				Prop->ExportText_Direct(ValueStr, VP, VP, Comp, PPF_None);
				P->SetStringField(TEXT("value"), ValueStr);
				Props.Add(MakeShared<FJsonValueObject>(P));
			}
			C->SetArrayField(TEXT("properties"), Props);
		}

		CompArr.Add(MakeShared<FJsonValueObject>(C));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
	Result->SetNumberField(TEXT("componentCount"), CompArr.Num());
	Result->SetArrayField(TEXT("components"), CompArr);
	return MCPResult(Result);
}

// #386/#387: compute target's transform expressed in reference's local space.
// Common dungeon/calibration workflow: figure out the local-space "snap rule"
// for an actor that was manually aligned to a parent actor. Previously this
// required execute_python with MathLibrary.inverse_transform_location.
TSharedPtr<FJsonValue> FLevelHandlers::GetRelativeTransform(const TSharedPtr<FJsonObject>& Params)
{
	FString TargetLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("targetLabel"), TEXT("target"), TargetLabel)) return Err;
	FString ReferenceLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("referenceLabel"), TEXT("reference"), ReferenceLabel)) return Err;

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World '%s' not available"), *WorldScope));

	AActor* TargetActor = nullptr;
	AActor* ReferenceActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (TargetActor && ReferenceActor) break;
		if (!TargetActor && (*It)->GetActorLabel() == TargetLabel) TargetActor = *It;
		if (!ReferenceActor && (*It)->GetActorLabel() == ReferenceLabel) ReferenceActor = *It;
	}
	if (!TargetActor) return MCPError(FString::Printf(TEXT("Target actor not found: %s"), *TargetLabel));
	if (!ReferenceActor) return MCPError(FString::Printf(TEXT("Reference actor not found: %s"), *ReferenceLabel));

	const FTransform Target = TargetActor->GetActorTransform();
	const FTransform Reference = ReferenceActor->GetActorTransform();
	const FTransform Relative = Target.GetRelativeTransform(Reference);

	auto MakeVec = [](const FVector& V) {
		auto O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), V.X);
		O->SetNumberField(TEXT("y"), V.Y);
		O->SetNumberField(TEXT("z"), V.Z);
		return O;
	};
	auto MakeRot = [](const FRotator& R) {
		auto O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("pitch"), R.Pitch);
		O->SetNumberField(TEXT("yaw"), R.Yaw);
		O->SetNumberField(TEXT("roll"), R.Roll);
		return O;
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("targetLabel"), TargetLabel);
	Result->SetStringField(TEXT("referenceLabel"), ReferenceLabel);
	Result->SetObjectField(TEXT("location"), MakeVec(Relative.GetLocation()));
	Result->SetObjectField(TEXT("rotation"), MakeRot(Relative.GetRotation().Rotator()));
	Result->SetObjectField(TEXT("scale"), MakeVec(Relative.GetScale3D()));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetCurrentLevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ULevel* CurrentLevel = World->GetCurrentLevel();
	if (!CurrentLevel)
	{
		return MCPError(TEXT("No current level"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("levelName"), World->GetName());
	Result->SetStringField(TEXT("levelPath"), World->GetPathName());

	// #166: Also return the map package path for tools that need the full asset reference
	UPackage* MapPackage = World->GetOutermost();
	if (MapPackage)
	{
		Result->SetStringField(TEXT("mapPackagePath"), MapPackage->GetName());
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::ListLevels(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> LevelsArray;

	// Add persistent level
	TSharedPtr<FJsonObject> PersistentObj = MakeShared<FJsonObject>();
	PersistentObj->SetStringField(TEXT("name"), World->GetName());
	PersistentObj->SetStringField(TEXT("type"), TEXT("persistent"));
	PersistentObj->SetBoolField(TEXT("isLoaded"), true);
	LevelsArray.Add(MakeShared<FJsonValueObject>(PersistentObj));

	// Add streaming levels
	const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();
	for (ULevelStreaming* StreamingLevel : StreamingLevels)
	{
		if (!StreamingLevel) continue;

		TSharedPtr<FJsonObject> LevelObj = MakeShared<FJsonObject>();
		LevelObj->SetStringField(TEXT("name"), StreamingLevel->GetWorldAssetPackageFName().ToString());
		LevelObj->SetStringField(TEXT("type"), TEXT("streaming"));
		LevelObj->SetBoolField(TEXT("isLoaded"), StreamingLevel->IsLevelLoaded());
		LevelObj->SetBoolField(TEXT("isVisible"), StreamingLevel->IsLevelVisible());
		LevelsArray.Add(MakeShared<FJsonValueObject>(LevelObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("levels"), LevelsArray);
	Result->SetNumberField(TEXT("count"), LevelsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetSelectedActors(const TSharedPtr<FJsonObject>& Params)
{
	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection)
	{
		return MCPError(TEXT("Unable to get selection"));
	}

	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	for (int32 i = 0; i < Selection->Num(); i++)
	{
		AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i));
		if (!Actor) continue;

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), Actor->GetName());
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		ActorObj->SetStringField(TEXT("path"), Actor->GetPathName());

		FVector Location = Actor->GetActorLocation();
		TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
		LocationObj->SetNumberField(TEXT("x"), Location.X);
		LocationObj->SetNumberField(TEXT("y"), Location.Y);
		LocationObj->SetNumberField(TEXT("z"), Location.Z);
		ActorObj->SetObjectField(TEXT("location"), LocationObj);

		ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("actors"), ActorsArray);
	Result->SetNumberField(TEXT("count"), ActorsArray.Num());

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FLevelHandlers::MoveActor(const TSharedPtr<FJsonObject>& Params)
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

	// Capture previous transform for rollback.
	const FVector PreviousLocation = Actor->GetActorLocation();
	const FRotator PreviousRotation = Actor->GetActorRotation();
	const FVector PreviousScale = Actor->GetActorScale3D();

	if (Params->HasField(TEXT("location")))
	{
		Actor->SetActorLocation(OptionalVec3(Params, TEXT("location"), Actor->GetActorLocation()));
	}
	if (Params->HasField(TEXT("rotation")))
	{
		Actor->SetActorRotation(OptionalRotator(Params, TEXT("rotation"), Actor->GetActorRotation()));
	}
	if (Params->HasField(TEXT("scale")))
	{
		Actor->SetActorScale3D(OptionalVec3(Params, TEXT("scale"), Actor->GetActorScale3D()));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Actor->GetActorLocation()));
	Result->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Actor->GetActorRotation()));
	Result->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Actor->GetActorScale3D()));
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);

	// Self-inverse: call move_actor with previous transform.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(PreviousLocation));
	Payload->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(PreviousRotation));
	Payload->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(PreviousScale));
	MCPSetRollback(Result, TEXT("move_actor"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SelectActors(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* ActorLabelsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("actorLabels"), ActorLabelsArray))
	{
		return MCPError(TEXT("Missing 'actorLabels' parameter"));
	}

	REQUIRE_EDITOR_WORLD(World);

	// Deselect all
	GEditor->SelectNone(true, true, false);

	TArray<TSharedPtr<FJsonValue>> SelectedArray;
	TArray<TSharedPtr<FJsonValue>> NotFoundArray;

	for (const TSharedPtr<FJsonValue>& LabelValue : *ActorLabelsArray)
	{
		FString Label = LabelValue->AsString();
		bool bFound = false;

		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			if ((*ActorIt)->GetActorLabel() == Label)
			{
				GEditor->SelectActor(*ActorIt, true, true, true);
				SelectedArray.Add(MakeShared<FJsonValueString>(Label));
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			NotFoundArray.Add(MakeShared<FJsonValueString>(Label));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("selected"), SelectedArray);
	Result->SetArrayField(TEXT("notFound"), NotFoundArray);
	Result->SetNumberField(TEXT("selectedCount"), SelectedArray.Num());

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FLevelHandlers::AddComponentToActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString ComponentClass;
	if (auto Err = RequireString(Params, TEXT("componentClass"), ComponentClass)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	REQUIRE_EDITOR_WORLD(World);

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

	// Idempotency: check for an existing component with the same name on the actor.
	FName CompName = FName(*ComponentName);
	for (UActorComponent* Existing : Actor->GetComponents())
	{
		if (Existing && Existing->GetFName() == CompName)
		{
			if (OnConflict == TEXT("error"))
			{
				return MCPError(FString::Printf(
					TEXT("Component '%s' already exists on '%s'"), *ComponentName, *ActorLabel));
			}
			auto ExistingResult = MCPSuccess();
			MCPSetExisted(ExistingResult);
			ExistingResult->SetStringField(TEXT("actorLabel"), ActorLabel);
			ExistingResult->SetStringField(TEXT("componentName"), ComponentName);
			ExistingResult->SetStringField(TEXT("componentClass"), Existing->GetClass()->GetName());
			return MCPResult(ExistingResult);
		}
	}

	// (#137) Robust class resolution: full path, short name, or engine-module implicit lookup.
	UClass* CompClass = nullptr;
	if (ComponentClass.Contains(TEXT("/")) || ComponentClass.Contains(TEXT(".")))
	{
		CompClass = LoadObject<UClass>(nullptr, *ComponentClass);
	}
	if (!CompClass)
	{
		CompClass = FindClassByShortName(ComponentClass);
	}
	if (!CompClass)
	{
		CompClass = LoadObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + ComponentClass));
	}

	if (!CompClass)
	{
		return MCPError(FString::Printf(TEXT("Component class not found: %s. Try the short name (e.g. 'StaticMeshComponent') or the full path ('/Script/Engine.StaticMeshComponent')."), *ComponentClass));
	}

	if (!CompClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("Class '%s' is not an ActorComponent"), *ComponentClass));
	}

	UActorComponent* NewComponent = NewObject<UActorComponent>(Actor, CompClass, CompName);
	if (!NewComponent)
	{
		return MCPError(TEXT("Failed to create component"));
	}

	USceneComponent* SceneComp = Cast<USceneComponent>(NewComponent);
	if (SceneComp && Actor->GetRootComponent())
	{
		SceneComp->SetupAttachment(Actor->GetRootComponent());
	}

	NewComponent->RegisterComponent();
	Actor->AddInstanceComponent(NewComponent);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("componentClass"), NewComponent->GetClass()->GetName());
	// No generic remove-instance-component handler exists yet; not emitting a
	// rollback record. Adding one later will make this reversible.
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::LoadLevel(const TSharedPtr<FJsonObject>& Params)
{
	FString LevelPath;
	if (auto Err = RequireString(Params, TEXT("levelPath"), LevelPath)) return Err;

	// Use the LevelEditorSubsystem to load the level
	ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LevelEditorSubsystem)
	{
		return MCPError(TEXT("LevelEditorSubsystem not available"));
	}

	bool bSuccess = LevelEditorSubsystem->LoadLevel(LevelPath);
	if (!bSuccess)
	{
		return MCPError(FString::Printf(TEXT("Failed to load level: %s"), *LevelPath));
	}

	// Get info about the newly loaded world
	auto Result = MCPSuccess();
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		Result->SetStringField(TEXT("worldName"), World->GetName());
		Result->SetStringField(TEXT("worldPath"), World->GetPathName());
	}

	Result->SetStringField(TEXT("levelPath"), LevelPath);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString ComponentName = OptionalString(Params, TEXT("componentName"));

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorLabel)
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	// Find the component -- exact match first, then prefix/class match
	UActorComponent* TargetComp = nullptr;
	if (!ComponentName.IsEmpty())
	{
		TArray<UActorComponent*> Components;
		TargetActor->GetComponents(Components);
		// Pass 1: exact match by name or class name
		for (UActorComponent* Comp : Components)
		{
			if (Comp->GetName() == ComponentName || Comp->GetClass()->GetName() == ComponentName)
			{
				TargetComp = Comp;
				break;
			}
		}
		// Pass 2: prefix match (e.g. "StaticMeshComponent" matches "StaticMeshComponent0")
		if (!TargetComp)
		{
			for (UActorComponent* Comp : Components)
			{
				if (Comp->GetName().StartsWith(ComponentName) || Comp->GetClass()->GetName().StartsWith(ComponentName))
				{
					TargetComp = Comp;
					break;
				}
			}
		}
	}
	else
	{
		// Use root component as default
		TargetComp = TargetActor->GetRootComponent();
	}

	if (!TargetComp)
	{
		return MCPError(FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *ActorLabel));
	}

	// #216: walk dotted property paths so callers can write
	// "GraphInstance.Graph" without us silently no-oping at the top level.
	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));
	if (PathParts.Num() == 0)
	{
		return MCPError(TEXT("Empty propertyName"));
	}

	UStruct* CurrentStruct = TargetComp->GetClass();
	void* CurrentContainer = TargetComp;
	FProperty* Prop = nullptr;
	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FProperty* SegmentProp = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!SegmentProp)
		{
			return MCPError(FString::Printf(TEXT("Property '%s' not found at '%s'"), *PathParts[i], *PropertyName));
		}
		if (i < PathParts.Num() - 1)
		{
			if (FStructProperty* SP = CastField<FStructProperty>(SegmentProp))
			{
				CurrentContainer = SP->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = SP->Struct;
			}
			else if (FObjectProperty* OP = CastField<FObjectProperty>(SegmentProp))
			{
				// #305: descend through Instanced UObject sub-objects.
				UObject* SubObject = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CurrentContainer));
				if (!SubObject)
				{
					return MCPError(FString::Printf(
						TEXT("Cannot descend into '%s' - the sub-object reference is null"),
						*PathParts[i]));
				}
				SubObject->Modify();
				CurrentContainer = SubObject;
				CurrentStruct = SubObject->GetClass();
			}
			else
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is not a struct or sub-object - cannot descend"), *PathParts[i]));
			}
		}
		else
		{
			Prop = SegmentProp;
		}
	}

	const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
	if (!ValueField || !(*ValueField).IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CurrentContainer);

	// Capture previous value as a string for self-inverse rollback.
	FString PreviousValueStr;
	Prop->ExportText_Direct(PreviousValueStr, ValuePtr, ValuePtr, TargetComp, PPF_None);

	FString ValueStr;
	if ((*ValueField)->TryGetString(ValueStr))
	{
		// #121: resolve bare actor labels (e.g. TargetActor=BP_Portcullis) to full object paths
		// so ImportText_Direct can resolve TObjectPtr<AActor> fields in struct arrays.
		if (!ValueStr.IsEmpty() && ValueStr.Contains(TEXT("=")))
		{
			FString Result;
			Result.Reserve(ValueStr.Len());
			int32 i = 0;
			while (i < ValueStr.Len())
			{
				TCHAR C = ValueStr[i];
				Result.AppendChar(C);
				if (C == TEXT('='))
				{
					// Gather the following identifier token (letters, digits, underscore) — stop before quotes/parens/paths
					int32 Start = i + 1;
					int32 End = Start;
					while (End < ValueStr.Len())
					{
						TCHAR TC = ValueStr[End];
						if (FChar::IsAlnum(TC) || TC == TEXT('_')) End++;
						else break;
					}
					if (End > Start && (End >= ValueStr.Len() || ValueStr[End] == TEXT(',') || ValueStr[End] == TEXT(')') || ValueStr[End] == TEXT(']') || ValueStr[End] == TEXT('}')))
					{
						FString Token = ValueStr.Mid(Start, End - Start);
						// Skip obvious non-identifiers
						if (Token != TEXT("True") && Token != TEXT("False") && Token != TEXT("None") && !Token.IsNumeric())
						{
							// Try to resolve as actor label
							for (TActorIterator<AActor> It(World); It; ++It)
							{
								if (It->GetActorLabel() == Token)
								{
									Result.Append(It->GetPathName());
									i = End;
									goto AppendDone;
								}
							}
						}
					}
				}
			AppendDone:
				i++;
			}
			ValueStr = Result;
		}
		Prop->ImportText_Direct(*ValueStr, ValuePtr, TargetComp, PPF_None);
	}
	else
	{
		double NumValue;
		if ((*ValueField)->TryGetNumber(NumValue))
		{
			ValueStr = FString::SanitizeFloat(NumValue);
			Prop->ImportText_Direct(*ValueStr, ValuePtr, TargetComp, PPF_None);
		}
		else
		{
			bool BoolValue;
			if ((*ValueField)->TryGetBool(BoolValue))
			{
				ValueStr = BoolValue ? TEXT("true") : TEXT("false");
				Prop->ImportText_Direct(*ValueStr, ValuePtr, TargetComp, PPF_None);
			}
			else
			{
				// #216: structured JSON values (objects/arrays). Drives UObject
				// asset paths, FVector {x,y,z}, nested struct fields, etc.
				FString SetErr;
				if (!MCPJsonProperty::SetJsonOnProperty(Prop, ValuePtr, *ValueField, SetErr))
				{
					return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
				}
			}
		}
	}

	TargetComp->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentClass"), TargetComp->GetClass()->GetName());
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("previousValue"), PreviousValueStr);

	// Self-inverse: same handler with previous value as string.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	if (!ComponentName.IsEmpty()) Payload->SetStringField(TEXT("componentName"), ComponentName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PreviousValueStr);
	MCPSetRollback(Result, TEXT("set_component_property"), Payload);

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FLevelHandlers::GetWorldSettings(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	AWorldSettings* Settings = World->GetWorldSettings();
	if (!Settings)
	{
		return MCPError(TEXT("WorldSettings not available"));
	}

	// DefaultGameMode
	auto Result = MCPSuccess();
	if (Settings->DefaultGameMode)
	{
		Result->SetStringField(TEXT("defaultGameMode"), Settings->DefaultGameMode->GetPathName());
	}
	else
	{
		Result->SetStringField(TEXT("defaultGameMode"), TEXT("None"));
	}

	// KillZ
	Result->SetNumberField(TEXT("killZ"), Settings->KillZ);

	// GlobalGravityZ
	Result->SetNumberField(TEXT("globalGravityZ"), Settings->GlobalGravityZ);

	// bEnableWorldBoundsChecks
	Result->SetBoolField(TEXT("enableWorldBoundsChecks"), Settings->bEnableWorldBoundsChecks);

	// bEnableNavigationSystem
	Result->SetBoolField(TEXT("enableNavigationSystem"), Settings->IsNavigationSystemEnabled());

	// World name
	Result->SetStringField(TEXT("worldName"), World->GetName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetWorldSettings(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	AWorldSettings* Settings = World->GetWorldSettings();
	if (!Settings)
	{
		return MCPError(TEXT("WorldSettings not available"));
	}

	// Capture previous values for rollback before mutating.
	const FString PrevGameMode = Settings->DefaultGameMode ? Settings->DefaultGameMode->GetPathName() : TEXT("None");
	const double PrevKillZ = Settings->KillZ;
	const double PrevGravityZ = Settings->GlobalGravityZ;
	const bool PrevBoundsChecks = Settings->bEnableWorldBoundsChecks;

	TArray<TSharedPtr<FJsonValue>> Changes;
	TSharedPtr<FJsonObject> PrevPayload = MakeShared<FJsonObject>();

	FString GameModeStr;
	if (Params->TryGetStringField(TEXT("defaultGameMode"), GameModeStr))
	{
		if (GameModeStr.Equals(TEXT("None"), ESearchCase::IgnoreCase) || GameModeStr.IsEmpty())
		{
			Settings->DefaultGameMode = nullptr;
			Changes.Add(MakeShared<FJsonValueString>(TEXT("defaultGameMode")));
			PrevPayload->SetStringField(TEXT("defaultGameMode"), PrevGameMode);
		}
		else
		{
			UClass* GMClass = LoadObject<UClass>(nullptr, *GameModeStr);
			if (!GMClass)
			{
				GMClass = FindClassByShortName(GameModeStr);
			}
			if (GMClass && GMClass->IsChildOf(AGameModeBase::StaticClass()))
			{
				Settings->DefaultGameMode = GMClass;
				Changes.Add(MakeShared<FJsonValueString>(TEXT("defaultGameMode")));
				PrevPayload->SetStringField(TEXT("defaultGameMode"), PrevGameMode);
			}
			else
			{
				return MCPError(FString::Printf(TEXT("GameMode class not found or invalid: %s"), *GameModeStr));
			}
		}
	}

	double KillZ;
	if (Params->TryGetNumberField(TEXT("killZ"), KillZ))
	{
		Settings->KillZ = KillZ;
		Changes.Add(MakeShared<FJsonValueString>(TEXT("killZ")));
		PrevPayload->SetNumberField(TEXT("killZ"), PrevKillZ);
	}

	double GravityZ;
	if (Params->TryGetNumberField(TEXT("globalGravityZ"), GravityZ))
	{
		Settings->GlobalGravityZ = GravityZ;
		Changes.Add(MakeShared<FJsonValueString>(TEXT("globalGravityZ")));
		PrevPayload->SetNumberField(TEXT("globalGravityZ"), PrevGravityZ);
	}

	bool bBoundsChecks;
	if (Params->TryGetBoolField(TEXT("enableWorldBoundsChecks"), bBoundsChecks))
	{
		Settings->bEnableWorldBoundsChecks = bBoundsChecks;
		Changes.Add(MakeShared<FJsonValueString>(TEXT("enableWorldBoundsChecks")));
		PrevPayload->SetBoolField(TEXT("enableWorldBoundsChecks"), PrevBoundsChecks);
	}

	Settings->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetArrayField(TEXT("changes"), Changes);
	Result->SetStringField(TEXT("worldName"), World->GetName());

	if (Changes.Num() > 0)
	{
		MCPSetRollback(Result, TEXT("set_world_settings"), PrevPayload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetActorMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString MaterialPath;
	if (auto Err = RequireString(Params, TEXT("materialPath"), MaterialPath)) return Err;

	int32 SlotIndex = OptionalInt(Params, TEXT("slotIndex"), 0);

	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorLabel)
		{
			Actor = *It;
			break;
		}
	}

	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material)
	{
		return MCPError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
	}

	UPrimitiveComponent* PrimComp = Actor->FindComponentByClass<UPrimitiveComponent>();
	if (!PrimComp)
	{
		return MCPError(FString::Printf(TEXT("Actor '%s' has no primitive component"), *ActorLabel));
	}

	// Capture previous material BEFORE mutating so rollback can restore it.
	FString PreviousMaterialPath;
	if (UMaterialInterface* Prev = PrimComp->GetMaterial(SlotIndex))
	{
		PreviousMaterialPath = Prev->GetPathName();
	}

	PrimComp->SetMaterial(SlotIndex, Material);
	PrimComp->MarkRenderStateDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("materialPath"), MaterialPath);
	Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Result->SetStringField(TEXT("previousMaterialPath"), PreviousMaterialPath);

	// Self-inverse: call set_actor_material again with the previous path.
	// (If previous was unset, passing an empty path would fail material load;
	//  skip the rollback record in that case — best-effort.)
	if (!PreviousMaterialPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("materialPath"), PreviousMaterialPath);
		Payload->SetNumberField(TEXT("slotIndex"), SlotIndex);
		MCPSetRollback(Result, TEXT("set_actor_material"), Payload);
	}

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FLevelHandlers::GetActorsByClass(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;

	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	TArray<TSharedPtr<FJsonValue>> Out;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		FString CName = A->GetClass()->GetName();
		if (CName == ClassName || (A->GetClass()->IsChildOf(AActor::StaticClass()) && CName.Contains(ClassName)))
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("label"), A->GetActorLabel());
			E->SetStringField(TEXT("class"), CName);
			E->SetStringField(TEXT("path"), A->GetPathName());
			Out.Add(MakeShared<FJsonValueObject>(E));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("actors"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	return MCPResult(Result);
}

// #146: histogram of actors by class name. Cheaper than get_outliner when
// the caller only needs counts (e.g. "how many PCGVolume are loaded?").
TSharedPtr<FJsonValue> FLevelHandlers::CountActorsByClass(const TSharedPtr<FJsonObject>& Params)
{
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	const int32 TopN = OptionalInt(Params, TEXT("topN"), 0);

	TMap<FString, int32> Counts;
	int32 Total = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		const FString CName = A->GetClass()->GetName();
		int32& Ref = Counts.FindOrAdd(CName);
		Ref++;
		Total++;
	}

	// Sort by count desc
	TArray<TPair<FString, int32>> Sorted;
	Sorted.Reserve(Counts.Num());
	for (const auto& Pair : Counts) { Sorted.Emplace(Pair.Key, Pair.Value); }
	Sorted.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });

	if (TopN > 0 && Sorted.Num() > TopN)
	{
		Sorted.SetNum(TopN);
	}

	TArray<TSharedPtr<FJsonValue>> Out;
	for (const auto& Pair : Sorted)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("class"), Pair.Key);
		Entry->SetNumberField(TEXT("count"), Pair.Value);
		Out.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("classes"), Out);
	Result->SetNumberField(TEXT("uniqueClasses"), Counts.Num());
	Result->SetNumberField(TEXT("totalActors"), Total);
	return MCPResult(Result);
}

// #150: compact RVT volume summary. Returns each RuntimeVirtualTextureVolume
// actor with its RVT component's bound VirtualTexture asset path. Avoids the
// Python workaround that ranged across 'virtual_texture' / 'VirtualTexture'
// property-name variants and reflected get_editor_property by class name.
TSharedPtr<FJsonValue> FLevelHandlers::GetRVTSummary(const TSharedPtr<FJsonObject>& Params)
{
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	TArray<TSharedPtr<FJsonValue>> VolumesArr;
	TSet<FString> UniqueTextures;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		const FString ClassName = A->GetClass()->GetName();
		if (!ClassName.Contains(TEXT("RuntimeVirtualTexture"))) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("label"), A->GetActorLabel());
		Entry->SetStringField(TEXT("class"), ClassName);
		Entry->SetStringField(TEXT("path"), A->GetPathName());

		// Reflectively walk components for a RuntimeVirtualTextureComponent
		TArray<UActorComponent*> Comps;
		A->GetComponents(Comps);
		TArray<TSharedPtr<FJsonValue>> CompArr;
		for (UActorComponent* C : Comps)
		{
			if (!C) continue;
			const FString CName = C->GetClass()->GetName();
			if (!CName.Contains(TEXT("RuntimeVirtualTexture"))) continue;

			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("name"), C->GetName());
			CObj->SetStringField(TEXT("class"), CName);
			// Try both common property names — UE has renamed this across versions.
			if (FObjectProperty* VT = CastField<FObjectProperty>(C->GetClass()->FindPropertyByName(TEXT("VirtualTexture"))))
			{
				if (UObject* Asset = VT->GetObjectPropertyValue_InContainer(C))
				{
					CObj->SetStringField(TEXT("virtualTexture"), Asset->GetPathName());
					UniqueTextures.Add(Asset->GetPathName());
				}
			}
			CompArr.Add(MakeShared<FJsonValueObject>(CObj));
		}
		Entry->SetArrayField(TEXT("components"), CompArr);

		const FVector Loc = A->GetActorLocation();
		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), Loc.X);
		LocObj->SetNumberField(TEXT("y"), Loc.Y);
		LocObj->SetNumberField(TEXT("z"), Loc.Z);
		Entry->SetObjectField(TEXT("location"), LocObj);

		VolumesArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TArray<TSharedPtr<FJsonValue>> UniqueTexArr;
	for (const FString& T : UniqueTextures) UniqueTexArr.Add(MakeShared<FJsonValueString>(T));

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("volumes"), VolumesArr);
	Result->SetNumberField(TEXT("volumeCount"), VolumesArr.Num());
	Result->SetArrayField(TEXT("uniqueVirtualTextures"), UniqueTexArr);
	return MCPResult(Result);
}

// ─── #151 set_water_body_property ───────────────────────────────────
// Set a property on the first UWaterBodyComponent of an actor (ShapeDilation,
// WaterLevel, etc.). Uses runtime class lookup so the Water plugin is not a
// hard build dependency — if the plugin isn't loaded, the handler returns
// a clear error rather than failing to link.
TSharedPtr<FJsonValue> FLevelHandlers::SetWaterBodyProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;
	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	FString ValueStr;
	bool bHaveValue = false;
	TSharedPtr<FJsonValue> V = Params->TryGetField(TEXT("value"));
	if (V.IsValid())
	{
		if (V->TryGetString(ValueStr)) bHaveValue = true;
		else if (V->Type == EJson::Number) { ValueStr = FString::SanitizeFloat(V->AsNumber()); bHaveValue = true; }
		else if (V->Type == EJson::Boolean) { ValueStr = V->AsBool() ? TEXT("true") : TEXT("false"); bHaveValue = true; }
	}
	if (!bHaveValue) return MCPError(TEXT("Missing or non-coerceable 'value' parameter"));

	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	}
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

	UClass* WBClass = LoadClass<UActorComponent>(nullptr, TEXT("/Script/Water.WaterBodyComponent"));
	if (!WBClass)
	{
		return MCPError(TEXT("WaterBodyComponent class not available — enable the Water plugin"));
	}

	UActorComponent* WBComp = nullptr;
	TArray<UActorComponent*> Comps;
	Actor->GetComponents(Comps);
	for (UActorComponent* C : Comps)
	{
		if (C && C->GetClass()->IsChildOf(WBClass)) { WBComp = C; break; }
	}
	if (!WBComp) return MCPError(FString::Printf(TEXT("Actor '%s' has no WaterBodyComponent"), *ActorLabel));

	FProperty* Prop = WBComp->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop) return MCPError(FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *WBComp->GetClass()->GetName()));

	WBComp->Modify();
	void* Addr = Prop->ContainerPtrToValuePtr<void>(WBComp);
	const TCHAR* R = Prop->ImportText_Direct(*ValueStr, Addr, WBComp, PPF_None);
	if (R == nullptr) return MCPError(FString::Printf(TEXT("ImportText failed for '%s'"), *ValueStr));

	// Fire PostEditChangeProperty so the water body rebuilds / re-renders.
	FPropertyChangedEvent Evt(Prop);
	WBComp->PostEditChangeProperty(Evt);
	Actor->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), WBComp->GetName());
	Result->SetStringField(TEXT("componentClass"), WBComp->GetClass()->GetName());
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), ValueStr);
	return MCPResult(Result);
}

// ─── #188 get_actor_bounds ──────────────────────────────────────────
// Returns the axis-aligned bounding box (origin + extent) for an actor
// found by its editor label.
TSharedPtr<FJsonValue> FLevelHandlers::GetActorBounds(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorLabel)
		{
			Actor = *It;
			break;
		}
	}

	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	FVector Origin;
	FVector Extent;
	Actor->GetActorBounds(false, Origin, Extent);

	TSharedPtr<FJsonObject> OriginObj = MakeShared<FJsonObject>();
	OriginObj->SetNumberField(TEXT("x"), Origin.X);
	OriginObj->SetNumberField(TEXT("y"), Origin.Y);
	OriginObj->SetNumberField(TEXT("z"), Origin.Z);

	TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
	ExtentObj->SetNumberField(TEXT("x"), Extent.X);
	ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
	ExtentObj->SetNumberField(TEXT("z"), Extent.Z);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetObjectField(TEXT("origin"), OriginObj);
	Result->SetObjectField(TEXT("extent"), ExtentObj);
	return MCPResult(Result);
}

// ─── #178 resolve_actor ─────────────────────────────────────────────
// Resolves an actor by its internal/runtime UObject name (e.g.
// "StaticMeshActor_141") and returns its label, path, class, and location.
TSharedPtr<FJsonValue> FLevelHandlers::ResolveActor(const TSharedPtr<FJsonObject>& Params)
{
	FString InternalName;
	if (auto Err = RequireString(Params, TEXT("internalName"), InternalName)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == InternalName)
		{
			Actor = *It;
			break;
		}
	}

	if (!Actor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found by internal name: %s"), *InternalName));
	}

	TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	FVector Location = Actor->GetActorLocation();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("className"), Actor->GetClass()->GetName());
	Result->SetObjectField(TEXT("location"), LocationObj);
	return MCPResult(Result);
}

// #202/#230: generic per-instance UPROPERTY writer for level actors. Resolves
// the actor by label, walks dotted property paths, and routes the value
// through the recursive JSON setter so object refs / vectors / nested
// structs all apply. The optional `force` flag flips off the EditDefaultsOnly
// gate so per-instance overrides on EditDefaultsOnly properties go through
// (the per-instance value always existed - the editor UI just hides it).
TSharedPtr<FJsonValue> FLevelHandlers::SetActorProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
	if (!ValueField || !(*ValueField).IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	const bool bForce = OptionalBool(Params, TEXT("force"), false);
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));

	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

	AActor* TargetActor = nullptr;
	const bool bWorldSettings = ActorLabel.Equals(TEXT("WorldSettings"), ESearchCase::IgnoreCase);
	if (bWorldSettings)
	{
		TargetActor = World->GetWorldSettings();
	}
	else
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == ActorLabel)
			{
				TargetActor = *It;
				break;
			}
		}
	}

	if (!TargetActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));
	if (PathParts.Num() == 0) return MCPError(TEXT("Empty propertyName"));

	UStruct* CurrentStruct = TargetActor->GetClass();
	void* CurrentContainer = TargetActor;
	FProperty* Prop = nullptr;
	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FProperty* Seg = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Seg) return MCPError(FString::Printf(TEXT("Property '%s' not found at '%s'"), *PathParts[i], *PropertyName));
		if (i < PathParts.Num() - 1)
		{
			if (FStructProperty* SP = CastField<FStructProperty>(Seg))
			{
				CurrentContainer = SP->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = SP->Struct;
			}
			// #305: descend through Instanced UObject sub-objects too. The path
			// "APCGWorldActor.LandscapeCacheObject.SerializationMode" hits an
			// FObjectProperty (not a struct) - the previous "is not a struct"
			// rejection forced execute_python on every instanced-subobject write.
			else if (FObjectProperty* OP = CastField<FObjectProperty>(Seg))
			{
				UObject* SubObject = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(CurrentContainer));
				if (!SubObject)
				{
					return MCPError(FString::Printf(
						TEXT("Cannot descend into '%s' - the sub-object reference is null"),
						*PathParts[i]));
				}
				SubObject->Modify();
				CurrentContainer = SubObject;
				CurrentStruct = SubObject->GetClass();
			}
			else
			{
				return MCPError(FString::Printf(
					TEXT("'%s' is not a struct or sub-object - cannot descend"), *PathParts[i]));
			}
		}
		else
		{
			Prop = Seg;
		}
	}

	// Strip the EditDefaultsOnly gate locally for the duration of the write,
	// then restore. Other UPROPERTY flags stay untouched.
	const EPropertyFlags OriginalFlags = Prop->PropertyFlags;
	if (bForce)
	{
		Prop->PropertyFlags &= ~CPF_DisableEditOnInstance;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CurrentContainer);

	FString PrevValue;
	Prop->ExportText_Direct(PrevValue, ValuePtr, ValuePtr, TargetActor, PPF_None);

	TargetActor->Modify();

	// If the JSON value is a string and the property is an object reference,
	// try resolving the string as an actor label first so callers can write
	// {value: "Hopper_01"} for AHopper* references.
	TSharedPtr<FJsonValue> Value = *ValueField;
	if (Value->Type == EJson::String)
	{
		FString S = Value->AsString();
		if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
		{
			AActor* RefActor = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->GetActorLabel() == S) { RefActor = *It; break; }
			}
			if (RefActor && RefActor->IsA(OP->PropertyClass))
			{
				OP->SetObjectPropertyValue(ValuePtr, RefActor);
				goto WriteDone;
			}
		}
	}

	{
		FString SetErr;
		if (!MCPJsonProperty::SetJsonOnProperty(Prop, ValuePtr, Value, SetErr))
		{
			Prop->PropertyFlags = OriginalFlags;
			return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
		}
	}

WriteDone:
	Prop->PropertyFlags = OriginalFlags;

	FPropertyChangedEvent ChangeEvent(Prop);
	TargetActor->PostEditChangeProperty(ChangeEvent);
	TargetActor->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("previousValue"), PrevValue);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	if (bForce) Payload->SetBoolField(TEXT("force"), true);
	MCPSetRollback(Result, TEXT("set_actor_property"), Payload);

	return MCPResult(Result);
}

namespace
{
}

// #220: bulk delete actors matching label prefix / class / tag.
TSharedPtr<FJsonValue> FLevelHandlers::DeleteActors(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FString LabelPrefix = OptionalString(Params, TEXT("labelPrefix"));
	const FString ClassName = OptionalString(Params, TEXT("className"));
	const FString Tag = OptionalString(Params, TEXT("tag"));
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);

	if (LabelPrefix.IsEmpty() && ClassName.IsEmpty() && Tag.IsEmpty())
	{
		return MCPError(TEXT("Provide at least one filter: labelPrefix, className, or tag"));
	}

	TArray<AActor*> Matches;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) continue;
		if (!LabelPrefix.IsEmpty() && !A->GetActorLabel().StartsWith(LabelPrefix)) continue;
		if (!ClassName.IsEmpty())
		{
			const FString CName = A->GetClass()->GetName();
			if (!CName.Contains(ClassName)) continue;
		}
		if (!Tag.IsEmpty() && !A->ActorHasTag(FName(*Tag))) continue;
		Matches.Add(A);
	}

	TArray<TSharedPtr<FJsonValue>> Labels;
	for (AActor* A : Matches)
	{
		Labels.Add(MakeShared<FJsonValueString>(A->GetActorLabel()));
	}

	int32 Deleted = 0;
	if (!bDryRun)
	{
		UEditorActorSubsystem* EAS = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
		for (AActor* A : Matches)
		{
			bool bOk = false;
			if (EAS)
			{
				bOk = EAS->DestroyActor(A);
			}
			if (!bOk && A)
			{
				bOk = World->DestroyActor(A);
			}
			if (bOk) Deleted++;
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetNumberField(TEXT("matched"), Matches.Num());
	Result->SetNumberField(TEXT("deleted"), Deleted);
	Result->SetArrayField(TEXT("labels"), Labels);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::AddActorTag(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return E;
	FString Tag; if (auto E = RequireString(Params, TEXT("tag"), Tag)) return E;

	AActor* A = FindActorByLabel(World, ActorLabel);
	if (!A) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

	const FName TagName(*Tag);
	const bool bAlreadyHad = A->Tags.Contains(TagName);
	if (!bAlreadyHad)
	{
		A->Modify();
		A->Tags.Add(TagName);
		A->MarkPackageDirty();
	}
	auto Result = MCPSuccess();
	if (bAlreadyHad) MCPSetExisted(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("tag"), Tag);
	TArray<TSharedPtr<FJsonValue>> TagsOut;
	for (const FName& T : A->Tags) TagsOut.Add(MakeShared<FJsonValueString>(T.ToString()));
	Result->SetArrayField(TEXT("tags"), TagsOut);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::RemoveActorTag(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return E;
	FString Tag; if (auto E = RequireString(Params, TEXT("tag"), Tag)) return E;

	AActor* A = FindActorByLabel(World, ActorLabel);
	if (!A) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

	const FName TagName(*Tag);
	const int32 Removed = A->Tags.Remove(TagName);
	if (Removed > 0)
	{
		A->Modify();
		A->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	if (Removed == 0) MCPSetExisted(Result); else MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("tag"), Tag);
	Result->SetNumberField(TEXT("removed"), Removed);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetActorTags(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return E;

	AActor* A = FindActorByLabel(World, ActorLabel);
	if (!A) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

	const TArray<TSharedPtr<FJsonValue>>* TagsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("tags"), TagsArr) || !TagsArr)
	{
		return MCPError(TEXT("Missing 'tags' array"));
	}

	A->Modify();
	A->Tags.Reset();
	for (const TSharedPtr<FJsonValue>& V : *TagsArr)
	{
		FString S;
		if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
		{
			A->Tags.AddUnique(FName(*S));
		}
	}
	A->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FName& T : A->Tags) Out.Add(MakeShared<FJsonValueString>(T.ToString()));
	Result->SetArrayField(TEXT("tags"), Out);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::ListActorTags(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return E;

	AActor* A = FindActorByLabel(World, ActorLabel);
	if (!A) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FName& T : A->Tags) Out.Add(MakeShared<FJsonValueString>(T.ToString()));
	Result->SetArrayField(TEXT("tags"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	return MCPResult(Result);
}

// #205: attach an actor's root component to a parent actor.
TSharedPtr<FJsonValue> FLevelHandlers::AttachActor(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ChildLabel; if (auto E = RequireString(Params, TEXT("childLabel"), ChildLabel)) return E;
	FString ParentLabel; if (auto E = RequireString(Params, TEXT("parentLabel"), ParentLabel)) return E;

	AActor* Child = FindActorByLabel(World, ChildLabel);
	AActor* Parent = FindActorByLabel(World, ParentLabel);
	if (!Child) return MCPError(FString::Printf(TEXT("Child actor not found: %s"), *ChildLabel));
	if (!Parent) return MCPError(FString::Printf(TEXT("Parent actor not found: %s"), *ParentLabel));

	const FString RuleStr = OptionalString(Params, TEXT("attachRule"), TEXT("KeepWorld")).ToLower();
	EAttachmentRule Loc = EAttachmentRule::KeepWorld;
	if (RuleStr.Contains(TEXT("relative"))) Loc = EAttachmentRule::KeepRelative;
	else if (RuleStr.Contains(TEXT("snap"))) Loc = EAttachmentRule::SnapToTarget;

	const FString SocketName = OptionalString(Params, TEXT("socketName"));

	Child->Modify();
	const bool bOk = Child->AttachToActor(Parent, FAttachmentTransformRules(Loc, Loc, Loc, true), FName(*SocketName));
	Child->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("childLabel"), ChildLabel);
	Result->SetStringField(TEXT("parentLabel"), ParentLabel);
	Result->SetBoolField(TEXT("attached"), bOk);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("childLabel"), ChildLabel);
	MCPSetRollback(Result, TEXT("detach_actor"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::DetachActor(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ChildLabel; if (auto E = RequireString(Params, TEXT("childLabel"), ChildLabel)) return E;

	AActor* Child = FindActorByLabel(World, ChildLabel);
	if (!Child) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ChildLabel));

	Child->Modify();
	Child->DetachFromActor(FDetachmentTransformRules(EDetachmentRule::KeepWorld, true));
	Child->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("childLabel"), ChildLabel);
	Result->SetBoolField(TEXT("detached"), true);
	return MCPResult(Result);
}

// #205: set USceneComponent::Mobility on the actor's root component.
TSharedPtr<FJsonValue> FLevelHandlers::SetActorMobility(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel; if (auto E = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return E;
	FString MobilityStr; if (auto E = RequireString(Params, TEXT("mobility"), MobilityStr)) return E;

	AActor* A = FindActorByLabel(World, ActorLabel);
	if (!A) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	USceneComponent* Root = A->GetRootComponent();
	if (!Root) return MCPError(FString::Printf(TEXT("Actor '%s' has no root component"), *ActorLabel));

	const FString L = MobilityStr.ToLower();
	EComponentMobility::Type M = EComponentMobility::Static;
	if (L == TEXT("movable") || L == TEXT("moveable")) M = EComponentMobility::Movable;
	else if (L == TEXT("stationary")) M = EComponentMobility::Stationary;
	else if (L == TEXT("static")) M = EComponentMobility::Static;
	else return MCPError(FString::Printf(TEXT("Unknown mobility '%s' (expected static|stationary|movable)"), *MobilityStr));

	const EComponentMobility::Type Prev = Root->Mobility;
	Root->Modify();
	Root->SetMobility(M);
	A->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("mobility"), MobilityStr);

	const TCHAR* PrevStr = Prev == EComponentMobility::Movable ? TEXT("movable")
		: Prev == EComponentMobility::Stationary ? TEXT("stationary") : TEXT("static");
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("mobility"), PrevStr);
	MCPSetRollback(Result, TEXT("set_actor_mobility"), Payload);
	return MCPResult(Result);
}

// #204: read the current edit-target sub-level. UE drops new actors into this
// level when multiple sub-levels are loaded; without a way to query/set it the
// caller can't reliably target a particular streaming sub-level for spawns.
TSharedPtr<FJsonValue> FLevelHandlers::GetCurrentEditLevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	ULevel* Cur = World->GetCurrentLevel();
	auto Result = MCPSuccess();
	if (Cur)
	{
		Result->SetStringField(TEXT("levelName"), Cur->GetOuter()->GetName());
		Result->SetStringField(TEXT("levelPath"), Cur->GetOuter()->GetPathName());
		Result->SetBoolField(TEXT("isPersistent"), Cur == World->PersistentLevel);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetCurrentEditLevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString LevelName;
	if (!Params->TryGetStringField(TEXT("levelName"), LevelName))
	{
		Params->TryGetStringField(TEXT("levelPath"), LevelName);
	}
	if (LevelName.IsEmpty()) return MCPError(TEXT("Missing levelName (or levelPath)"));

	ULevelEditorSubsystem* LES = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LES) return MCPError(TEXT("LevelEditorSubsystem not available"));

	const bool bOk = LES->SetCurrentLevelByName(FName(*LevelName));
	if (!bOk)
	{
		return MCPError(FString::Printf(TEXT("No loaded sub-level named '%s'"), *LevelName));
	}

	ULevel* Cur = World->GetCurrentLevel();
	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	if (Cur)
	{
		Result->SetStringField(TEXT("levelName"), Cur->GetOuter()->GetName());
		Result->SetStringField(TEXT("levelPath"), Cur->GetOuter()->GetPathName());
	}
	return MCPResult(Result);
}

// #206: streaming sub-level CRUD
namespace
{
	static ULevelStreaming* FindStreamingByName(UWorld* World, const FString& NameOrPath)
	{
		if (!World) return nullptr;
		for (ULevelStreaming* SL : World->GetStreamingLevels())
		{
			if (!SL) continue;
			const FString PkgName = SL->GetWorldAssetPackageName();
			if (PkgName == NameOrPath) return SL;
			if (FPaths::GetBaseFilename(PkgName) == NameOrPath) return SL;
			if (SL->GetName() == NameOrPath) return SL;
		}
		return nullptr;
	}
}

TSharedPtr<FJsonValue> FLevelHandlers::ListStreamingSublevels(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> Out;
	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		const FString PkgName = SL->GetWorldAssetPackageName();
		O->SetStringField(TEXT("levelName"), FPaths::GetBaseFilename(PkgName));
		O->SetStringField(TEXT("packageName"), PkgName);
		O->SetStringField(TEXT("streamingClass"), SL->GetClass()->GetName());
		O->SetBoolField(TEXT("initiallyLoaded"), SL->ShouldBeLoaded());
		O->SetBoolField(TEXT("initiallyVisible"), SL->GetShouldBeVisibleFlag());
		O->SetBoolField(TEXT("loaded"), SL->IsLevelLoaded());
		O->SetBoolField(TEXT("visible"), SL->GetShouldBeVisibleFlag());
		const FTransform T = SL->LevelTransform;
		TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
		Loc->SetNumberField(TEXT("x"), T.GetLocation().X);
		Loc->SetNumberField(TEXT("y"), T.GetLocation().Y);
		Loc->SetNumberField(TEXT("z"), T.GetLocation().Z);
		O->SetObjectField(TEXT("location"), Loc);
		Out.Add(MakeShared<FJsonValueObject>(O));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("sublevels"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::AddStreamingSublevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString LevelPath; if (auto E = RequireString(Params, TEXT("levelPath"), LevelPath)) return E;

	const FString StreamingClassName = OptionalString(Params, TEXT("streamingClass"), TEXT("LevelStreamingDynamic"));
	UClass* StreamingClass = ULevelStreamingDynamic::StaticClass();
	if (StreamingClassName.Equals(TEXT("LevelStreamingAlwaysLoaded"), ESearchCase::IgnoreCase))
	{
		StreamingClass = LoadClass<ULevelStreaming>(nullptr, TEXT("/Script/Engine.LevelStreamingAlwaysLoaded"));
		if (!StreamingClass) StreamingClass = ULevelStreamingDynamic::StaticClass();
	}

	ULevelStreaming* SL = UEditorLevelUtils::AddLevelToWorld(World, *LevelPath, StreamingClass);
	if (!SL)
	{
		return MCPError(FString::Printf(TEXT("Failed to add sub-level '%s'"), *LevelPath));
	}

	if (Params->HasField(TEXT("initiallyLoaded"))) SL->SetShouldBeLoaded(OptionalBool(Params, TEXT("initiallyLoaded"), true));
	if (Params->HasField(TEXT("initiallyVisible"))) SL->SetShouldBeVisible(OptionalBool(Params, TEXT("initiallyVisible"), true));

	if (Params->HasField(TEXT("location")))
	{
		FTransform T = SL->LevelTransform;
		T.SetLocation(OptionalVec3(Params, TEXT("location")));
		SL->LevelTransform = T;
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("levelPath"), LevelPath);
	Result->SetStringField(TEXT("levelName"), FPaths::GetBaseFilename(LevelPath));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::RemoveStreamingSublevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString Name;
	if (!Params->TryGetStringField(TEXT("levelName"), Name)) Params->TryGetStringField(TEXT("levelPath"), Name);
	if (Name.IsEmpty()) return MCPError(TEXT("Missing levelName (or levelPath)"));

	ULevelStreaming* SL = FindStreamingByName(World, Name);
	if (!SL) return MCPError(FString::Printf(TEXT("Streaming sub-level not found: %s"), *Name));

	ULevel* Loaded = SL->GetLoadedLevel();
	if (Loaded)
	{
		UEditorLevelUtils::RemoveLevelFromWorld(Loaded);
	}
	World->RemoveStreamingLevels({ SL });

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("levelName"), Name);
	Result->SetBoolField(TEXT("removed"), true);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetStreamingSublevelProperties(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString Name;
	if (!Params->TryGetStringField(TEXT("levelName"), Name)) Params->TryGetStringField(TEXT("levelPath"), Name);
	if (Name.IsEmpty()) return MCPError(TEXT("Missing levelName (or levelPath)"));

	ULevelStreaming* SL = FindStreamingByName(World, Name);
	if (!SL) return MCPError(FString::Printf(TEXT("Streaming sub-level not found: %s"), *Name));

	bool bChanged = false;
	if (Params->HasField(TEXT("initiallyLoaded"))) { SL->SetShouldBeLoaded(OptionalBool(Params, TEXT("initiallyLoaded"), true)); bChanged = true; }
	if (Params->HasField(TEXT("initiallyVisible"))) { SL->SetShouldBeVisible(OptionalBool(Params, TEXT("initiallyVisible"), true)); bChanged = true; }

	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocObj) && LocObj && (*LocObj).IsValid())
	{
		double X = 0, Y = 0, Z = 0;
		(*LocObj)->TryGetNumberField(TEXT("x"), X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Z);
		FTransform T = SL->LevelTransform;
		T.SetLocation(FVector(X, Y, Z));
		SL->LevelTransform = T;
		bChanged = true;
	}

	bool bEditorVisibleSet = false;
	const bool bEditorVisible = OptionalBool(Params, TEXT("editorVisible"), true);
	if (Params->HasField(TEXT("editorVisible")))
	{
		ULevel* Loaded = SL->GetLoadedLevel();
		if (Loaded)
		{
			UEditorLevelUtils::SetLevelVisibility(Loaded, bEditorVisible, false);
		}
		bEditorVisibleSet = true;
	}

	auto Result = MCPSuccess();
	if (bChanged) MCPSetUpdated(Result); else MCPSetExisted(Result);
	Result->SetStringField(TEXT("levelName"), Name);
	Result->SetBoolField(TEXT("initiallyLoaded"), SL->ShouldBeLoaded());
	Result->SetBoolField(TEXT("initiallyVisible"), SL->GetShouldBeVisibleFlag());
	if (bEditorVisibleSet) Result->SetBoolField(TEXT("editorVisible"), bEditorVisible);
	return MCPResult(Result);
}

// #203: batch spawn StaticMeshActors on a 3D grid (or jittered cloud) so
// agents don't ship one place_actor per mesh. Bounds are an FBox; density
// drives count along each axis.
TSharedPtr<FJsonValue> FLevelHandlers::SpawnGrid(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString MeshPath; if (auto E = RequireString(Params, TEXT("staticMesh"), MeshPath)) return E;
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (!Mesh) return MCPError(FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath));

	FVector Min, Max;
	if (auto Err = RequireVec3(Params, TEXT("min"), Min)) return Err;
	if (auto Err = RequireVec3(Params, TEXT("max"), Max)) return Err;

	const int32 CountX = FMath::Max(1, OptionalInt(Params, TEXT("countX"), 4));
	const int32 CountY = FMath::Max(1, OptionalInt(Params, TEXT("countY"), 4));
	const int32 CountZ = FMath::Max(1, OptionalInt(Params, TEXT("countZ"), 1));
	const double Jitter = OptionalNumber(Params, TEXT("jitter"), 0.0);
	const FString LabelPrefix = OptionalString(Params, TEXT("labelPrefix"), TEXT("Grid"));

	const FVector Step = FVector(
		CountX > 1 ? (Max.X - Min.X) / (CountX - 1) : 0,
		CountY > 1 ? (Max.Y - Min.Y) / (CountY - 1) : 0,
		CountZ > 1 ? (Max.Z - Min.Z) / (CountZ - 1) : 0);

	FRandomStream Rand((int32)FDateTime::Now().GetTicks());

	TArray<TSharedPtr<FJsonValue>> Spawned;
	int32 Index = 0;
	for (int32 zi = 0; zi < CountZ; ++zi)
	for (int32 yi = 0; yi < CountY; ++yi)
	for (int32 xi = 0; xi < CountX; ++xi)
	{
		FVector Loc = Min + FVector(xi * Step.X, yi * Step.Y, zi * Step.Z);
		if (Jitter > 0.0)
		{
			Loc += FVector(Rand.FRandRange(-Jitter, Jitter), Rand.FRandRange(-Jitter, Jitter), Rand.FRandRange(-Jitter, Jitter));
		}
		FActorSpawnParameters SpawnParams;
		AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Loc, FRotator::ZeroRotator, SpawnParams);
		if (!SMA) continue;
		SMA->SetMobility(EComponentMobility::Movable);
		if (UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent())
		{
			SMC->SetStaticMesh(Mesh);
		}
		SMA->SetActorLabel(FString::Printf(TEXT("%s_%d"), *LabelPrefix, Index));
		Spawned.Add(MakeShared<FJsonValueString>(SMA->GetActorLabel()));
		Index++;
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetNumberField(TEXT("count"), Spawned.Num());
	Result->SetArrayField(TEXT("labels"), Spawned);
	return MCPResult(Result);
}

// #203: batch translate by label list or tag.
TSharedPtr<FJsonValue> FLevelHandlers::BatchTranslate(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FVector Offset;
	if (auto Err = RequireVec3(Params, TEXT("offset"), Offset)) return Err;

	TSet<AActor*> Targets;
	const TArray<TSharedPtr<FJsonValue>>* LabelArr = nullptr;
	if (Params->TryGetArrayField(TEXT("actorLabels"), LabelArr) && LabelArr)
	{
		for (const auto& V : *LabelArr)
		{
			FString S; if (V.IsValid() && V->TryGetString(S))
			{
				if (AActor* A = FindActorByLabel(World, S)) Targets.Add(A);
			}
		}
	}
	FString TagFilter; if (Params->TryGetStringField(TEXT("tag"), TagFilter) && !TagFilter.IsEmpty())
	{
		const FName TagName(*TagFilter);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(TagName)) Targets.Add(*It);
		}
	}
	if (Targets.Num() == 0) return MCPError(TEXT("Provide actorLabels[] or tag matching at least one actor"));

	for (AActor* A : Targets)
	{
		A->Modify();
		A->SetActorLocation(A->GetActorLocation() + Offset);
		A->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetNumberField(TEXT("count"), Targets.Num());
	return MCPResult(Result);
}

// #264 — place_actors_batch: spawn many StaticMeshActors with per-instance
// mesh + transform. Avoids the chatty place_actor-per-row pattern that filled
// up the workaround log for procedural placement scripts.
TSharedPtr<FJsonValue> FLevelHandlers::PlaceActorsBatch(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const TArray<TSharedPtr<FJsonValue>>* ActorsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("actors"), ActorsArr) || !ActorsArr)
	{
		return MCPError(TEXT("Missing 'actors' (array of {staticMesh, location?, rotation?, scale?, label?})"));
	}

	auto ReadVec = [](const TSharedPtr<FJsonObject>& Obj, FVector Default) -> FVector
	{
		if (!Obj.IsValid()) return Default;
		FVector V = Default;
		double X = 0; if (Obj->TryGetNumberField(TEXT("x"), X)) V.X = X;
		double Y = 0; if (Obj->TryGetNumberField(TEXT("y"), Y)) V.Y = Y;
		double Z = 0; if (Obj->TryGetNumberField(TEXT("z"), Z)) V.Z = Z;
		return V;
	};
	auto ReadRot = [](const TSharedPtr<FJsonObject>& Obj) -> FRotator
	{
		if (!Obj.IsValid()) return FRotator::ZeroRotator;
		FRotator R(0, 0, 0);
		double V = 0; if (Obj->TryGetNumberField(TEXT("pitch"), V)) R.Pitch = V;
		if (Obj->TryGetNumberField(TEXT("yaw"), V)) R.Yaw = V;
		if (Obj->TryGetNumberField(TEXT("roll"), V)) R.Roll = V;
		return R;
	};

	// Cache mesh loads by path so a 1000-row batch with 5 unique meshes only
	// does 5 LoadObject calls.
	TMap<FString, UStaticMesh*> MeshCache;
	auto ResolveMesh = [&MeshCache](const FString& Path) -> UStaticMesh*
	{
		if (Path.IsEmpty()) return nullptr;
		if (UStaticMesh** Cached = MeshCache.Find(Path)) return *Cached;
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		MeshCache.Add(Path, Mesh);
		return Mesh;
	};

	int32 Spawned = 0, FailedMesh = 0, FailedSpawn = 0;
	TArray<TSharedPtr<FJsonValue>> Labels;
	TArray<TSharedPtr<FJsonValue>> Errors;

	for (int32 i = 0; i < ActorsArr->Num(); i++)
	{
		const TSharedPtr<FJsonValue>& Entry = (*ActorsArr)[i];
		const TSharedPtr<FJsonObject> Row = Entry.IsValid() ? Entry->AsObject() : nullptr;
		if (!Row.IsValid()) { FailedSpawn++; continue; }

		FString MeshPath;
		Row->TryGetStringField(TEXT("staticMesh"), MeshPath);
		UStaticMesh* Mesh = ResolveMesh(MeshPath);
		if (!Mesh)
		{
			FailedMesh++;
			TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetNumberField(TEXT("index"), i);
			Err->SetStringField(TEXT("staticMesh"), MeshPath);
			Err->SetStringField(TEXT("reason"), TEXT("static_mesh_not_found"));
			Errors.Add(MakeShared<FJsonValueObject>(Err));
			continue;
		}

		const TSharedPtr<FJsonObject>* LocObj = nullptr;
		const TSharedPtr<FJsonObject>* RotObj = nullptr;
		const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
		Row->TryGetObjectField(TEXT("location"), LocObj);
		Row->TryGetObjectField(TEXT("rotation"), RotObj);
		Row->TryGetObjectField(TEXT("scale"), ScaleObj);

		const FVector Loc   = LocObj ? ReadVec(*LocObj, FVector::ZeroVector) : FVector::ZeroVector;
		const FRotator Rot  = RotObj ? ReadRot(*RotObj) : FRotator::ZeroRotator;
		const FVector Scale = ScaleObj ? ReadVec(*ScaleObj, FVector::OneVector) : FVector::OneVector;

		FActorSpawnParameters SpawnParams;
		AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Loc, Rot, SpawnParams);
		if (!SMA) { FailedSpawn++; continue; }
		SMA->SetMobility(EComponentMobility::Movable);
		if (UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent())
		{
			SMC->SetStaticMesh(Mesh);
			SMC->SetWorldScale3D(Scale);
		}

		FString Label;
		if (Row->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
		{
			SMA->SetActorLabel(Label);
		}
		Labels.Add(MakeShared<FJsonValueString>(SMA->GetActorLabel()));
		Spawned++;
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetNumberField(TEXT("requested"), ActorsArr->Num());
	Result->SetNumberField(TEXT("spawned"), Spawned);
	Result->SetNumberField(TEXT("failedMesh"), FailedMesh);
	Result->SetNumberField(TEXT("failedSpawn"), FailedSpawn);
	Result->SetArrayField(TEXT("labels"), Labels);
	if (Errors.Num() > 0) Result->SetArrayField(TEXT("errors"), Errors);
	return MCPResult(Result);
}

