#include "SequencerHandlers.h"
#include "SequencerHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneTimeUnit.h"

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
	Registry.RegisterHandler(TEXT("scrub_sequence"), &ScrubSequence);
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

	// Resolve (creating if needed) the possessable binding GUID for an actor the
	// caller already selected. #983: the selection happens through
	// MCPResolveActor at the call site, so a duplicated label is refused before
	// a binding is created against the wrong actor.
	bool ResolveActorBinding(ULevelSequence* Sequence, UMovieScene* MovieScene, AActor* TargetActor, FGuid& OutGuid, FString& OutError)
	{
		UWorld* World = GetEditorWorld();
		if (!World) { OutError = TEXT("No editor world available"); return false; }
		if (!TargetActor) { OutError = TEXT("Target actor is null"); return false; }
		const FString ActorLabel = TargetActor->GetActorLabel();

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
	const FString ActorPath = OptionalString(Params, TEXT("actorPath"));
	auto Result = MCPSuccess();

	// Stamped here rather than at each return, because every path out of this
	// handler answers the same way. The bridge registers no action that removes
	// a MovieScene track or a possessable binding - the sequencer surface is
	// create_level_sequence, get_sequence_info, add_sequence_track,
	// add_sequence_section, set_sequence_keyframes, set_sequence_playback_range
	// and the two transport actions - so there is no inverse call to name, and
	// naming one would send a flow at a method that does not exist. This also
	// covers the "track already existed" returns, which can still have created
	// the actor's possessable binding on the way to finding the track.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("No action removes a MovieScene track or an actor's possessable binding, so an added track cannot be taken back out by a call. ")
		TEXT("Delete and rebuild the sequence with asset(delete) plus editor(create_sequence) if a track has to go."));

	if (!ActorLabel.IsEmpty() || !ActorPath.IsEmpty())
	{
		// Find the binding for this actor
		REQUIRE_EDITOR_WORLD(World);

		TSharedPtr<FJsonValue> ActorErr;
		AActor* TargetActor = MCPResolveActor(World, Params, ActorErr);
		if (!TargetActor) return ActorErr;
		ActorLabel = TargetActor->GetActorLabel();

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
			Result->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
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
		Result->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
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

	// The transport as it stands before the verb runs. Both halves are needed:
	// the rollback restores the play/pause state, and the playhead is what stop
	// throws away.
	const bool bWasPlaying = ULevelSequenceEditorBlueprintLibrary::IsPlaying();
	const double PreviousFrame =
		ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition(EMovieSceneTimeUnit::DisplayRate).Frame.AsDecimal();

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
	Result->SetBoolField(TEXT("wasPlaying"), bWasPlaying);
	Result->SetNumberField(TEXT("previousFrame"), PreviousFrame);
	// A play on a sequence that is already playing, or a pause on one that is
	// already paused, moved nothing.
	Result->SetBoolField(TEXT("unchanged"), (bPlay && bWasPlaying) || (bPause && !bWasPlaying));

	// The transport verb that was in force before. `action` is this handler's
	// own parameter name, which is what the payload has to carry: a rollback
	// payload goes straight to the bridge rather than through an action's
	// parameter mapping.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("sequencePath"), Current->GetPathName());
	Payload->SetStringField(TEXT("action"), bWasPlaying ? TEXT("play") : TEXT("pause"));
	MCPSetRollback(Result, TEXT("play_sequence"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The rollback restores the play/pause state this call found, and nothing else. It does NOT put the playhead back where it was ")
		TEXT("- stop rewinds to frame 0 and play has moved it on since - so follow it with editor(scrub_sequence) at previousFrame if the ")
		TEXT("position matters. It also opens the sequence, which is what this action does too."));
	return MCPResult(Result);
}

// scrub_sequence (#881) - park the playhead on an exact time and evaluate there.
//
// Building a data-driven cinematic means capturing the evaluated world at a
// known frame, and play_sequence only offers play/pause/stop: realtime playback
// races capture_scene_png and the frame that lands is whatever the tick gave
// you. This puts the playhead on the frame that was asked for and forces the
// evaluation before answering, which is what makes scrub-then-capture
// reproducible.
//
// A separate action rather than a fourth verb on play_sequence: sequenceAction
// is a closed enum of play|pause|stop and widening it is a contract change on
// the transport, while a scrub carries a time argument the transport verbs have
// no use for.
TSharedPtr<FJsonValue> FSequencerHandlers::ScrubSequence(const TSharedPtr<FJsonObject>& Params)
{
	// The Sequencer scripting surface acts on whatever is currently open, so a
	// named sequence has to be opened first or the scrub would move a different
	// one. Same rule as play_sequence.
	const FString RequestedPath = OptionalString(Params, TEXT("sequencePath"), OptionalString(Params, TEXT("assetPath")));
	if (!RequestedPath.IsEmpty())
	{
		ULevelSequence* Sequence = LoadAssetByPath<ULevelSequence>(RequestedPath);
		if (!Sequence)
		{
			return MCPError(FString::Printf(TEXT("Level Sequence not found: %s"), *RequestedPath));
		}
		if (!ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Sequence))
		{
			return MCPError(FString::Printf(TEXT("Failed to open '%s' in Sequencer."), *Sequence->GetPathName()));
		}
	}

	ULevelSequence* Current = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (!Current)
	{
		return MCPError(TEXT("No Level Sequence is open in Sequencer. Pass sequencePath to open one, or open it in the editor first."));
	}
	UMovieScene* MovieScene = Current->GetMovieScene();
	if (!MovieScene)
	{
		return MCPError(TEXT("LevelSequence has no MovieScene"));
	}

	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	const FFrameRate TickResolution = MovieScene->GetTickResolution();

	// Two units are in play and confusing them is an 800x error, so the unit is
	// named rather than guessed: 'display' is the frame number Sequencer shows,
	// 'tick' is what get_sequence_info's playbackRange reports.
	const FString TimeUnit = OptionalString(Params, TEXT("timeUnit"), TEXT("display")).ToLower();
	if (TimeUnit != TEXT("display") && TimeUnit != TEXT("tick"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown timeUnit '%s'. Use 'display' (the frame numbers Sequencer shows) or 'tick' (the units get_sequence_info's playbackRange reports)."),
			*TimeUnit));
	}

	double RequestedSeconds = 0.0;
	double RequestedFrame = 0.0;
	const bool bHasSeconds = Params->TryGetNumberField(TEXT("seconds"), RequestedSeconds);
	const bool bHasFrame = Params->TryGetNumberField(TEXT("frame"), RequestedFrame);
	if (bHasSeconds == bHasFrame)
	{
		return MCPError(TEXT("Provide exactly one of 'seconds' or 'frame'"));
	}

	// Everything resolves to a display-rate frame time, which is the unit
	// SetGlobalPosition takes and the unit the Sequencer time field shows.
	FFrameTime TargetDisplay;
	if (bHasSeconds)
	{
		TargetDisplay = DisplayRate.AsFrameTime(RequestedSeconds);
	}
	else if (TimeUnit == TEXT("tick"))
	{
		const FFrameTime AsTicks(FFrameNumber(static_cast<int32>(FMath::RoundToDouble(RequestedFrame))));
		TargetDisplay = FFrameRate::TransformTime(AsTicks, TickResolution, DisplayRate);
	}
	else
	{
		TargetDisplay = FFrameTime(FFrameNumber(static_cast<int32>(FMath::RoundToDouble(RequestedFrame))));
	}

	// Where the playhead is now, in display-rate frames, which is exactly what
	// this action's own 'frame' parameter takes with timeUnit 'display'.
	const bool bWasPlaying = ULevelSequenceEditorBlueprintLibrary::IsPlaying();
	const double PreviousDisplayFrame =
		ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition(EMovieSceneTimeUnit::DisplayRate).Frame.AsDecimal();

	// Pause before scrubbing: a playing sequence moves the playhead again on the
	// next tick, and the capture would not be at the time that was asked for.
	ULevelSequenceEditorBlueprintLibrary::Pause();
	const FMovieSceneSequencePlaybackParams ScrubTo(TargetDisplay, EUpdatePositionMethod::Scrub);
	ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(ScrubTo, EMovieSceneTimeUnit::DisplayRate);
	// Evaluate now instead of on the next tick. The playhead move alone does not
	// write possessed-actor transforms; the evaluation does, and a capture taken
	// before it would read the previous frame's world.
	ULevelSequenceEditorBlueprintLibrary::ForceUpdate();

	const FFrameTime TargetTicks = FFrameRate::TransformTime(TargetDisplay, DisplayRate, TickResolution);
	const double EvaluatedSeconds = DisplayRate.AsSeconds(TargetDisplay);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("sequencePath"), Current->GetPathName());
	Result->SetStringField(TEXT("timeUnit"), TimeUnit);
	Result->SetNumberField(TEXT("seconds"), EvaluatedSeconds);
	Result->SetNumberField(TEXT("frame"), TargetDisplay.AsDecimal());
	Result->SetNumberField(TEXT("tick"), TargetTicks.AsDecimal());
	Result->SetBoolField(TEXT("evaluated"), true);
	// `unchanged` has to answer for everything this call changed, and the Pause()
	// above runs whatever the playhead does. So a scrub that arrived on a PLAYING
	// sequence stopped playback and is a change even when the frame it was asked
	// for is the frame it was already on. Both halves, or it would contradict the
	// wasPlaying and rollbackNote fields on this same result.
	const bool bPlayheadMoved = !FMath::IsNearlyEqual(TargetDisplay.AsDecimal(), PreviousDisplayFrame);
	Result->SetBoolField(TEXT("playheadMoved"), bPlayheadMoved);
	Result->SetBoolField(TEXT("unchanged"), !bPlayheadMoved && !bWasPlaying);
	// Read the transport back rather than reporting what was asked for.
	Result->SetBoolField(TEXT("playing"), ULevelSequenceEditorBlueprintLibrary::IsPlaying());

	TSharedPtr<FJsonObject> DisplayRateObj = MakeShared<FJsonObject>();
	DisplayRateObj->SetNumberField(TEXT("numerator"), DisplayRate.Numerator);
	DisplayRateObj->SetNumberField(TEXT("denominator"), DisplayRate.Denominator);
	Result->SetObjectField(TEXT("displayRate"), DisplayRateObj);

	TSharedPtr<FJsonObject> TickRateObj = MakeShared<FJsonObject>();
	TickRateObj->SetNumberField(TEXT("numerator"), TickResolution.Numerator);
	TickRateObj->SetNumberField(TEXT("denominator"), TickResolution.Denominator);
	Result->SetObjectField(TEXT("tickResolution"), TickRateObj);

	// A scrub outside the playback range is legal and evaluates, but a track
	// that has no section there reads as its nearest key, which presents as
	// "the scrub did nothing". Say so rather than leaving it to be guessed.
	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	TSharedPtr<FJsonObject> RangeObj = MakeShared<FJsonObject>();
	if (PlaybackRange.HasLowerBound())
	{
		RangeObj->SetNumberField(TEXT("startTick"), PlaybackRange.GetLowerBoundValue().Value);
		RangeObj->SetNumberField(TEXT("startSeconds"), TickResolution.AsSeconds(FFrameTime(PlaybackRange.GetLowerBoundValue())));
	}
	if (PlaybackRange.HasUpperBound())
	{
		RangeObj->SetNumberField(TEXT("endTick"), PlaybackRange.GetUpperBoundValue().Value);
		RangeObj->SetNumberField(TEXT("endSeconds"), TickResolution.AsSeconds(FFrameTime(PlaybackRange.GetUpperBoundValue())));
	}
	Result->SetObjectField(TEXT("playbackRange"), RangeObj);

	const bool bWithinRange = PlaybackRange.Contains(TargetTicks.FrameNumber);
	Result->SetBoolField(TEXT("withinPlaybackRange"), bWithinRange);
	if (!bWithinRange)
	{
		Result->SetStringField(TEXT("warning"), TEXT(
			"The requested time is outside the sequence's playback range. The playhead moved and the sequence "
			"evaluated, but a track with no section there holds its nearest key, which looks like a scrub that "
			"did nothing. playbackRange above is in ticks; seconds are given alongside."));
	}

	// Scrubbing back to where the playhead was is this same action with the
	// captured display frame. The 'frame' parameter is read as a number and
	// rounded to a whole display frame, so a sub-frame position comes back
	// rounded; the transport was also paused to take the scrub and that is not
	// restored either.
	Result->SetBoolField(TEXT("wasPlaying"), bWasPlaying);
	Result->SetNumberField(TEXT("previousFrame"), PreviousDisplayFrame);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("sequencePath"), Current->GetPathName());
	Payload->SetNumberField(TEXT("frame"), PreviousDisplayFrame);
	Payload->SetStringField(TEXT("timeUnit"), TEXT("display"));
	MCPSetRollback(Result, TEXT("scrub_sequence"), Payload);

	const bool bSubFrame = !FMath::IsNearlyEqual(PreviousDisplayFrame, FMath::RoundToDouble(PreviousDisplayFrame));
	Result->SetBoolField(TEXT("rollbackLossy"), bSubFrame || bWasPlaying);
	if (bSubFrame || bWasPlaying)
	{
		FString Note = TEXT("The rollback scrubs back to previousFrame.");
		if (bSubFrame)
		{
			Note += TEXT(" The playhead was at a sub-frame position and 'frame' is rounded to a whole display frame, so it lands on the nearest one.");
		}
		if (bWasPlaying)
		{
			Note += TEXT(" The sequence was playing when this call arrived and scrubbing pauses it, so the rollback leaves it paused; editor(play_sequence) with sequenceAction 'play' resumes it.");
		}
		Result->SetStringField(TEXT("rollbackNote"), Note);
	}

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

	// The range in place before the write, in the same seconds this action takes
	// in, so it replays through this same action.
	//
	// GetLowerBoundValue / GetUpperBoundValue hand back the stored bound VALUE
	// and say nothing about whether it is included. The write below builds
	// TRange<FFrameNumber>(Start, End), which is inclusive-lower and
	// exclusive-upper, so a range stored with an INCLUSIVE upper bound would
	// come back one tick short if its value were replayed as it stands, and one
	// with an exclusive lower bound one tick long. Normalise both bounds to the
	// form this action writes before converting them to seconds.
	const TRange<FFrameNumber> PreviousRange = MovieScene->GetPlaybackRange();
	const bool bHadPreviousRange = PreviousRange.HasLowerBound() && PreviousRange.HasUpperBound();
	FFrameNumber PreviousLower(0);
	FFrameNumber PreviousUpper(0);
	if (bHadPreviousRange)
	{
		// The +1 is guarded against the top of the frame-number space. A bound
		// already at MAX_int32 has no next tick to normalise to, and signed
		// overflow is undefined behaviour rather than a wrap, so such a bound is
		// left at its stored value. It is not reachable from any real sequence:
		// at the default 24000 tick resolution MAX_int32 ticks is about 24 hours
		// of playback.
		PreviousLower = (PreviousRange.GetLowerBound().IsExclusive()
			&& PreviousRange.GetLowerBoundValue().Value < MAX_int32)
			? FFrameNumber(PreviousRange.GetLowerBoundValue().Value + 1)
			: PreviousRange.GetLowerBoundValue();
		PreviousUpper = (PreviousRange.GetUpperBound().IsInclusive()
			&& PreviousRange.GetUpperBoundValue().Value < MAX_int32)
			? FFrameNumber(PreviousRange.GetUpperBoundValue().Value + 1)
			: PreviousRange.GetUpperBoundValue();
	}
	const double PreviousStartSeconds = bHadPreviousRange ? Tick.AsSeconds(FFrameTime(PreviousLower)) : 0.0;
	const double PreviousEndSeconds = bHadPreviousRange ? Tick.AsSeconds(FFrameTime(PreviousUpper)) : 0.0;

	const FFrameNumber Start = Tick.AsFrameNumber(StartSeconds);
	const FFrameNumber End = Tick.AsFrameNumber(EndSeconds);
	// Compared against the NORMALISED bounds, because that is what this write
	// produces: a range stored inclusive-upper over the same frames is not a
	// no-op, it is a change of bound form.
	const bool bUnchanged = bHadPreviousRange
		&& PreviousLower == Start
		&& PreviousUpper == End;

	MovieScene->SetPlaybackRange(TRange<FFrameNumber>(Start, End));
	Sequence->GetOutermost()->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("sequencePath"), Path);
	Result->SetNumberField(TEXT("startSeconds"), StartSeconds);
	Result->SetNumberField(TEXT("endSeconds"), EndSeconds);
	Result->SetBoolField(TEXT("unchanged"), bUnchanged);

	if (bHadPreviousRange)
	{
		Result->SetNumberField(TEXT("previousStartSeconds"), PreviousStartSeconds);
		Result->SetNumberField(TEXT("previousEndSeconds"), PreviousEndSeconds);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("sequencePath"), Path);
		Payload->SetNumberField(TEXT("startSeconds"), PreviousStartSeconds);
		Payload->SetNumberField(TEXT("endSeconds"), PreviousEndSeconds);
		MCPSetRollback(Result, TEXT("set_sequence_playback_range"), Payload);
		// Seconds are converted back through the same tick resolution that
		// produced them, and both bounds were normalised to the inclusive-lower,
		// exclusive-upper form this action writes, so the restored range covers
		// exactly the frames the original covered.
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The sequence's playback range was unbounded on one or both ends before this call, and set_sequence_playback_range takes ")
			TEXT("two finite seconds values, so it cannot put an open bound back."));
	}
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
	const FString ActorPath = OptionalString(Params, TEXT("actorPath"));

	UMovieSceneTrack* Track = nullptr;
	FGuid BindingGuid;
	if (!ActorLabel.IsEmpty() || !ActorPath.IsEmpty())
	{
		REQUIRE_EDITOR_WORLD(BindingWorld);
		TSharedPtr<FJsonValue> ActorErr;
		AActor* BoundActor = MCPResolveActor(BindingWorld, Params, ActorErr);
		if (!BoundActor) return ActorErr;
		if (!ResolveActorBinding(Sequence, MovieScene, BoundActor, BindingGuid, Err)) return MCPError(Err);
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
	const FString CameraActorPath = OptionalString(Params, TEXT("cameraActorPath"));
	FGuid CamGuid;
	if (!CameraActorLabel.IsEmpty() || !CameraActorPath.IsEmpty())
	{
		REQUIRE_EDITOR_WORLD(CameraWorld);
		FMCPActorSelector CameraSel;
		CameraSel.LabelKey = TEXT("cameraActorLabel");
		CameraSel.PathKey = TEXT("cameraActorPath");
		TSharedPtr<FJsonValue> CameraErr;
		AActor* CameraActor = MCPResolveActor(CameraWorld, Params, CameraErr, CameraSel);
		if (!CameraActor) return CameraErr;
		if (!ResolveActorBinding(Sequence, MovieScene, CameraActor, CamGuid, Err)) return MCPError(Err);
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

	// Same reason as add_sequence_track: nothing in the bridge removes a
	// MovieScene section, a track, or a possessable binding, and this call can
	// create all three. There is no method to name, so none is named.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("No action removes a MovieScene section, and this call also creates the track and the actor binding when they were absent. ")
		TEXT("Nothing can take any of the three back out, so no rollback is offered. editor(get_sequence_info) reports what the sequence holds."));
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
	const FString ActorPath = OptionalString(Params, TEXT("actorPath"));
	UMovieSceneTrack* Track = nullptr;
	if (!ActorLabel.IsEmpty() || !ActorPath.IsEmpty())
	{
		REQUIRE_EDITOR_WORLD(BindingWorld);
		TSharedPtr<FJsonValue> ActorErr;
		AActor* BoundActor = MCPResolveActor(BindingWorld, Params, ActorErr);
		if (!BoundActor) return ActorErr;
		FGuid BindingGuid;
		if (!ResolveActorBinding(Sequence, MovieScene, BoundActor, BindingGuid, Err)) return MCPError(Err);
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
	// The keys this call overwrote, as {seconds, value} pairs this same action
	// takes back in. A frame that carried no key before is counted separately:
	// writing a value there added a key, and no action removes one, so the
	// restore cannot undo that half.
	TArray<TSharedPtr<FJsonValue>> OverwrittenKeys;
	int32 NewKeyFrames = 0;
	// Frames THIS call has already written. Two keyframe entries naming the same
	// seconds resolve to the same frame number: the first creates the key, and
	// without this the second would find that key and record it as pre-existing,
	// so the rollback would write a value at a frame that originally held
	// nothing and keysOverwritten would count a key this call made.
	TSet<int32> FramesWrittenHere;
	// The three reported counts do not all count the same thing, so this closes
	// the arithmetic instead of leaving a caller to work it out. keysAdded
	// counts INPUT ENTRIES written; keysOverwritten and keyFramesCreated count
	// DISTINCT FRAMES, because the guard above only classifies a frame once.
	// This is every entry that landed on a frame an earlier entry in the same
	// batch had already written, so
	// keysAdded == keysOverwritten + keyFramesCreated + duplicateFrameWrites.
	int32 DuplicateFrameWrites = 0;
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

				// Read what is on that frame before writing it, skipping a frame this
				// call has already written: that key is this call's own work, not state
				// to restore.
				if (!FramesWrittenHere.Contains(Frame.Value))
				{
					TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = DoubleChannels[i]->GetData();
					const int32 ExistingKey = ChannelData.FindKey(Frame);
					if (ExistingKey != INDEX_NONE)
					{
						TSharedPtr<FJsonObject> Prior = MakeShared<FJsonObject>();
						Prior->SetNumberField(TEXT("seconds"), Sec);
						Prior->SetNumberField(TEXT("value"), ChannelData.GetValues()[ExistingKey].Value);
						OverwrittenKeys.Add(MakeShared<FJsonValueObject>(Prior));
					}
					else
					{
						++NewKeyFrames;
					}
					FramesWrittenHere.Add(Frame.Value);
				}
				else
				{
					++DuplicateFrameWrites;
				}

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

				// Same duplicate-frame guard as the double path above.
				if (!FramesWrittenHere.Contains(Frame.Value))
				{
					TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = FloatChannels[ChosenIdx]->GetData();
					const int32 ExistingKey = ChannelData.FindKey(Frame);
					if (ExistingKey != INDEX_NONE)
					{
						TSharedPtr<FJsonObject> Prior = MakeShared<FJsonObject>();
						Prior->SetNumberField(TEXT("seconds"), Sec);
						Prior->SetNumberField(TEXT("value"), ChannelData.GetValues()[ExistingKey].Value);
						OverwrittenKeys.Add(MakeShared<FJsonValueObject>(Prior));
					}
					else
					{
						++NewKeyFrames;
					}
					FramesWrittenHere.Add(Frame.Value);
				}
				else
				{
					++DuplicateFrameWrites;
				}

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
	Result->SetNumberField(TEXT("keysOverwritten"), OverwrittenKeys.Num());
	Result->SetNumberField(TEXT("keyFramesCreated"), NewKeyFrames);
	Result->SetNumberField(TEXT("duplicateFrameWrites"), DuplicateFrameWrites);
	Result->SetStringField(TEXT("keyCountsNote"), FString::Printf(
		TEXT("keysAdded counts input keyframe entries written (%d). keysOverwritten (%d) and keyFramesCreated (%d) count DISTINCT frames, ")
		TEXT("since each frame is classified once. duplicateFrameWrites (%d) is the entries that landed on a frame an earlier entry in this ")
		TEXT("same batch had already written, which is what closes the sum: %d = %d + %d + %d."),
		KeysAdded, OverwrittenKeys.Num(), NewKeyFrames, DuplicateFrameWrites,
		KeysAdded, OverwrittenKeys.Num(), NewKeyFrames, DuplicateFrameWrites));

	if (OverwrittenKeys.Num() > 0)
	{
		// Writing the old values back over the same frames restores the keys this
		// call replaced. It cannot delete the keys it created on frames that had
		// none, because no action removes a keyframe, so the note says so and
		// keyFramesCreated says how many.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("sequencePath"), Path);
		Payload->SetStringField(TEXT("trackType"), TrackType);
		Payload->SetStringField(TEXT("channel"), ChannelName);
		Payload->SetNumberField(TEXT("sectionIndex"), SectionIndex);
		if (!ActorLabel.IsEmpty()) Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		if (!ActorPath.IsEmpty()) Payload->SetStringField(TEXT("actorPath"), ActorPath);
		Payload->SetStringField(TEXT("interpolation"), bLinear ? TEXT("linear") : TEXT("cubic"));
		Payload->SetArrayField(TEXT("keyframes"), OverwrittenKeys);
		MCPSetRollback(Result, TEXT("set_sequence_keyframes"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The rollback writes the %d previous key value(s) back over the frames this call replaced. It does NOT remove the %d key(s) ")
			TEXT("this call created on frames that had none - no action removes a key from a Level Sequence channel - and it re-adds every ")
			TEXT("restored key with '%s' ")
			TEXT("interpolation rather than the tangents each one originally carried."),
			OverwrittenKeys.Num(), NewKeyFrames, bLinear ? TEXT("linear") : TEXT("cubic")));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("Every one of the %d key(s) this call wrote landed on a frame that had none, so there is no previous value to restore, and ")
			TEXT("no action removes a key from a Level Sequence channel. editor(get_sequence_info) with includeSectionDetails reports the key ")
			TEXT("times a channel holds."),
			NewKeyFrames));
	}
	return MCPResult(Result);
}
