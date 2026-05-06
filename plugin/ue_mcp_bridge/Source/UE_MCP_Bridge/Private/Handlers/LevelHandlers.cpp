#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "VolumeHelpers_Internal.h"
#include "EditorScriptingUtilities/Public/EditorLevelLibrary.h"
#include "Editor.h"
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
#include "Materials/MaterialInterface.h"
#include "HandlerJsonProperty.h"

void FLevelHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("get_world_outliner"), &GetOutliner);
	Registry.RegisterHandler(TEXT("place_actor"), &PlaceActor);
	Registry.RegisterHandler(TEXT("delete_actor"), &DeleteActor);
	Registry.RegisterHandler(TEXT("get_actor_details"), &GetActorDetails);
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
	Registry.RegisterHandler(TEXT("save_level"), &SaveLevel);
	Registry.RegisterHandler(TEXT("list_sublevels"), &ListSublevels);
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

	// Idempotency: reuse an actor that already has this label.
	if (!Label.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Actor '%s' already exists"), *Label));
				}
				auto Existing = MCPSuccess();
				MCPSetExisted(Existing);
				Existing->SetStringField(TEXT("actorLabel"), Label);
				Existing->SetStringField(TEXT("actorClass"), It->GetClass()->GetName());
				return MCPResult(Existing);
			}
		}
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

	// Location
	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj))
	{
		(*LocationObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocationObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocationObj)->TryGetNumberField(TEXT("z"), Location.Z);
	}

	// Rotation
	FRotator Rotation = FRotator::ZeroRotator;
	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotObj))
	{
		(*RotObj)->TryGetNumberField(TEXT("pitch"), Rotation.Pitch);
		(*RotObj)->TryGetNumberField(TEXT("yaw"), Rotation.Yaw);
		(*RotObj)->TryGetNumberField(TEXT("roll"), Rotation.Roll);
	}

	// Spawn
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

	// Scale
	const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
	if (Params->TryGetObjectField(TEXT("scale"), ScaleObj))
	{
		FVector Scale = FVector::OneVector;
		(*ScaleObj)->TryGetNumberField(TEXT("x"), Scale.X);
		(*ScaleObj)->TryGetNumberField(TEXT("y"), Scale.Y);
		(*ScaleObj)->TryGetNumberField(TEXT("z"), Scale.Z);
		NewActor->SetActorScale3D(Scale);
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

TSharedPtr<FJsonValue> FLevelHandlers::ListVolumes(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString VolumeType = OptionalString(Params, TEXT("volumeType"));

	TArray<TSharedPtr<FJsonValue>> VolumesArray;
	for (TActorIterator<AVolume> ActorIt(World); ActorIt; ++ActorIt)
	{
		AVolume* Volume = *ActorIt;
		if (!Volume) continue;

		FString ClassName = Volume->GetClass()->GetName();
		if (!VolumeType.IsEmpty() && !ClassName.Contains(VolumeType))
		{
			continue;
		}

		TSharedPtr<FJsonObject> VolumeObj = MakeShared<FJsonObject>();
		VolumeObj->SetStringField(TEXT("name"), Volume->GetName());
		VolumeObj->SetStringField(TEXT("label"), Volume->GetActorLabel());
		VolumeObj->SetStringField(TEXT("class"), ClassName);
		VolumeObj->SetStringField(TEXT("path"), Volume->GetPathName());

		FVector Location = Volume->GetActorLocation();
		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), Location.X);
		LocObj->SetNumberField(TEXT("y"), Location.Y);
		LocObj->SetNumberField(TEXT("z"), Location.Z);
		VolumeObj->SetObjectField(TEXT("location"), LocObj);

		VolumesArray.Add(MakeShared<FJsonValueObject>(VolumeObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("volumes"), VolumesArray);
	Result->SetNumberField(TEXT("count"), VolumesArray.Num());

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

	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocObj))
	{
		FVector Location = Actor->GetActorLocation();
		(*LocObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Location.Z);
		Actor->SetActorLocation(Location);
	}

	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotObj))
	{
		FRotator Rotation = Actor->GetActorRotation();
		(*RotObj)->TryGetNumberField(TEXT("pitch"), Rotation.Pitch);
		(*RotObj)->TryGetNumberField(TEXT("yaw"), Rotation.Yaw);
		(*RotObj)->TryGetNumberField(TEXT("roll"), Rotation.Roll);
		Actor->SetActorRotation(Rotation);
	}

	const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
	if (Params->TryGetObjectField(TEXT("scale"), ScaleObj))
	{
		FVector Scale = Actor->GetActorScale3D();
		(*ScaleObj)->TryGetNumberField(TEXT("x"), Scale.X);
		(*ScaleObj)->TryGetNumberField(TEXT("y"), Scale.Y);
		(*ScaleObj)->TryGetNumberField(TEXT("z"), Scale.Z);
		Actor->SetActorScale3D(Scale);
	}

	FVector NewLocation = Actor->GetActorLocation();
	TSharedPtr<FJsonObject> NewLocationObj = MakeShared<FJsonObject>();
	NewLocationObj->SetNumberField(TEXT("x"), NewLocation.X);
	NewLocationObj->SetNumberField(TEXT("y"), NewLocation.Y);
	NewLocationObj->SetNumberField(TEXT("z"), NewLocation.Z);

	FRotator NewRotation = Actor->GetActorRotation();
	TSharedPtr<FJsonObject> NewRotationObj = MakeShared<FJsonObject>();
	NewRotationObj->SetNumberField(TEXT("pitch"), NewRotation.Pitch);
	NewRotationObj->SetNumberField(TEXT("yaw"), NewRotation.Yaw);
	NewRotationObj->SetNumberField(TEXT("roll"), NewRotation.Roll);

	FVector NewScale = Actor->GetActorScale3D();
	TSharedPtr<FJsonObject> NewScaleObj = MakeShared<FJsonObject>();
	NewScaleObj->SetNumberField(TEXT("x"), NewScale.X);
	NewScaleObj->SetNumberField(TEXT("y"), NewScale.Y);
	NewScaleObj->SetNumberField(TEXT("z"), NewScale.Z);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetObjectField(TEXT("location"), NewLocationObj);
	Result->SetObjectField(TEXT("rotation"), NewRotationObj);
	Result->SetObjectField(TEXT("scale"), NewScaleObj);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);

	// Self-inverse: call move_actor with previous transform.
	TSharedPtr<FJsonObject> PrevLoc = MakeShared<FJsonObject>();
	PrevLoc->SetNumberField(TEXT("x"), PreviousLocation.X);
	PrevLoc->SetNumberField(TEXT("y"), PreviousLocation.Y);
	PrevLoc->SetNumberField(TEXT("z"), PreviousLocation.Z);
	TSharedPtr<FJsonObject> PrevRot = MakeShared<FJsonObject>();
	PrevRot->SetNumberField(TEXT("pitch"), PreviousRotation.Pitch);
	PrevRot->SetNumberField(TEXT("yaw"), PreviousRotation.Yaw);
	PrevRot->SetNumberField(TEXT("roll"), PreviousRotation.Roll);
	TSharedPtr<FJsonObject> PrevScale = MakeShared<FJsonObject>();
	PrevScale->SetNumberField(TEXT("x"), PreviousScale.X);
	PrevScale->SetNumberField(TEXT("y"), PreviousScale.Y);
	PrevScale->SetNumberField(TEXT("z"), PreviousScale.Z);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetObjectField(TEXT("location"), PrevLoc);
	Payload->SetObjectField(TEXT("rotation"), PrevRot);
	Payload->SetObjectField(TEXT("scale"), PrevScale);
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

TSharedPtr<FJsonValue> FLevelHandlers::SpawnLight(const TSharedPtr<FJsonObject>& Params)
{
	FString LightType;
	if (auto Err = RequireString(Params, TEXT("lightType"), LightType)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	const FString Label = OptionalString(Params, TEXT("label"));

	// Idempotency by label.
	if (!Label.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Light '%s' already exists"), *Label));
				}
				auto Existing = MCPSuccess();
				MCPSetExisted(Existing);
				Existing->SetStringField(TEXT("actorLabel"), Label);
				Existing->SetStringField(TEXT("lightType"), LightType);
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

	double Intensity = OptionalNumber(Params, TEXT("intensity"), 5000.0);

	UClass* LightClass = nullptr;
	if (LightType.Equals(TEXT("point"), ESearchCase::IgnoreCase))
	{
		LightClass = APointLight::StaticClass();
	}
	else if (LightType.Equals(TEXT("spot"), ESearchCase::IgnoreCase))
	{
		LightClass = ASpotLight::StaticClass();
	}
	else if (LightType.Equals(TEXT("directional"), ESearchCase::IgnoreCase))
	{
		LightClass = ADirectionalLight::StaticClass();
	}
	else if (LightType.Equals(TEXT("rect"), ESearchCase::IgnoreCase))
	{
		LightClass = ARectLight::StaticClass();
	}
	else if (LightType.Equals(TEXT("sky"), ESearchCase::IgnoreCase) || LightType.Equals(TEXT("skylight"), ESearchCase::IgnoreCase))
	{
		LightClass = ASkyLight::StaticClass();
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown light type: %s. Use point, spot, directional, rect, or sky."), *LightType));
	}

	FTransform LightTransform(FRotator::ZeroRotator, Location);
	AActor* NewLight = World->SpawnActor<AActor>(LightClass, LightTransform);
	if (!NewLight)
	{
		return MCPError(TEXT("Failed to spawn light actor"));
	}

	if (!Label.IsEmpty())
	{
		NewLight->SetActorLabel(Label);
	}

	if (ULightComponent* LightComponent = NewLight->FindComponentByClass<ULightComponent>())
	{
		LightComponent->SetIntensity(Intensity);
	}
	else if (USkyLightComponent* SkyComp = NewLight->FindComponentByClass<USkyLightComponent>())
	{
		// SkyLight has no ULightComponent — set intensity on USkyLightComponent
		// directly and recapture so the change takes effect.
		SkyComp->SetIntensity(Intensity);
		SkyComp->RecaptureSky();
	}

	const FString FinalLabel = NewLight->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), FinalLabel);
	Result->SetStringField(TEXT("actorName"), NewLight->GetName());
	Result->SetStringField(TEXT("lightType"), LightType);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), FinalLabel);
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetLightProperties(const TSharedPtr<FJsonObject>& Params)
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

	ULightComponent* LightComponent = Actor->FindComponentByClass<ULightComponent>();
	if (!LightComponent)
	{
		return MCPError(FString::Printf(TEXT("Actor '%s' does not have a light component"), *ActorLabel));
	}

	// Capture previous values before mutation for self-inverse rollback.
	const double PreviousIntensity = LightComponent->Intensity;
	const FLinearColor PreviousColor = LightComponent->GetLightColor();
	const FRotator PreviousRotation = Actor->GetActorRotation();

	bool bAnyChange = false;

	double Intensity = 0.0;
	if (Params->TryGetNumberField(TEXT("intensity"), Intensity))
	{
		LightComponent->SetIntensity(Intensity);
		bAnyChange = true;
	}

	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (Params->TryGetObjectField(TEXT("color"), ColorObj))
	{
		double R = 255.0, G = 255.0, B = 255.0;
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		LightComponent->SetLightColor(FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f));
		bAnyChange = true;
	}

	// #94: DirectionalLight rotation support (sun angle for time-of-day)
	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotObj))
	{
		double Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
		(*RotObj)->TryGetNumberField(TEXT("pitch"), Pitch);
		(*RotObj)->TryGetNumberField(TEXT("yaw"), Yaw);
		(*RotObj)->TryGetNumberField(TEXT("roll"), Roll);
		Actor->SetActorRotation(FRotator((float)Pitch, (float)Yaw, (float)Roll));
		bAnyChange = true;
	}

	// #94: SkyLight recapture after intensity/color change.
	// USkyLightComponent does not inherit from ULightComponent, so look up
	// directly on the actor instead of casting from LightComponent (#207).
	if (USkyLightComponent* Sky = Actor->FindComponentByClass<USkyLightComponent>())
	{
		bool bRecapture = false;
		Params->TryGetBoolField(TEXT("recaptureSky"), bRecapture);
		if (bRecapture || bAnyChange)
		{
			Sky->RecaptureSky();
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetNumberField(TEXT("intensity"), LightComponent->Intensity);

	FLinearColor CurrentColor = LightComponent->GetLightColor();
	TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
	ColorResult->SetNumberField(TEXT("r"), CurrentColor.R * 255.0f);
	ColorResult->SetNumberField(TEXT("g"), CurrentColor.G * 255.0f);
	ColorResult->SetNumberField(TEXT("b"), CurrentColor.B * 255.0f);
	Result->SetObjectField(TEXT("color"), ColorResult);

	if (bAnyChange)
	{
		TSharedPtr<FJsonObject> PrevColor = MakeShared<FJsonObject>();
		PrevColor->SetNumberField(TEXT("r"), PreviousColor.R * 255.0f);
		PrevColor->SetNumberField(TEXT("g"), PreviousColor.G * 255.0f);
		PrevColor->SetNumberField(TEXT("b"), PreviousColor.B * 255.0f);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetNumberField(TEXT("intensity"), PreviousIntensity);
		Payload->SetObjectField(TEXT("color"), PrevColor);
		MCPSetRollback(Result, TEXT("set_light_properties"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SpawnVolume(const TSharedPtr<FJsonObject>& Params)
{
	FString VolumeType;
	if (auto Err = RequireString(Params, TEXT("volumeType"), VolumeType)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	const FString Label = OptionalString(Params, TEXT("label"));

	// Idempotency by label.
	if (!Label.IsEmpty())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Volume '%s' already exists"), *Label));
				}
				auto Existing = MCPSuccess();
				MCPSetExisted(Existing);
				Existing->SetStringField(TEXT("actorLabel"), Label);
				Existing->SetStringField(TEXT("volumeType"), VolumeType);
				return MCPResult(Existing);
			}
		}
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

	// Get extent
	FVector Extent = FVector(100.0, 100.0, 100.0);
	const TSharedPtr<FJsonObject>* ExtentObj = nullptr;
	if (Params->TryGetObjectField(TEXT("extent"), ExtentObj))
	{
		(*ExtentObj)->TryGetNumberField(TEXT("x"), Extent.X);
		(*ExtentObj)->TryGetNumberField(TEXT("y"), Extent.Y);
		(*ExtentObj)->TryGetNumberField(TEXT("z"), Extent.Z);
	}

	// Determine volume class
	UClass* VolumeClass = nullptr;
	if (VolumeType.Equals(TEXT("BlockingVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("blocking"), ESearchCase::IgnoreCase))
	{
		VolumeClass = ABlockingVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("TriggerVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("trigger"), ESearchCase::IgnoreCase))
	{
		VolumeClass = ATriggerVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("PostProcessVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("postprocess"), ESearchCase::IgnoreCase))
	{
		VolumeClass = APostProcessVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("AudioVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("audio"), ESearchCase::IgnoreCase))
	{
		VolumeClass = AAudioVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("LightmassImportanceVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("lightmass"), ESearchCase::IgnoreCase))
	{
		VolumeClass = ALightmassImportanceVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("CullDistanceVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("culldistance"), ESearchCase::IgnoreCase))
	{
		VolumeClass = FindClassByShortName(TEXT("CullDistanceVolume"));
	}
	else if (VolumeType.Equals(TEXT("NavMeshBoundsVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("navmesh"), ESearchCase::IgnoreCase))
	{
		VolumeClass = ANavMeshBoundsVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("PainCausingVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("pain"), ESearchCase::IgnoreCase))
	{
		VolumeClass = APainCausingVolume::StaticClass();
	}
	else
	{
		// Try broad class lookup
		VolumeClass = FindClassByShortName(VolumeType);
	}

	if (!VolumeClass)
	{
		return MCPError(FString::Printf(TEXT("Volume class not found: %s"), *VolumeType));
	}

	FTransform VolumeTransform(FRotator::ZeroRotator, Location);
	AActor* NewVolume = World->SpawnActor<AActor>(VolumeClass, VolumeTransform);
	if (!NewVolume)
	{
		return MCPError(TEXT("Failed to spawn volume actor"));
	}

	if (!Label.IsEmpty())
	{
		NewVolume->SetActorLabel(Label);
	}

	// #238: AVolume subclasses ship with an empty UModel by default - actor
	// scale alone leaves bounds at zero, which silently breaks downstream
	// systems (PCG samplers, navmesh bounds, audio queries). Build an actual
	// cube and run FBSPOps::HandleVolumeShapeChanged to prep + re-register.
	// Non-Volume actors keep the old scale-based behavior.
	if (AVolume* Volume = Cast<AVolume>(NewVolume))
	{
		UEMCP::BuildVolumeAsCube(World, Volume, Extent);
	}
	else
	{
		NewVolume->SetActorScale3D(Extent / 100.0);
	}

	const FString FinalLabel = NewVolume->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), FinalLabel);
	Result->SetStringField(TEXT("actorName"), NewVolume->GetName());
	Result->SetStringField(TEXT("volumeType"), VolumeType);

	// #238: when spawning a PCGVolume, accept and bind a graphPath so callers
	// don't have to make a follow-up set_actor_property call.
	FString GraphPath;
	if (Params->TryGetStringField(TEXT("graphPath"), GraphPath) && !GraphPath.IsEmpty())
	{
		if (UPCGComponent* PCGComp = NewVolume->FindComponentByClass<UPCGComponent>())
		{
			if (UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath))
			{
				PCGComp->SetGraph(Graph);
				Result->SetStringField(TEXT("graphPath"), GraphPath);
				Result->SetStringField(TEXT("graphName"), Graph->GetName());
			}
			else
			{
				Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("PCGGraph not found: %s - volume spawned without graph"), *GraphPath));
			}
		}
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), FinalLabel);
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

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

TSharedPtr<FJsonValue> FLevelHandlers::SaveLevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	// Use the LevelEditorSubsystem to save the current level
	ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LevelEditorSubsystem)
	{
		return MCPError(TEXT("LevelEditorSubsystem not available"));
	}

	bool bSuccess = LevelEditorSubsystem->SaveCurrentLevel();

	if (!bSuccess)
	{
		return MCPError(TEXT("Failed to save current level"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("levelName"), World->GetName());
	Result->SetStringField(TEXT("levelPath"), World->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::ListSublevels(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> SublevelsArray;

	// Iterate streaming/sublevels
	const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();
	for (ULevelStreaming* StreamingLevel : StreamingLevels)
	{
		if (!StreamingLevel) continue;

		TSharedPtr<FJsonObject> LevelObj = MakeShared<FJsonObject>();
		LevelObj->SetStringField(TEXT("packageName"), StreamingLevel->GetWorldAssetPackageFName().ToString());
		LevelObj->SetStringField(TEXT("class"), StreamingLevel->GetClass()->GetName());
		LevelObj->SetBoolField(TEXT("isLoaded"), StreamingLevel->IsLevelLoaded());
		LevelObj->SetBoolField(TEXT("isVisible"), StreamingLevel->IsLevelVisible());
		LevelObj->SetBoolField(TEXT("shouldBeLoaded"), StreamingLevel->HasLoadRequestPending() || StreamingLevel->IsLevelLoaded());

		// Get streaming level transform
		FTransform LevelTransform = StreamingLevel->LevelTransform;
		TSharedPtr<FJsonObject> TransformObj = MakeShared<FJsonObject>();
		FVector Location = LevelTransform.GetLocation();
		TransformObj->SetNumberField(TEXT("x"), Location.X);
		TransformObj->SetNumberField(TEXT("y"), Location.Y);
		TransformObj->SetNumberField(TEXT("z"), Location.Z);
		LevelObj->SetObjectField(TEXT("location"), TransformObj);

		// Actor count if loaded
		if (StreamingLevel->IsLevelLoaded() && StreamingLevel->GetLoadedLevel())
		{
			LevelObj->SetNumberField(TEXT("actorCount"), StreamingLevel->GetLoadedLevel()->Actors.Num());
		}

		SublevelsArray.Add(MakeShared<FJsonValueObject>(LevelObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("persistentLevel"), World->GetName());
	Result->SetArrayField(TEXT("sublevels"), SublevelsArray);
	Result->SetNumberField(TEXT("count"), SublevelsArray.Num());

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
			FStructProperty* SP = CastField<FStructProperty>(SegmentProp);
			if (!SP)
			{
				return MCPError(FString::Printf(TEXT("'%s' is not a struct - cannot descend"), *PathParts[i]));
			}
			CurrentContainer = SP->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = SP->Struct;
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

TSharedPtr<FJsonValue> FLevelHandlers::SetVolumeProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorLabel || It->GetName() == ActorLabel)
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return MCPError(FString::Printf(TEXT("Volume not found: %s"), *ActorLabel));
	}

	TArray<TSharedPtr<FJsonValue>> Changes;
	TArray<TSharedPtr<FJsonValue>> Skipped;
	TSharedPtr<FJsonObject> PreviousValues = MakeShared<FJsonObject>();

	// #238: callers pass either flat (BrushExtent:{...} at top-level) or
	// wrapped ({properties:{BrushExtent:{...}}}). The TS schema documents
	// the wrapped form; the original handler only walked top-level keys
	// and silently dropped wrapped writes. Walk both.
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> Pairs;
	for (auto& Pair : Params->Values)
	{
		if (Pair.Key == TEXT("actorLabel") || Pair.Key == TEXT("action") || Pair.Key == TEXT("properties"))
			continue;
		Pairs.Emplace(Pair.Key, Pair.Value);
	}
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && (*PropsObj).IsValid())
	{
		for (auto& Pair : (*PropsObj)->Values)
		{
			Pairs.Emplace(Pair.Key, Pair.Value);
		}
	}
	for (auto& Pair : Pairs)
	{

		// #238: BrushExtent isn't a real UPROPERTY on AVolume - it's a synthetic
		// property the bridge owns. Rebuild the cube via UCubeBuilder and run
		// FBSPOps so the new geometry is actually applied (the prior path
		// silently no-op'd on FindPropertyByName).
		if (Pair.Key.Equals(TEXT("BrushExtent"), ESearchCase::IgnoreCase) || Pair.Key.Equals(TEXT("brushExtent"), ESearchCase::IgnoreCase))
		{
			AVolume* Volume = Cast<AVolume>(TargetActor);
			if (!Volume)
			{
				Skipped.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: actor is not an AVolume"), *Pair.Key)));
				continue;
			}
			const TSharedPtr<FJsonObject>* ExtObj = nullptr;
			FVector NewExtent = FVector::ZeroVector;
			bool bGotExtent = false;
			if (Pair.Value->TryGetObject(ExtObj) && ExtObj && (*ExtObj).IsValid())
			{
				double V = 0;
				if ((*ExtObj)->TryGetNumberField(TEXT("X"), V) || (*ExtObj)->TryGetNumberField(TEXT("x"), V)) { NewExtent.X = V; bGotExtent = true; }
				if ((*ExtObj)->TryGetNumberField(TEXT("Y"), V) || (*ExtObj)->TryGetNumberField(TEXT("y"), V)) { NewExtent.Y = V; bGotExtent = true; }
				if ((*ExtObj)->TryGetNumberField(TEXT("Z"), V) || (*ExtObj)->TryGetNumberField(TEXT("z"), V)) { NewExtent.Z = V; bGotExtent = true; }
			}
			if (!bGotExtent || NewExtent.X <= 0 || NewExtent.Y <= 0 || NewExtent.Z <= 0)
			{
				Skipped.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: expected {X,Y,Z} object with positive values"), *Pair.Key)));
				continue;
			}
			UEMCP::BuildVolumeAsCube(World, Volume, NewExtent);
			Changes.Add(MakeShared<FJsonValueString>(Pair.Key));
			continue;
		}

		FProperty* Prop = TargetActor->GetClass()->FindPropertyByName(*Pair.Key);
		if (!Prop)
		{
			Skipped.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: not a property on %s"), *Pair.Key, *TargetActor->GetClass()->GetName())));
			continue;
		}

		FString PrevStr;
		Prop->ExportText_Direct(PrevStr, Prop->ContainerPtrToValuePtr<void>(TargetActor),
			Prop->ContainerPtrToValuePtr<void>(TargetActor), TargetActor, PPF_None);

		FString ValueStr;
		bool bApplied = false;
		if (Pair.Value->TryGetString(ValueStr))
		{
			Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(TargetActor), TargetActor, PPF_None);
			bApplied = true;
		}
		else
		{
			double NumVal;
			if (Pair.Value->TryGetNumber(NumVal))
			{
				ValueStr = FString::SanitizeFloat(NumVal);
				Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(TargetActor), TargetActor, PPF_None);
				bApplied = true;
			}
		}

		if (bApplied)
		{
			Changes.Add(MakeShared<FJsonValueString>(Pair.Key));
			PreviousValues->SetStringField(Pair.Key, PrevStr);
		}
		else
		{
			Skipped.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: value type not coercible"), *Pair.Key)));
		}
	}

	auto Result = MCPSuccess();
	if (Changes.Num() > 0) MCPSetUpdated(Result); else MCPSetExisted(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetArrayField(TEXT("changes"), Changes);
	if (Skipped.Num() > 0)
	{
		Result->SetArrayField(TEXT("skipped"), Skipped);
	}

	if (Changes.Num() > 0 && PreviousValues->Values.Num() > 0)
	{
		// Self-inverse for property-only changes; BrushExtent rebuild has no
		// reversible recipe (no prior extent recorded), so omit from rollback.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		for (auto& Prev : PreviousValues->Values)
		{
			Payload->SetField(Prev.Key, Prev.Value);
		}
		MCPSetRollback(Result, TEXT("set_volume_properties"), Payload);
	}

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

// #94: ExponentialHeightFog tuning
TSharedPtr<FJsonValue> FLevelHandlers::SetFogProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World) return MCPError(TEXT("World not available"));

	FString ActorLabel = OptionalString(Params, TEXT("actorLabel"));

	AExponentialHeightFog* Fog = nullptr;
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (ActorLabel.IsEmpty() || It->GetActorLabel() == ActorLabel)
		{
			Fog = *It;
			break;
		}
	}
	if (!Fog) return MCPError(TEXT("No ExponentialHeightFog actor found"));

	UExponentialHeightFogComponent* FC = Fog->GetComponent();
	if (!FC) return MCPError(TEXT("Fog component missing"));

	double Density = 0.0;
	if (Params->TryGetNumberField(TEXT("fogDensity"), Density))
	{
		FC->FogDensity = (float)Density;
	}
	double HeightFalloff = 0.0;
	if (Params->TryGetNumberField(TEXT("fogHeightFalloff"), HeightFalloff))
	{
		FC->FogHeightFalloff = (float)HeightFalloff;
	}
	double StartDistance = 0.0;
	if (Params->TryGetNumberField(TEXT("startDistance"), StartDistance))
	{
		FC->StartDistance = (float)StartDistance;
	}
	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (Params->TryGetObjectField(TEXT("fogInscatteringColor"), ColorObj) ||
	    Params->TryGetObjectField(TEXT("color"), ColorObj))
	{
		double R = 255, G = 255, B = 255;
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		FC->FogInscatteringLuminance = FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f);
	}

	FC->MarkRenderStateDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), Fog->GetActorLabel());
	Result->SetNumberField(TEXT("fogDensity"), FC->FogDensity);
	Result->SetNumberField(TEXT("fogHeightFalloff"), FC->FogHeightFalloff);
	return MCPResult(Result);
}

// #94: Bulk actor lookup helper
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
			FStructProperty* SP = CastField<FStructProperty>(Seg);
			if (!SP) return MCPError(FString::Printf(TEXT("'%s' is not a struct - cannot descend"), *PathParts[i]));
			CurrentContainer = SP->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = SP->Struct;
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
	static AActor* FindActorByLabel(UWorld* World, const FString& Label)
	{
		if (!World) return nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label) return *It;
		}
		return nullptr;
	}
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

	const TSharedPtr<FJsonObject>* MinObj = nullptr;
	const TSharedPtr<FJsonObject>* MaxObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("min"), MinObj) || !Params->TryGetObjectField(TEXT("max"), MaxObj))
	{
		return MCPError(TEXT("Missing 'min' / 'max' Vec3 fields for grid bounds"));
	}
	auto ReadVec = [](const TSharedPtr<FJsonObject>* O) -> FVector
	{
		FVector V = FVector::ZeroVector;
		double X = 0; if ((*O)->TryGetNumberField(TEXT("x"), X)) V.X = X;
		double Y = 0; if ((*O)->TryGetNumberField(TEXT("y"), Y)) V.Y = Y;
		double Z = 0; if ((*O)->TryGetNumberField(TEXT("z"), Z)) V.Z = Z;
		return V;
	};
	const FVector Min = ReadVec(MinObj);
	const FVector Max = ReadVec(MaxObj);

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

	const TSharedPtr<FJsonObject>* OffObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("offset"), OffObj))
	{
		return MCPError(TEXT("Missing 'offset' Vec3"));
	}
	double X = 0, Y = 0, Z = 0;
	(*OffObj)->TryGetNumberField(TEXT("x"), X);
	(*OffObj)->TryGetNumberField(TEXT("y"), Y);
	(*OffObj)->TryGetNumberField(TEXT("z"), Z);
	const FVector Offset(X, Y, Z);

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
