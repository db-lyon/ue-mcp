#pragma once

#include "CoreMinimal.h"
#include "PIEFrameSampler.h"
#include "PIESequenceFormat.h"

class UWorld;

/**
 * PIE input recorder: opt-in arm-then-record. Module-owned singleton, hooked
 * to FEditorDelegates::BeginPIE / EndPIE and FCoreDelegates::OnEndFrame. The
 * recorder is dormant in Idle state; only after pie_record_arm does it bind
 * the end-of-frame tick.
 *
 * Recording artifacts land in <Saved>/MCPRecordings/<id>/:
 *   manifest.json, sequence.json, recording.csv
 *
 * State machine:
 *   Idle              No recording armed; no end-frame tick.
 *   Armed             Will start on the next BeginPIE.
 *   WaitingForPawn    BeginPIE fired; sampler waiting for the player pawn.
 *   Recording         Sampler attached; emit one row per end-of-frame.
 *                     Returns to Idle (and writes artifacts) on EndPIE
 *                     or pie_record_stop.
 */
namespace UEMCPPIE
{
	enum class ERecorderState : uint8
	{
		Idle,
		Armed,
		WaitingForPawn,
		Recording
	};

	struct FRecorderArmConfig
	{
		FString Id;                       // empty = auto-generate
		FString RecordingsRoot;           // empty = ProjectSavedDir/MCPRecordings
		TArray<FString> ActionPaths;      // empty = record every bound action
		TArray<FString> TrackedValuePaths;
		float AxisThreshold = 0.15f;
		int32 SampleHz = 60;
		int32 PinFPS = 60;                // 0 to skip the t.MaxFPS pin
		bool bCapturePawnState = true;
		bool bCaptureMontage = true;
		int64 RngSeed = 0;                // 0 = auto-generate
		bool bUserSuppliedSeed = false;
		int32 RunGapFrames = 6;           // sequence run-extraction gap tolerance
	};

	struct FRecorderStatus
	{
		ERecorderState State = ERecorderState::Idle;
		FString Id;
		FString RecordingDir;
		int32 CurrentFrame = 0;
		double ElapsedSeconds = 0.0;
		int32 TrackedActionCount = 0;
	};

	struct FRecorderFinishResult
	{
		bool bSuccess = false;
		FString Error;
		FString Id;
		FString RecordingDir;
		FString ManifestPath;
		FString CSVPath;
		FString SequencePath;
		int32 TotalFrames = 0;
		double DurationSeconds = 0.0;
		TArray<FString> DiscoveredActions;
		TArray<FMarker> Markers;
	};

	class FPIEInputRecorder
	{
	public:
		static FPIEInputRecorder& Get();

		// Lifecycle. Init binds the editor delegates; Shutdown clears them.
		void Init();
		void Shutdown();

		// Arm a recording for the next BeginPIE (or immediately if PIE is
		// already running). Returns true on success and writes a short
		// description into OutMessage; on failure (already recording, etc.)
		// returns false with OutError populated.
		bool Arm(const FRecorderArmConfig& Cfg, FString& OutError, FString& OutMessage);
		bool Disarm(FString& OutError);

		// Force-finalise the in-flight recording even if EndPIE has not
		// fired. Returns the same shape EndPIE would have produced.
		FRecorderFinishResult ForceStop();

		// Insert a marker into the current frame's edge events. Returns
		// false (and writes "not recording" diagnostic) when idle.
		bool Mark(const FString& Label, FRecorderStatus& OutStatus);

		FRecorderStatus GetStatus() const;

		bool IsActive() const { return State != ERecorderState::Idle; }

	private:
		void OnBeginPIE(bool bIsSimulating);
		void OnEndPIE(bool bIsSimulating);
		void OnEndFrame();

		FRecorderFinishResult FinaliseCurrent();
		void ApplyFPSPin(UWorld* PIEWorld);

		// Run-extraction encoder: builds sequence.json steps from the row buffer.
		void BuildSequenceSteps(FSequence& OutSequence) const;

		FRecorderArmConfig Pending;
		bool bArmed = false;
		ERecorderState State = ERecorderState::Idle;
		FPIEFrameSampler Sampler;
		FString CurrentId;
		FString CurrentDir;
		FString CSVHeader;
		FString CSVBody;
		FCSVHeader CSVHdr;
		TArray<FCSVRow> Rows;
		TArray<FMarker> Markers;
		double StartTime = 0.0;
		FString StartedAt;

		FDelegateHandle BeginPIEHandle;
		FDelegateHandle EndPIEHandle;
		FDelegateHandle OnEndFrameHandle;
		bool bEndFrameBound = false;
	};
}
