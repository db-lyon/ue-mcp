#pragma once

#include "CoreMinimal.h"
#include "Handlers/PIE/PIEArrangement.h"
#include "Handlers/PIE/PIEClipDrivers.h"
#include "Handlers/PIE/PIERawInputTap.h"
#include "Handlers/PIE/PIEMaxFPSPin.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;
class UInputAction;
class ULocalPlayer;

/**
 * Plays one arrangement, or one region of it, in one PIE session (#2146), and records a performance into a clip.
 *
 * Frame 0 is the first steady frame once the observer samples the editor's player. Before the actors tick on each
 * frame the player moves the position on by that frame's game time (a fixed 1/rate timestep while it plays), and
 * hands every lane at the new position to the driver of its track's performer (PIEClipDrivers.h): Input Action
 * lanes to a player's, move lanes to an actor's, action and value lanes to either. At the end of the frame it judges
 * the Checks track (FArrangementTimeline). A hold check stops the position until it is true or times out; a manual
 * check pauses PIE until it has a verdict. A play that started PIE ends it when it finishes or stops.
 *
 * A recording plays the punched clip up to the punch-in with the person's input locked out, then stops driving what
 * it records and reads what the performer does instead, until the punch-out or clip_record_stop. The take replaces
 * that range of the clip (PunchClipInput, PunchClipLanes). Without a punch it records from frame 0: a player's Input
 * Actions, or the route an actor walks.
 */
namespace MCPPIE
{
	enum class EArrangementState : uint8 { Idle, Starting, Running, AwaitingVerdict, Finished, Stopped, Error };
	FString ArrangementStateToString(EArrangementState State);

	struct FArrangementPlayConfig
	{
		FString RunId;
		FArrangementPlan Plan;
	};

	struct FClipRecordConfig
	{
		FClipPerformer Performer;
		double Rate = 60.0;
		// A punch-in: the clip whose range [PunchFrom, PunchTo) the take replaces.
		bool bPunch = false;
		FClip Clip;
		double PunchFrom = 0.0;
		double PunchTo = 0.0;
	};

	class FPIEArrangementPlayer
	{
	public:
		static FPIEArrangementPlayer& Get();

		void Init();
		void Shutdown();

		bool IsActive() const;
		bool IsRecording() const { return bRecording; }

		bool Play(const FArrangementPlayConfig& Cfg, bool& bOutStartedPIE, FString& OutError);
		bool Stop(const FString& RunId, const FString& Reason, FString& OutError);
		bool Verdict(const FString& RunId, const FString& CheckId, const FString& Verdict, const FString& Note, FString& OutError);
		/** { runId, state, frame?, checks, error?, pieStarted? }, or null when RunId is not the last run. */
		TSharedPtr<FJsonObject> Status(const FString& RunId) const;

		bool StartRecording(const FClipRecordConfig& Cfg, FString& OutError);
		/** Ends the recording and answers the clip with the take merged in. */
		bool StopRecording(FClip& OutClip, FString& OutError);

	private:
		void Begin(bool bStartPIE);
		void OnPreActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
		void OnEndFrame();
		void OnEndPIE(bool bIsSimulating);
		void StartClock(UWorld* World);
		void Drive(UWorld* World);
		void LaneError(const FString& LaneId, const FString& Error);
		FClipPerformerDriver* DriverFor(const FClipPerformer& Performer);
		bool ReadCheck(const FHubCheckRun& Check, double& OutValue);
		void PunchIn(UWorld* World);
		void SampleTake(UWorld* World);
		void FinishTake();
		void Finish(EArrangementState FinalState, const FString& Reason);
		ULocalPlayer* OwnLocalPlayer() const;
		void SetPaused(UWorld* World, bool bPaused);
		void ApplyFixedTimestep();
		void RestoreFixedTimestep();

		FString RunId;
		FArrangementTimeline Timeline;
		EArrangementState State = EArrangementState::Idle;
		FString Error;
		FString ObserverRunId;
		bool bStartedPIE = false;
		bool bClockStarted = false;
		bool bPaused = false;
		double LastGameSeconds = 0.0;
		int32 SteadyFrames = 0;
		// Move, action and value lanes: the next point or call each fires, by index into the plan's lanes.
		TArray<int32> LaneNext;
		TArray<FString> LaneErrors;

		// Recording.
		bool bRecording = false;
		bool bPunchedIn = false;
		bool bTakeDone = false;
		FClipRecordConfig RecordCfg;
		FClip Take;
		double PunchedOutAt = -1.0;
		TArray<FString> Armed;
		TMap<FString, EClipValueType> ArmedTypes;
		TMap<FString, TWeakObjectPtr<UInputAction>> ArmedActions;
		TArray<FClipTakeSample> Samples;
		TArray<FClipLaneRecording> LaneRecordings;

		TMap<FString, TUniquePtr<FClipPerformerDriver>> Drivers;
		struct FTriggerWatch { TWeakObjectPtr<const UInputAction> Action; bool bReleased = false; };
		TMap<FString, FTriggerWatch> TriggerWatches;
		FPIERawInputTaps Taps;
		FPIEMaxFPSPin FPSPin;
		bool bFixedApplied = false;
		bool bSavedUseFixed = false;
		double SavedFixedDelta = 0.0;

		FDelegateHandle PreActorTickHandle;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle EndPIEHandle;
	};
}
