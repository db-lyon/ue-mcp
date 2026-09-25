// render_sequence_frames (#1098): scrub + capture for a whole frame range in
// one bridge call. Member of FSequencerHandlers; registered in
// SequencerHandlers.cpp.

#include "SequencerHandlers.h"
#include "SequencerHandlers_Internal.h"
#include "HandlerUtils.h"
#include "HandlerSceneCapture.h"
#include "HandlerSequencerPlayhead.h"

#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieSceneTimeHelpers.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UEMCP::SequenceRender
{
	inline constexpr int32 DefaultMaxFrames = 300;
	inline constexpr int32 HardMaxFrames = 5000;
	// Stops short of the 600s handler timeout and returns what was written.
	inline constexpr double TimeBudgetSeconds = 540.0;

	enum class ECameraMode : uint8 { Cuts, Actor, Fixed };

	inline const TCHAR* CameraModeName(ECameraMode Mode)
	{
		switch (Mode)
		{
			case ECameraMode::Actor: return TEXT("actor");
			case ECameraMode::Fixed: return TEXT("fixed");
			default: return TEXT("cuts");
		}
	}

	inline UCameraComponent* CameraFromObject(UObject* Object)
	{
		if (UCameraComponent* Camera = Cast<UCameraComponent>(Object)) return Camera;
		if (ACameraActor* CameraActor = Cast<ACameraActor>(Object)) return CameraActor->GetCameraComponent();
		if (AActor* Actor = Cast<AActor>(Object)) return Actor->FindComponentByClass<UCameraComponent>();
		return nullptr;
	}

	/** The camera the open sequence's camera cut track names at Tick. */
	inline UCameraComponent* CameraCutCameraAt(UMovieSceneTrack* CutTrack, const FFrameNumber Tick)
	{
		if (!CutTrack) return nullptr;
		for (UMovieSceneSection* Section : CutTrack->GetAllSections())
		{
			UMovieSceneCameraCutSection* Cut = Cast<UMovieSceneCameraCutSection>(Section);
			if (!Cut || !Cut->IsActive() || !Cut->GetRange().Contains(Tick)) continue;
			const TArray<UObject*> Bound = ULevelSequenceEditorBlueprintLibrary::GetBoundObjects(Cut->GetCameraBindingID());
			for (UObject* Object : Bound)
			{
				if (UCameraComponent* Camera = CameraFromObject(Object)) return Camera;
			}
		}
		return nullptr;
	}

	inline FString CameraName(const UCameraComponent* Camera)
	{
		if (!Camera) return FString();
		const AActor* Owner = Camera->GetOwner();
		return Owner ? Owner->GetActorLabel() : Camera->GetName();
	}
}

TSharedPtr<FJsonValue> FSequencerHandlers::RenderSequenceFrames(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCP::SequenceRender;

	if (auto PieErr = MCPRefuseDuringPlayInEditor(TEXT("render_sequence_frames"))) return PieErr;

	const FString RequestedPath = OptionalString(Params, TEXT("sequencePath"), OptionalString(Params, TEXT("assetPath")));
	if (RequestedPath.IsEmpty()) return MCPError(TEXT("Missing 'sequencePath' (also accepted as 'assetPath')"));
	ULevelSequence* Sequence = LoadAssetByPath<ULevelSequence>(RequestedPath);
	if (!Sequence) return MCPAssetLoadError(RequestedPath, TEXT("LevelSequence"));
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return MCPError(TEXT("LevelSequence has no MovieScene"));

	const FString Format = OptionalString(Params, TEXT("format"), TEXT("png")).ToLower();
	if (Format != TEXT("png"))
	{
		return MCPError(FString::Printf(TEXT("Unsupported format '%s'. Only 'png' is written (RGBA8 LDR)."), *Format));
	}

	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	if (!UEMCP::SequencerInfo::IsUsableFrameRate(DisplayRate) || !UEMCP::SequencerInfo::IsUsableFrameRate(TickResolution))
	{
		return MCPError(TEXT("LevelSequence has invalid timing rates"));
	}

	// ── Frame range, in display frames. endFrame is inclusive; endSeconds is
	// exclusive, like the playback range it usually comes from.
	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	const bool bRangeBounded = PlaybackRange.HasLowerBound() && PlaybackRange.HasUpperBound();

	double StartFrameIn = 0.0, EndFrameIn = 0.0, StartSecondsIn = 0.0, EndSecondsIn = 0.0;
	const bool bHasStartFrame = TryGetNumberParam(Params, TEXT("startFrame"), StartFrameIn);
	const bool bHasEndFrame = TryGetNumberParam(Params, TEXT("endFrame"), EndFrameIn);
	const bool bHasStartSeconds = TryGetNumberParam(Params, TEXT("startSeconds"), StartSecondsIn);
	const bool bHasEndSeconds = TryGetNumberParam(Params, TEXT("endSeconds"), EndSecondsIn);
	if (bHasStartFrame && bHasStartSeconds) return MCPError(TEXT("Pass startFrame or startSeconds, not both"));
	if (bHasEndFrame && bHasEndSeconds) return MCPError(TEXT("Pass endFrame or endSeconds, not both"));

	int64 StartFrame = 0;
	if (bHasStartFrame)
	{
		StartFrame = FMath::RoundToInt64(StartFrameIn);
	}
	else if (bHasStartSeconds)
	{
		StartFrame = DisplayRate.AsFrameTime(StartSecondsIn).CeilToFrame().Value;
	}
	else if (bRangeBounded)
	{
		const FFrameTime Lower(UE::MovieScene::DiscreteInclusiveLower(PlaybackRange));
		StartFrame = FFrameRate::TransformTime(Lower, TickResolution, DisplayRate).CeilToFrame().Value;
	}
	else
	{
		return MCPError(TEXT("The sequence's playback range has no start; pass startFrame or startSeconds"));
	}

	int64 EndFrame = 0;
	if (bHasEndFrame)
	{
		EndFrame = FMath::RoundToInt64(EndFrameIn);
	}
	else if (bHasEndSeconds)
	{
		EndFrame = static_cast<int64>(DisplayRate.AsFrameTime(EndSecondsIn).CeilToFrame().Value) - 1;
	}
	else if (bRangeBounded)
	{
		const FFrameTime Upper(UE::MovieScene::DiscreteExclusiveUpper(PlaybackRange));
		EndFrame = static_cast<int64>(FFrameRate::TransformTime(Upper, TickResolution, DisplayRate).CeilToFrame().Value) - 1;
	}
	else
	{
		return MCPError(TEXT("The sequence's playback range has no end; pass endFrame or endSeconds"));
	}

	const int32 FrameStep = OptionalInt(Params, TEXT("frameStep"), 1);
	if (FrameStep < 1) return MCPError(TEXT("frameStep must be 1 or more"));
	if (EndFrame < StartFrame)
	{
		return MCPError(FString::Printf(TEXT("Empty range: endFrame %lld is before startFrame %lld (endFrame is inclusive)"), EndFrame, StartFrame));
	}
	if (StartFrame < MIN_int32 || EndFrame > MAX_int32) return MCPError(TEXT("Frame range is out of bounds"));

	const int64 RequestedFrames = (EndFrame - StartFrame) / FrameStep + 1;
	const int32 MaxFrames = FMath::Clamp(OptionalInt(Params, TEXT("maxFrames"), DefaultMaxFrames), 1, HardMaxFrames);
	if (RequestedFrames > MaxFrames)
	{
		return MCPError(FString::Printf(
			TEXT("The range asks for %lld frames and maxFrames is %d. Raise maxFrames (up to %d), raise frameStep, or split the range."),
			RequestedFrames, MaxFrames, HardMaxFrames));
	}

	// ── Output
	const int32 Width = FMath::Clamp(OptionalInt(Params, TEXT("width"), 1280), 16, 8192);
	const int32 Height = FMath::Clamp(OptionalInt(Params, TEXT("height"), 720), 16, 8192);
	const bool bFullyLoadTextures = OptionalBool(Params, TEXT("fullyLoadTextures"), true);

	const FString OutputDirIn = OptionalString(Params, TEXT("outputDir"),
		FString::Printf(TEXT("Saved/SequenceFrames/%s"), *Sequence->GetName()));
	FString OutputDir = FPaths::ConvertRelativePathToFull(UEMCP::SceneCapture::ResolveOutputPath(OutputDirIn));
	FPaths::NormalizeDirectoryName(OutputDir);
	if (!IFileManager::Get().MakeDirectory(*OutputDir, /*Tree*/ true))
	{
		return MCPError(FString::Printf(TEXT("Could not create outputDir '%s'"), *OutputDir));
	}

	REQUIRE_EDITOR_WORLD(World);

	// ── Camera: an explicit actor, a fixed transform, or the camera cuts.
	ECameraMode Mode = ECameraMode::Cuts;
	UCameraComponent* FixedCamera = nullptr;
	FVector FixedLocation = FVector::ZeroVector;
	FRotator FixedRotation = FRotator::ZeroRotator;
	float FixedFov = 90.0f;
	UMovieSceneTrack* CutTrack = nullptr;

	if (HasParam(Params, TEXT("cameraActorLabel")) || HasParam(Params, TEXT("cameraActorPath")))
	{
		Mode = ECameraMode::Actor;
		FMCPActorSelector CameraSel;
		CameraSel.LabelKey = TEXT("cameraActorLabel");
		CameraSel.PathKey = TEXT("cameraActorPath");
		TSharedPtr<FJsonValue> CameraErr;
		AActor* CameraActor = MCPResolveActor(World, Params, CameraErr, CameraSel);
		if (!CameraActor) return CameraErr;
		FixedCamera = CameraFromObject(CameraActor);
		if (!FixedCamera)
		{
			return MCPError(FString::Printf(TEXT("Actor '%s' has no CameraComponent"), *CameraActor->GetActorLabel()));
		}
	}
	else if (HasParam(Params, TEXT("location")))
	{
		Mode = ECameraMode::Fixed;
		FixedLocation = OptionalVec3(Params, TEXT("location"));
		FixedRotation = OptionalRotator(Params, TEXT("rotation"));
		FixedFov = static_cast<float>(OptionalNumber(Params, TEXT("fov"), 90.0));
		if (!FMath::IsFinite(FixedFov) || FixedFov <= 0.0f || FixedFov >= 180.0f)
		{
			return MCPError(TEXT("fov must be finite and between 0 and 180 degrees (exclusive)"));
		}
		if (FixedLocation.ContainsNaN() || FixedRotation.ContainsNaN())
		{
			return MCPError(TEXT("Capture camera location and rotation must be finite"));
		}
	}
	else
	{
		CutTrack = MovieScene->GetCameraCutTrack();
		if (!CutTrack || CutTrack->GetAllSections().Num() == 0)
		{
			return MCPError(TEXT("The sequence has no camera cuts. Pass cameraActorLabel/cameraActorPath, or location/rotation/fov for a fixed camera."));
		}
	}

	// ── Sequencer acts on whatever is open, so open this one first.
	if (!ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Sequence))
	{
		return MCPError(FString::Printf(TEXT("Failed to open '%s' in Sequencer."), *Sequence->GetPathName()));
	}
	if (ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence() != Sequence)
	{
		return MCPError(TEXT("Sequencer did not make the requested sequence current; close other sequences and retry."));
	}

	const bool bWasPlaying = ULevelSequenceEditorBlueprintLibrary::IsPlaying();
	const double PreviousDisplayFrame =
		ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition(EMovieSceneTimeUnit::DisplayRate).Frame.AsDecimal();
	// Every exit puts the playhead back where it was. Playback stays paused.
	ON_SCOPE_EXIT
	{
		UEMCP::SequencerPlayhead::ScrubAndEvaluate(Sequence, FFrameTime::FromDecimal(PreviousDisplayFrame));
	};

	const int32 RemovedStrayCaptures = UEMCP::SceneCapture::RemoveStrayCaptureActors(World);
	UEMCP::SceneCapture::FTransientCapture Rig;
	FString RigError;
	if (!Rig.Spawn(World, FixedLocation, FixedRotation, Width, Height, FixedFov, RigError)) return MCPError(RigError);
	USceneCaptureComponent2D* CaptureComp = Rig.GetComponent();

	// ── The loop
	TArray<TSharedPtr<FJsonValue>> Files;
	TArray<TSharedPtr<FJsonValue>> FrameRows;
	TArray<FString> CamerasUsed;
	int32 SkippedFrames = 0;
	int32 Overwrote = 0;
	bool bTruncated = false;
	int64 NextStartFrame = INDEX_NONE;
	double EvaluateTotal = 0.0, CaptureTotal = 0.0, ExportTotal = 0.0;
	const UCameraComponent* LastCamera = nullptr;
	bool bFirstCapture = true;
	const double StartedAt = FPlatformTime::Seconds();

	for (int64 Frame = StartFrame; Frame <= EndFrame; Frame += FrameStep)
	{
		if (Files.Num() > 0 && (FPlatformTime::Seconds() - StartedAt) > TimeBudgetSeconds)
		{
			bTruncated = true;
			NextStartFrame = Frame;
			break;
		}

		const double T0 = FPlatformTime::Seconds();
		const FFrameTime Display{FFrameNumber(static_cast<int32>(Frame))};
		UEMCP::SequencerPlayhead::ScrubAndEvaluate(Sequence, Display);
		const double T1 = FPlatformTime::Seconds();

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("frame"), static_cast<double>(Frame));
		Row->SetNumberField(TEXT("seconds"), DisplayRate.AsSeconds(Display));

		// The camera is read after evaluation, so an animated camera is where
		// the sequence puts it on this frame.
		UCameraComponent* Camera = FixedCamera;
		if (Mode == ECameraMode::Cuts)
		{
			const FFrameNumber Tick = FFrameRate::TransformTime(Display, DisplayRate, TickResolution).FloorToFrame();
			Camera = CameraCutCameraAt(CutTrack, Tick);
			if (!Camera)
			{
				++SkippedFrames;
				Row->SetBoolField(TEXT("skipped"), true);
				Row->SetStringField(TEXT("reason"), TEXT("No active camera cut with a resolvable camera at this frame"));
				FrameRows.Add(MakeShared<FJsonValueObject>(Row));
				continue;
			}
		}
		if (Camera)
		{
			FMinimalViewInfo View;
			Camera->GetCameraView(0.0f, View);
			const float ViewFov = (FMath::IsFinite(View.FOV) && View.FOV > 0.0f && View.FOV < 180.0f) ? View.FOV : 90.0f;
			Rig.SetView(View.Location, View.Rotation, ViewFov);
			// The camera's own post process (exposure, depth of field).
			CaptureComp->PostProcessSettings = View.PostProcessSettings;
			CaptureComp->PostProcessBlendWeight = View.PostProcessBlendWeight;
			const FString Name = CameraName(Camera);
			Row->SetStringField(TEXT("camera"), Name);
			CamerasUsed.AddUnique(Name);
		}

		// Stream textures on the first frame and at every cut.
		Rig.Capture(bFullyLoadTextures && (bFirstCapture || Camera != LastCamera));
		bFirstCapture = false;
		LastCamera = Camera;
		const double T2 = FPlatformTime::Seconds();

		const FString FilePath = FPaths::Combine(OutputDir,
			FString::Printf(TEXT("%s.%04d.png"), *Sequence->GetName(), static_cast<int32>(Frame)));
		if (IFileManager::Get().FileExists(*FilePath)) ++Overwrote;
		int64 Size = -1;
		FString ExportError;
		if (!Rig.ExportPng(FilePath, Size, ExportError))
		{
			return MCPError(FString::Printf(TEXT("%s (frame %lld; %d frame(s) already written to %s)"),
				*ExportError, Frame, Files.Num(), *OutputDir));
		}
		const double T3 = FPlatformTime::Seconds();

		EvaluateTotal += T1 - T0;
		CaptureTotal += T2 - T1;
		ExportTotal += T3 - T2;

		Files.Add(MakeShared<FJsonValueString>(FilePath));
		Row->SetStringField(TEXT("path"), FilePath);
		Row->SetNumberField(TEXT("sizeBytes"), static_cast<double>(Size));
		Row->SetNumberField(TEXT("evaluateMs"), (T1 - T0) * 1000.0);
		Row->SetNumberField(TEXT("captureMs"), (T2 - T1) * 1000.0);
		Row->SetNumberField(TEXT("exportMs"), (T3 - T2) * 1000.0);
		Row->SetNumberField(TEXT("totalMs"), (T3 - T0) * 1000.0);
		FrameRows.Add(MakeShared<FJsonValueObject>(Row));
	}
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartedAt;

	if (Files.Num() == 0)
	{
		return MCPError(FString::Printf(
			TEXT("No frame was written: none of the %d evaluated frame(s) had an active camera cut with a resolvable camera. ")
			TEXT("Pass cameraActorLabel/cameraActorPath, or location/rotation/fov for a fixed camera."),
			FrameRows.Num()));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("sequencePath"), Sequence->GetPathName());
	Result->SetStringField(TEXT("outputDir"), OutputDir);
	Result->SetStringField(TEXT("format"), TEXT("png"));
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	Result->SetStringField(TEXT("cameraMode"), CameraModeName(Mode));
	TArray<TSharedPtr<FJsonValue>> CameraArr;
	for (const FString& Name : CamerasUsed) CameraArr.Add(MakeShared<FJsonValueString>(Name));
	Result->SetArrayField(TEXT("cameras"), CameraArr);
	Result->SetNumberField(TEXT("startFrame"), static_cast<double>(StartFrame));
	Result->SetNumberField(TEXT("endFrame"), static_cast<double>(EndFrame));
	Result->SetNumberField(TEXT("frameStep"), FrameStep);
	Result->SetNumberField(TEXT("requestedFrames"), static_cast<double>(RequestedFrames));
	Result->SetNumberField(TEXT("renderedFrames"), Files.Num());
	Result->SetNumberField(TEXT("skippedFrames"), SkippedFrames);
	Result->SetArrayField(TEXT("files"), Files);
	Result->SetArrayField(TEXT("frames"), FrameRows);

	TSharedPtr<FJsonObject> DisplayRateObj = MakeShared<FJsonObject>();
	UEMCP::SequencerInfo::SetFrameRateFields(*DisplayRateObj, DisplayRate);
	Result->SetObjectField(TEXT("displayRate"), DisplayRateObj);

	TSharedPtr<FJsonObject> Timing = MakeShared<FJsonObject>();
	Timing->SetNumberField(TEXT("totalMs"), ElapsedSeconds * 1000.0);
	Timing->SetNumberField(TEXT("averageFrameMs"), ElapsedSeconds * 1000.0 / Files.Num());
	Timing->SetNumberField(TEXT("evaluateMs"), EvaluateTotal * 1000.0);
	Timing->SetNumberField(TEXT("captureMs"), CaptureTotal * 1000.0);
	Timing->SetNumberField(TEXT("exportMs"), ExportTotal * 1000.0);
	Result->SetObjectField(TEXT("timing"), Timing);

	Result->SetBoolField(TEXT("truncated"), bTruncated);
	if (bTruncated)
	{
		Result->SetNumberField(TEXT("nextStartFrame"), static_cast<double>(NextStartFrame));
		Result->SetStringField(TEXT("warning"), FString::Printf(
			TEXT("Stopped after %.0fs to stay inside the handler timeout. Call again with startFrame=%lld to continue."),
			TimeBudgetSeconds, NextStartFrame));
	}
	if (RemovedStrayCaptures > 0)
	{
		Result->SetNumberField(TEXT("strayCaptureActorsRemoved"), RemovedStrayCaptures);
	}
	Result->SetNumberField(TEXT("overwroteFiles"), Overwrote);
	Result->SetBoolField(TEXT("wasPlaying"), bWasPlaying);
	Result->SetNumberField(TEXT("playheadRestoredTo"), PreviousDisplayFrame);
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("No bridge call deletes a file from disk, so the written frames cannot be removed again. ")
		TEXT("The playhead is put back where it was and the capture rig is gone; playback is left paused."));
	return MCPResult(Result);
}
