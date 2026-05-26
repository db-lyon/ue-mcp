#pragma once

#include "CoreMinimal.h"
#include "PIEFrameSampler.h"
#include "PIESequenceFormat.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;
class AActor;
class APlayerController;
class APawn;

/**
 * PIE replayer: drives a previously-recorded sequence.json (or an inline
 * step array) through the input injection pipeline and samples drift
 * against a source recording. Module-owned singleton; same lifecycle
 * shape as FPIEInputRecorder.
 */
namespace UEMCPPIE
{
	enum class EReplayerState : uint8
	{
		Idle,
		Armed,
		WaitingForPawn,
		Replaying,
		Completed
	};

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
		// Multi-client PIE: which local player to drive injections / sample
		// for drift. 0 = first (default), 1+ selects subsequent local players.
		int32 ClientId = 0;
		float ThrPosCm = 5.0f;
		float ThrRotDeg = 2.0f;
		float ThrVelCms = 25.0f;
		// Default tolerance applied to every tracked reflection path that
		// does not have an explicit entry in TrackedThresholds. <= 0 means
		// "don't trip frames_over_threshold from tracked-value deltas".
		float ThrTrackedDefault = 0.f;
		TMap<FString, float> TrackedThresholds;
		bool bEject = false;
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
		bool IsActive() const { return State != EReplayerState::Idle && State != EReplayerState::Completed; }

	private:
		void OnBeginPIE(bool bIsSimulating);
		void OnEndPIE(bool bIsSimulating);
		void OnEndFrame();
		FReplayerFinishResult FinaliseCurrent();
		void ExecutePendingSteps(double ElapsedMs);
		void ApplyFPSPin(UWorld* PIEWorld, int32 Hz);

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

		FString CurrentSourceCSV;
		FString CurrentDriftPath;
		TArray<FSourceFrame> SourceFrames;
		FPIEFrameSampler Sampler;

		double AttachTime = 0.0;
		int32 NextStepIndex = 0;
		int32 ExecutedSteps = 0;
		FString StartedAt;

		// Active hold lifecycles: maps "step <i> stop time ms" -> injection id.
		struct FHoldHandle { int32 StepIndex; double StopAtMs; FString InjectionId; };
		TArray<FHoldHandle> ActiveHolds;

		// Drift accumulators
		TArray<FDriftFrameEntry> DriftFrames;
		float MaxPosDriftCm = 0.f;
		uint64 MaxPosDriftFrame = 0;
		float MaxVelDriftCms = 0.f;
		float MaxRotDriftDeg = 0.f;
		int32 MontageMismatches = 0;
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
	};
}
