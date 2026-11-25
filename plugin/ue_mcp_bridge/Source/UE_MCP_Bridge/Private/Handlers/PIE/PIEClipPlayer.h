#pragma once

#include "CoreMinimal.h"
#include "Handlers/PIE/PIEClip.h"
#include "Handlers/PIE/PIERawInputTap.h"
#include "Handlers/PIE/PIEStepActions.h"
#include "Handlers/PIE/PIEMaxFPSPin.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;
class UInputAction;
class ULocalPlayer;

/**
 * Plays a playtest clip in PIE on game time (#2097), and records into it.
 *
 * Frame 0 is the first frame the observer samples the player. Before the actors tick on each frame the player
 * moves the clip's position on by that frame's game time, fires the events it passed, and feeds every input track's
 * raw value at the new position to its Input Action with InjectInputForAction, so the action's own modifiers and
 * triggers run once and the value is processed on the frame it belongs to. At the end of the frame it judges the
 * check notes against what the observer read. A hold check stops the position at its frame until it is true, or
 * until its timeout; a manual check pauses PIE until it has a verdict.
 *
 * With a fixed timestep (the clip's default) every frame is exactly 1/rate of game time.
 *
 * Recording plays the clip up to the punch-in frame with the player's own input locked out, then stops feeding the
 * armed tracks and reads what the person does to them instead, until the punch-out frame or clip_record_stop. The
 * take replaces the armed tracks over that range (FClip::CompClipTake) and the clip is saved.
 */
namespace MCPPIE
{
	enum class EClipRunState : uint8 { Idle, Armed, Running, Finished, Stopped };
	FString ClipRunStateToString(EClipRunState State);

	struct FClipPlayConfig
	{
		FClip Clip;
		// Asset path, or empty for an inline clip.
		FString ClipPath;
		FString Name;
		// Judge only this region's checks and stop at its end. Playback still starts at frame 0.
		FString RegionId;
		bool bStartPIE = false;
		bool bKeepPIE = false;
		// -1 the clip's setting, 0 variable, 1 fixed at 1/rate.
		int32 FixedTimestep = -1;
		// Keep the person's keyboard, mouse and gamepad out of PIE while the clip plays.
		bool bLockInput = true;
		int32 PIEInstance = INDEX_NONE;
		int32 ClientId = 0;

		bool bRecord = false;
		// Input Actions the take records. Empty records every action the player has a mapping for.
		TArray<FString> Armed;
		double PunchIn = 0.0;
		// -1 records until clip_record_stop or PIE ends.
		double PunchOut = -1.0;
	};

	struct FClipFiredEvent
	{
		FString Id;
		double Frame = 0.0;
		double GameMs = 0.0;
		bool bOk = true;
		FString Error;
	};

	struct FClipRun
	{
		FString RunId;
		FString ClipPath;
		FString ClipName;
		FClip Clip;
		FString RegionId;
		EClipRunState State = EClipRunState::Idle;
		FString StopReason;
		FString WaitingFor;
		FString StartedAt;
		FString EndedAt;
		FString OutputDir;
		FString ResultsPath;
		bool bFixedTimestep = true;
		bool bClockStarted = false;
		double StartGameSeconds = 0.0;
		double LastGameSeconds = 0.0;
		double Position = 0.0;
		double GameMs = 0.0;
		double HeldMs = 0.0;
		int32 Frames = 0;
		// The hold or manual check playback is standing at, or INDEX_NONE.
		int32 HoldingCheck = INDEX_NONE;
		double HoldStartMs = 0.0;
		bool bPausedForVerdict = false;
		TArray<FClipCheckRun> Checks;
		TArray<FClipFiredEvent> FiredEvents;
		int32 NextEvent = 0;
		// Pawn per frame: frame, location, velocity. Written as track.json; a later run measures drift against it.
		struct FPawnSample { double Frame; FVector Location; FVector Velocity; };
		TArray<FPawnSample> PawnTrack;
		TArray<FClipDriftSample> Drift;
		bool bHasReference = false;
		FString ReferenceError;
		bool bStartedPIE = false;
		bool bKeepPIE = false;

		// Recording.
		bool bRecording = false;
		bool bPunchedIn = false;
		double PunchIn = 0.0;
		double PunchOut = -1.0;
		double PunchedOutAt = -1.0;
		TArray<FString> Armed;
		TMap<FString, EClipValueType> ArmedTypes;
		TArray<FClipTakeSample> Samples;
		FString TakeId;
		bool bTakeSaved = false;
		FString TakeError;
	};

	class FPIEClipPlayer
	{
	public:
		static FPIEClipPlayer& Get();

		void Init();
		void Shutdown();

		bool IsActive() const;
		bool Start(const FClipPlayConfig& Cfg, FString& OutError, FString& OutMessage);
		bool Stop(const FString& Reason, FString& OutError);
		/** Ends a recording at the current frame and saves the take; the run then plays on to the clip's end. */
		bool StopRecording(FString& OutError);
		bool Verdict(const FString& CheckId, const FString& Verdict, const FString& Note, const FString& Source,
			const FString& EvidenceNote, const TArray<FString>& Captures, FString& OutError);

		const FClipRun* GetRun() const { return bHasRun ? &Run : nullptr; }
		TSharedRef<FJsonObject> RunToJson(bool bLive) const;

	private:
		void OnPreActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
		void OnEndFrame();
		void OnEndPIE(bool bIsSimulating);
		void StartClock(UWorld* World);
		void FireEvents(UWorld* World, double UpTo);
		void FeedInput(UWorld* World);
		void FeedRemoteLane(const FClipTrack& Track, bool bActive, const FVector& Value);
		void StopRemoteLanes();
		void JudgeChecks(UWorld* World);
		bool ReadCheckValue(int32 Index, double& OutValue);
		void SamplePawnAndDrift(UWorld* World);
		void SampleTake(UWorld* World);
		void PunchIn(UWorld* World);
		void FinishTake();
		void Finish(EClipRunState FinalState, const FString& Reason);
		bool WriteResults() const;
		void LoadReference();
		ULocalPlayer* TargetLocalPlayer() const;
		double HoldFrame(const FClipCheck& Check) const;
		void ApplyFixedTimestep();
		void RestoreFixedTimestep();
		void SetPaused(UWorld* World, bool bPaused);

		FClipRun Run;
		bool bHasRun = false;
		FClipPlayConfig Config;
		TArray<FClipRun::FPawnSample> Reference;
		TMap<FString, TWeakObjectPtr<UInputAction>> Actions;
		struct FTriggerWatch { TWeakObjectPtr<const UInputAction> Action; bool bReleased = false; };
		TMap<int32, FTriggerWatch> TriggerWatches;
		FPIERawInputTaps Taps;
		// Remote clients' lanes, as holds on those clients, by "<client>|<action>".
		struct FRemoteLaneHold { FString Id; FVector Value; };
		TMap<FString, FRemoteLaneHold> RemoteHolds;
		TSet<FString> RemoteLaneErrors;
		TSharedPtr<FStepActionRunner> Runner;

		FPIEMaxFPSPin FPSPin;
		bool bFixedApplied = false;
		bool bSavedUseFixed = false;
		double SavedFixedDelta = 0.0;

		FDelegateHandle PreActorTickHandle;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle EndPIEHandle;
	};
}
