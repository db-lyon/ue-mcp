#include "SequencerHandlers.h"
#include "HandlerRegistry.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
// LevelSequenceFactoryNew may not be available; use AssetTools directly
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

void FSequencerHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("create_level_sequence"), &CreateLevelSequence);
	Registry.RegisterHandler(TEXT("read_sequence_info"), &ReadSequenceInfo);
	Registry.RegisterHandler(TEXT("add_track"), &AddTrack);
	Registry.RegisterHandler(TEXT("sequence_control"), &SequenceControl);
}

TSharedPtr<FJsonValue> FSequencerHandlers::CreateLevelSequence(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/Cinematics");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	// Create the package
	FString FullPackagePath = PackagePath / Name;
	UPackage* Package = CreatePackage(*FullPackagePath);
	if (!Package)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create package at '%s'"), *FullPackagePath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Create the level sequence asset
	ULevelSequence* NewSequence = NewObject<ULevelSequence>(Package, FName(*Name), RF_Public | RF_Standalone);
	NewSequence->Initialize();

	if (!NewSequence)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create LevelSequence asset"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Notify the asset registry
	FAssetRegistryModule::AssetCreated(NewSequence);

	// Mark package dirty so it can be saved
	Package->MarkPackageDirty();

	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("path"), NewSequence->GetPathName());
	Result->SetStringField(TEXT("packagePath"), FullPackagePath);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FSequencerHandlers::ReadSequenceInfo(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	ULevelSequence* Sequence = Cast<ULevelSequence>(LoadedAsset);
	if (!Sequence)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load LevelSequence at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		Result->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Result->SetStringField(TEXT("name"), Sequence->GetName());
	Result->SetStringField(TEXT("path"), Sequence->GetPathName());

	// Display rate
	FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	TSharedPtr<FJsonObject> DisplayRateObj = MakeShared<FJsonObject>();
	DisplayRateObj->SetNumberField(TEXT("numerator"), DisplayRate.Numerator);
	DisplayRateObj->SetNumberField(TEXT("denominator"), DisplayRate.Denominator);
	Result->SetObjectField(TEXT("displayRate"), DisplayRateObj);

	// Playback range
	TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	TSharedPtr<FJsonObject> RangeObj = MakeShared<FJsonObject>();
	if (PlaybackRange.HasLowerBound())
	{
		RangeObj->SetNumberField(TEXT("startFrame"), PlaybackRange.GetLowerBoundValue().Value);
	}
	if (PlaybackRange.HasUpperBound())
	{
		RangeObj->SetNumberField(TEXT("endFrame"), PlaybackRange.GetUpperBoundValue().Value);
	}
	Result->SetObjectField(TEXT("playbackRange"), RangeObj);

	// Bindings (possessables and spawnables)
	TArray<TSharedPtr<FJsonValue>> BindingsArray;
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
		TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
		BindingObj->SetStringField(TEXT("name"), Possessable.GetName());
		BindingObj->SetStringField(TEXT("guid"), Possessable.GetGuid().ToString());
		BindingObj->SetStringField(TEXT("type"), TEXT("possessable"));

		// List tracks for this binding
		TArray<TSharedPtr<FJsonValue>> TrackNames;
		const FMovieSceneBinding* Binding = MovieScene->FindBinding(Possessable.GetGuid());
		if (Binding)
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				if (Track)
				{
					TrackNames.Add(MakeShared<FJsonValueString>(Track->GetClass()->GetName()));
				}
			}
		}
		BindingObj->SetArrayField(TEXT("tracks"), TrackNames);

		BindingsArray.Add(MakeShared<FJsonValueObject>(BindingObj));
	}

	for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
	{
		const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(i);
		TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
		BindingObj->SetStringField(TEXT("name"), Spawnable.GetName());
		BindingObj->SetStringField(TEXT("guid"), Spawnable.GetGuid().ToString());
		BindingObj->SetStringField(TEXT("type"), TEXT("spawnable"));

		TArray<TSharedPtr<FJsonValue>> TrackNames;
		const FMovieSceneBinding* Binding = MovieScene->FindBinding(Spawnable.GetGuid());
		if (Binding)
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				if (Track)
				{
					TrackNames.Add(MakeShared<FJsonValueString>(Track->GetClass()->GetName()));
				}
			}
		}
		BindingObj->SetArrayField(TEXT("tracks"), TrackNames);

		BindingsArray.Add(MakeShared<FJsonValueObject>(BindingObj));
	}
	Result->SetArrayField(TEXT("bindings"), BindingsArray);
	Result->SetNumberField(TEXT("bindingCount"), BindingsArray.Num());

	// Master tracks
	TArray<TSharedPtr<FJsonValue>> MasterTracksArray;
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		if (!Track) continue;
		TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
		TrackObj->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
		TrackObj->SetStringField(TEXT("class"), Track->GetClass()->GetName());
		TrackObj->SetNumberField(TEXT("sectionCount"), Track->GetAllSections().Num());
		MasterTracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
	}
	Result->SetArrayField(TEXT("masterTracks"), MasterTracksArray);
	Result->SetNumberField(TEXT("masterTrackCount"), MasterTracksArray.Num());

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FSequencerHandlers::AddTrack(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString TrackType;
	if (!Params->TryGetStringField(TEXT("trackType"), TrackType))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'trackType' parameter (e.g. Transform, Float, SkeletalAnimation, CameraCut, Audio, Event, Fade)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	ULevelSequence* Sequence = Cast<ULevelSequence>(LoadedAsset);
	if (!Sequence)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load LevelSequence at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		Result->SetStringField(TEXT("error"), TEXT("LevelSequence has no MovieScene"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Determine track class from type name
	UClass* TrackClass = nullptr;
	if (TrackType.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
	{
		TrackClass = UMovieScene3DTransformTrack::StaticClass();
	}
	else if (TrackType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
	{
		TrackClass = UMovieSceneFloatTrack::StaticClass();
	}
	else if (TrackType.Equals(TEXT("SkeletalAnimation"), ESearchCase::IgnoreCase))
	{
		TrackClass = UMovieSceneSkeletalAnimationTrack::StaticClass();
	}
	else if (TrackType.Equals(TEXT("CameraCut"), ESearchCase::IgnoreCase))
	{
		TrackClass = UMovieSceneCameraCutTrack::StaticClass();
	}
	else if (TrackType.Equals(TEXT("Audio"), ESearchCase::IgnoreCase))
	{
		TrackClass = UMovieSceneAudioTrack::StaticClass();
	}
	else if (TrackType.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
	{
		TrackClass = UMovieSceneEventTrack::StaticClass();
	}
	else if (TrackType.Equals(TEXT("Fade"), ESearchCase::IgnoreCase))
	{
		TrackClass = UMovieSceneFadeTrack::StaticClass();
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown track type: '%s'. Use Transform, Float, SkeletalAnimation, CameraCut, Audio, Event, or Fade."), *TrackType));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Check if we should add to an actor binding or as a master track
	FString ActorLabel;
	if (Params->TryGetStringField(TEXT("actorLabel"), ActorLabel) && !ActorLabel.IsEmpty())
	{
		// Find the binding for this actor
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetStringField(TEXT("error"), TEXT("Editor world not available"));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		// Find actor by label
		AActor* TargetActor = nullptr;
		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			if ((*ActorIt)->GetActorLabel() == ActorLabel)
			{
				TargetActor = *ActorIt;
				break;
			}
		}

		if (!TargetActor)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		// Find or create a binding for this actor
		FGuid BindingGuid;
		bool bFoundBinding = false;

		// Search existing possessables
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
			if (Possessable.GetName() == ActorLabel || Possessable.GetName() == TargetActor->GetName())
			{
				BindingGuid = Possessable.GetGuid();
				bFoundBinding = true;
				break;
			}
		}

		if (!bFoundBinding)
		{
			// Create a new possessable binding for the actor
			BindingGuid = MovieScene->AddPossessable(ActorLabel, TargetActor->GetClass());
			Sequence->BindPossessableObject(BindingGuid, *TargetActor, World);
		}

		// Add track to binding
		UMovieSceneTrack* NewTrack = MovieScene->AddTrack(TrackClass, BindingGuid);
		if (!NewTrack)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to add %s track to actor '%s'"), *TrackType, *ActorLabel));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		Result->SetStringField(TEXT("actorLabel"), ActorLabel);
		Result->SetStringField(TEXT("bindingGuid"), BindingGuid.ToString());
		Result->SetStringField(TEXT("trackType"), TrackType);
		Result->SetStringField(TEXT("trackClass"), NewTrack->GetClass()->GetName());
		Result->SetStringField(TEXT("scope"), TEXT("binding"));
		Result->SetBoolField(TEXT("success"), true);
	}
	else
	{
		// Add as master track
		UMovieSceneTrack* NewTrack = MovieScene->AddTrack(TrackClass);
		if (!NewTrack)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to add master %s track"), *TrackType));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		Result->SetStringField(TEXT("trackType"), TrackType);
		Result->SetStringField(TEXT("trackClass"), NewTrack->GetClass()->GetName());
		Result->SetStringField(TEXT("scope"), TEXT("master"));
		Result->SetBoolField(TEXT("success"), true);
	}

	// Mark the sequence package dirty
	Sequence->GetOutermost()->MarkPackageDirty();

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FSequencerHandlers::SequenceControl(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Action;
	if (!Params->TryGetStringField(TEXT("action"), Action))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'action' parameter (play, pause, stop)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString Command;
	if (Action.Equals(TEXT("play"), ESearchCase::IgnoreCase))
	{
		Command = TEXT("Sequencer.Play");
	}
	else if (Action.Equals(TEXT("pause"), ESearchCase::IgnoreCase))
	{
		Command = TEXT("Sequencer.Pause");
	}
	else if (Action.Equals(TEXT("stop"), ESearchCase::IgnoreCase))
	{
		Command = TEXT("Sequencer.Stop");
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: '%s'. Use play, pause, or stop."), *Action));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Execute via console command
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		GEditor->Exec(World, *Command, *GLog);
	}

	Result->SetStringField(TEXT("action"), Action);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}
