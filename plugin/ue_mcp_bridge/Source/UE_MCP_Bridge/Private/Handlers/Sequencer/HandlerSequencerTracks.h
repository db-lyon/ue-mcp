#pragma once

// Track, binding and channel addressing shared by the sequencer handler files.
// Inline in a header because the module is a unity build: a helper copied
// into two .cpp files is a redefinition.

#include "CoreMinimal.h"
#include "HandlerUtils.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Sections/MovieSceneParameterSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "Tracks/MovieSceneMaterialParameterCollectionTrack.h"
#include "Materials/MaterialParameterCollection.h"
#include "Curves/RichCurve.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HandlerEngineVersion.h"
#include "UniversalObjectLocatorResolveParams.h"
#if UE_MCP_HAS_5_5_API
#include "MovieSceneBindingReferences.h"
#include "Bindings/MovieSceneCustomBinding.h"
#endif

namespace UEMCP::SequencerTracks
{
	inline ULevelSequence* LoadSequence(const FString& Path, bool bHasPath, TSharedPtr<FJsonValue>& OutError)
	{
		if (!bHasPath || Path.IsEmpty())
		{
			OutError = MCPError(TEXT("Missing 'sequencePath' parameter"));
			return nullptr;
		}
		ULevelSequence* Seq = LoadAssetByPath<ULevelSequence>(Path);
		if (!Seq) OutError = MCPAssetLoadError(Path, TEXT("LevelSequence"));
		return Seq;
	}

	/** Friendly track type, or a UMovieSceneTrack subclass name with or without its U prefix. */
	inline UClass* ResolveTrackClass(const FString& TrackType)
	{
		if (TrackType.Equals(TEXT("Transform"), ESearchCase::IgnoreCase)) return UMovieScene3DTransformTrack::StaticClass();
		if (TrackType.Equals(TEXT("Float"), ESearchCase::IgnoreCase)) return UMovieSceneFloatTrack::StaticClass();
		if (TrackType.Equals(TEXT("SkeletalAnimation"), ESearchCase::IgnoreCase)) return UMovieSceneSkeletalAnimationTrack::StaticClass();
		if (TrackType.Equals(TEXT("CameraCut"), ESearchCase::IgnoreCase)) return UMovieSceneCameraCutTrack::StaticClass();
		if (TrackType.Equals(TEXT("Audio"), ESearchCase::IgnoreCase)) return UMovieSceneAudioTrack::StaticClass();
		if (TrackType.Equals(TEXT("Event"), ESearchCase::IgnoreCase)) return UMovieSceneEventTrack::StaticClass();
		if (TrackType.Equals(TEXT("Fade"), ESearchCase::IgnoreCase)) return UMovieSceneFadeTrack::StaticClass();
		if (TrackType.Equals(TEXT("MaterialParameterCollection"), ESearchCase::IgnoreCase)
			|| TrackType.Equals(TEXT("MPC"), ESearchCase::IgnoreCase))
		{
			return UMovieSceneMaterialParameterCollectionTrack::StaticClass();
		}

		FString Name = TrackType;
		if (Name.Len() > 1 && Name[0] == TEXT('U') && FChar::IsUpper(Name[1])) Name.RightChopInline(1);
		if (UClass* Found = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::None))
		{
			if (Found->IsChildOf(UMovieSceneTrack::StaticClass())) return Found;
		}
		return nullptr;
	}

	inline FString TrackTypeHelp()
	{
		return TEXT("Transform, Float, SkeletalAnimation, CameraCut, Audio, Event, Fade, MaterialParameterCollection, or a track class name such as MovieSceneMaterialParameterCollectionTrack");
	}

	/** Friendly transform channel names to the canonical Location/Rotation/Scale ones. */
	inline FString CanonicalTransformChannel(const FString& In)
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

	inline FString TrackDisplayName(UMovieSceneTrack* Track)
	{
		if (!Track) return FString();
		return Track->GetDisplayName().ToString();
	}

	inline TSharedPtr<FJsonObject> DescribeTrack(UMovieSceneTrack* Track)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("class"), Track->GetClass()->GetName());
		Obj->SetStringField(TEXT("displayName"), TrackDisplayName(Track));
		Obj->SetNumberField(TEXT("sectionCount"), Track->GetAllSections().Num());
		if (UMovieSceneMaterialParameterCollectionTrack* MPCTrack = Cast<UMovieSceneMaterialParameterCollectionTrack>(Track))
		{
			Obj->SetStringField(TEXT("materialParameterCollection"), MPCTrack->MPC ? MPCTrack->MPC->GetPathName() : FString());
		}
		return Obj;
	}

	/** Every binding name and guid, for a miss message. */
	inline FString ListBindings(UMovieScene* MovieScene)
	{
		TArray<FString> Names;
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& P = MovieScene->GetPossessable(i);
			Names.Add(FString::Printf(TEXT("%s (%s)"), *P.GetName(), *P.GetGuid().ToString()));
		}
		for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
		{
			const FMovieSceneSpawnable& S = MovieScene->GetSpawnable(i);
			Names.Add(FString::Printf(TEXT("%s (%s, spawnable)"), *S.GetName(), *S.GetGuid().ToString()));
		}
		return Names.Num() ? FString::Join(Names, TEXT(", ")) : FString(TEXT("none"));
	}

	/** The binding a name or guid string names. Possessables first, then spawnables. Never creates one. */
	inline bool FindBindingByNameOrGuid(UMovieScene* MovieScene, const FString& GuidText, const FString& Name, FGuid& OutGuid, FString& OutError)
	{
		if (!GuidText.IsEmpty())
		{
			FGuid Parsed;
			if (!FGuid::Parse(GuidText, Parsed))
			{
				OutError = FString::Printf(TEXT("bindingGuid '%s' is not a GUID. get_sequence_info reports each binding's guid."), *GuidText);
				return false;
			}
			if (MovieScene->FindPossessable(Parsed) || MovieScene->FindSpawnable(Parsed))
			{
				OutGuid = Parsed;
				return true;
			}
			OutError = FString::Printf(TEXT("No binding with guid %s. Bindings: %s"), *GuidText, *ListBindings(MovieScene));
			return false;
		}

		TArray<FGuid> Matches;
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& P = MovieScene->GetPossessable(i);
			if (P.GetName().Equals(Name, ESearchCase::IgnoreCase)) Matches.Add(P.GetGuid());
		}
		for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
		{
			const FMovieSceneSpawnable& S = MovieScene->GetSpawnable(i);
			if (S.GetName().Equals(Name, ESearchCase::IgnoreCase)) Matches.Add(S.GetGuid());
		}
		if (Matches.Num() == 1)
		{
			OutGuid = Matches[0];
			return true;
		}
		OutError = Matches.Num() == 0
			? FString::Printf(TEXT("No binding named '%s'. Bindings: %s"), *Name, *ListBindings(MovieScene))
			: FString::Printf(TEXT("%d bindings are named '%s'; pass bindingGuid. Bindings: %s"), Matches.Num(), *Name, *ListBindings(MovieScene));
		return false;
	}

	/** The objects a binding resolves to against Context, outside playback (no spawnables or custom bindings). */
	inline void LocateBound(const ULevelSequence* Sequence, const FGuid& Guid, UObject* Context, TArray<UObject*, TInlineAllocator<1>>& Out)
	{
#if UE_MCP_HAS_5_5_API
		Sequence->LocateBoundObjects(Guid, UE::UniversalObjectLocator::FResolveParams(Context), nullptr, Out);
#else
		Sequence->LocateBoundObjects(Guid, UE::UniversalObjectLocator::FResolveParams(Context), Out);
#endif
	}

	/** An actor's existing possessable: by label or name, then by what the sequence resolves. Never creates one. */
	inline bool FindActorBinding(ULevelSequence* Sequence, UMovieScene* MovieScene, AActor* Actor, UWorld* World, FGuid& OutGuid)
	{
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& P = MovieScene->GetPossessable(i);
			if (P.GetName() == Actor->GetActorLabel() || P.GetName() == Actor->GetName())
			{
				OutGuid = P.GetGuid();
				return true;
			}
		}
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& P = MovieScene->GetPossessable(i);
			if (P.GetParent().IsValid()) continue;
			TArray<UObject*, TInlineAllocator<1>> Found;
			LocateBound(Sequence, P.GetGuid(), World, Found);
			if (Found.Contains(Actor))
			{
				OutGuid = P.GetGuid();
				return true;
			}
		}
		return false;
	}

	struct FTrackQuery
	{
		FString TrackType;
		FString TrackName;
		int32 TrackIndex = 0;
		bool bHasBinding = false;
		FGuid Binding;
	};

	/** The track a query names: root tracks (camera cut first) or one binding's tracks, filtered by type and name. */
	inline UMovieSceneTrack* FindTrack(UMovieScene* MovieScene, const FTrackQuery& Query, FString& OutError)
	{
		TArray<UMovieSceneTrack*> Pool;
		if (Query.bHasBinding)
		{
			const FMovieSceneBinding* Binding = MovieScene->FindBinding(Query.Binding);
			if (!Binding)
			{
				OutError = FString::Printf(TEXT("Binding %s has no tracks"), *Query.Binding.ToString());
				return nullptr;
			}
			Pool = Binding->GetTracks();
		}
		else
		{
			if (UMovieSceneTrack* Cut = MovieScene->GetCameraCutTrack()) Pool.Add(Cut);
			for (UMovieSceneTrack* T : MovieScene->GetTracks()) Pool.AddUnique(T);
		}

		UClass* TrackClass = nullptr;
		if (!Query.TrackType.IsEmpty())
		{
			TrackClass = ResolveTrackClass(Query.TrackType);
			if (!TrackClass)
			{
				OutError = FString::Printf(TEXT("Unknown trackType '%s'. Use %s."), *Query.TrackType, *TrackTypeHelp());
				return nullptr;
			}
		}

		TArray<UMovieSceneTrack*> Matches;
		TArray<FString> Seen;
		for (UMovieSceneTrack* Track : Pool)
		{
			if (!Track) continue;
			Seen.Add(FString::Printf(TEXT("%s '%s'"), *Track->GetClass()->GetName(), *TrackDisplayName(Track)));
			if (TrackClass && !Track->IsA(TrackClass)) continue;
			if (!Query.TrackName.IsEmpty())
			{
				bool bNameMatch = TrackDisplayName(Track).Equals(Query.TrackName, ESearchCase::IgnoreCase)
					|| Track->GetTrackName().ToString().Equals(Query.TrackName, ESearchCase::IgnoreCase);
				if (UMovieSceneMaterialParameterCollectionTrack* MPCTrack = Cast<UMovieSceneMaterialParameterCollectionTrack>(Track))
				{
					bNameMatch |= MPCTrack->MPC && (MPCTrack->MPC->GetName().Equals(Query.TrackName, ESearchCase::IgnoreCase)
						|| MPCTrack->MPC->GetPathName().Equals(Query.TrackName, ESearchCase::IgnoreCase));
				}
				if (!bNameMatch) continue;
			}
			Matches.Add(Track);
		}

		const FString Scope = Query.bHasBinding ? FString::Printf(TEXT("binding %s"), *Query.Binding.ToString()) : FString(TEXT("the root (master) tracks"));
		if (Matches.Num() == 0)
		{
			OutError = FString::Printf(TEXT("No track matches trackType '%s' trackName '%s' in %s. Tracks there: %s"),
				*Query.TrackType, *Query.TrackName, *Scope, Seen.Num() ? *FString::Join(Seen, TEXT(", ")) : TEXT("none"));
			return nullptr;
		}
		if (Query.TrackIndex < 0 || Query.TrackIndex >= Matches.Num())
		{
			OutError = FString::Printf(TEXT("trackIndex %d out of range: %d track(s) match in %s"), Query.TrackIndex, Matches.Num(), *Scope);
			return nullptr;
		}
		return Matches[Query.TrackIndex];
	}

	enum class EChannelKind : uint8 { Float, Double, Bool, Integer, Byte, Unsupported };

	inline EChannelKind ChannelKindOf(FName TypeName)
	{
		if (TypeName == FMovieSceneFloatChannel::StaticStruct()->GetFName()) return EChannelKind::Float;
		if (TypeName == FMovieSceneDoubleChannel::StaticStruct()->GetFName()) return EChannelKind::Double;
		if (TypeName == FMovieSceneBoolChannel::StaticStruct()->GetFName()) return EChannelKind::Bool;
		if (TypeName == FMovieSceneIntegerChannel::StaticStruct()->GetFName()) return EChannelKind::Integer;
		if (TypeName == FMovieSceneByteChannel::StaticStruct()->GetFName()) return EChannelKind::Byte;
		return EChannelKind::Unsupported;
	}

	struct FChannelRef
	{
		FMovieSceneChannel* Channel = nullptr;
		EChannelKind Kind = EChannelKind::Unsupported;
		FString Name;
		FString TypeName;
		TArray<FString> Aliases;
	};

	/**
	 * Every channel on a section. A parameter section (material parameter
	 * collection, material and Niagara parameter tracks) names its curves from
	 * the parameter: Name, Name.X/Y/Z, Name.R/G/B/A, Name.Translation.X.
	 */
	inline void GatherChannels(UMovieSceneSection* Section, TArray<FChannelRef>& Out)
	{
		auto Add = [&Out](FMovieSceneChannel* Channel, EChannelKind Kind, const FString& Name, const TCHAR* Type)
		{
			FChannelRef Ref;
			Ref.Channel = Channel;
			Ref.Kind = Kind;
			Ref.Name = Name;
			Ref.TypeName = Type;
			Out.Add(MoveTemp(Ref));
		};

		if (UMovieSceneParameterSection* Params = Cast<UMovieSceneParameterSection>(Section))
		{
			for (FScalarParameterNameAndCurve& P : Params->GetScalarParameterNamesAndCurves())
			{
				Add(&P.ParameterCurve, EChannelKind::Float, P.ParameterName.ToString(), TEXT("MovieSceneFloatChannel"));
			}
			for (FBoolParameterNameAndCurve& P : Params->GetBoolParameterNamesAndCurves())
			{
				Add(&P.ParameterCurve, EChannelKind::Bool, P.ParameterName.ToString(), TEXT("MovieSceneBoolChannel"));
			}
			for (FVector2DParameterNameAndCurves& P : Params->GetVector2DParameterNamesAndCurves())
			{
				const FString N = P.ParameterName.ToString();
				Add(&P.XCurve, EChannelKind::Float, N + TEXT(".X"), TEXT("MovieSceneFloatChannel"));
				Add(&P.YCurve, EChannelKind::Float, N + TEXT(".Y"), TEXT("MovieSceneFloatChannel"));
			}
			for (FVectorParameterNameAndCurves& P : Params->GetVectorParameterNamesAndCurves())
			{
				const FString N = P.ParameterName.ToString();
				Add(&P.XCurve, EChannelKind::Float, N + TEXT(".X"), TEXT("MovieSceneFloatChannel"));
				Add(&P.YCurve, EChannelKind::Float, N + TEXT(".Y"), TEXT("MovieSceneFloatChannel"));
				Add(&P.ZCurve, EChannelKind::Float, N + TEXT(".Z"), TEXT("MovieSceneFloatChannel"));
			}
			for (FColorParameterNameAndCurves& P : Params->GetColorParameterNamesAndCurves())
			{
				const FString N = P.ParameterName.ToString();
				Add(&P.RedCurve, EChannelKind::Float, N + TEXT(".R"), TEXT("MovieSceneFloatChannel"));
				Add(&P.GreenCurve, EChannelKind::Float, N + TEXT(".G"), TEXT("MovieSceneFloatChannel"));
				Add(&P.BlueCurve, EChannelKind::Float, N + TEXT(".B"), TEXT("MovieSceneFloatChannel"));
				Add(&P.AlphaCurve, EChannelKind::Float, N + TEXT(".A"), TEXT("MovieSceneFloatChannel"));
			}
			for (FTransformParameterNameAndCurves& P : Params->GetTransformParameterNamesAndCurves())
			{
				const FString N = P.ParameterName.ToString();
				static const TCHAR* Axes[3] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
				for (int32 i = 0; i < 3; ++i)
				{
					Add(&P.Translation[i], EChannelKind::Float, FString::Printf(TEXT("%s.Translation.%s"), *N, Axes[i]), TEXT("MovieSceneFloatChannel"));
					Add(&P.Rotation[i], EChannelKind::Float, FString::Printf(TEXT("%s.Rotation.%s"), *N, Axes[i]), TEXT("MovieSceneFloatChannel"));
					Add(&P.Scale[i], EChannelKind::Float, FString::Printf(TEXT("%s.Scale.%s"), *N, Axes[i]), TEXT("MovieSceneFloatChannel"));
				}
			}
		}

		const FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
		for (const FMovieSceneChannelEntry& Entry : Proxy.GetAllEntries())
		{
			const FName TypeName = Entry.GetChannelTypeName();
			const TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();
			const TArrayView<const FMovieSceneChannelMetaData> MetaData = Entry.GetMetaData();
			for (int32 i = 0; i < Channels.Num(); ++i)
			{
				FMovieSceneChannel* Channel = Channels[i];
				if (!Channel) continue;
				FString Name = MetaData.IsValidIndex(i) && !MetaData[i].Name.IsNone()
					? MetaData[i].Name.ToString()
					: FString::Printf(TEXT("%s[%d]"), *TypeName.ToString(), i);
				FChannelRef* Existing = Out.FindByPredicate([Channel](const FChannelRef& R) { return R.Channel == Channel; });
				if (Existing)
				{
					if (!Existing->Name.Equals(Name, ESearchCase::IgnoreCase)) Existing->Aliases.Add(Name);
					continue;
				}
				FChannelRef Ref;
				Ref.Channel = Channel;
				Ref.Kind = ChannelKindOf(TypeName);
				Ref.Name = Name;
				Ref.TypeName = TypeName.ToString();
				if (MetaData.IsValidIndex(i))
				{
					const FString Display = MetaData[i].DisplayText.ToString();
					if (!Display.IsEmpty() && !Display.Equals(Name, ESearchCase::IgnoreCase)) Ref.Aliases.Add(Display);
				}
				Out.Add(MoveTemp(Ref));
			}
		}
	}

	inline FChannelRef* FindChannel(TArray<FChannelRef>& Channels, const FString& Requested)
	{
		const FString Canonical = CanonicalTransformChannel(Requested);
		for (const FString& Wanted : { Requested, Canonical })
		{
			for (FChannelRef& Ref : Channels)
			{
				if (Ref.Name.Equals(Wanted, ESearchCase::IgnoreCase)) return &Ref;
			}
			for (FChannelRef& Ref : Channels)
			{
				for (const FString& Alias : Ref.Aliases)
				{
					if (Alias.Equals(Wanted, ESearchCase::IgnoreCase)) return &Ref;
				}
			}
		}
		return nullptr;
	}

	inline FString ListChannels(const TArray<FChannelRef>& Channels)
	{
		TArray<FString> Names;
		for (const FChannelRef& Ref : Channels) Names.Add(Ref.Name);
		return Names.Num() ? FString::Join(Names, TEXT(", ")) : FString(TEXT("none"));
	}

	inline const TCHAR* InterpName(ERichCurveInterpMode Mode)
	{
		switch (Mode)
		{
		case RCIM_Constant: return TEXT("constant");
		case RCIM_Linear: return TEXT("linear");
		case RCIM_Cubic: return TEXT("cubic");
		default: return TEXT("none");
		}
	}

	/** cubic | linear | constant; false for anything else. */
	inline bool ParseInterp(const FString& In, ERichCurveInterpMode& Out)
	{
		if (In.Equals(TEXT("cubic"), ESearchCase::IgnoreCase) || In.Equals(TEXT("auto"), ESearchCase::IgnoreCase)) { Out = RCIM_Cubic; return true; }
		if (In.Equals(TEXT("linear"), ESearchCase::IgnoreCase)) { Out = RCIM_Linear; return true; }
		if (In.Equals(TEXT("constant"), ESearchCase::IgnoreCase) || In.Equals(TEXT("step"), ESearchCase::IgnoreCase)) { Out = RCIM_Constant; return true; }
		return false;
	}

	inline TSharedPtr<FJsonValue> KindValue(EChannelKind Kind, double Value)
	{
		if (Kind == EChannelKind::Bool) return MakeShared<FJsonValueBoolean>(Value != 0.0);
		return MakeShared<FJsonValueNumber>(Value);
	}

	/** A JSON number or boolean as the double a channel takes. */
	inline bool ReadKindValue(const TSharedPtr<FJsonValue>& In, double& Out)
	{
		if (!In.IsValid()) return false;
		bool B = false;
		if (In->Type == EJson::Boolean && In->TryGetBool(B)) { Out = B ? 1.0 : 0.0; return true; }
		return In->TryGetNumber(Out);
	}

	struct FKeyRead
	{
		FFrameNumber Time;
		double Value = 0.0;
		const TCHAR* Interp = TEXT("constant");
	};

	inline void ReadKeys(const FChannelRef& Ref, TArray<FKeyRead>& Out)
	{
		switch (Ref.Kind)
		{
		case EChannelKind::Float:
		{
			const FMovieSceneFloatChannel* Ch = static_cast<const FMovieSceneFloatChannel*>(Ref.Channel);
			TMovieSceneChannelData<const FMovieSceneFloatValue> Data = Ch->GetData();
			for (int32 i = 0; i < Data.GetTimes().Num(); ++i)
			{
				Out.Add({ Data.GetTimes()[i], (double)Data.GetValues()[i].Value, InterpName(Data.GetValues()[i].InterpMode) });
			}
			break;
		}
		case EChannelKind::Double:
		{
			const FMovieSceneDoubleChannel* Ch = static_cast<const FMovieSceneDoubleChannel*>(Ref.Channel);
			TMovieSceneChannelData<const FMovieSceneDoubleValue> Data = Ch->GetData();
			for (int32 i = 0; i < Data.GetTimes().Num(); ++i)
			{
				Out.Add({ Data.GetTimes()[i], Data.GetValues()[i].Value, InterpName(Data.GetValues()[i].InterpMode) });
			}
			break;
		}
		case EChannelKind::Bool:
		{
			const FMovieSceneBoolChannel* Ch = static_cast<const FMovieSceneBoolChannel*>(Ref.Channel);
			TMovieSceneChannelData<const bool> Data = Ch->GetData();
			for (int32 i = 0; i < Data.GetTimes().Num(); ++i) Out.Add({ Data.GetTimes()[i], Data.GetValues()[i] ? 1.0 : 0.0, TEXT("constant") });
			break;
		}
		case EChannelKind::Integer:
		{
			const FMovieSceneIntegerChannel* Ch = static_cast<const FMovieSceneIntegerChannel*>(Ref.Channel);
			TMovieSceneChannelData<const int32> Data = Ch->GetData();
			for (int32 i = 0; i < Data.GetTimes().Num(); ++i) Out.Add({ Data.GetTimes()[i], (double)Data.GetValues()[i], TEXT("constant") });
			break;
		}
		case EChannelKind::Byte:
		{
			const FMovieSceneByteChannel* Ch = static_cast<const FMovieSceneByteChannel*>(Ref.Channel);
			TMovieSceneChannelData<const uint8> Data = Ch->GetData();
			for (int32 i = 0; i < Data.GetTimes().Num(); ++i) Out.Add({ Data.GetTimes()[i], (double)Data.GetValues()[i], TEXT("constant") });
			break;
		}
		default:
			break;
		}
	}

	inline bool ReadDefault(const FChannelRef& Ref, double& Out)
	{
		switch (Ref.Kind)
		{
		case EChannelKind::Float: { TOptional<float> V = static_cast<const FMovieSceneFloatChannel*>(Ref.Channel)->GetDefault(); if (V.IsSet()) { Out = V.GetValue(); return true; } return false; }
		case EChannelKind::Double: { TOptional<double> V = static_cast<const FMovieSceneDoubleChannel*>(Ref.Channel)->GetDefault(); if (V.IsSet()) { Out = V.GetValue(); return true; } return false; }
		case EChannelKind::Bool: { TOptional<bool> V = static_cast<const FMovieSceneBoolChannel*>(Ref.Channel)->GetDefault(); if (V.IsSet()) { Out = V.GetValue() ? 1.0 : 0.0; return true; } return false; }
		case EChannelKind::Integer: { TOptional<int32> V = static_cast<const FMovieSceneIntegerChannel*>(Ref.Channel)->GetDefault(); if (V.IsSet()) { Out = V.GetValue(); return true; } return false; }
		case EChannelKind::Byte: { TOptional<uint8> V = static_cast<const FMovieSceneByteChannel*>(Ref.Channel)->GetDefault(); if (V.IsSet()) { Out = V.GetValue(); return true; } return false; }
		default: return false;
		}
	}

	inline void WriteDefault(const FChannelRef& Ref, double Value)
	{
		switch (Ref.Kind)
		{
		case EChannelKind::Float: static_cast<FMovieSceneFloatChannel*>(Ref.Channel)->SetDefault((float)Value); break;
		case EChannelKind::Double: static_cast<FMovieSceneDoubleChannel*>(Ref.Channel)->SetDefault(Value); break;
		case EChannelKind::Bool: static_cast<FMovieSceneBoolChannel*>(Ref.Channel)->SetDefault(Value != 0.0); break;
		case EChannelKind::Integer: static_cast<FMovieSceneIntegerChannel*>(Ref.Channel)->SetDefault(FMath::RoundToInt(Value)); break;
		case EChannelKind::Byte: static_cast<FMovieSceneByteChannel*>(Ref.Channel)->SetDefault((uint8)FMath::Clamp(FMath::RoundToInt(Value), 0, 255)); break;
		default: break;
		}
	}

	/** Adds or replaces the key on Time. Interp applies to float and double channels; the others are stepped. */
	inline void WriteKey(const FChannelRef& Ref, FFrameNumber Time, double Value, ERichCurveInterpMode Interp)
	{
		switch (Ref.Kind)
		{
		case EChannelKind::Float:
		{
			FMovieSceneFloatChannel* Ch = static_cast<FMovieSceneFloatChannel*>(Ref.Channel);
			if (Interp == RCIM_Constant) Ch->AddConstantKey(Time, (float)Value);
			else if (Interp == RCIM_Linear) Ch->AddLinearKey(Time, (float)Value);
			else Ch->AddCubicKey(Time, (float)Value);
			break;
		}
		case EChannelKind::Double:
		{
			FMovieSceneDoubleChannel* Ch = static_cast<FMovieSceneDoubleChannel*>(Ref.Channel);
			if (Interp == RCIM_Constant) Ch->AddConstantKey(Time, Value);
			else if (Interp == RCIM_Linear) Ch->AddLinearKey(Time, Value);
			else Ch->AddCubicKey(Time, Value);
			break;
		}
		case EChannelKind::Bool:
			static_cast<FMovieSceneBoolChannel*>(Ref.Channel)->GetData().UpdateOrAddKey(Time, Value != 0.0);
			break;
		case EChannelKind::Integer:
			static_cast<FMovieSceneIntegerChannel*>(Ref.Channel)->GetData().UpdateOrAddKey(Time, FMath::RoundToInt(Value));
			break;
		case EChannelKind::Byte:
			static_cast<FMovieSceneByteChannel*>(Ref.Channel)->GetData().UpdateOrAddKey(Time, (uint8)FMath::Clamp(FMath::RoundToInt(Value), 0, 255));
			break;
		default:
			break;
		}
	}

	/** The raw channel value at a time, ignoring section range and easing. */
	inline bool EvaluateChannel(const FChannelRef& Ref, FFrameTime Time, double& Out)
	{
		switch (Ref.Kind)
		{
		case EChannelKind::Float: { float V = 0.f; if (static_cast<const FMovieSceneFloatChannel*>(Ref.Channel)->Evaluate(Time, V)) { Out = V; return true; } return false; }
		case EChannelKind::Double: { double V = 0.0; if (static_cast<const FMovieSceneDoubleChannel*>(Ref.Channel)->Evaluate(Time, V)) { Out = V; return true; } return false; }
		case EChannelKind::Bool: { bool V = false; if (static_cast<const FMovieSceneBoolChannel*>(Ref.Channel)->Evaluate(Time, V)) { Out = V ? 1.0 : 0.0; return true; } return false; }
		case EChannelKind::Integer: { int32 V = 0; if (static_cast<const FMovieSceneIntegerChannel*>(Ref.Channel)->Evaluate(Time, V)) { Out = V; return true; } return false; }
		case EChannelKind::Byte: { uint8 V = 0; if (static_cast<const FMovieSceneByteChannel*>(Ref.Channel)->Evaluate(Time, V)) { Out = V; return true; } return false; }
		default: return false;
		}
	}

	/** Deletes every key on a channel; the default is left alone. */
	inline void ClearKeys(const FChannelRef& Ref)
	{
		TArray<FKeyHandle> Handles;
		Ref.Channel->GetKeys(TRange<FFrameNumber>::All(), nullptr, &Handles);
		if (Handles.Num() > 0) Ref.Channel->DeleteKeys(Handles);
	}

	inline const TCHAR* KindName(EChannelKind Kind)
	{
		switch (Kind)
		{
		case EChannelKind::Float: return TEXT("float");
		case EChannelKind::Double: return TEXT("double");
		case EChannelKind::Bool: return TEXT("bool");
		case EChannelKind::Integer: return TEXT("integer");
		case EChannelKind::Byte: return TEXT("byte");
		default: return TEXT("unsupported");
		}
	}

	/** Class and transform of a spawnable's template actor, or null when it has none. */
	inline TSharedPtr<FJsonObject> DescribeTemplate(const UObject* Template)
	{
		if (!Template) return nullptr;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("class"), Template->GetClass()->GetName());
		Obj->SetStringField(TEXT("path"), Template->GetPathName());
		if (const AActor* Actor = Cast<AActor>(Template))
		{
			const FTransform T = Actor->GetRootComponent() ? Actor->GetRootComponent()->GetRelativeTransform() : Actor->GetActorTransform();
			Obj->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(T.GetLocation()));
			Obj->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(T.Rotator()));
			Obj->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(T.GetScale3D()));
		}
		return Obj;
	}

	/** Spawnables authored as custom bindings (UE 5.5+) carry their template on the binding object. */
	inline void AppendCustomBindingTemplates(ULevelSequence* Sequence, UMovieScene* MovieScene, TArray<TSharedPtr<FJsonValue>>& Bindings)
	{
#if UE_MCP_HAS_5_5_API
		const FMovieSceneBindingReferences* Refs = Sequence ? Sequence->GetBindingReferences() : nullptr;
		if (!Refs) return;
		for (const TSharedPtr<FJsonValue>& Value : Bindings)
		{
			TSharedPtr<FJsonObject> Obj = Value->AsObject();
			FGuid Guid;
			if (!Obj.IsValid() || !FGuid::Parse(Obj->GetStringField(TEXT("guid")), Guid)) continue;
			const UMovieSceneCustomBinding* Custom = Refs->GetCustomBinding(Guid, 0);
			if (!Custom) continue;
			Obj->SetStringField(TEXT("customBinding"), Custom->GetClass()->GetName());
			if (const FObjectProperty* Prop = CastField<FObjectProperty>(Custom->GetClass()->FindPropertyByName(TEXT("ActorTemplate"))))
			{
				if (TSharedPtr<FJsonObject> Template = DescribeTemplate(Prop->GetObjectPropertyValue_InContainer(Custom)))
				{
					Obj->SetObjectField(TEXT("template"), Template);
					Obj->SetStringField(TEXT("type"), TEXT("spawnable"));
				}
			}
		}
#endif
	}
}
