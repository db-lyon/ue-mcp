#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
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
#include "GameFramework/Volume.h"
#include "Engine/BlockingVolume.h"
#include "Engine/TriggerVolume.h"
#include "Engine/PostProcessVolume.h"
#include "Sound/AudioVolume.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "GameFramework/PainCausingVolume.h"
#include "Selection.h"
#include "Engine/LevelStreaming.h"
#include "LevelEditorSubsystem.h"
#include "EditorLevelUtils.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

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
}

TSharedPtr<FJsonValue> FLevelHandlers::GetOutliner(const TSharedPtr<FJsonObject>& Params)
{
	FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));

	FString ClassFilter = OptionalString(Params, TEXT("classFilter"));
	FString NameFilter = OptionalString(Params, TEXT("nameFilter"));
	int32 Limit = OptionalInt(Params, TEXT("limit"), 500);

	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	int32 TotalCount = 0;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!Actor) continue;
		TotalCount++;

		FString ActorClass = Actor->GetClass()->GetName();
		FString ActorName = Actor->GetName();
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

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetObjectField(TEXT("location"), NewLocationObj);
	Result->SetObjectField(TEXT("rotation"), NewRotationObj);
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
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetObjectField(TEXT("location"), PrevLoc);
	Payload->SetObjectField(TEXT("rotation"), PrevRot);
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
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown light type: %s. Use point, spot, directional, or rect."), *LightType));
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

	ULightComponent* LightComponent = NewLight->FindComponentByClass<ULightComponent>();
	if (LightComponent)
	{
		LightComponent->SetIntensity(Intensity);
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

	// #94: SkyLight recapture after intensity/color change
	if (USkyLightComponent* Sky = Cast<USkyLightComponent>(LightComponent))
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

	// Set scale based on extent
	NewVolume->SetActorScale3D(Extent / 100.0);

	const FString FinalLabel = NewVolume->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), FinalLabel);
	Result->SetStringField(TEXT("actorName"), NewVolume->GetName());
	Result->SetStringField(TEXT("volumeType"), VolumeType);

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

	UClass* CompClass = FindObject<UClass>(nullptr, *ComponentClass);
	if (!CompClass)
	{
		CompClass = FindObject<UClass>(nullptr, *(TEXT("U") + ComponentClass));
	}

	if (!CompClass)
	{
		return MCPError(FString::Printf(TEXT("Component class not found: %s"), *ComponentClass));
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

	FProperty* Prop = TargetComp->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		return MCPError(FString::Printf(TEXT("Property '%s' not found on component"), *PropertyName));
	}

	const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
	if (!ValueField || !(*ValueField).IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	// Capture previous value as a string for self-inverse rollback.
	FString PreviousValueStr;
	Prop->ExportText_Direct(PreviousValueStr, Prop->ContainerPtrToValuePtr<void>(TargetComp),
		Prop->ContainerPtrToValuePtr<void>(TargetComp), TargetComp, PPF_None);

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
		Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(TargetComp), TargetComp, PPF_None);
	}
	else
	{
		double NumValue;
		if ((*ValueField)->TryGetNumber(NumValue))
		{
			ValueStr = FString::SanitizeFloat(NumValue);
			Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(TargetComp), TargetComp, PPF_None);
		}
		else
		{
			bool BoolValue;
			if ((*ValueField)->TryGetBool(BoolValue))
			{
				ValueStr = BoolValue ? TEXT("true") : TEXT("false");
				Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(TargetComp), TargetComp, PPF_None);
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
	TSharedPtr<FJsonObject> PreviousValues = MakeShared<FJsonObject>();
	for (auto& Pair : Params->Values)
	{
		if (Pair.Key == TEXT("actorLabel") || Pair.Key == TEXT("action"))
			continue;

		FProperty* Prop = TargetActor->GetClass()->FindPropertyByName(*Pair.Key);
		if (Prop)
		{
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
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetArrayField(TEXT("changes"), Changes);

	if (Changes.Num() > 0)
	{
		// Self-inverse: call again with previous values as strings.
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
		if (CName == ClassName || A->GetClass()->IsChildOf(AActor::StaticClass()) && CName.Contains(ClassName))
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
