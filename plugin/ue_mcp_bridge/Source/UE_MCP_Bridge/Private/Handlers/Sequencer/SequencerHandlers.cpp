#include "SequencerHandlers.h"
#include "SequencerHandlers_Internal.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneTimeUnit.h"

#include "HandlerSequencerPlayhead.h"
#include "HandlerSequencerTracks.h"
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
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Animation/AnimSequenceBase.h"
#if __has_include("Variants/MovieSceneTimeWarpVariant.h")
#include "Variants/MovieSceneTimeWarpVariant.h"
#define UE_MCP_SKELANIM_PLAYRATE_IS_TIMEWARP 1
#else
#define UE_MCP_SKELANIM_PLAYRATE_IS_TIMEWARP 0
#endif
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
	// Reports parameters its handlers never read (#1057). Tagged with the editor
	// category because the editor tool is the surface these methods are exposed
	// through, so their parameter specs generate into its module.
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("editor"));

	// #1057: a spec'd handler declares its parameters here and nowhere else;
	// the registry renames each alias to its parameter before the handler runs.
	using EType = EMCPParamType;
	auto SequencePathParam = [](const TCHAR* Description, bool bRequired)
	{
		FMCPParamSpec Spec = MCPParam::Required(TEXT("sequencePath"), EType::String, Description).Alias(TEXT("assetPath")).Alias(TEXT("path"));
		Spec.bRequired = bRequired;
		return Spec;
	};
	auto TrackTypeParam = []()
	{
		return MCPParam::Required(TEXT("trackType"), EType::String, TEXT("Transform | Float | SkeletalAnimation | CameraCut | Audio | Event | Fade | MaterialParameterCollection, or a track class name"));
	};
	auto MPCParam = []()
	{
		return MCPParam::Optional(TEXT("materialParameterCollection"), EType::String, TEXT("Material Parameter Collection asset a MaterialParameterCollection track animates (required for that track type)"));
	};
	auto DisplayRateParam = [](bool bRequired)
	{
		FMCPParamSpec Spec = MCPParam::Required(TEXT("displayRate"), EType::Any,
			TEXT("Display rate: frames per second (24, 30, 60; 23.976, 29.97 and 59.94 map to the NTSC rates) or {numerator, denominator}. Bakes and display frames sample at this rate"));
		Spec.bRequired = bRequired;
		return Spec;
	};
	auto ActorLabelParam = []()
	{
		return MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Actor label. Editor labels are not unique, so a label naming several actors is refused"));
	};
	auto ActorPathParam = []()
	{
		return MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Full actor object path, the unambiguous selector. Wins over actorLabel"));
	};

	Registry.RegisterHandler(TEXT("create_level_sequence"), &CreateLevelSequence, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Name of the new Level Sequence asset")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Cinematics)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns an existing asset untouched, error refuses")),
		DisplayRateParam(false),
	}, MCPSpec::ContractExempt(TEXT("creates a Level Sequence asset")));
	Registry.RegisterHandler(TEXT("get_sequence_info"), &ReadSequenceInfo, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Level Sequence asset path")).Alias(TEXT("path")),
		MCPParam::Optional(TEXT("includeSectionDetails"), EType::Boolean, TEXT("Add each section's blend type, attach socket, first-key transform values and channels with key times. Without it every section still lists its class, range, isActive and rowIndex")),
	});
	Registry.RegisterHandler(TEXT("add_sequence_track"), &AddTrack, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Level Sequence asset path")).Alias(TEXT("path")),
		TrackTypeParam(),
		ActorLabelParam(),
		ActorPathParam(),
		MPCParam(),
	});
	Registry.RegisterHandler(TEXT("play_sequence"), &SequenceControl, {
		SequencePathParam(TEXT("Level Sequence to open and drive; omit to drive the one already open"), false),
		MCPParam::Optional(TEXT("sequenceAction"), EType::String, TEXT("play (default) | pause | stop")),
	}, MCPSpec::ContractExempt(TEXT("opens a sequence in Sequencer and drives its transport")));
	Registry.RegisterHandler(TEXT("scrub_sequence"), &ScrubSequence, {
		SequencePathParam(TEXT("Level Sequence to open and scrub; omit to scrub the one already open"), false),
		MCPParam::Optional(TEXT("seconds"), EType::Number, TEXT("Playhead position in seconds. Pass exactly one of seconds and frame")),
		MCPParam::Optional(TEXT("frame"), EType::Number, TEXT("Playhead position as a frame number, read in timeUnit")),
		MCPParam::Optional(TEXT("timeUnit"), EType::String, TEXT("How to read frame: display (default, the frame numbers Sequencer shows) | tick (the units get_sequence_info's playbackRange reports)")),
	});
	// #1098: a whole frame range in one call, so it gets the long timeout.
	Registry.RegisterHandlerWithTimeout(TEXT("render_sequence_frames"), &RenderSequenceFrames, 600.0f, {
		SequencePathParam(TEXT("Level Sequence to render"), true),
		MCPParam::Optional(TEXT("startFrame"), EType::Number, TEXT("First display frame to render. Not with startSeconds")),
		MCPParam::Optional(TEXT("endFrame"), EType::Number, TEXT("Last display frame to render, inclusive. Not with endSeconds")),
		MCPParam::Optional(TEXT("startSeconds"), EType::Number, TEXT("Range start in seconds (default the playback range)")),
		MCPParam::Optional(TEXT("endSeconds"), EType::Number, TEXT("Range end in seconds, exclusive")),
		MCPParam::Optional(TEXT("frameStep"), EType::Number, TEXT("Render every Nth frame (default 1)")),
		MCPParam::Optional(TEXT("maxFrames"), EType::Number, TEXT("Refuse a range with more frames than this (default 300, max 5000)")),
		MCPParam::Optional(TEXT("width"), EType::Number, TEXT("Frame width in pixels (default 1280)")),
		MCPParam::Optional(TEXT("height"), EType::Number, TEXT("Frame height in pixels (default 720)")),
		MCPParam::Optional(TEXT("outputDir"), EType::String, TEXT("Absolute or project-relative directory for the frames (default Saved/SequenceFrames/<sequence>)")),
		MCPParam::Optional(TEXT("format"), EType::String, TEXT("Image format; only png")),
		MCPParam::Optional(TEXT("cameraActorLabel"), EType::String, TEXT("Camera actor to render through instead of the camera cuts")),
		MCPParam::Optional(TEXT("cameraActorPath"), EType::String, TEXT("Full object path of that camera actor. Wins over cameraActorLabel")),
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("Fixed camera location, when no camera actor is given")),
		MCPParam::Optional(TEXT("rotation"), EType::Rotator, TEXT("Fixed camera rotation")),
		MCPParam::Optional(TEXT("fov"), EType::Number, TEXT("Fixed camera FOV in degrees (default 90)")),
		MCPParam::Optional(TEXT("fullyLoadTextures"), EType::Boolean, TEXT("Stream textures in before each capture (default true)")),
	}, MCPSpec::ContractExempt(TEXT("writes image files")));
	Registry.RegisterHandler(TEXT("set_sequence_playback_range"), &SetPlaybackRange, {
		SequencePathParam(TEXT("Level Sequence asset path"), true),
		MCPParam::Required(TEXT("startSeconds"), EType::Number, TEXT("Range start in seconds")),
		MCPParam::Required(TEXT("endSeconds"), EType::Number, TEXT("Range end in seconds")),
	});
	// #1641: the frame grid display frames, scrubbing by frame and bakes use.
	Registry.RegisterHandler(TEXT("set_sequence_display_rate"), &SetDisplayRate, {
		SequencePathParam(TEXT("Level Sequence asset path"), true),
		DisplayRateParam(true),
	});
	Registry.RegisterHandler(TEXT("add_sequence_section"), &AddSection, {
		SequencePathParam(TEXT("Level Sequence asset path"), true),
		TrackTypeParam(),
		ActorLabelParam(),
		ActorPathParam(),
		MCPParam::Optional(TEXT("startSeconds"), EType::Number, TEXT("Section start in seconds")),
		MCPParam::Optional(TEXT("endSeconds"), EType::Number, TEXT("Section end in seconds (default one second after the start)")),
		MCPParam::Optional(TEXT("cameraActorLabel"), EType::String, TEXT("Camera actor to bind a CameraCut section to")),
		MCPParam::Optional(TEXT("cameraActorPath"), EType::String, TEXT("Full object path of the camera actor. Wins over cameraActorLabel")),
		MPCParam(),
		// #1641: what a SkeletalAnimation section plays.
		MCPParam::Optional(TEXT("animation"), EType::String, TEXT("AnimSequence, AnimComposite or AnimStreamable a SkeletalAnimation section plays. Without endSeconds the section spans the clip after offsets and play rate")),
		MCPParam::Optional(TEXT("startFrameOffset"), EType::Integer, TEXT("SkeletalAnimation only: ticks (the sequence's tick resolution, 24000/s by default) trimmed from the clip's start. Half a display frame samples a Step-interpolated clip inside each key")),
		MCPParam::Optional(TEXT("endFrameOffset"), EType::Integer, TEXT("SkeletalAnimation only: ticks trimmed from the clip's end")),
		MCPParam::Optional(TEXT("playRate"), EType::Number, TEXT("SkeletalAnimation only: clip play rate, above 0 (default 1)")),
	});
	Registry.RegisterHandler(TEXT("set_sequence_keyframes"), &SetKeyframes, {
		SequencePathParam(TEXT("Level Sequence asset path"), true),
		TrackTypeParam(),
		ActorLabelParam(),
		ActorPathParam(),
		MCPParam::Optional(TEXT("sectionIndex"), EType::Integer, TEXT("Target section index (default 0)")),
		MCPParam::Required(TEXT("channel"), EType::String, TEXT("Location.X/Y/Z or Rotation.X/Y/Z (also x/y/z, yaw/pitch/roll) on a Transform track; the float channel on Fade or Float")),
		MCPParam::Required(TEXT("keyframes"), EType::Array, TEXT("Keys to add, as [{seconds, value, interpolation?}]")).Items(EType::Object),
		MCPParam::Optional(TEXT("interpolation"), EType::String, TEXT("cubic (default) | linear | constant; a key's own interpolation wins")),
	});

	// #1407: the section a channel action addresses is a track picked by type
	// and/or name among the root tracks or one binding's tracks.
	Registry.RegisterHandler(TEXT("get_sequence_channel_keys"), &GetChannelKeys, {
		SequencePathParam(TEXT("Level Sequence asset path"), true),
		MCPParam::Optional(TEXT("trackType"), EType::String, TEXT("Track type or class name (Transform, Fade, MaterialParameterCollection, MovieSceneFloatTrack). Pass this or trackName")),
		MCPParam::Optional(TEXT("trackName"), EType::String, TEXT("Track display name; for a Material Parameter Collection track also the collection's name or path")),
		MCPParam::Optional(TEXT("trackIndex"), EType::Integer, TEXT("Which matching track, when several match (default 0)")),
		MCPParam::Optional(TEXT("bindingGuid"), EType::String, TEXT("Look in this binding's tracks instead of the root tracks")),
		MCPParam::Optional(TEXT("bindingName"), EType::String, TEXT("Look in the binding with this name")),
		ActorLabelParam(),
		ActorPathParam(),
		MCPParam::Optional(TEXT("sectionIndex"), EType::Integer, TEXT("Section index on the track (default 0)")),
		MCPParam::Optional(TEXT("channel"), EType::String, TEXT("Channel name (Location.X, a parameter name such as Intensity or Tint.R); omit for every channel on the section")),
		MCPParam::Optional(TEXT("sampleStartFrame"), EType::Number, TEXT("Also evaluate the channel from this display frame")),
		MCPParam::Optional(TEXT("sampleEndFrame"), EType::Number, TEXT("Last display frame to evaluate, inclusive")),
		MCPParam::Optional(TEXT("sampleStep"), EType::Number, TEXT("Display frames between samples (default 1)")),
	});
	Registry.RegisterHandler(TEXT("set_sequence_channel_keys"), &SetChannelKeys, {
		SequencePathParam(TEXT("Level Sequence asset path"), true),
		MCPParam::Optional(TEXT("trackType"), EType::String, TEXT("Track type or class name (Transform, Fade, MaterialParameterCollection, MovieSceneFloatTrack). Pass this or trackName")),
		MCPParam::Optional(TEXT("trackName"), EType::String, TEXT("Track display name; for a Material Parameter Collection track also the collection's name or path")),
		MCPParam::Optional(TEXT("trackIndex"), EType::Integer, TEXT("Which matching track, when several match (default 0)")),
		MCPParam::Optional(TEXT("bindingGuid"), EType::String, TEXT("Look in this binding's tracks instead of the root tracks")),
		MCPParam::Optional(TEXT("bindingName"), EType::String, TEXT("Look in the binding with this name")),
		ActorLabelParam(),
		ActorPathParam(),
		MCPParam::Optional(TEXT("sectionIndex"), EType::Integer, TEXT("Section index on the track (default 0)")),
		MCPParam::Required(TEXT("channel"), EType::String, TEXT("Channel name: Location.X, a parameter name such as Intensity, or Tint.R for a vector or color parameter")),
		MCPParam::Optional(TEXT("keyframes"), EType::Array, TEXT("Keys as [{seconds | frame | tick, value, interpolation?}]; frame is a display frame, value a number or boolean")).Items(EType::Object),
		MCPParam::Optional(TEXT("interpolation"), EType::String, TEXT("cubic (default) | linear | constant, for keys without their own")),
		MCPParam::Optional(TEXT("clearExisting"), EType::Boolean, TEXT("Delete the channel's keys before writing (default false)")),
		MCPParam::Optional(TEXT("defaultValue"), EType::Any, TEXT("Value the channel holds where it has no keys")),
		MCPParam::Optional(TEXT("clearDefault"), EType::Boolean, TEXT("Remove the channel's default value")),
		MCPParam::Optional(TEXT("parameterType"), EType::String, TEXT("scalar | bool | vector | vector2d | color: adds a missing parameter to a parameter section. Inferred from the collection on a Material Parameter Collection track")),
	});
	Registry.RegisterHandler(TEXT("remove_sequence_section"), &RemoveSection, {
		SequencePathParam(TEXT("Level Sequence asset path"), true),
		MCPParam::Optional(TEXT("trackType"), EType::String, TEXT("Track type or class name (Transform, Fade, MaterialParameterCollection, MovieSceneFloatTrack). Pass this or trackName")),
		MCPParam::Optional(TEXT("trackName"), EType::String, TEXT("Track display name; for a Material Parameter Collection track also the collection's name or path")),
		MCPParam::Optional(TEXT("trackIndex"), EType::Integer, TEXT("Which matching track, when several match (default 0)")),
		MCPParam::Optional(TEXT("bindingGuid"), EType::String, TEXT("Look in this binding's tracks instead of the root tracks")),
		MCPParam::Optional(TEXT("bindingName"), EType::String, TEXT("Look in the binding with this name")),
		ActorLabelParam(),
		ActorPathParam(),
		MCPParam::Required(TEXT("sectionIndex"), EType::Integer, TEXT("Index of the section to remove, as get_sequence_info lists them")),
	});

	// #1356: what bindings resolve to, and retargeting a stale possessable.
	Registry.RegisterHandler(TEXT("resolve_sequence_bindings"), &ResolveBindings, {
		SequencePathParam(TEXT("Level Sequence asset path"), true),
		MCPParam::Optional(TEXT("bindingGuid"), EType::String, TEXT("Report only this binding")),
		MCPParam::Optional(TEXT("bindingName"), EType::String, TEXT("Report only the binding with this name")),
	});
	Registry.RegisterHandler(TEXT("rebind_possessable"), &RebindPossessable, {
		SequencePathParam(TEXT("Level Sequence asset path"), true),
		MCPParam::Optional(TEXT("bindingGuid"), EType::String, TEXT("The possessable to retarget. Pass this or bindingName")),
		MCPParam::Optional(TEXT("bindingName"), EType::String, TEXT("The possessable's name, when it is unique")),
		ActorLabelParam(),
		ActorPathParam(),
		MCPParam::Optional(TEXT("includeChildren"), EType::Boolean, TEXT("Also retarget the binding's component possessables to the new actor's components (default true)")),
		MCPParam::Optional(TEXT("componentMap"), EType::Object, TEXT("{childBindingNameOrGuid: componentName} for children whose component name differs from the binding name")),
		MCPParam::Optional(TEXT("renameBinding"), EType::Boolean, TEXT("Rename the binding to the new actor's label (default false)")),
	});
	// #1232: runtime players and what each of their bindings resolved to.
	Registry.RegisterHandler(TEXT("get_sequence_runtime_bindings"), &GetRuntimeBindings, {
		MCPParam::Optional(TEXT("world"), EType::String, TEXT("auto (default: PIE when running, else editor) | pie | editor")),
		MCPParam::Optional(TEXT("pieInstance"), EType::Number, TEXT("PIE instance index when several clients run")),
		MCPParam::Optional(TEXT("sequencePath"), EType::String, TEXT("Report only actors playing this sequence")),
		MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Report only the LevelSequenceActor with this label or name")),
	});
	// #1640: what a binding's components hold, read off the template or the possessed actor.
	Registry.RegisterHandler(TEXT("get_sequence_binding_components"), &GetBindingComponents, {
		SequencePathParam(TEXT("Level Sequence asset path"), true),
		MCPParam::Optional(TEXT("bindingGuid"), EType::String, TEXT("Report only this binding")),
		MCPParam::Optional(TEXT("bindingName"), EType::String, TEXT("Report only the binding with this name")),
		MCPParam::Optional(TEXT("componentClass"), EType::String, TEXT("Report only components of this class or a subclass (ImagePlateComponent, CineCameraComponent, PrimitiveComponent)")),
		MCPParam::Optional(TEXT("propertyNames"), EType::Array, TEXT("Properties or dotted paths to read, in C++ or Python spelling (bHoldout, cast_shadow, LightingChannels.bChannel1); [\"*\"] reads every editable property. Default: the primitive rendering flags (CastShadow, bAffectDynamicIndirectLighting, bVisibleInRayTracing, bHoldout, LightingChannels, custom depth and stencil, visibility)")).Items(EType::String),
	});
}

// ─── #548 sequencer authoring helpers ────────────────────────────────
namespace
{
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

	// #1641: a display rate given as frames per second or {numerator, denominator}.
	// 23.976, 29.97 and 59.94 (any N * 1000/1001) map to the exact NTSC rate.
	bool ParseDisplayRate(const TSharedPtr<FJsonValue>& Value, FFrameRate& OutRate, FString& OutError)
	{
		OutError = TEXT("'displayRate' must be frames per second (e.g. 30 or 29.97) or {numerator, denominator}, both above 0");
		if (!Value.IsValid()) return false;
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Value->TryGetObject(Obj) && Obj && Obj->IsValid())
		{
			double Num = 0.0, Den = 0.0;
			if (!(*Obj)->TryGetNumberField(TEXT("numerator"), Num) || !(*Obj)->TryGetNumberField(TEXT("denominator"), Den)) return false;
			if (Num < 1.0 || Den < 1.0 || Num > MAX_int32 || Den > MAX_int32 || Num != FMath::RoundToDouble(Num) || Den != FMath::RoundToDouble(Den)) return false;
			OutRate = FFrameRate(static_cast<int32>(Num), static_cast<int32>(Den));
			return true;
		}
		double Fps = 0.0;
		if (!Value->TryGetNumber(Fps) || !(Fps > 0.0) || Fps > 1000.0) return false;
		const double Whole = FMath::RoundToDouble(Fps);
		if (FMath::Abs(Fps - Whole) < 1e-6)
		{
			OutRate = FFrameRate(static_cast<int32>(Whole), 1);
			return true;
		}
		const double NtscBase = FMath::RoundToDouble(Fps * 1.001);
		if (NtscBase >= 1.0 && FMath::Abs(Fps - NtscBase * 1000.0 / 1001.0) < 0.005)
		{
			OutRate = FFrameRate(static_cast<int32>(NtscBase) * 1000, 1001);
			return true;
		}
		OutError = FString::Printf(TEXT("'displayRate' %g is neither a whole frame rate nor an NTSC rate; pass {numerator, denominator}"), Fps);
		return false;
	}

	// What a SkeletalAnimation section plays: clip, offsets in ticks and seconds,
	// and play rate when it is a fixed number (#1641).
	TSharedPtr<FJsonObject> SkeletalAnimationParamsJson(const UMovieSceneSkeletalAnimationSection& Section, const FFrameRate& TickResolution)
	{
		const FMovieSceneSkeletalAnimationParams& P = Section.Params;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("animation"), P.Animation ? P.Animation->GetPathName() : FString());
		if (P.Animation) Obj->SetNumberField(TEXT("animationLengthSeconds"), P.Animation->GetPlayLength());
		Obj->SetNumberField(TEXT("startFrameOffset"), P.StartFrameOffset.Value);
		Obj->SetNumberField(TEXT("endFrameOffset"), P.EndFrameOffset.Value);
		Obj->SetNumberField(TEXT("startOffsetSeconds"), TickResolution.AsSeconds(FFrameTime(P.StartFrameOffset)));
		Obj->SetNumberField(TEXT("endOffsetSeconds"), TickResolution.AsSeconds(FFrameTime(P.EndFrameOffset)));
#if UE_MCP_SKELANIM_PLAYRATE_IS_TIMEWARP
		if (P.PlayRate.GetType() == EMovieSceneTimeWarpType::FixedPlayRate)
		{
			Obj->SetNumberField(TEXT("playRate"), P.PlayRate.AsFixedPlayRate());
		}
#else
		Obj->SetNumberField(TEXT("playRate"), P.PlayRate);
#endif
		return Obj;
	}

	TSharedPtr<FJsonObject> DisplayRateJson(const FFrameRate& Rate)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		UEMCP::SequencerInfo::SetFrameRateFields(*Obj, Rate);
		Obj->SetNumberField(TEXT("fps"), Rate.AsDecimal());
		return Obj;
	}
}

TSharedPtr<FJsonValue> FSequencerHandlers::CreateLevelSequence(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Cinematics"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	// Parsed before the asset exists, so a bad rate creates nothing (#1641).
	TSharedPtr<FJsonValue> DisplayRateValue = Params->TryGetField(TEXT("displayRate"));
	if (DisplayRateValue.IsValid() && DisplayRateValue->IsNull()) DisplayRateValue.Reset();
	FFrameRate DisplayRate;
	if (DisplayRateValue.IsValid())
	{
		FString RateErr;
		if (!ParseDisplayRate(DisplayRateValue, DisplayRate, RateErr)) return MCPError(RateErr);
	}

	auto Created = MCPCreateAssetIdempotentNewObject<ULevelSequence>(Name, PackagePath, OnConflict, TEXT("LevelSequence"));
	if (Created.EarlyReturn) return Created.EarlyReturn;
	ULevelSequence* NewSequence = Created.Asset;
	NewSequence->Initialize();
	UMovieScene* NewMovieScene = NewSequence->GetMovieScene();
	if (DisplayRateValue.IsValid() && NewMovieScene)
	{
		NewMovieScene->SetDisplayRate(DisplayRate);
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("path"), NewSequence->GetPathName());
	Result->SetStringField(TEXT("packagePath"), PackagePath + TEXT("/") + Name);
	if (NewMovieScene) Result->SetObjectField(TEXT("displayRate"), DisplayRateJson(NewMovieScene->GetDisplayRate()));
	MCPSetDeleteAssetRollback(Result, NewSequence->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FSequencerHandlers::ReadSequenceInfo(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail (#1057); `path` arrives
	// as `assetPath`, renamed by the registry.
	FString AssetPath;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("assetPath"), AssetPath);
	const bool bIncludeDetails = OptionalBool(Params, TEXT("includeSectionDetails"));

	if (auto Err = MCPRefuseDuringPlayInEditor(TEXT("get_sequence_info"))) return Err;
	if (PathErr) return PathErr;

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
		if (!Track) return;

		const TArray<UMovieSceneSection*>& AllSections = Track->GetAllSections();
		if (!bIncludeDetails)
		{
			// Every section's range and activity, unbudgeted: a few fields per
			// section, enough to tell what a binding still holds (#1644).
			TArray<TSharedPtr<FJsonValue>> Summary;
			for (int32 SectionIndex = 0; SectionIndex < AllSections.Num(); ++SectionIndex)
			{
				const UMovieSceneSection* Section = AllSections[SectionIndex];
				if (!Section) continue;
				TSharedPtr<FJsonObject> SObj = MakeShared<FJsonObject>();
				SObj->SetNumberField(TEXT("index"), SectionIndex);
				SObj->SetStringField(TEXT("class"), Section->GetClass()->GetName());
				UEMCP::SequencerInfo::SetTimingRangeFields(*SObj, Section->GetTrueRange(), TickResolution, DisplayRate);
				SObj->SetBoolField(TEXT("isActive"), Section->IsActive());
				SObj->SetNumberField(TEXT("rowIndex"), Section->GetRowIndex());
				if (const UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section))
				{
					SObj->SetObjectField(TEXT("animation"), SkeletalAnimationParamsJson(*AnimSection, TickResolution));
				}
				Summary.Add(MakeShared<FJsonValueObject>(SObj));
			}
			TrackObj->SetArrayField(TEXT("sections"), Summary);
			return;
		}
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

			// Blend type, activity and row decide how overlapping sections combine (#1232).
			const FOptionalMovieSceneBlendType Blend = Section->GetBlendType();
			if (Blend.IsValid())
			{
				SObj->SetStringField(TEXT("blendType"), StaticEnum<EMovieSceneBlendType>()->GetNameStringByValue((int64)Blend.Get()));
			}
			SObj->SetBoolField(TEXT("isActive"), Section->IsActive());
			SObj->SetNumberField(TEXT("rowIndex"), Section->GetRowIndex());

			if (const UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section))
			{
				SObj->SetObjectField(TEXT("animation"), SkeletalAnimationParamsJson(*AnimSection, TickResolution));
			}

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
					// The value a channel holds with no keys, which is what a constant
					// transform with zero keys evaluates to (#1232).
					UEMCP::SequencerTracks::FChannelRef DefaultRef;
					DefaultRef.Channel = Channel;
					DefaultRef.Kind = UEMCP::SequencerTracks::ChannelKindOf(Entry.GetChannelTypeName());
					double DefaultValue = 0.0;
					const bool bHasDefault = UEMCP::SequencerTracks::ReadDefault(DefaultRef, DefaultValue);
					ChannelObj->SetBoolField(TEXT("hasDefault"), bHasDefault);
					if (bHasDefault)
					{
						ChannelObj->SetField(TEXT("default"), UEMCP::SequencerTracks::KindValue(DefaultRef.Kind, DefaultValue));
					}
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
#if WITH_EDITORONLY_DATA
		// The label Sequencer shows, e.g. the rig name on a Control Rig track.
		TrackObj->SetStringField(TEXT("displayName"), Track->GetDisplayName().ToString());
#endif
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
		if (Possessable.GetParent().IsValid())
		{
			BindingsArray.Last()->AsObject()->SetStringField(TEXT("parentGuid"), Possessable.GetParent().ToString());
		}
	}

	for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
	{
		const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(i);
		AppendBinding(Spawnable.GetName(), Spawnable.GetGuid(), TEXT("spawnable"));
		// The template's transform is where the spawned actor starts (#1232).
		if (TSharedPtr<FJsonObject> Template = UEMCP::SequencerTracks::DescribeTemplate(Spawnable.GetObjectTemplate()))
		{
			BindingsArray.Last()->AsObject()->SetObjectField(TEXT("template"), Template);
		}
	}
	UEMCP::SequencerTracks::AppendCustomBindingTemplates(Sequence, MovieScene, BindingsArray);
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
	// Every parameter is read before anything can fail (#1057); `path` arrives
	// as `assetPath`, renamed by the registry.
	FString AssetPath;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("assetPath"), AssetPath);

	FString TrackType;
	TSharedPtr<FJsonValue> TrackTypeErr = RequireString(Params, TEXT("trackType"), TrackType);

	// Check if we should add to an actor binding or as a master track
	FString ActorLabel = OptionalString(Params, TEXT("actorLabel"));
	const FString ActorPath = OptionalString(Params, TEXT("actorPath"));
	const FString MPCPath = OptionalString(Params, TEXT("materialParameterCollection"));

	if (PathErr) return PathErr;
	if (TrackTypeErr) return TrackTypeErr;

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

	UClass* TrackClass = UEMCP::SequencerTracks::ResolveTrackClass(TrackType);
	if (!TrackClass)
	{
		return MCPError(FString::Printf(TEXT("Unknown track type: '%s'. Use %s."), *TrackType, *UEMCP::SequencerTracks::TrackTypeHelp()));
	}

	// A Material Parameter Collection track animates one collection, so the
	// collection is part of the track's identity (#1407).
	const bool bIsMPCTrack = TrackClass->IsChildOf(UMovieSceneMaterialParameterCollectionTrack::StaticClass());
	UMaterialParameterCollection* MPC = nullptr;
	if (bIsMPCTrack)
	{
		if (MPCPath.IsEmpty())
		{
			return MCPError(TEXT("A MaterialParameterCollection track needs 'materialParameterCollection', the collection asset it animates"));
		}
		MPC = LoadAssetByPath<UMaterialParameterCollection>(MPCPath);
		if (!MPC) return MCPAssetLoadError(MPCPath, TEXT("MaterialParameterCollection"));
	}
	auto MatchesTrack = [&](UMovieSceneTrack* T)
	{
		if (!T || !T->IsA(TrackClass)) return false;
		if (!bIsMPCTrack) return true;
		UMovieSceneMaterialParameterCollectionTrack* MPCTrack = Cast<UMovieSceneMaterialParameterCollectionTrack>(T);
		return MPCTrack && MPCTrack->MPC == MPC;
	};
	auto ConfigureNewTrack = [&](UMovieSceneTrack* T)
	{
		if (UMovieSceneMaterialParameterCollectionTrack* MPCTrack = Cast<UMovieSceneMaterialParameterCollectionTrack>(T))
		{
			MPCTrack->MPC = MPC;
		}
	};

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
		UMovieSceneTrack* ExistingTrack = nullptr;
		if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid))
		{
			for (UMovieSceneTrack* T : Binding->GetTracks())
			{
				if (MatchesTrack(T)) { ExistingTrack = T; break; }
			}
		}
		if (ExistingTrack)
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
		ConfigureNewTrack(NewTrack);

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
			if (MatchesTrack(T))
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
		ConfigureNewTrack(NewTrack);

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
	// sequenceAction, not action: that name is the category tool's dispatch
	// field, and a routing name no spec may declare (#1057).
	const FString Action = OptionalString(Params, TEXT("sequenceAction"), TEXT("play"));

	const bool bPlay  = Action.Equals(TEXT("play"), ESearchCase::IgnoreCase);
	const bool bPause = Action.Equals(TEXT("pause"), ESearchCase::IgnoreCase);
	const bool bStop  = Action.Equals(TEXT("stop"), ESearchCase::IgnoreCase);
	if (!bPlay && !bPause && !bStop)
	{
		return MCPError(FString::Printf(TEXT("Unknown sequenceAction: '%s'. Use play, pause, or stop."), *Action));
	}

	// Open the requested sequence first: the transport acts on whatever
	// Sequencer currently has open, so naming one and not opening it would
	// drive a different sequence. assetPath and path reach it as sequencePath,
	// renamed by the registry.
	FString RequestedPath = OptionalString(Params, TEXT("sequencePath"));
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

	// The transport verb that was in force before, under this handler's own
	// parameter name: a rollback payload goes straight to the bridge.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("sequencePath"), Current->GetPathName());
	Payload->SetStringField(TEXT("sequenceAction"), bWasPlaying ? TEXT("play") : TEXT("pause"));
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
	// Every parameter is read before anything can fail (#1057). `assetPath`
	// and `path` arrive as `sequencePath`, renamed by the registry.
	const FString RequestedPath = OptionalString(Params, TEXT("sequencePath"));
	// Two units are in play and confusing them is an 800x error, so the unit is
	// named rather than guessed: 'display' is the frame number Sequencer shows,
	// 'tick' is what get_sequence_info's playbackRange reports.
	const FString TimeUnit = OptionalString(Params, TEXT("timeUnit"), TEXT("display")).ToLower();
	double RequestedSeconds = 0.0;
	double RequestedFrame = 0.0;
	const bool bHasSeconds = TryGetNumberParam(Params, TEXT("seconds"), RequestedSeconds);
	const bool bHasFrame = TryGetNumberParam(Params, TEXT("frame"), RequestedFrame);

	// The Sequencer scripting surface acts on whatever is currently open, so a
	// named sequence has to be opened first or the scrub would move a different
	// one. Same rule as play_sequence.
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

	if (TimeUnit != TEXT("display") && TimeUnit != TEXT("tick"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown timeUnit '%s'. Use 'display' (the frame numbers Sequencer shows) or 'tick' (the units get_sequence_info's playbackRange reports)."),
			*TimeUnit));
	}

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

	// Pause before scrubbing (a playing sequence moves again on the next tick),
	// then evaluate now so possessed-actor transforms are written.
	UEMCP::SequencerPlayhead::ScrubAndEvaluate(Current, TargetDisplay);

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
	// Every parameter is read before anything can fail (#1057).
	FString Path, Err;
	const bool bHasPath = TryGetStringParam(Params, TEXT("sequencePath"), Path);
	double StartSeconds = 0.0, EndSeconds = 0.0;
	const bool bHasStart = TryGetNumberParam(Params, TEXT("startSeconds"), StartSeconds);
	const bool bHasEnd = TryGetNumberParam(Params, TEXT("endSeconds"), EndSeconds);

	TSharedPtr<FJsonValue> LoadErr;
	ULevelSequence* Sequence = UEMCP::SequencerTracks::LoadSequence(Path, bHasPath, LoadErr);
	if (!Sequence) return LoadErr;
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("Sequence has no MovieScene"));

	if (!bHasStart || !bHasEnd)
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

// set_sequence_display_rate: the frame grid Sequencer shows and bakes sample
// at. Keys and ranges are stored in ticks, so they keep their times (#1641).
TSharedPtr<FJsonValue> FSequencerHandlers::SetDisplayRate(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail (#1057).
	FString Path;
	const bool bHasPath = TryGetStringParam(Params, TEXT("sequencePath"), Path);
	const TSharedPtr<FJsonValue> RateValue = Params->TryGetField(TEXT("displayRate"));

	TSharedPtr<FJsonValue> LoadErr;
	ULevelSequence* Sequence = UEMCP::SequencerTracks::LoadSequence(Path, bHasPath, LoadErr);
	if (!Sequence) return LoadErr;
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("Sequence has no MovieScene"));

	if (!RateValue.IsValid()) return MCPError(TEXT("Missing 'displayRate'"));
	FFrameRate NewRate;
	FString RateErr;
	if (!ParseDisplayRate(RateValue, NewRate, RateErr)) return MCPError(RateErr);

	const FFrameRate Previous = MovieScene->GetDisplayRate();
	const bool bUnchanged = Previous == NewRate;
	if (!bUnchanged)
	{
		MovieScene->Modify();
		MovieScene->SetDisplayRate(NewRate);
		Sequence->GetOutermost()->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("sequencePath"), Path);
	Result->SetObjectField(TEXT("displayRate"), DisplayRateJson(MovieScene->GetDisplayRate()));
	Result->SetObjectField(TEXT("previousDisplayRate"), DisplayRateJson(Previous));
	Result->SetBoolField(TEXT("unchanged"), bUnchanged);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("sequencePath"), Path);
	TSharedPtr<FJsonObject> PreviousObj = MakeShared<FJsonObject>();
	UEMCP::SequencerInfo::SetFrameRateFields(*PreviousObj, Previous);
	Payload->SetObjectField(TEXT("displayRate"), PreviousObj);
	MCPSetRollback(Result, TEXT("set_sequence_display_rate"), Payload);
	return MCPResult(Result);
}

// add_sequence_section -- add a section to a track (creating the track if
// needed), set its start/end in seconds, and for a CameraCut track bind it to
// a camera actor. Returns the section index and its channel names. (#548)
// Params: sequencePath, trackType, actorLabel? (binding scope), startSeconds?,
// endSeconds?, cameraActorLabel? (CameraCut).
TSharedPtr<FJsonValue> FSequencerHandlers::AddSection(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail (#1057).
	FString Path, Err;
	const bool bHasPath = TryGetStringParam(Params, TEXT("sequencePath"), Path);
	FString TrackType;
	TSharedPtr<FJsonValue> TrackTypeErr = RequireString(Params, TEXT("trackType"), TrackType);
	const FString ActorLabel = OptionalString(Params, TEXT("actorLabel"));
	const FString ActorPath = OptionalString(Params, TEXT("actorPath"));
	// Read here so a bad cameraActorLabel fails before we create an orphan
	// section; the binding is resolved below.
	const FString CameraActorLabel = OptionalString(Params, TEXT("cameraActorLabel"));
	const FString CameraActorPath = OptionalString(Params, TEXT("cameraActorPath"));
	double StartSeconds = 0.0, EndSeconds = 0.0;
	const bool bHasStart = TryGetNumberParam(Params, TEXT("startSeconds"), StartSeconds);
	const bool bHasEnd = TryGetNumberParam(Params, TEXT("endSeconds"), EndSeconds);
	const FString MPCPath = OptionalString(Params, TEXT("materialParameterCollection"));
	// #1641: what a SkeletalAnimation section plays.
	const FString AnimationPath = OptionalString(Params, TEXT("animation"));
	double StartOffsetTicks = 0.0, EndOffsetTicks = 0.0, PlayRate = 1.0;
	const bool bHasStartOffset = TryGetNumberParam(Params, TEXT("startFrameOffset"), StartOffsetTicks);
	const bool bHasEndOffset = TryGetNumberParam(Params, TEXT("endFrameOffset"), EndOffsetTicks);
	const bool bHasPlayRate = TryGetNumberParam(Params, TEXT("playRate"), PlayRate);
	const bool bHasAnimParams = !AnimationPath.IsEmpty() || bHasStartOffset || bHasEndOffset || bHasPlayRate;

	TSharedPtr<FJsonValue> LoadErr;
	ULevelSequence* Sequence = UEMCP::SequencerTracks::LoadSequence(Path, bHasPath, LoadErr);
	if (!Sequence) return LoadErr;
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("Sequence has no MovieScene"));

	if (TrackTypeErr) return TrackTypeErr;
	UClass* TrackClass = UEMCP::SequencerTracks::ResolveTrackClass(TrackType);
	if (!TrackClass) return MCPError(FString::Printf(TEXT("Unknown track type: '%s'. Use %s."), *TrackType, *UEMCP::SequencerTracks::TrackTypeHelp()));

	// Checked before any track or section exists, so a bad value creates nothing.
	UAnimSequenceBase* Animation = nullptr;
	double AnimSpanSeconds = 0.0;
	if (bHasAnimParams)
	{
		if (!TrackClass->IsChildOf(UMovieSceneSkeletalAnimationTrack::StaticClass()))
		{
			return MCPError(TEXT("animation, startFrameOffset, endFrameOffset and playRate apply only to a SkeletalAnimation track"));
		}
		auto IsTickCount = [](double V) { return V >= 0.0 && V <= MAX_int32 && V == FMath::RoundToDouble(V); };
		if (!IsTickCount(StartOffsetTicks) || !IsTickCount(EndOffsetTicks))
		{
			return MCPError(TEXT("startFrameOffset and endFrameOffset are whole tick counts of 0 or more"));
		}
		if (!(PlayRate > 0.0)) return MCPError(TEXT("playRate must be above 0"));
		if (!AnimationPath.IsEmpty())
		{
			Animation = LoadAssetByPath<UAnimSequenceBase>(AnimationPath);
			if (!Animation) return MCPAssetLoadError(AnimationPath, TEXT("AnimSequenceBase"));
			const FFrameRate TickRate = MovieScene->GetTickResolution();
			const double ClipSeconds = Animation->GetPlayLength()
				- TickRate.AsSeconds(FFrameTime(FFrameNumber(static_cast<int32>(StartOffsetTicks))))
				- TickRate.AsSeconds(FFrameTime(FFrameNumber(static_cast<int32>(EndOffsetTicks))));
			if (!bHasEnd && ClipSeconds <= 0.0)
			{
				return MCPError(FString::Printf(TEXT("The offsets trim all of '%s' (%.4f s); pass smaller offsets or an explicit endSeconds"),
					*AnimationPath, Animation->GetPlayLength()));
			}
			AnimSpanSeconds = ClipSeconds / PlayRate;
		}
	}

	// An MPC track is identified by its collection as well as its class (#1407).
	UMaterialParameterCollection* MPC = nullptr;
	if (TrackClass->IsChildOf(UMovieSceneMaterialParameterCollectionTrack::StaticClass()))
	{
		if (MPCPath.IsEmpty())
		{
			return MCPError(TEXT("A MaterialParameterCollection track needs 'materialParameterCollection', the collection asset it animates"));
		}
		MPC = LoadAssetByPath<UMaterialParameterCollection>(MPCPath);
		if (!MPC) return MCPAssetLoadError(MPCPath, TEXT("MaterialParameterCollection"));
	}
	auto MatchesTrack = [&](UMovieSceneTrack* T)
	{
		if (!T || !T->IsA(TrackClass)) return false;
		if (!MPC) return true;
		UMovieSceneMaterialParameterCollectionTrack* MPCTrack = Cast<UMovieSceneMaterialParameterCollectionTrack>(T);
		return MPCTrack && MPCTrack->MPC == MPC;
	};

	UMovieSceneTrack* Track = nullptr;
	bool bTrackCreated = false;
	FGuid BindingGuid;
	if (!ActorLabel.IsEmpty() || !ActorPath.IsEmpty())
	{
		REQUIRE_EDITOR_WORLD(BindingWorld);
		TSharedPtr<FJsonValue> ActorErr;
		AActor* BoundActor = MCPResolveActor(BindingWorld, Params, ActorErr);
		if (!BoundActor) return ActorErr;
		if (!ResolveActorBinding(Sequence, MovieScene, BoundActor, BindingGuid, Err)) return MCPError(Err);
		if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid))
		{
			for (UMovieSceneTrack* T : Binding->GetTracks())
			{
				if (MatchesTrack(T)) { Track = T; break; }
			}
		}
		if (!Track) { Track = MovieScene->AddTrack(TrackClass, BindingGuid); bTrackCreated = Track != nullptr; }
	}
	else
	{
		for (UMovieSceneTrack* T : MovieScene->GetTracks())
		{
			if (MatchesTrack(T)) { Track = T; break; }
		}
		if (!Track) { Track = MovieScene->AddTrack(TrackClass); bTrackCreated = Track != nullptr; }
	}
	if (!Track) return MCPError(FString::Printf(TEXT("Failed to resolve/add %s track"), *TrackType));
	if (bTrackCreated && MPC)
	{
		if (UMovieSceneMaterialParameterCollectionTrack* MPCTrack = Cast<UMovieSceneMaterialParameterCollectionTrack>(Track)) MPCTrack->MPC = MPC;
	}

	// Resolve the camera binding up front so a bad cameraActorLabel fails before
	// we create an orphan section.
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

	// A track that arrives with a section of its own gets that section rather
	// than a second, overlapping one (#1232).
	UMovieSceneSection* Section = nullptr;
	const bool bReusedDefaultSection = bTrackCreated && Track->GetAllSections().Num() == 1;
	if (bReusedDefaultSection)
	{
		Section = Track->GetAllSections()[0];
	}
	else
	{
		Section = Track->CreateNewSection();
		if (!Section) return MCPError(TEXT("Failed to create section"));
		Track->AddSection(*Section);
	}

	UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section);
	if (bHasAnimParams && AnimSection)
	{
		AnimSection->Modify();
		if (Animation) AnimSection->Params.Animation = Animation;
		if (bHasStartOffset) AnimSection->Params.StartFrameOffset = FFrameNumber(static_cast<int32>(StartOffsetTicks));
		if (bHasEndOffset) AnimSection->Params.EndFrameOffset = FFrameNumber(static_cast<int32>(EndOffsetTicks));
		// A float before the time-warp variant, a variant assignable from a number after.
		if (bHasPlayRate) AnimSection->Params.PlayRate = static_cast<float>(PlayRate);
	}

	const FFrameRate Tick = MovieScene->GetTickResolution();
	if (bHasStart || bHasEnd || Animation)
	{
		// Without endSeconds a clip section spans the clip after offsets and play rate.
		const double DefaultSpan = Animation ? AnimSpanSeconds : 1.0;
		const FFrameNumber Start = Tick.AsFrameNumber(StartSeconds);
		const FFrameNumber End = Tick.AsFrameNumber(bHasEnd ? EndSeconds : StartSeconds + DefaultSpan);
		Section->SetRange(TRange<FFrameNumber>(Start, End));
	}

	// CameraCut: bind to the camera actor's possessable resolved above.
	if (CamGuid.IsValid())
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
	if (BindingGuid.IsValid()) Result->SetStringField(TEXT("bindingGuid"), BindingGuid.ToString());
	Result->SetNumberField(TEXT("sectionIndex"), SectionIndex);
	Result->SetNumberField(TEXT("sectionCount"), Track->GetAllSections().Num());
	Result->SetBoolField(TEXT("trackCreated"), bTrackCreated);
	Result->SetBoolField(TEXT("reusedDefaultSection"), bReusedDefaultSection);
	Result->SetArrayField(TEXT("channels"), ChannelNames);
	TSharedPtr<FJsonObject> RangeObj = MakeShared<FJsonObject>();
	UEMCP::SequencerInfo::SetTimingRangeFields(*RangeObj, Section->GetRange(), Tick, MovieScene->GetDisplayRate());
	Result->SetObjectField(TEXT("range"), RangeObj);
	if (AnimSection)
	{
		Result->SetObjectField(TEXT("animation"), SkeletalAnimationParamsJson(*AnimSection, Tick));
	}

	// Same reason as add_sequence_track: nothing in the bridge removes a
	// MovieScene section, a track, or a possessable binding, and this call can
	// create all three. There is no method to name, so none is named.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("editor(remove_sequence_section) removes the section by sectionIndex, but this call can also create the track and the actor binding, ")
		TEXT("and nothing removes those, so no automatic rollback is offered. editor(get_sequence_info) reports what the sequence holds."));
	return MCPResult(Result);
}

// set_sequence_keyframes -- add keyframes to a named channel of a section.
// Supports transform double channels (Location.X..Scale.Z, plus friendly
// x/y/z/yaw/pitch/roll) and float channels (Fade/Float). (#548)
// Params: sequencePath, trackType, actorLabel? (binding scope), sectionIndex?
// (default 0), channel, keyframes ([{seconds, value}]), interpolation? (cubic|linear).
TSharedPtr<FJsonValue> FSequencerHandlers::SetKeyframes(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail (#1057).
	FString Path, Err;
	const bool bHasPath = TryGetStringParam(Params, TEXT("sequencePath"), Path);
	FString TrackType;
	TSharedPtr<FJsonValue> TrackTypeErr = RequireString(Params, TEXT("trackType"), TrackType);
	FString ChannelName;
	TSharedPtr<FJsonValue> ChannelErr = RequireString(Params, TEXT("channel"), ChannelName);
	const TArray<TSharedPtr<FJsonValue>>* Keyframes = nullptr;
	const bool bHasKeyframes = TryGetArrayParam(Params, TEXT("keyframes"), Keyframes) && Keyframes;
	const FString ActorLabel = OptionalString(Params, TEXT("actorLabel"));
	const FString ActorPath = OptionalString(Params, TEXT("actorPath"));
	int32 SectionIndex = 0;
	TryGetNumberParam(Params, TEXT("sectionIndex"), SectionIndex);
	const FString InterpText = OptionalString(Params, TEXT("interpolation"), TEXT("cubic"));

	if (auto PieErr = MCPRefuseDuringPlayInEditor(TEXT("set_sequence_keyframes"))) return PieErr;

	// cubic | linear | constant, per call and overridable per key (#1232).
	ERichCurveInterpMode DefaultInterp = RCIM_Cubic;
	if (!UEMCP::SequencerTracks::ParseInterp(InterpText, DefaultInterp))
	{
		return MCPError(FString::Printf(TEXT("Unknown interpolation '%s'. Use cubic, linear or constant."), *InterpText));
	}
	auto KeyInterp = [DefaultInterp](const TSharedPtr<FJsonObject>& Kf)
	{
		ERichCurveInterpMode Mode = DefaultInterp;
		FString PerKey;
		if (Kf->TryGetStringField(TEXT("interpolation"), PerKey)) UEMCP::SequencerTracks::ParseInterp(PerKey, Mode);
		return Mode;
	};

	TSharedPtr<FJsonValue> LoadErr;
	ULevelSequence* Sequence = UEMCP::SequencerTracks::LoadSequence(Path, bHasPath, LoadErr);
	if (!Sequence) return LoadErr;
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("Sequence has no MovieScene"));

	if (TrackTypeErr) return TrackTypeErr;
	UClass* TrackClass = UEMCP::SequencerTracks::ResolveTrackClass(TrackType);
	if (!TrackClass) return MCPError(FString::Printf(TEXT("Unknown track type: '%s'"), *TrackType));

	if (ChannelErr) return ChannelErr;

	if (!bHasKeyframes)
	{
		return MCPError(TEXT("Missing 'keyframes' array ([{seconds, value}, ...])"));
	}

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
	if (SectionIndex < 0 || SectionIndex >= Sections.Num())
	{
		return MCPError(FString::Printf(TEXT("sectionIndex %d out of range (sections=%d)"), SectionIndex, Sections.Num()));
	}
	UMovieSceneSection* Section = Sections[SectionIndex];

	const FString Canonical = UEMCP::SequencerTracks::CanonicalTransformChannel(ChannelName);
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

				const ERichCurveInterpMode Mode = KeyInterp(*Kf);
				if (Mode == RCIM_Constant) DoubleChannels[i]->AddConstantKey(Frame, Val);
				else if (Mode == RCIM_Linear) DoubleChannels[i]->AddLinearKey(Frame, Val);
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

				const ERichCurveInterpMode Mode = KeyInterp(*Kf);
				if (Mode == RCIM_Constant) FloatChannels[ChosenIdx]->AddConstantKey(Frame, (float)Val);
				else if (Mode == RCIM_Linear) FloatChannels[ChosenIdx]->AddLinearKey(Frame, (float)Val);
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
		Payload->SetStringField(TEXT("interpolation"), UEMCP::SequencerTracks::InterpName(DefaultInterp));
		Payload->SetArrayField(TEXT("keyframes"), OverwrittenKeys);
		MCPSetRollback(Result, TEXT("set_sequence_keyframes"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The rollback writes the %d previous key value(s) back over the frames this call replaced. It does NOT remove the %d key(s) ")
			TEXT("this call created on frames that had none (editor(set_sequence_channel_keys) with clearExisting rewrites a whole channel), and it re-adds every ")
			TEXT("restored key with '%s' ")
			TEXT("interpolation rather than the tangents each one originally carried."),
			OverwrittenKeys.Num(), NewKeyFrames, UEMCP::SequencerTracks::InterpName(DefaultInterp)));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("Every one of the %d key(s) this call wrote landed on a frame that had none, so there is no previous value to restore. ")
			TEXT("editor(set_sequence_channel_keys) rewrites a whole channel and returns its own rollback; editor(get_sequence_channel_keys) ")
			TEXT("reports the keys a channel holds."),
			NewKeyFrames));
	}
	return MCPResult(Result);
}
