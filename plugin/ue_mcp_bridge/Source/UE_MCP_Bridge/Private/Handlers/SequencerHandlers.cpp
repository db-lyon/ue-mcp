#include "SequencerHandlers.h"
#include "SequencerHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieSceneSequencePlayer.h"

#include "Subsystems/AssetEditorSubsystem.h"
#include "HandlerAssetCreate.h"
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
#include "Sections/MovieSceneCameraCutSection.h"
#include "Channels/MovieSceneChannel.h"
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
	Registry.RegisterHandler(TEXT("get_sequence_info"), &ReadSequenceInfo);
	Registry.RegisterHandler(TEXT("add_sequence_track"), &AddTrack);
	Registry.RegisterHandler(TEXT("play_sequence"), &SequenceControl);
	Registry.RegisterHandler(TEXT("set_sequence_playback_range"), &SetPlaybackRange);
	Registry.RegisterHandler(TEXT("add_sequence_section"), &AddSection);
	Registry.RegisterHandler(TEXT("set_sequence_keyframes"), &SetKeyframes);
}

// ─── #548 sequencer authoring helpers ────────────────────────────────
namespace
{
	ULevelSequence* LoadSequence(const TSharedPtr<FJsonObject>& Params, FString& OutPath, FString& OutError)
	{
		if (!Params->TryGetStringField(TEXT("sequencePath"), OutPath))
		{
			if (!Params->TryGetStringField(TEXT("assetPath"), OutPath) && !Params->TryGetStringField(TEXT("path"), OutPath))
			{
				OutError = TEXT("Missing 'sequencePath' parameter");
				return nullptr;
			}
		}
		ULevelSequence* Seq = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(OutPath));
		if (!Seq) { OutError = FString::Printf(TEXT("LevelSequence not found: %s"), *OutPath); return nullptr; }
		return Seq;
	}

	// Resolve (creating if needed) the actor's possessable binding GUID.
	bool ResolveActorBinding(ULevelSequence* Sequence, UMovieScene* MovieScene, const FString& ActorLabel, FGuid& OutGuid, FString& OutError)
	{
		UWorld* World = GetEditorWorld();
		if (!World) { OutError = TEXT("No editor world available"); return false; }
		AActor* TargetActor = FindActorByLabel(World, ActorLabel);
		if (!TargetActor) { OutError = FString::Printf(TEXT("Actor not found: %s"), *ActorLabel); return false; }

		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& P = MovieScene->GetPossessable(i);
			if (P.GetName() == ActorLabel || P.GetName() == TargetActor->GetName()) { OutGuid = P.GetGuid(); return true; }
		}
		OutGuid = MovieScene->AddPossessable(ActorLabel, TargetActor->GetClass());
		Sequence->BindPossessableObject(OutGuid, *TargetActor, World);
		return true;
	}

	UClass* ResolveTrackClass(const FString& TrackType)
	{
		if (TrackType.Equals(TEXT("Transform"), ESearchCase::IgnoreCase)) return UMovieScene3DTransformTrack::StaticClass();
		if (TrackType.Equals(TEXT("Float"), ESearchCase::IgnoreCase)) return UMovieSceneFloatTrack::StaticClass();
		if (TrackType.Equals(TEXT("SkeletalAnimation"), ESearchCase::IgnoreCase)) return UMovieSceneSkeletalAnimationTrack::StaticClass();
		if (TrackType.Equals(TEXT("CameraCut"), ESearchCase::IgnoreCase)) return UMovieSceneCameraCutTrack::StaticClass();
		if (TrackType.Equals(TEXT("Audio"), ESearchCase::IgnoreCase)) return UMovieSceneAudioTrack::StaticClass();
		if (TrackType.Equals(TEXT("Event"), ESearchCase::IgnoreCase)) return UMovieSceneEventTrack::StaticClass();
		if (TrackType.Equals(TEXT("Fade"), ESearchCase::IgnoreCase)) return UMovieSceneFadeTrack::StaticClass();
		return nullptr;
	}

	// Map a friendly channel name to the canonical transform channel name.
	FString CanonicalTransformChannel(const FString& In)
	{
		const FString L = In.ToLower();
		if (L == TEXT("x") || L == TEXT("location.x")) return TEXT("Location.X");
		if (L == TEXT("y") || L == TEXT("location.y")) return TEXT("Location.Y");
		if (L == TEXT("z") || L == TEXT("location.z")) return TEXT("Location.Z");
		if (L == TEXT("roll") || L == TEXT("rotation.x")) return TEXT("Rotation.X");
		if (L == TEXT("pitch") || L == TEXT("rotation.y")) return TEXT("Rotation.Y");
		if (L == TEXT("yaw") || L == TEXT("rotation.z")) return TEXT("Rotation.Z");
		if (L == TEXT("scale.x")) return TEXT("Scale.X");
		if (L == TEXT("scale.y")) return TEXT("Scale.Y");
		if (L == TEXT("scale.z")) return TEXT("Scale.Z");
		return In;
	}
}

TSharedPtr<FJsonValue> FSequencerHandlers::CreateLevelSequence(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Cinematics"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Created = MCPCreateAssetIdempotentNewObject<ULevelSequence>(Name, PackagePath, OnConflict, TEXT("LevelSequence"));
	if (Created.EarlyReturn) return Created.EarlyReturn;
	ULevelSequence* NewSequence = Created.Asset;
	NewSequence->Initialize();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("path"), NewSequence->GetPathName());
	Result->SetStringField(TEXT("packagePath"), PackagePath + TEXT("/") + Name);
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

	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	if (!UEMCP::SequencerInfo::IsUsableFrameRate(DisplayRate) ||
		!UEMCP::SequencerInfo::IsUsableFrameRate(TickResolution))
	{
		return MCPError(FString::Printf(
			TEXT("LevelSequence has invalid timing rates (displayRate=%d/%d, tickResolution=%d/%d)"),
			DisplayRate.Numerator,
			DisplayRate.Denominator,
			TickResolution.Numerator,
			TickResolution.Denominator));
	}

	TSharedPtr<FJsonObject> DisplayRateObj = MakeShared<FJsonObject>();
	UEMCP::SequencerInfo::SetFrameRateFields(*DisplayRateObj, DisplayRate);
	Result->SetObjectField(TEXT("displayRate"), DisplayRateObj);

	TSharedPtr<FJsonObject> TickResolutionObj = MakeShared<FJsonObject>();
	UEMCP::SequencerInfo::SetFrameRateFields(*TickResolutionObj, TickResolution);
	Result->SetObjectField(TEXT("tickResolution"), TickResolutionObj);

	// Keep the legacy startFrame/endFrame values in tick-resolution units.
	// The explicit fields below remove that ambiguity without changing callers.
	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	TSharedPtr<FJsonObject> RangeObj = MakeShared<FJsonObject>();
	if (PlaybackRange.HasLowerBound())
	{
		RangeObj->SetNumberField(TEXT("startFrame"), PlaybackRange.GetLowerBoundValue().Value);
	}
	if (PlaybackRange.HasUpperBound())
	{
		RangeObj->SetNumberField(TEXT("endFrame"), PlaybackRange.GetUpperBoundValue().Value);
	}
	UEMCP::SequencerInfo::SetTimingRangeFields(*RangeObj, PlaybackRange, TickResolution, DisplayRate);
	Result->SetObjectField(TEXT("playbackRange"), RangeObj);

	const bool bIncludeDetails = OptionalBool(Params, TEXT("includeSectionDetails"));
	UEMCP::SequencerInfo::FDetailBudget DetailBudget;

	if (bIncludeDetails)
	{
		TSharedPtr<FJsonObject> LimitsObj = MakeShared<FJsonObject>();
		LimitsObj->SetNumberField(TEXT("maxSections"), UEMCP::SequencerInfo::MaxDetailedSections);
		LimitsObj->SetNumberField(TEXT("maxChannels"), UEMCP::SequencerInfo::MaxDetailedChannels);
		LimitsObj->SetNumberField(TEXT("maxKeyTimes"), UEMCP::SequencerInfo::MaxDetailedKeyTimes);
		LimitsObj->SetNumberField(TEXT("maxKeyTimesPerChannel"), UEMCP::SequencerInfo::MaxKeyTimesPerChannel);
		Result->SetObjectField(TEXT("detailLimits"), LimitsObj);
	}

	auto ExtractSectionDetails = [&](UMovieSceneTrack* Track, const TSharedPtr<FJsonObject>& TrackObj)
	{
		if (!bIncludeDetails || !Track) return;

		const TArray<UMovieSceneSection*>& AllSections = Track->GetAllSections();
		TArray<TSharedPtr<FJsonValue>> SectionsArr;
		for (int32 SectionIndex = 0; SectionIndex < AllSections.Num(); ++SectionIndex)
		{
			if (DetailBudget.Sections <= 0)
			{
				DetailBudget.bTruncated = true;
				break;
			}

			UMovieSceneSection* Section = AllSections[SectionIndex];
			if (!Section)
			{
				DetailBudget.bTruncated = true;
				continue;
			}
			--DetailBudget.Sections;

			TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
			SObj->SetNumberField(TEXT("index"), SectionIndex);
			SObj->SetStringField(TEXT("class"), Section->GetClass()->GetName());
			UEMCP::SequencerInfo::SetTimingRangeFields(
				*SObj,
				Section->GetTrueRange(),
				TickResolution,
				DisplayRate);

			if (UMovieScene3DAttachSection* Attach = Cast<UMovieScene3DAttachSection>(Section))
			{
				SObj->SetStringField(TEXT("attachSocket"), Attach->AttachSocketName.ToString());
				SObj->SetStringField(TEXT("attachComponent"), Attach->AttachComponentName.ToString());
			}

			const FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
			if (Cast<UMovieScene3DTransformSection>(Section))
			{
				TArray<FName> ChannelNames = {
					TEXT("Location.X"), TEXT("Location.Y"), TEXT("Location.Z"),
					TEXT("Rotation.X"), TEXT("Rotation.Y"), TEXT("Rotation.Z"),
					TEXT("Scale.X"), TEXT("Scale.Y"), TEXT("Scale.Z"),
				};
				TSharedPtr<FJsonObject> FirstKeys = MakeShared<FJsonObject>();
				for (FName ChName : ChannelNames)
				{
					// UE 5.7: GetChannel<T>(FName) overload was removed. Use
					// GetChannelByName<T>(FName) which returns a typed handle,
					// and GetData().GetValues() now yields a TArrayView.
					if (FMovieSceneDoubleChannel* Ch =
						Proxy.GetChannelByName<FMovieSceneDoubleChannel>(ChName).Get())
					{
						TArrayView<const FMovieSceneDoubleValue> Values = Ch->GetData().GetValues();
						if (Values.Num() > 0)
						{
							FirstKeys->SetNumberField(ChName.ToString(), Values[0].Value);
						}
					}
					else if (FMovieSceneFloatChannel* FCh =
						Proxy.GetChannelByName<FMovieSceneFloatChannel>(ChName).Get())
					{
						TArrayView<const FMovieSceneFloatValue> FVs = FCh->GetData().GetValues();
						if (FVs.Num() > 0)
						{
							FirstKeys->SetNumberField(ChName.ToString(), FVs[0].Value);
						}
					}
				}
				SObj->SetObjectField(TEXT("firstKeyValues"), FirstKeys);
			}

			const int32 ChannelCount = Proxy.NumChannels();
			TArray<TSharedPtr<FJsonValue>> ChannelsArr;
			bool bStopChannels = false;
			for (const FMovieSceneChannelEntry& Entry : Proxy.GetAllEntries())
			{
				const FString ChannelType = Entry.GetChannelTypeName().ToString();
				const TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();
				const TArrayView<const FMovieSceneChannelMetaData> MetaData = Entry.GetMetaData();

				for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
				{
					if (DetailBudget.Channels <= 0)
					{
						DetailBudget.bTruncated = true;
						bStopChannels = true;
						break;
					}

					FMovieSceneChannel* Channel = Channels[ChannelIndex];
					if (!Channel)
					{
						DetailBudget.bTruncated = true;
						continue;
					}
					--DetailBudget.Channels;

					FString ChannelName;
					if (MetaData.IsValidIndex(ChannelIndex) && !MetaData[ChannelIndex].Name.IsNone())
					{
						ChannelName = MetaData[ChannelIndex].Name.ToString();
					}
					else
					{
						ChannelName = FString::Printf(TEXT("%s[%d]"), *ChannelType, ChannelIndex);
					}

					TArray<FFrameNumber> KeyTimes;
					if (DetailBudget.KeyTimes > 0)
					{
						Channel->GetKeys(TRange<FFrameNumber>::All(), &KeyTimes, nullptr);
						KeyTimes.Sort([](const FFrameNumber A, const FFrameNumber B)
						{
							return A.Value < B.Value;
						});
					}

					const int32 KeyCount = FMath::Max(Channel->GetNumKeys(), KeyTimes.Num());
					const int32 KeyTimesToReturn = FMath::Min3(
						KeyTimes.Num(),
						UEMCP::SequencerInfo::MaxKeyTimesPerChannel,
						DetailBudget.KeyTimes);

					TArray<TSharedPtr<FJsonValue>> KeyTimesArr;
					KeyTimesArr.Reserve(KeyTimesToReturn);
					for (int32 KeyIndex = 0; KeyIndex < KeyTimesToReturn; ++KeyIndex)
					{
						KeyTimesArr.Add(MakeShared<FJsonValueObject>(
							UEMCP::SequencerInfo::MakeKeyTimeObject(
								KeyTimes[KeyIndex],
								TickResolution,
								DisplayRate)));
					}
					DetailBudget.KeyTimes -= KeyTimesToReturn;

					const bool bKeyTimesTruncated = KeyTimesToReturn < KeyCount;
					DetailBudget.bTruncated |= bKeyTimesTruncated;

					TSharedPtr<FJsonObject> ChannelObj = MakeShared<FJsonObject>();
					ChannelObj->SetNumberField(TEXT("index"), ChannelIndex);
					ChannelObj->SetStringField(TEXT("name"), ChannelName);
					ChannelObj->SetStringField(TEXT("type"), ChannelType);
					ChannelObj->SetNumberField(TEXT("keyCount"), KeyCount);
					ChannelObj->SetArrayField(TEXT("keyTimes"), KeyTimesArr);
					ChannelObj->SetBoolField(TEXT("keyTimesTruncated"), bKeyTimesTruncated);
					ChannelsArr.Add(MakeShared<FJsonValueObject>(ChannelObj));
				}

				if (bStopChannels) break;
			}

			const bool bChannelsTruncated = ChannelsArr.Num() < ChannelCount;
			DetailBudget.bTruncated |= bChannelsTruncated;
			SObj->SetNumberField(TEXT("channelCount"), ChannelCount);
			SObj->SetArrayField(TEXT("channels"), ChannelsArr);
			SObj->SetBoolField(TEXT("channelsTruncated"), bChannelsTruncated);
			SectionsArr.Add(MakeShared<FJsonValueObject>(SObj));
		}

		const bool bSectionsTruncated = SectionsArr.Num() < AllSections.Num();
		DetailBudget.bTruncated |= bSectionsTruncated;
		TrackObj->SetArrayField(TEXT("sections"), SectionsArr);
		TrackObj->SetBoolField(TEXT("sectionsTruncated"), bSectionsTruncated);
	};

	auto MakeTrackObject = [&](UMovieSceneTrack* Track) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
		TrackObj->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
		TrackObj->SetStringField(TEXT("class"), Track->GetClass()->GetName());
		TrackObj->SetNumberField(TEXT("sectionCount"), Track->GetAllSections().Num());
		ExtractSectionDetails(Track, TrackObj);
		return TrackObj;
	};

	// Camera cuts are stored separately from UMovieScene::GetTracks(). Emit the
	// camera-cut track first so the root-track inspection is never hidden behind
	// the bounded detail budget, then append the ordinary root tracks once each.
	TArray<UMovieSceneTrack*> RootTracks;
	if (UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack())
	{
		RootTracks.Add(CameraCutTrack);
	}
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		if (Track && !RootTracks.Contains(Track))
		{
			RootTracks.Add(Track);
		}
	}

	TArray<TSharedPtr<FJsonValue>> MasterTracksArray;
	for (UMovieSceneTrack* Track : RootTracks)
	{
		if (!Track) continue;
		MasterTracksArray.Add(MakeShared<FJsonValueObject>(MakeTrackObject(Track)));
	}
	Result->SetArrayField(TEXT("masterTracks"), MasterTracksArray);
	Result->SetNumberField(TEXT("masterTrackCount"), MasterTracksArray.Num());

	// #556: collect the Sequencer binding tags (group labels) that reference a
	// given binding guid, from the MovieScene's tagged-binding map.
	auto TagsForGuid = [MovieScene](const FGuid& Guid) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TPair<FName, FMovieSceneObjectBindingIDs>& Pair : MovieScene->AllTaggedBindings())
		{
			for (const FMovieSceneObjectBindingID& ID : Pair.Value.IDs)
			{
				if (ID.GetGuid() == Guid)
				{
					Out.Add(MakeShared<FJsonValueString>(Pair.Key.ToString()));
					break;
				}
			}
		}
		return Out;
	};

	TArray<TSharedPtr<FJsonValue>> BindingsArray;
	auto AppendBinding = [&](const FString& Name, const FGuid& Guid, const TCHAR* Type)
	{
		TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
		BindingObj->SetStringField(TEXT("name"), Name);
		BindingObj->SetStringField(TEXT("guid"), Guid.ToString());
		BindingObj->SetStringField(TEXT("type"), Type);
		BindingObj->SetArrayField(TEXT("tags"), TagsForGuid(Guid));

		TArray<TSharedPtr<FJsonValue>> TrackArr;
		const FMovieSceneBinding* Binding = MovieScene->FindBinding(Guid);
		if (Binding)
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				if (!Track) continue;
				TrackArr.Add(MakeShared<FJsonValueObject>(MakeTrackObject(Track)));
			}
		}
		BindingObj->SetArrayField(TEXT("tracks"), TrackArr);
		BindingsArray.Add(MakeShared<FJsonValueObject>(BindingObj));
	};

	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
		AppendBinding(Possessable.GetName(), Possessable.GetGuid(), TEXT("possessable"));
	}

	for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
	{
		const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(i);
		AppendBinding(Spawnable.GetName(), Spawnable.GetGuid(), TEXT("spawnable"));
	}
	Result->SetArrayField(TEXT("bindings"), BindingsArray);
	Result->SetNumberField(TEXT("bindingCount"), BindingsArray.Num());

	if (bIncludeDetails)
	{
		Result->SetBoolField(TEXT("detailsTruncated"), DetailBudget.bTruncated);
	}

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

		AActor* TargetActor = FindActorByLabel(World, ActorLabel);
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

// play_sequence - drive the Sequencer editor's transport.
//
// This used to issue "Sequencer.Play"/"Sequencer.Pause"/"Sequencer.Stop"
// through GEditor->Exec. No such console commands exist in the engine: the
// Sequencer module registers neither an exec handler nor a console command for
// them, so Exec returned false and the handler reported success for a call that
// did nothing at all. Every use of this action since it shipped was a no-op.
//
// ULevelSequenceEditorBlueprintLibrary is the real scripting surface - the same
// one the editor's own Python/Blueprint automation uses.
TSharedPtr<FJsonValue> FSequencerHandlers::SequenceControl(const TSharedPtr<FJsonObject>& Params)
{
	FString Action;
	if (auto Err = RequireString(Params, TEXT("action"), Action)) return Err;

	const bool bPlay  = Action.Equals(TEXT("play"), ESearchCase::IgnoreCase);
	const bool bPause = Action.Equals(TEXT("pause"), ESearchCase::IgnoreCase);
	const bool bStop  = Action.Equals(TEXT("stop"), ESearchCase::IgnoreCase);
	if (!bPlay && !bPause && !bStop)
	{
		return MCPError(FString::Printf(TEXT("Unknown action: '%s'. Use play, pause, or stop."), *Action));
	}

	// Open the requested sequence first: the transport acts on whatever
	// Sequencer currently has open, so naming one and not opening it would
	// drive a different sequence.
	FString RequestedPath = OptionalString(Params, TEXT("sequencePath"), OptionalString(Params, TEXT("assetPath")));
	if (!RequestedPath.IsEmpty())
	{
		ULevelSequence* Sequence = LoadAssetByPath<ULevelSequence>(RequestedPath);
		if (!Sequence)
		{
			return MCPError(FString::Printf(TEXT("Level Sequence not found: %s"), *RequestedPath));
		}
		if (!ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Sequence))
		{
			return MCPError(FString::Printf(
				TEXT("Failed to open '%s' in Sequencer."), *Sequence->GetPathName()));
		}
	}

	ULevelSequence* Current = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (!Current)
	{
		return MCPError(TEXT("No Level Sequence is open in Sequencer. Pass sequencePath to open one, or open it in the editor first."));
	}

	if (bPlay)
	{
		ULevelSequenceEditorBlueprintLibrary::Play();
	}
	else if (bPause)
	{
		ULevelSequenceEditorBlueprintLibrary::Pause();
	}
	else
	{
		// There is no Stop(): pause, then rewind to the start of the playback
		// range, which is what stopping means to a caller.
		ULevelSequenceEditorBlueprintLibrary::Pause();
		const FMovieSceneSequencePlaybackParams Rewind(FFrameTime(0), EUpdatePositionMethod::Scrub);
		ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(Rewind);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("action"), Action);
	Result->SetStringField(TEXT("sequencePath"), Current->GetPathName());
	// Read the transport back rather than reporting what was asked for.
	Result->SetBoolField(TEXT("playing"), ULevelSequenceEditorBlueprintLibrary::IsPlaying());
	return MCPResult(Result);
}

// set_sequence_playback_range -- set a Level Sequence's playback range in
// seconds. (#548) Params: sequencePath, startSeconds, endSeconds.
TSharedPtr<FJsonValue> FSequencerHandlers::SetPlaybackRange(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, Err;
	ULevelSequence* Sequence = LoadSequence(Params, Path, Err);
	if (!Sequence) return MCPError(Err);
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("Sequence has no MovieScene"));

	double StartSeconds = 0.0, EndSeconds = 0.0;
	if (!Params->TryGetNumberField(TEXT("startSeconds"), StartSeconds) ||
		!Params->TryGetNumberField(TEXT("endSeconds"), EndSeconds))
	{
		return MCPError(TEXT("Missing 'startSeconds' and/or 'endSeconds'"));
	}

	const FFrameRate Tick = MovieScene->GetTickResolution();
	const FFrameNumber Start = Tick.AsFrameNumber(StartSeconds);
	const FFrameNumber End = Tick.AsFrameNumber(EndSeconds);
	MovieScene->SetPlaybackRange(TRange<FFrameNumber>(Start, End));
	Sequence->GetOutermost()->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("sequencePath"), Path);
	Result->SetNumberField(TEXT("startSeconds"), StartSeconds);
	Result->SetNumberField(TEXT("endSeconds"), EndSeconds);
	return MCPResult(Result);
}

// add_sequence_section -- add a section to a track (creating the track if
// needed), set its start/end in seconds, and for a CameraCut track bind it to
// a camera actor. Returns the section index and its channel names. (#548)
// Params: sequencePath, trackType, actorLabel? (binding scope), startSeconds?,
// endSeconds?, cameraActorLabel? (CameraCut).
TSharedPtr<FJsonValue> FSequencerHandlers::AddSection(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, Err;
	ULevelSequence* Sequence = LoadSequence(Params, Path, Err);
	if (!Sequence) return MCPError(Err);
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("Sequence has no MovieScene"));

	FString TrackType;
	if (auto E = RequireString(Params, TEXT("trackType"), TrackType)) return E;
	UClass* TrackClass = ResolveTrackClass(TrackType);
	if (!TrackClass) return MCPError(FString::Printf(TEXT("Unknown track type: '%s'"), *TrackType));

	const FString ActorLabel = OptionalString(Params, TEXT("actorLabel"));

	UMovieSceneTrack* Track = nullptr;
	FGuid BindingGuid;
	if (!ActorLabel.IsEmpty())
	{
		if (!ResolveActorBinding(Sequence, MovieScene, ActorLabel, BindingGuid, Err)) return MCPError(Err);
		Track = MovieScene->FindTrack(TrackClass, BindingGuid);
		if (!Track) Track = MovieScene->AddTrack(TrackClass, BindingGuid);
	}
	else
	{
		for (UMovieSceneTrack* T : MovieScene->GetTracks())
		{
			if (T && T->IsA(TrackClass)) { Track = T; break; }
		}
		if (!Track) Track = MovieScene->AddTrack(TrackClass);
	}
	if (!Track) return MCPError(FString::Printf(TEXT("Failed to resolve/add %s track"), *TrackType));

	// Resolve the camera binding up front so a bad cameraActorLabel fails before
	// we create an orphan section.
	const FString CameraActorLabel = OptionalString(Params, TEXT("cameraActorLabel"));
	FGuid CamGuid;
	if (!CameraActorLabel.IsEmpty())
	{
		if (!ResolveActorBinding(Sequence, MovieScene, CameraActorLabel, CamGuid, Err)) return MCPError(Err);
	}

	UMovieSceneSection* Section = Track->CreateNewSection();
	if (!Section) return MCPError(TEXT("Failed to create section"));
	Track->AddSection(*Section);

	const FFrameRate Tick = MovieScene->GetTickResolution();
	double StartSeconds = 0.0, EndSeconds = 0.0;
	const bool bHasStart = Params->TryGetNumberField(TEXT("startSeconds"), StartSeconds);
	const bool bHasEnd = Params->TryGetNumberField(TEXT("endSeconds"), EndSeconds);
	if (bHasStart || bHasEnd)
	{
		const FFrameNumber Start = Tick.AsFrameNumber(StartSeconds);
		const FFrameNumber End = Tick.AsFrameNumber(bHasEnd ? EndSeconds : StartSeconds + 1.0);
		Section->SetRange(TRange<FFrameNumber>(Start, End));
	}

	// CameraCut: bind to the camera actor's possessable resolved above.
	if (!CameraActorLabel.IsEmpty())
	{
		if (UMovieSceneCameraCutSection* CutSection = Cast<UMovieSceneCameraCutSection>(Section))
		{
			CutSection->SetCameraGuid(CamGuid);
		}
	}

	const int32 SectionIndex = Track->GetAllSections().IndexOfByKey(Section);

	// Enumerate channel names so the caller knows what to key.
	TArray<TSharedPtr<FJsonValue>> ChannelNames;
	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	for (const FMovieSceneChannelEntry& Entry : Proxy.GetAllEntries())
	{
		TArrayView<const FMovieSceneChannelMetaData> AllMeta = Entry.GetMetaData();
		for (const FMovieSceneChannelMetaData& Meta : AllMeta)
		{
			ChannelNames.Add(MakeShared<FJsonValueString>(Meta.Name.ToString()));
		}
	}

	Sequence->GetOutermost()->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("sequencePath"), Path);
	Result->SetStringField(TEXT("trackType"), TrackType);
	if (!ActorLabel.IsEmpty()) Result->SetStringField(TEXT("bindingGuid"), BindingGuid.ToString());
	Result->SetNumberField(TEXT("sectionIndex"), SectionIndex);
	Result->SetArrayField(TEXT("channels"), ChannelNames);
	return MCPResult(Result);
}

// set_sequence_keyframes -- add keyframes to a named channel of a section.
// Supports transform double channels (Location.X..Scale.Z, plus friendly
// x/y/z/yaw/pitch/roll) and float channels (Fade/Float). (#548)
// Params: sequencePath, trackType, actorLabel? (binding scope), sectionIndex?
// (default 0), channel, keyframes ([{seconds, value}]), interpolation? (cubic|linear).
TSharedPtr<FJsonValue> FSequencerHandlers::SetKeyframes(const TSharedPtr<FJsonObject>& Params)
{
	FString Path, Err;
	ULevelSequence* Sequence = LoadSequence(Params, Path, Err);
	if (!Sequence) return MCPError(Err);
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("Sequence has no MovieScene"));

	FString TrackType;
	if (auto E = RequireString(Params, TEXT("trackType"), TrackType)) return E;
	UClass* TrackClass = ResolveTrackClass(TrackType);
	if (!TrackClass) return MCPError(FString::Printf(TEXT("Unknown track type: '%s'"), *TrackType));

	FString ChannelName;
	if (auto E = RequireString(Params, TEXT("channel"), ChannelName)) return E;

	const TArray<TSharedPtr<FJsonValue>>* Keyframes = nullptr;
	if (!Params->TryGetArrayField(TEXT("keyframes"), Keyframes) || !Keyframes)
	{
		return MCPError(TEXT("Missing 'keyframes' array ([{seconds, value}, ...])"));
	}

	const FString ActorLabel = OptionalString(Params, TEXT("actorLabel"));
	UMovieSceneTrack* Track = nullptr;
	if (!ActorLabel.IsEmpty())
	{
		FGuid BindingGuid;
		if (!ResolveActorBinding(Sequence, MovieScene, ActorLabel, BindingGuid, Err)) return MCPError(Err);
		Track = MovieScene->FindTrack(TrackClass, BindingGuid);
	}
	else
	{
		for (UMovieSceneTrack* T : MovieScene->GetTracks())
		{
			if (T && T->IsA(TrackClass)) { Track = T; break; }
		}
	}
	if (!Track) return MCPError(FString::Printf(TEXT("No %s track found (add a section first)"), *TrackType));

	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (Sections.Num() == 0) return MCPError(TEXT("Track has no sections (call add_sequence_section first)"));
	int32 SectionIndex = 0;
	Params->TryGetNumberField(TEXT("sectionIndex"), SectionIndex);
	if (SectionIndex < 0 || SectionIndex >= Sections.Num())
	{
		return MCPError(FString::Printf(TEXT("sectionIndex %d out of range (sections=%d)"), SectionIndex, Sections.Num()));
	}
	UMovieSceneSection* Section = Sections[SectionIndex];

	const FString Canonical = CanonicalTransformChannel(ChannelName);
	const bool bLinear = OptionalString(Params, TEXT("interpolation"), TEXT("cubic")).Equals(TEXT("linear"), ESearchCase::IgnoreCase);
	const FFrameRate Tick = MovieScene->GetTickResolution();
	Section->Modify();

	int32 KeysAdded = 0;
	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();

	// Try double channels (transform) by metadata name.
	TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
	TArrayView<const FMovieSceneChannelMetaData> DoubleMeta = Proxy.GetMetaData<FMovieSceneDoubleChannel>();
	bool bMatched = false;
	for (int32 i = 0; i < DoubleChannels.Num(); ++i)
	{
		if (DoubleMeta.IsValidIndex(i) && DoubleMeta[i].Name.ToString().Equals(Canonical, ESearchCase::IgnoreCase))
		{
			for (const TSharedPtr<FJsonValue>& KfVal : *Keyframes)
			{
				const TSharedPtr<FJsonObject>* Kf = nullptr;
				if (!KfVal->TryGetObject(Kf) || !Kf) continue;
				double Sec = 0.0, Val = 0.0;
				(*Kf)->TryGetNumberField(TEXT("seconds"), Sec);
				(*Kf)->TryGetNumberField(TEXT("value"), Val);
				const FFrameNumber Frame = Tick.AsFrameNumber(Sec);
				if (bLinear) DoubleChannels[i]->AddLinearKey(Frame, Val);
				else DoubleChannels[i]->AddCubicKey(Frame, Val);
				++KeysAdded;
			}
			bMatched = true;
			break;
		}
	}

	// Otherwise try float channels (fade/float track). Match by name, or take
	// the only channel when the name doesn't disambiguate.
	if (!bMatched)
	{
		TArrayView<FMovieSceneFloatChannel*> FloatChannels = Proxy.GetChannels<FMovieSceneFloatChannel>();
		TArrayView<const FMovieSceneChannelMetaData> FloatMeta = Proxy.GetMetaData<FMovieSceneFloatChannel>();
		int32 ChosenIdx = INDEX_NONE;
		for (int32 i = 0; i < FloatChannels.Num(); ++i)
		{
			if (FloatMeta.IsValidIndex(i) && FloatMeta[i].Name.ToString().Equals(ChannelName, ESearchCase::IgnoreCase)) { ChosenIdx = i; break; }
		}
		if (ChosenIdx == INDEX_NONE && FloatChannels.Num() == 1) ChosenIdx = 0;
		if (ChosenIdx != INDEX_NONE)
		{
			for (const TSharedPtr<FJsonValue>& KfVal : *Keyframes)
			{
				const TSharedPtr<FJsonObject>* Kf = nullptr;
				if (!KfVal->TryGetObject(Kf) || !Kf) continue;
				double Sec = 0.0, Val = 0.0;
				(*Kf)->TryGetNumberField(TEXT("seconds"), Sec);
				(*Kf)->TryGetNumberField(TEXT("value"), Val);
				const FFrameNumber Frame = Tick.AsFrameNumber(Sec);
				if (bLinear) FloatChannels[ChosenIdx]->AddLinearKey(Frame, (float)Val);
				else FloatChannels[ChosenIdx]->AddCubicKey(Frame, (float)Val);
				++KeysAdded;
			}
			bMatched = true;
		}
	}

	if (!bMatched)
	{
		return MCPError(FString::Printf(TEXT("Channel '%s' not found on the section. For Transform use Location.X/Rotation.Z/etc or x/yaw; for Fade use the float channel."), *ChannelName));
	}

	Sequence->GetOutermost()->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("sequencePath"), Path);
	Result->SetStringField(TEXT("trackType"), TrackType);
	Result->SetStringField(TEXT("channel"), ChannelName);
	Result->SetNumberField(TEXT("sectionIndex"), SectionIndex);
	Result->SetNumberField(TEXT("keysAdded"), KeysAdded);
	return MCPResult(Result);
}
