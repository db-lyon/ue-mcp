#include "SequencerHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
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
#include "Tracks/MovieScene3DAttachTrack.h"
#include "Sections/MovieScene3DAttachSection.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
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

	// Aliases
	Registry.RegisterHandler(TEXT("get_sequence_info"), &ReadSequenceInfo);
	Registry.RegisterHandler(TEXT("add_sequence_track"), &AddTrack);
	Registry.RegisterHandler(TEXT("play_sequence"), &SequenceControl);
}

TSharedPtr<FJsonValue> FSequencerHandlers::CreateLevelSequence(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Cinematics"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("LevelSequence")))
	{
		return Existing;
	}

	FString FullPackagePath = PackagePath / Name;
	UPackage* Package = CreatePackage(*FullPackagePath);
	if (!Package)
	{
		return MCPError(FString::Printf(TEXT("Failed to create package at '%s'"), *FullPackagePath));
	}

	ULevelSequence* NewSequence = NewObject<ULevelSequence>(Package, FName(*Name), RF_Public | RF_Standalone);
	NewSequence->Initialize();

	if (!NewSequence)
	{
		return MCPError(TEXT("Failed to create LevelSequence asset"));
	}

	FAssetRegistryModule::AssetCreated(NewSequence);
	Package->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("path"), NewSequence->GetPathName());
	Result->SetStringField(TEXT("packagePath"), FullPackagePath);
	MCPSetDeleteAssetRollback(Result, NewSequence->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSequencerHandlers::ReadSequenceInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	ULevelSequence* Sequence = Cast<ULevelSequence>(LoadedAsset);
	if (!Sequence)
	{
		return MCPError(FString::Printf(TEXT("Failed to load LevelSequence at '%s'"), *AssetPath));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return MCPError(TEXT("LevelSequence has no MovieScene"));
	}

	auto Result = MCPSuccess();
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

	// #52: optional section-level detail (attach sockets, first transform key values)
	const bool bIncludeDetails = OptionalBool(Params, TEXT("includeSectionDetails"));

	auto ExtractSectionDetails = [&](UMovieSceneTrack* Track, TSharedPtr<FJsonObject>& TrackObj)
	{
		if (!bIncludeDetails || !Track) return;
		TArray<TSharedPtr<FJsonValue>> SectionsArr;
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (!Section) continue;
			TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
			if (UMovieScene3DAttachSection* Attach = Cast<UMovieScene3DAttachSection>(Section))
			{
				SObj->SetStringField(TEXT("attachSocket"), Attach->AttachSocketName.ToString());
				SObj->SetStringField(TEXT("attachComponent"), Attach->AttachComponentName.ToString());
			}
			if (UMovieScene3DTransformSection* Xf = Cast<UMovieScene3DTransformSection>(Section))
			{
				FMovieSceneChannelProxy& Proxy = Xf->GetChannelProxy();
				TArray<FName> ChannelNames = {
					TEXT("Location.X"), TEXT("Location.Y"), TEXT("Location.Z"),
					TEXT("Rotation.X"), TEXT("Rotation.Y"), TEXT("Rotation.Z"),
					TEXT("Scale.X"), TEXT("Scale.Y"), TEXT("Scale.Z"),
				};
				TSharedPtr<FJsonObject> FirstKeys = MakeShared<FJsonObject>();
				for (FName ChName : ChannelNames)
				{
					if (FMovieSceneDoubleChannel* Ch = Proxy.GetChannel<FMovieSceneDoubleChannel>(ChName))
					{
						const TArray<double>& Values = Ch->GetData().GetValues();
						if (Values.Num() > 0)
						{
							FirstKeys->SetNumberField(ChName.ToString(), Values[0]);
						}
					}
					else if (FMovieSceneFloatChannel* FCh = Proxy.GetChannel<FMovieSceneFloatChannel>(ChName))
					{
						const TArray<FMovieSceneFloatValue>& FVs = FCh->GetData().GetValues();
						if (FVs.Num() > 0)
						{
							FirstKeys->SetNumberField(ChName.ToString(), FVs[0].Value);
						}
					}
				}
				SObj->SetObjectField(TEXT("firstKeyValues"), FirstKeys);
			}
			SObj->SetStringField(TEXT("class"), Section->GetClass()->GetName());
			SectionsArr.Add(MakeShared<FJsonValueObject>(SObj));
		}
		TrackObj->SetArrayField(TEXT("sections"), SectionsArr);
	};

	// Bindings (possessables and spawnables)
	TArray<TSharedPtr<FJsonValue>> BindingsArray;
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
		TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
		BindingObj->SetStringField(TEXT("name"), Possessable.GetName());
		BindingObj->SetStringField(TEXT("guid"), Possessable.GetGuid().ToString());
		BindingObj->SetStringField(TEXT("type"), TEXT("possessable"));

		// List tracks for this binding (with optional section detail)
		TArray<TSharedPtr<FJsonValue>> TrackArr;
		const FMovieSceneBinding* Binding = MovieScene->FindBinding(Possessable.GetGuid());
		if (Binding)
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				if (!Track) continue;
				TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
				TObj->SetStringField(TEXT("class"), Track->GetClass()->GetName());
				TObj->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
				ExtractSectionDetails(Track, TObj);
				TrackArr.Add(MakeShared<FJsonValueObject>(TObj));
			}
		}
		BindingObj->SetArrayField(TEXT("tracks"), TrackArr);

		BindingsArray.Add(MakeShared<FJsonValueObject>(BindingObj));
	}

	for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
	{
		const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(i);
		TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
		BindingObj->SetStringField(TEXT("name"), Spawnable.GetName());
		BindingObj->SetStringField(TEXT("guid"), Spawnable.GetGuid().ToString());
		BindingObj->SetStringField(TEXT("type"), TEXT("spawnable"));

		TArray<TSharedPtr<FJsonValue>> TrackArr;
		const FMovieSceneBinding* Binding = MovieScene->FindBinding(Spawnable.GetGuid());
		if (Binding)
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				if (!Track) continue;
				TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
				TObj->SetStringField(TEXT("class"), Track->GetClass()->GetName());
				TObj->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
				ExtractSectionDetails(Track, TObj);
				TrackArr.Add(MakeShared<FJsonValueObject>(TObj));
			}
		}
		BindingObj->SetArrayField(TEXT("tracks"), TrackArr);

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

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSequencerHandlers::AddTrack(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString TrackType;
	if (auto Err = RequireString(Params, TEXT("trackType"), TrackType)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	ULevelSequence* Sequence = Cast<ULevelSequence>(LoadedAsset);
	if (!Sequence)
	{
		return MCPError(FString::Printf(TEXT("Failed to load LevelSequence at '%s'"), *AssetPath));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return MCPError(TEXT("LevelSequence has no MovieScene"));
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
		return MCPError(FString::Printf(TEXT("Unknown track type: '%s'. Use Transform, Float, SkeletalAnimation, CameraCut, Audio, Event, or Fade."), *TrackType));
	}

	// Check if we should add to an actor binding or as a master track
	FString ActorLabel = OptionalString(Params, TEXT("actorLabel"));
	auto Result = MCPSuccess();

	if (!ActorLabel.IsEmpty())
	{
		// Find the binding for this actor
		REQUIRE_EDITOR_WORLD(World);

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
			return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
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

		// Idempotency: existing track of this class on binding?
		if (UMovieSceneTrack* ExistingTrack = MovieScene->FindTrack(TrackClass, BindingGuid))
		{
			MCPSetExisted(Result);
			Result->SetStringField(TEXT("actorLabel"), ActorLabel);
			Result->SetStringField(TEXT("bindingGuid"), BindingGuid.ToString());
			Result->SetStringField(TEXT("trackType"), TrackType);
			Result->SetStringField(TEXT("trackClass"), ExistingTrack->GetClass()->GetName());
			Result->SetStringField(TEXT("scope"), TEXT("binding"));
			return MCPResult(Result);
		}

		// Add track to binding
		UMovieSceneTrack* NewTrack = MovieScene->AddTrack(TrackClass, BindingGuid);
		if (!NewTrack)
		{
			return MCPError(FString::Printf(TEXT("Failed to add %s track to actor '%s'"), *TrackType, *ActorLabel));
		}

		MCPSetCreated(Result);
		Result->SetStringField(TEXT("actorLabel"), ActorLabel);
		Result->SetStringField(TEXT("bindingGuid"), BindingGuid.ToString());
		Result->SetStringField(TEXT("trackType"), TrackType);
		Result->SetStringField(TEXT("trackClass"), NewTrack->GetClass()->GetName());
		Result->SetStringField(TEXT("scope"), TEXT("binding"));
	}
	else
	{
		// Idempotency: any existing master track of this class?
		TArray<UMovieSceneTrack*> MasterTracks = MovieScene->GetTracks();
		for (UMovieSceneTrack* T : MasterTracks)
		{
			if (T && T->IsA(TrackClass))
			{
				MCPSetExisted(Result);
				Result->SetStringField(TEXT("trackType"), TrackType);
				Result->SetStringField(TEXT("trackClass"), T->GetClass()->GetName());
				Result->SetStringField(TEXT("scope"), TEXT("master"));
				return MCPResult(Result);
			}
		}

		// Add as master track
		UMovieSceneTrack* NewTrack = MovieScene->AddTrack(TrackClass);
		if (!NewTrack)
		{
			return MCPError(FString::Printf(TEXT("Failed to add master %s track"), *TrackType));
		}

		MCPSetCreated(Result);
		Result->SetStringField(TEXT("trackType"), TrackType);
		Result->SetStringField(TEXT("trackClass"), NewTrack->GetClass()->GetName());
		Result->SetStringField(TEXT("scope"), TEXT("master"));
	}

	// Mark the sequence package dirty
	Sequence->GetOutermost()->MarkPackageDirty();

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSequencerHandlers::SequenceControl(const TSharedPtr<FJsonObject>& Params)
{
	FString Action;
	if (auto Err = RequireString(Params, TEXT("action"), Action)) return Err;

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
		return MCPError(FString::Printf(TEXT("Unknown action: '%s'. Use play, pause, or stop."), *Action));
	}

	// Execute via console command
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		GEditor->Exec(World, *Command, *GLog);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("action"), Action);
	Result->SetStringField(TEXT("command"), Command);

	return MCPResult(Result);
}
