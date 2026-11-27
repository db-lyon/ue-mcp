#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Handlers/PIE/PIEClip.h"
#include "Templates/Function.h"

/**
 * An arrangement as the plugin plays it (#2146), and its Checks track, as plain data so the automation tests drive
 * all of it without PIE. The daemon hands over the arrangement with the clips it places (pie arrangement_play); the
 * plugin reads no hub file.
 *
 * CompileArrangement places every clip instance on its track's timeline: each lane is moved to the instance's frame
 * and scaled from the clip's rate to the arrangement's, and takes the track's performer. A region plays from frame 0
 * to the region's end and judges only the checks that start inside it.
 *
 * FArrangementTimeline moves a play on frame by frame. A check judges from its start:
 *   by_end      passes once the condition holds on any frame before its end, fails at its end.
 *   throughout  fails on the first frame it does not hold, passes at its end.
 *   hold        playback stands at its start until the condition holds; it fails after its length (or its
 *               timeout when the length is 0) of game time held.
 *   manual      playback stands at its start, with PIE paused, until it has a verdict.
 */
namespace MCPPIE
{
	// Triggered waits for a person to fire an Input Action; the others read a value.
	enum class EHubExpectOp : uint8 { Decreasing, Increasing, Eq, Gt, Lt, Visible, Exists, Triggered };
	FString HubExpectOpToString(EHubExpectOp Op);

	/** What a check reads: a value, an op and a target. Subject names an actor; the run's player when empty. */
	struct FHubExpect
	{
		FString Value;
		EHubExpectOp Op = EHubExpectOp::Eq;
		bool bHasTarget = false;
		double Target = 0.0;
		FString Subject;

		// The value as the observer tracks it: Subject.Value, or Value alone.
		FString Path() const;
		FString Describe() const;
	};

	// {value, op, target?, subject?}. False with why when it does not read.
	bool ParseHubExpect(const TSharedPtr<FJsonObject>& Obj, FHubExpect& Out, FString& OutError);
	// Whether Current satisfies the expectation. AtStart is the baseline for decreasing and increasing.
	bool EvaluateHubExpect(const FHubExpect& Expect, double AtStart, double Current);

	enum class EHubCheckMode : uint8 { ByEnd, Throughout, Hold, Manual };
	enum class EHubCheckStatus : uint8 { Pending, Running, Passed, Failed, AwaitingVerdict, Skipped };
	FString HubCheckModeToString(EHubCheckMode Mode);
	bool ParseHubCheckMode(const FString& Str, EHubCheckMode& Out);
	FString HubCheckStatusToString(EHubCheckStatus Status);

	struct FHubCheck
	{
		FString Id;
		FString Title;
		double At = 0.0;
		double Length = 0.0;
		EHubCheckMode Mode = EHubCheckMode::ByEnd;
		FHubExpect Expect;
		FString Instruction;
		bool bPersonOnly = false;
		// A hold of length 0: the most frames it waits.
		double Timeout = 0.0;

		double End() const { return At + Length; }
		// The frames of game time a hold waits before it fails: its length, its timeout, else ten seconds.
		double HoldLimit(double Rate) const;
	};

	bool HubCheckFromJson(const TSharedPtr<FJsonObject>& Obj, int32 Index, FHubCheck& Out, FString& OutError);

	struct FHubCheckRun
	{
		FHubCheck Def;
		EHubCheckStatus Status = EHubCheckStatus::Pending;
		// The frame it settled on, or -1.
		double Frame = -1.0;
		FString Message;
		bool bHasAtStart = false;
		double AtStart = 0.0;
		bool bHasLast = false;
		double Last = 0.0;
		// hold: the frames of game time playback has stood at it.
		double HeldFrames = 0.0;
		FString Note;

		bool IsSettled() const { return Status == EHubCheckStatus::Passed || Status == EHubCheckStatus::Failed || Status == EHubCheckStatus::Skipped; }
	};

	TSharedRef<FJsonObject> HubCheckRunToJson(const FHubCheckRun& Run);

	/** An input lane placed on the arrangement's timeline, live from From to To. */
	struct FPlacedInput
	{
		FClipTrack Track;
		double From = 0.0;
		double To = 0.0;
	};

	struct FArrangementPlan
	{
		FString Id;
		// The map to play in; empty plays the open one.
		FString Level;
		double Rate = 60.0;
		FString RegionId;
		// The frame playback ends on: the region's end, or the arrangement's last instance, check or region.
		double End = 0.0;
		TArray<FPlacedInput> Input;
		// Move, action and value lanes, each with its track's performer, ids <track>/<instance>/<lane>.
		TArray<FClipLane> Lanes;
		TArray<FHubCheckRun> Checks;
		// Every performer the tracks drive.
		TArray<FClipPerformer> Performers;
	};

	/**
	 * Places the clips of an arrangement's audible tracks (none muted, only the soloed when any is) on its timeline.
	 * Clips is { <id>: clip JSON }. Region, when not empty, names the region to play. False with why when the
	 * arrangement does not play: an instance's clip is missing or is for another performer kind, a check or region
	 * does not read, or the region is unknown.
	 */
	bool CompileArrangement(const TSharedPtr<FJsonObject>& Arrangement, const TSharedPtr<FJsonObject>& Clips,
		const FString& Region, FArrangementPlan& Out, FString& OutError);

	/** A play moving on frame by frame, without PIE. */
	struct FArrangementTimeline
	{
		FArrangementPlan Plan;
		double Position = 0.0;
		// The hold or manual check playback is standing at, or INDEX_NONE.
		int32 Holding = INDEX_NONE;

		/**
		 * Moves the position on by Frames of game time, stopping at the start of the first hold or manual check that
		 * has not settled. While standing at one the position does not move, and the time counts as held.
		 */
		void Advance(double Frames);
		/** Judges every check at the position. Read gives a check's value, false when it could not be read. */
		void Judge(TFunctionRef<bool(const FHubCheckRun&, double&)> Read);
		/** A manual check waits for its verdict. */
		bool AwaitingVerdict() const;
		/** The position reached the end and nothing holds it. */
		bool Done() const;
		/** Settles a manual check awaiting its verdict: pass or fail, with a note. */
		bool Verdict(const FString& CheckId, const FString& Verdict, const FString& Note, FString& OutError);
		/** Settles what is still open when the play ends: running and awaiting checks fail, pending ones are skipped. */
		void SettleAtEnd(const FString& Reason);
	};
}
