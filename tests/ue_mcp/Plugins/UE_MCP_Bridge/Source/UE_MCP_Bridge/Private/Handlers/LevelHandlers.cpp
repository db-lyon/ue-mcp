#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "EditorScriptingUtilities/Public/EditorLevelLibrary.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
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
#include "Engine/BrushBuilder.h"
#include "GameFramework/Volume.h"
#include "Engine/BlockingVolume.h"
#include "Engine/TriggerVolume.h"
#include "Selection.h"
#include "Engine/LevelStreaming.h"
#include "LevelEditorSubsystem.h"
#include "EditorLevelUtils.h"
#include "FileHelpers.h"

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
}

TSharedPtr<FJsonValue> FLevelHandlers::GetOutliner(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ClassFilter;
	Params->TryGetStringField(TEXT("classFilter"), ClassFilter);
	FString NameFilter;
	Params->TryGetStringField(TEXT("nameFilter"), NameFilter);
	int32 Limit = 500;
	Params->TryGetNumberField(TEXT("limit"), Limit);

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

		ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetNumberField(TEXT("totalActors"), TotalCount);
	Result->SetNumberField(TEXT("returnedActors"), ActorsArray.Num());
	Result->SetArrayField(TEXT("actors"), ActorsArray);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::PlaceActor(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorClass;
	if (!Params->TryGetStringField(TEXT("actorClass"), ActorClass))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorClass' parameter"));
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

	// Find actor class
	UClass* Class = FindObject<UClass>(nullptr, *ActorClass);
	if (!Class)
	{
		Class = FindObject<UClass>(nullptr, *(TEXT("A") + ActorClass));
	}

	if (!Class)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor class not found: %s"), *ActorClass));
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

	// Spawn actor
	FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	AActor* NewActor = World->SpawnActor<AActor>(Class, SpawnTransform);
	if (!NewActor)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn actor"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("actorLabel"), NewActor->GetActorLabel());
	Result->SetStringField(TEXT("actorClass"), ActorClass);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::DeleteActor(const TSharedPtr<FJsonObject>& Params)
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
	AActor* ActorToDelete = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			ActorToDelete = *ActorIt;
			break;
		}
	}

	if (!ActorToDelete)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	World->DestroyActor(ActorToDelete);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetActorDetails(const TSharedPtr<FJsonObject>& Params)
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

	Result->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	Result->SetStringField(TEXT("path"), Actor->GetPathName());

	FVector Location = Actor->GetActorLocation();
	TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);
	Result->SetObjectField(TEXT("location"), LocationObj);

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetCurrentLevel(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	ULevel* CurrentLevel = World->GetCurrentLevel();
	if (!CurrentLevel)
	{
		Result->SetStringField(TEXT("error"), TEXT("No current level"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("levelName"), World->GetName());
	Result->SetStringField(TEXT("levelPath"), World->GetPathName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::ListLevels(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetArrayField(TEXT("levels"), LevelsArray);
	Result->SetNumberField(TEXT("count"), LevelsArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetSelectedActors(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection)
	{
		Result->SetStringField(TEXT("error"), TEXT("Unable to get selection"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetArrayField(TEXT("actors"), ActorsArray);
	Result->SetNumberField(TEXT("count"), ActorsArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::ListVolumes(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString VolumeType;
	Params->TryGetStringField(TEXT("volumeType"), VolumeType);

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

	Result->SetArrayField(TEXT("volumes"), VolumesArray);
	Result->SetNumberField(TEXT("count"), VolumesArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::MoveActor(const TSharedPtr<FJsonObject>& Params)
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

	// Get location
	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocObj))
	{
		FVector Location = Actor->GetActorLocation();
		(*LocObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Location.Z);
		Actor->SetActorLocation(Location);
	}

	// Get rotation
	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotObj))
	{
		FRotator Rotation = Actor->GetActorRotation();
		(*RotObj)->TryGetNumberField(TEXT("pitch"), Rotation.Pitch);
		(*RotObj)->TryGetNumberField(TEXT("yaw"), Rotation.Yaw);
		(*RotObj)->TryGetNumberField(TEXT("roll"), Rotation.Roll);
		Actor->SetActorRotation(Rotation);
	}

	// Return new transform
	FVector NewLocation = Actor->GetActorLocation();
	TSharedPtr<FJsonObject> NewLocationObj = MakeShared<FJsonObject>();
	NewLocationObj->SetNumberField(TEXT("x"), NewLocation.X);
	NewLocationObj->SetNumberField(TEXT("y"), NewLocation.Y);
	NewLocationObj->SetNumberField(TEXT("z"), NewLocation.Z);
	Result->SetObjectField(TEXT("location"), NewLocationObj);

	FRotator NewRotation = Actor->GetActorRotation();
	TSharedPtr<FJsonObject> NewRotationObj = MakeShared<FJsonObject>();
	NewRotationObj->SetNumberField(TEXT("pitch"), NewRotation.Pitch);
	NewRotationObj->SetNumberField(TEXT("yaw"), NewRotation.Yaw);
	NewRotationObj->SetNumberField(TEXT("roll"), NewRotation.Roll);
	Result->SetObjectField(TEXT("rotation"), NewRotationObj);

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SelectActors(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	const TArray<TSharedPtr<FJsonValue>>* ActorLabelsArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("actorLabels"), ActorLabelsArray))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabels' parameter"));
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

	Result->SetArrayField(TEXT("selected"), SelectedArray);
	Result->SetArrayField(TEXT("notFound"), NotFoundArray);
	Result->SetNumberField(TEXT("selectedCount"), SelectedArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SpawnLight(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString LightType;
	if (!Params->TryGetStringField(TEXT("lightType"), LightType))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'lightType' parameter"));
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

	// Get location
	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj))
	{
		(*LocationObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocationObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocationObj)->TryGetNumberField(TEXT("z"), Location.Z);
	}

	double Intensity = 5000.0;
	Params->TryGetNumberField(TEXT("intensity"), Intensity);

	// Determine light class
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown light type: %s. Use point, spot, directional, or rect."), *LightType));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FTransform LightTransform(FRotator::ZeroRotator, Location);
	AActor* NewLight = World->SpawnActor<AActor>(LightClass, LightTransform);
	if (!NewLight)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn light actor"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set label if provided
	FString Label;
	if (Params->TryGetStringField(TEXT("label"), Label))
	{
		NewLight->SetActorLabel(Label);
	}

	// Set intensity on light component
	ULightComponent* LightComponent = NewLight->FindComponentByClass<ULightComponent>();
	if (LightComponent)
	{
		LightComponent->SetIntensity(Intensity);
	}

	Result->SetStringField(TEXT("actorLabel"), NewLight->GetActorLabel());
	Result->SetStringField(TEXT("actorName"), NewLight->GetName());
	Result->SetStringField(TEXT("lightType"), LightType);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SetLightProperties(const TSharedPtr<FJsonObject>& Params)
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

	ULightComponent* LightComponent = Actor->FindComponentByClass<ULightComponent>();
	if (!LightComponent)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' does not have a light component"), *ActorLabel));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set intensity if provided
	double Intensity = 0.0;
	if (Params->TryGetNumberField(TEXT("intensity"), Intensity))
	{
		LightComponent->SetIntensity(Intensity);
	}

	// Set color if provided
	const TSharedPtr<FJsonObject>* ColorObj = nullptr;
	if (Params->TryGetObjectField(TEXT("color"), ColorObj))
	{
		double R = 255.0, G = 255.0, B = 255.0;
		(*ColorObj)->TryGetNumberField(TEXT("r"), R);
		(*ColorObj)->TryGetNumberField(TEXT("g"), G);
		(*ColorObj)->TryGetNumberField(TEXT("b"), B);
		LightComponent->SetLightColor(FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f));
	}

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetNumberField(TEXT("intensity"), LightComponent->Intensity);

	FLinearColor CurrentColor = LightComponent->GetLightColor();
	TSharedPtr<FJsonObject> ColorResult = MakeShared<FJsonObject>();
	ColorResult->SetNumberField(TEXT("r"), CurrentColor.R * 255.0f);
	ColorResult->SetNumberField(TEXT("g"), CurrentColor.G * 255.0f);
	ColorResult->SetNumberField(TEXT("b"), CurrentColor.B * 255.0f);
	Result->SetObjectField(TEXT("color"), ColorResult);

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SpawnVolume(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString VolumeType;
	if (!Params->TryGetStringField(TEXT("volumeType"), VolumeType))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'volumeType' parameter"));
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
	else
	{
		// Try to find class by name
		VolumeClass = FindObject<UClass>(nullptr, *VolumeType);
		if (!VolumeClass)
		{
			VolumeClass = FindObject<UClass>(nullptr, *(TEXT("A") + VolumeType));
		}
	}

	if (!VolumeClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Volume class not found: %s"), *VolumeType));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FTransform VolumeTransform(FRotator::ZeroRotator, Location);
	AActor* NewVolume = World->SpawnActor<AActor>(VolumeClass, VolumeTransform);
	if (!NewVolume)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to spawn volume actor"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Set label if provided
	FString Label;
	if (Params->TryGetStringField(TEXT("label"), Label))
	{
		NewVolume->SetActorLabel(Label);
	}

	// Set scale based on extent
	NewVolume->SetActorScale3D(Extent / 100.0);

	Result->SetStringField(TEXT("actorLabel"), NewVolume->GetActorLabel());
	Result->SetStringField(TEXT("actorName"), NewVolume->GetName());
	Result->SetStringField(TEXT("volumeType"), VolumeType);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::AddComponentToActor(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actorLabel"), ActorLabel))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'actorLabel' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ComponentClass;
	if (!Params->TryGetStringField(TEXT("componentClass"), ComponentClass))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'componentClass' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString ComponentName;
	if (!Params->TryGetStringField(TEXT("componentName"), ComponentName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'componentName' parameter"));
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

	// Find component class
	UClass* CompClass = FindObject<UClass>(nullptr, *ComponentClass);
	if (!CompClass)
	{
		CompClass = FindObject<UClass>(nullptr, *(TEXT("U") + ComponentClass));
	}

	if (!CompClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Component class not found: %s"), *ComponentClass));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (!CompClass->IsChildOf(UActorComponent::StaticClass()))
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Class '%s' is not an ActorComponent"), *ComponentClass));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FName CompName = FName(*ComponentName);
	UActorComponent* NewComponent = NewObject<UActorComponent>(Actor, CompClass, CompName);
	if (!NewComponent)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create component"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// If it's a scene component, attach it to root
	USceneComponent* SceneComp = Cast<USceneComponent>(NewComponent);
	if (SceneComp && Actor->GetRootComponent())
	{
		SceneComp->SetupAttachment(Actor->GetRootComponent());
	}

	NewComponent->RegisterComponent();
	Actor->AddInstanceComponent(NewComponent);

	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("componentClass"), NewComponent->GetClass()->GetName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::LoadLevel(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString LevelPath;
	if (!Params->TryGetStringField(TEXT("levelPath"), LevelPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'levelPath' parameter (e.g. /Game/Maps/MyMap)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Use the LevelEditorSubsystem to load the level
	ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LevelEditorSubsystem)
	{
		Result->SetStringField(TEXT("error"), TEXT("LevelEditorSubsystem not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bSuccess = LevelEditorSubsystem->LoadLevel(LevelPath);
	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load level: %s"), *LevelPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Get info about the newly loaded world
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		Result->SetStringField(TEXT("worldName"), World->GetName());
		Result->SetStringField(TEXT("worldPath"), World->GetPathName());
	}

	Result->SetStringField(TEXT("levelPath"), LevelPath);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SaveLevel(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Use the LevelEditorSubsystem to save the current level
	ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LevelEditorSubsystem)
	{
		Result->SetStringField(TEXT("error"), TEXT("LevelEditorSubsystem not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bSuccess = LevelEditorSubsystem->SaveCurrentLevel();

	Result->SetStringField(TEXT("levelName"), World->GetName());
	Result->SetStringField(TEXT("levelPath"), World->GetPathName());
	Result->SetBoolField(TEXT("success"), bSuccess);

	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to save current level"));
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::ListSublevels(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetStringField(TEXT("persistentLevel"), World->GetName());
	Result->SetArrayField(TEXT("sublevels"), SublevelsArray);
	Result->SetNumberField(TEXT("count"), SublevelsArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}
