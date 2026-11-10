#pragma once

#include "CoreMinimal.h"
#include "Handlers/PIE/PIEFrameSampler.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Handlers/PIE/PIEMaxFPSPin.h"
#include "Handlers/PIE/PIEUIPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;
class AActor;
class APlayerController;
class ULocalPlayer;
class APawn;
/**
 * PIE replayer: drives a previously-recorded sequence.json (or an inline
 * step array) through the input injection pipeline and samples drift
 * against a source recording. Module-owned singleton; same lifecycle
 * shape as FPIEInputRecorder.
 */
namespace MCPPIE
{
	enum class EReplayerState : uint8
	{
		Idle,
		Armed,
		WaitingForPawn,
		Replaying,
		Completed
	};

	// replay start_when (#1657): hold the steps until the player can act, so
	// an intro whose length varies between runs does not shift the input.
	struct FReplayStartGate
	{
		bool bEnabled = false;
		// Pawn speed at most MaxSpeedCms, and on the ground for a Character.
		bool bPawnAtRest = true;
		float MaxSpeedCms = 5.f;
		// How long the condition must hold before the gate opens.
		int32 HoldMs = 500;
		// A line containing this must have been logged in the session.
		FString LogContains;
		int32 TimeoutMs = 60000;
	};

	// Where the source recording's own gate opened, in ms from its first row:
	// the end of HoldMs at rest in the rest run that leads into the first
	// input (OnsetMs), capped at OnsetMs. -1 when the pawn was moving at the
	// onset or no row precedes it. TimesMs and Speeds are per source row.
	double FindSourceRestAnchorMs(const TArray<double>& TimesMs, const TArray<float>& Speeds,
		double OnsetMs, float MaxSpeedCms, int32 HoldMs);

	// Parses a start_when object. Unknown fields and a gate with nothing to
	// wait on are refused.
	bool ParseReplayStartGate(const TSharedPtr<FJsonObject>& Obj, FReplayStartGate& Out, FString& OutError);

	struct FReplayerArmConfig
	{
		FString SourceRecordingId;
		FString SourceDir;            // empty -> default root + SourceRecordingId
		FString SequencePath;         // explicit path (alternative to id)
		FSequence InlineSequence;     // honoured when InlineSequenceProvided
		bool bInlineSequenceProvided = false;
		int32 SettleMs = -1;          // -1 = use sequence's own
		int32 PinFPS = -1;            // -1 = use sequence sample_hz; 0 = skip
		bool bApplyRngSeed = true;
		bool bRecordDrift = true;
		bool bAutoStopPIE = false;
		// Monitor mode: skip input injection / step execution but keep drift
		// sampling running. Lets a human play manually against a reference
		// recording and watch divergence live via pie_replay_status.
		bool bMonitor = false;
		// Per-frame viewport capture. 0 = off, N>0 = grab a screenshot every
		// Nth sampled frame. Files land in <recording_dir>/frames/, or a
		// fallback under Saved/Screenshots/MCPReplay/ for inline replays.
		// Document the ffmpeg incantation to assemble a GIF/MP4 from the
		// resulting PNG sequence.
		int32 CaptureFrameEvery = 0;
		// Also encode an animated GIF from the captured frames on finish. Off by
		// default: a vision model cannot parse GIF animation, so the frames + the
		// contact sheet are the useful artifacts. GIF is for human eyeballing.
		bool bEncodeGif = false;
		// Selected PIE world and local-player index. PIEInstance >= 0 selects
		// exact FWorldContext::PIEInstance; INDEX_NONE prefers active PlayWorld
		// when eligible, then first eligible PIE/Game world.
		int32 ClientId = 0;
		int32 PIEInstance = INDEX_NONE;
		float ThrPosCm = 5.0f;
		float ThrRotDeg = 2.0f;
		float ThrVelCms = 25.0f;
		// Default tolerance applied to every tracked reflection path that
		// does not have an explicit entry in TrackedThresholds. <= 0 means
		// "don't trip frames_over_threshold from tracked-value deltas".
		float ThrTrackedDefault = 0.f;
		TMap<FString, float> TrackedThresholds;
		bool bEject = false;
		// Block the user's own keyboard, mouse and gamepad from the PIE game
		// while the replay runs, so only the recording drives the player.
		// Ignored in monitor mode, where the user plays on purpose.
		bool bLockInput = false;
		float TimeScale = 1.0f;
		// Opt-in stronger reproducibility (item X2): drive the engine at a fixed
		// delta time (1/pin_fps) during replay instead of just capping FPS. Makes
		// runs "more reproducible", never fully deterministic (Chaos/async still
		// vary). Restored on finish.
		bool bFixedTimestep = false;
		FReplayStartGate StartGate;
	};

	struct FReplayerStatus
	{
		EReplayerState State = EReplayerState::Idle;
		FString SourceRecordingId;
		int32 CurrentStep = 0;
		int32 TotalSteps = 0;
		double ElapsedSeconds = 0.0;
		float MaxPositionDriftCm = 0.f;
		float MaxVelocityDriftCms = 0.f;
		int32 FramesCaptured = 0;
		// True while a PIE session is live. Lets an unattended caller that
		// kicked off replay_run poll until PIE has torn itself down.
		bool bPIEActive = false;
		FString LastError;
		// Snapshot of the most recent finished replay, retained after the
		// replayer returns to Idle so a poller can read the outcome (drift
		// report path + peak drift) without racing the finalize.
		bool bHasLastResult = false;
		FString LastDriftReportPath;
		FString LastGifPath;
		float LastMaxPositionDriftCm = 0.f;
		float LastMaxVelocityDriftCms = 0.f;
		int32 LastFramesCompared = 0;
		FString LastFrameDir;
		int32 LastFrameCount = 0;
		FString LastContactSheetPath;
		FString LastReplayCSVPath;
		// While waiting: what the replay is waiting on (no player target, no
		// pawn, or the start_when gate).
		FString WaitingFor;
	};

	struct FLiveReplaySnapshot
	{
		EReplayerState State = EReplayerState::Idle;
		FString SourceRecordingId;
		int32 CurrentStep = 0;
		int32 TotalSteps = 0;
		double ElapsedSeconds = 0.0;
		int32 FramesCompared = 0;
		float MaxPositionDriftCm = 0.f;
		float MaxVelocityDriftCms = 0.f;
		float MaxRotationDriftDeg = 0.f;
		int32 MontageMismatches = 0;
		TMap<FString, float> MaxTrackedDeltas;
	};

	struct FReplayerFinishResult
	{
		bool bSuccess = false;
		FString Error;
		FString DriftReportPath;
		FDriftReport Drift;
		int32 ExecutedSteps = 0;
		int32 FramesCaptured = 0;
		FString CaptureDir;
		FString GifPath;
		// Kept frames + the labeled contact sheet (item 1b). FrameDir holds the
		// per-frame JPEGs (frame_NNNNN.jpg); ContactSheetPath is a single grid
		// montage the agent reads at a glance.
		FString FrameDir;
		int32 FrameCount = 0;
		FString ContactSheetPath;
	};

	class FPIEInputReplayer
	{
	public:
		static FPIEInputReplayer& Get();

		void Init();
		void Shutdown();

		bool Arm(const FReplayerArmConfig& Cfg, FString& OutError, FString& OutMessage);
		bool Disarm(FString& OutError);
		FReplayerFinishResult ForceStop();
		FReplayerStatus GetStatus() const;
		FLiveReplaySnapshot GetLiveSnapshot() const;
		bool IsActive() const { return State != EReplayerState::Idle && State != EReplayerState::Completed; }

	private:
		void OnBeginPIE(bool bIsSimulating);
		void OnEndPIE(bool bIsSimulating);
		void OnEndFrame();
		FReplayerFinishResult FinaliseCurrent();
		void ExecutePendingSteps(double ElapsedMs);
		// ui_input steps run on their own cursor and clock (real time, so a
		// paused game still plays its menu input).
		void ExecutePendingUISteps(double UIElapsedMs);
		void ApplyFPSPin(UWorld* PIEWorld, int32 Hz);
		bool UpdateStartGate(UWorld* PIEWorld, double Now, FString& OutBlocker);

		// Source recording's per-frame pawn state for drift comparison.
		// Sparse rows: only fields needed for drift; populated by ReadSourceCSV.
		struct FSourceFrame
		{
			uint64 Frame = 0;
			double Time = 0.0;
			FVector PawnLocation = FVector::ZeroVector;
			FRotator PawnRotation = FRotator::ZeroRotator;
			FVector PawnVelocity = FVector::ZeroVector;
			float Speed2D = 0.f;
			FString MontageSection;
			TMap<FString, double> TrackedValues;
		};
		bool LoadSourceFrames(const FString& CSVPath, FString& OutError);
		TArray<FString> SourceTrackedPaths;

		FReplayerArmConfig Pending;
		FSequence ActiveSequence;
		EReplayerState State = EReplayerState::Idle;
		bool bArmed = false;

		// Retained outcome of the last finished replay (see FReplayerStatus).
		FReplayerFinishResult LastFinish;
		bool bHasLastFinish = false;
		// Set once we have asked the editor to end PIE for an auto-stop replay,
		// so OnEndFrame doesn't spam RequestEndPlayMap every frame while the
		// session winds down.
		bool bEndPIERequested = false;

		FString CurrentSourceCSV;
		FString CurrentDriftPath;
		TArray<FSourceFrame> SourceFrames;
		FPIEFrameSampler Sampler;
		FPIEMaxFPSPin FPSPin;
		TWeakObjectPtr<UWorld> TargetWorld;
		TWeakObjectPtr<APlayerController> TargetPlayerController;
		TWeakObjectPtr<ULocalPlayer> TargetLocalPlayer;
		int32 ResolvedPIEInstance = INDEX_NONE;
		bool bTargetResolved = false;
		double TargetResolveStartSeconds = 0.0;
		FString TargetResolveError;

		double AttachTime = 0.0;
		// Time alignment for drift (#1690). Replay steps fire SettleMs after
		// attach, so replay time minus the settle is compared with the source
		// row at the same offset from the source's first row.
		int32 SourceCursor = 0;
		bool bSourceExhausted = false;
		uint64 ReplayFrameIndex = 0;
		double AlignOffsetMs = 0.0;
		double SourceInputOnsetMs = -1.0;
		double ReplayInputOnsetMs = -1.0;
		// Step clock: ms since attach minus StepOffsetMs. Starts after the
		// settle delay, or when the start gate opens.
		bool bStepClockStarted = false;
		double StepOffsetMs = 0.0;
		// Start gate state.
		double SourceGateMs = 0.0;
		FString GateAnchor;
		double GateRestSince = -1.0;
		bool bGateLogSeen = false;
		double GateLogCheckedAt = -1.0;
		double GateOpenedMs = -1.0;
		FString GateBlocker;
		FString GateError;
		TArray<FCSVRow> ReplayRows;
		int32 NextStepIndex = 0;
		int32 ExecutedSteps = 0;
		// ui_input playback (#1822).
		FPIEUIPointerPlayer UIPlayer;
		int32 NextUIStepIndex = 0;
		int32 UIStepErrors = 0;
		double AttachRealTime = 0.0;
		double UIClockStartRealMs = 0.0;
		double UIClockStartStepMs = 0.0;
		FString StartedAt;

		// Active hold lifecycles: maps "step <i> stop time ms" -> injection id.
		struct FHoldHandle { int32 StepIndex; double StopAtMs; FString InjectionId; };
		TArray<FHoldHandle> ActiveHolds;
		TArray<FString> ActiveTapes;

		// Drift accumulators
		TArray<FDriftFrameEntry> DriftFrames;
		float MaxPosDriftCm = 0.f;
		uint64 MaxPosDriftFrame = 0;
		float MaxVelDriftCms = 0.f;
		float MaxRotDriftDeg = 0.f;
		int32 MontageMismatches = 0;
		// First frame/channel to cross threshold (item 1c). Captured live so the
		// exact source/replay values at the crossing are available for the lead.
		FDriftDivergence FirstDivergence;
		int32 FramesCompared = 0;
		int32 FramesMissingInReplay = 0;
		TMap<FString, float> MaxTrackedDeltas;

		// Actor-scoped tracking (tracked.jsonl) loaded from source recording.
		TArray<FTrackedActorRow> SourceActorRows;
		TArray<FString> SourceActorIds;
		TMap<FString, TWeakObjectPtr<AActor>> ReplayActorCache;
		TMap<FString, FActorDrift> ActorDriftAccum;
		int32 FramesCaptured = 0;
		uint64 CaptureFrameCounter = 0;
		FString CaptureDir;
		TSharedPtr<class FPIEViewportCapture> ViewportCapture;

		// Eject state: controller unpossesses pawn on replay start.
		bool bEjected = false;
		TWeakObjectPtr<APlayerController> EjectedPC;
		TWeakObjectPtr<APawn> EjectedPawn;
		void EjectPlayer(UWorld* PIEWorld);
		void RepossessPlayer();
		void TeleportPawnToStart(APawn* Pawn);

		FDelegateHandle BeginPIEHandle;
		FDelegateHandle EndPIEHandle;
		FDelegateHandle OnEndFrameHandle;
		bool bEndFrameBound = false;

		// Fixed-timestep save/restore (item X2).
		bool bFixedTimestepApplied = false;
		bool bSavedUseFixedTimestep = false;
		double bSavedFixedDeltaTime = 0.0;
		void ApplyFixedTimestep(int32 Hz);
		void RestoreFixedTimestep();
	};
}
