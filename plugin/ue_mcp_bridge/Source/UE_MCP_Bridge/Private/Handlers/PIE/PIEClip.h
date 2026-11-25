#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Handlers/PIE/PIEScriptRunner.h"
#include "Handlers/PIE/PIEScriptValidation.h"

/**
 * Playtest clips (#2097): a playtest as a timeline on game time rather than an
 * ordered list of steps. People record into a clip and edit it; agents write and
 * edit the same clip as data. This header is the clip itself, its JSON and its
 * compact text form, its checks and edit operations, and the pieces of a run
 * that are plain data, so the automation tests drive all of it without PIE.
 *
 * Time is in frames at the clip's rate (frames per second of game time). A
 * frame number may be fractional when the clip's grid is free.
 *
 *   Input tracks   One per Input Action, for client 0. A track holds the raw value
 *                  that reached the action: after the key mapping's modifiers,
 *                  before the action's own modifiers and triggers. A Boolean
 *                  action is a row of notes; an axis is a curve of keys, and may
 *                  also carry notes, which hold a constant value over their span
 *                  and win over the curve there.
 *   Events         What arrange and act do in a step-list script: spawn, set,
 *                  call, place, console, capture, key, press, hold, tape, inject.
 *   Checks         Notes with a start, a length and a mode: by_end (true before
 *                  the note ends), throughout (true for the whole note) or hold
 *                  (playback stops advancing until it is true). A manual check is
 *                  a hold with PIE paused until it has a verdict.
 *   Regions        Named spans that tile the timeline without overlapping. Results
 *                  report against them.
 */
namespace MCPPIE
{
	constexpr int32 kClipFormatVersion = 1;

	enum class EClipValueType : uint8 { Bool, Axis1D, Axis2D, Axis3D };
	enum class EClipInterp : uint8 { Linear, Constant, Cubic };
	enum class EClipCheckMode : uint8 { ByEnd, Throughout, Hold };

	FString ClipValueTypeToString(EClipValueType Type);
	bool ParseClipValueType(const FString& Str, EClipValueType& Out);
	FString ClipInterpToString(EClipInterp Interp);
	bool ParseClipInterp(const FString& Str, EClipInterp& Out);
	FString ClipCheckModeToString(EClipCheckMode Mode);
	bool ParseClipCheckMode(const FString& Str, EClipCheckMode& Out);
	int32 ClipValueDims(EClipValueType Type);

	struct FClipNote
	{
		FString Id;
		double Start = 0.0;
		double Length = 1.0;
		// Ignored on a Boolean track. On an axis track, the value held over the note.
		FVector Value = FVector(1.0, 0.0, 0.0);

		double End() const { return Start + Length; }
	};

	struct FClipKey
	{
		FString Id;
		double Time = 0.0;
		FVector Value = FVector::ZeroVector;
		EClipInterp Interp = EClipInterp::Linear;
	};

	struct FClipTrack
	{
		// Input Action object path, /Game/Input/IA_Jump.IA_Jump.
		FString Action;
		EClipValueType Type = EClipValueType::Bool;
		TArray<FClipNote> Notes;
		TArray<FClipKey> Keys;
	};

	struct FClipEvent
	{
		FString Id;
		double At = 0.0;
		// An act entry, as a step's act takes it ({console: ...}, {place: {...}}, {key: {...}}), or an arrange
		// entry ({kind: spawn|set|call, ...}).
		TSharedPtr<FJsonObject> Do;

		bool IsArrange() const { return Do.IsValid() && Do->HasField(TEXT("kind")); }
	};

	// How a hold check resolved when its clip was recorded: the drift reference for later runs.
	struct FClipHoldRecord
	{
		bool bValid = false;
		double Frame = 0.0;
		double GameMs = 0.0;
		double Value = 0.0;
	};

	struct FClipCheck
	{
		FString Id;
		FString Name;
		double Start = 0.0;
		// by_end and throughout: the note's span. hold: how long it may wait, in frames of game time (0: the clip's
		// hold_timeout; a manual check with 0 waits for its verdict however long it takes).
		double Length = 0.0;
		EClipCheckMode Mode = EClipCheckMode::ByEnd;
		// A manual check is judged by a verdict, with PIE paused. Always a hold.
		bool bManual = false;
		FString Instruction;
		bool bAgentJudgeable = true;
		bool bResolveInPIE = false;
		FScriptExpect Expect;
		FClipHoldRecord Recorded;

		double End() const { return Start + Length; }
	};

	struct FClipRegion
	{
		FString Id;
		FString Name;
		double Start = 0.0;
		double End = 0.0;
	};

	// A take a later recording replaced: the notes and keys of the armed tracks over the range it covered. Kept as
	// history, never mixed back in.
	struct FClipTake
	{
		FString Id;
		FString RecordedAt;
		double Start = 0.0;
		double End = 0.0;
		TArray<FClipTrack> Tracks;
	};

	struct FClip
	{
		int32 Version = kClipFormatVersion;
		// Frames per second of game time: the grid, the fixed timestep of a replay, and the curve sample rate.
		int32 Rate = 60;
		// Frames. 0 means it ends at the last thing on it.
		double Length = 0.0;
		// frame (snap to whole frames), 1/N (snap to Nths of a second) or free.
		FString Grid = TEXT("frame");
		FString Description;
		FString Map;
		FString Profile;
		// Replays run at a fixed 1/rate timestep.
		bool bFixedTimestep = true;
		int64 Seed = 0;
		// Frames a hold check waits when its own length is 0.
		double HoldTimeout = 600.0;
		// Where the player pawn starts, teleported there before frame 0. Set by a recording from frame 0.
		bool bHasStartPose = false;
		FVector StartLocation = FVector::ZeroVector;
		FRotator StartRotation = FRotator::ZeroRotator;
		// A run id whose pawn track drift is measured against. Set by a recording.
		FString Reference;
		// Where a recording's frames landed, in clip frames, ascending. A fixed-timestep replay takes each frame's
		// delta from these over the range they cover, so every replayed frame lands where the recorded one did and
		// the input on it is the input recorded on it. Elsewhere a frame is 1/rate.
		TArray<double> FrameTimes;
		TArray<FClipRegion> Regions;
		TArray<FClipTrack> Tracks;
		TArray<FClipEvent> Events;
		TArray<FClipCheck> Checks;
		TArray<FClipTake> Takes;

		// Length, or the end of the last thing on the clip when Length is 0.
		double EffectiveLength() const;
		// The game seconds from Position to the next frame of a fixed-timestep replay: to the next recorded frame
		// time when FrameTimes covers Position, else 1/rate.
		double NextFrameDelta(double Position) const;
		// Position, or the recorded frame time within a thousandth of a frame of it.
		double SnapToFrameTime(double Position) const;
		FClipTrack* FindTrack(const FString& Action);
		const FClipTrack* FindTrack(const FString& Action) const;
	};

	// ── Issues ──────────────────────────────────────────────────────────

	struct FClipIssue
	{
		// The id of the note, key, check, event or region at fault, or a track's action. Empty for the clip as a whole.
		FString Id;
		// The field at fault, such as start, expect.value or action.
		FString Field;
		FString Message;
		TArray<FString> Candidates;
		// The clip cannot be saved with this issue. Every other issue is a warning.
		bool bBlocksSave = false;
	};

	TSharedRef<FJsonObject> ClipIssueToJson(const FClipIssue& Issue);
	TArray<TSharedPtr<FJsonValue>> ClipIssuesToJson(const TArray<FClipIssue>& Issues);

	// ── JSON and text ───────────────────────────────────────────────────

	// Parses the clip's JSON. Every problem that stops a field from being read is an issue that blocks saving;
	// missing ids are filled in.
	bool ClipFromJson(const TSharedPtr<FJsonObject>& Obj, FClip& Out, TArray<FClipIssue>& OutIssues);
	TSharedRef<FJsonObject> ClipToJson(const FClip& Clip);

	/**
	 * The compact text form, one thing per line, times in frames:
	 *
	 *   clip rate=60 length=600 grid=frame fixed_timestep=true seed=0 hold_timeout=600
	 *   map /Game/Maps/L_Arena
	 *   profile /Game/PIEStudio/Profiles/Door
	 *   description "The door opens"
	 *   start 0,0,92 0,90,0
	 *   reference clip_DoorOpens_20261125_100000
	 *   region r1 0 300 "Walk to the door"
	 *   track /Game/Input/IA_Jump.IA_Jump bool
	 *     note n1 30 10
	 *   track /Game/Input/IA_Move.IA_Move axis2d
	 *     key k1 0 0,1 constant
	 *     note n2 120 30 1,0
	 *   event e1 0 {"place":{"at":"Door","offset":[-150,0,0]}}
	 *   check c1 0 120 by_end pos_x increasing 100
	 *   check c2 120 300 hold Door.OpenAngle gt 80 name="Door open"
	 *   check c3 200 60 throughout speed2d lt 700
	 *   check c4 300 0 manual "Did the door open smoothly?" agent_judgeable=false
	 *   recorded c2 140 2333.3 81.5
	 *
	 * A value is comma-separated with no spaces. A string with spaces is quoted, with \" and \\ escapes.
	 * Takes are history and are not written.
	 */
	FString ClipToText(const FClip& Clip);
	bool ClipFromText(const FString& Text, FClip& Out, TArray<FClipIssue>& OutIssues);

	// ── Checks without PIE ──────────────────────────────────────────────

	// Ids filled in where missing (n1, k1, e1, c1, r1 by kind, after the highest already used).
	void AssignClipIds(FClip& Clip);
	// Structure: overlaps, order, ranges, duplicate ids, value shapes, events and expectations that do not parse.
	void ValidateClipStructure(const FClip& Clip, TArray<FClipIssue>& OutIssues);
	// What resolves against the level: each track's Input Action and value type, each check's value, and what the
	// events name, through the same resolvers script_validate uses. ActionTypes gives each found action's value type.
	void ValidateClipReferences(const FClip& Clip, const FScriptValidationContext& Ctx,
		const TFunction<bool(const FString& Action, EClipValueType& OutType)>& ActionType, TArray<FClipIssue>& OutIssues);
	bool ClipIssuesBlockSave(const TArray<FClipIssue>& Issues);

	// The step definition a check is judged as, and an event's step (its act or arrange), for reuse of the
	// script runner's evaluation and validation.
	FScriptStepDef ClipCheckToStep(const FClipCheck& Check);
	FScriptStepDef ClipEventToStep(const FClipEvent& Event);

	// ── Edits ───────────────────────────────────────────────────────────

	// Snaps a time to the clip's grid.
	double SnapClipTime(const FClip& Clip, double Frames);

	/**
	 * Applies note-level operations in order, all or none:
	 *
	 *   {op: add, kind: note, track, start, length, value?}       {op: add, kind: key, track, time, value, interp?}
	 *   {op: add, kind: track, action, type}                       {op: add, kind: check, start, length, mode, expect | manual}
	 *   {op: add, kind: event, at, do}                             {op: add, kind: region, name, start, end}
	 *   {op: move, id, to? | by?}                                  {op: trim, id, start?, length?, end?}
	 *   {op: set, id, ...fields}                                   {op: delete, id}   (a track by its action)
	 *   {op: quantize, grid?, track? | ids?}                       {op: set_clip, ...clip fields}
	 *
	 * Times land on the grid unless the clip's grid is free. OutChanged lists the ids added or changed, in order.
	 */
	bool ApplyClipEdits(FClip& Clip, const TArray<TSharedPtr<FJsonValue>>& Ops, TArray<FString>& OutChanged, TArray<FClipIssue>& OutIssues);

	// ── Playback values ─────────────────────────────────────────────────

	// The raw value of a track at a frame: a note covering it, else the curve, else zero. bOutActive is false where
	// nothing covers the frame, so the player feeds nothing there.
	FVector EvaluateClipTrack(const FClipTrack& Track, double Frame, bool& bOutActive);

	// ── Recording ───────────────────────────────────────────────────────

	// One frame of a take: the clip frame it fell on and each armed action's raw value.
	struct FClipTakeSample
	{
		double Frame = 0.0;
		TMap<FString, FVector> Values;
	};

	/**
	 * Replaces the armed tracks over [Start, End) with what was recorded there, the way a DAW comps: the old notes
	 * and keys in the range go into a new take in Clip.Takes, never mixed with the new ones. Boolean tracks become
	 * notes from the value's edges; axis tracks are resampled at the clip's rate and reduced to keys with FRichCurve.
	 */
	void CompClipTake(FClip& Clip, const TArray<FString>& ArmedActions, const TMap<FString, EClipValueType>& Types,
		double Start, double End, const TArray<FClipTakeSample>& Samples, const FString& TakeId, const FString& RecordedAt);

	// ── Runs ────────────────────────────────────────────────────────────

	enum class EClipCheckStatus : uint8 { Pending, Running, Passed, Failed, AwaitingVerdict, Skipped };
	FString ClipCheckStatusToString(EClipCheckStatus Status);

	struct FClipCheckRun
	{
		FClipCheck Def;
		EClipCheckStatus Status = EClipCheckStatus::Pending;
		// Clip frames and game ms from the run's first frame.
		double StartedFrame = -1.0;
		double EndedFrame = -1.0;
		double StartedMs = -1.0;
		double EndedMs = -1.0;
		bool bHasAtStart = false;
		double AtStart = 0.0;
		bool bHasLast = false;
		double Last = 0.0;
		int32 Samples = 0;
		FString Reason;
		// hold: how long playback stood still, and how it resolved against the recorded resolution.
		double HeldMs = 0.0;
		FString Note;
		FString VerdictSource;
		FString EvidenceNote;
		TArray<FString> EvidenceCaptures;
	};

	struct FClipRegionResult
	{
		FClipRegion Def;
		// passed, failed, pending (a check in it not settled) or empty (no checks in it).
		FString Status;
		TArray<FString> CheckIds;
		// Pawn drift against the clip's reference over the region, when it has one.
		bool bHasDrift = false;
		float MaxPositionCm = 0.f;
		float MaxVelocityCms = 0.f;
		double FirstOverFrame = -1.0;
		int32 FramesCompared = 0;
	};

	// The judgement of one check on one frame. Value is read when bObserved.
	struct FClipCheckFrame
	{
		double Frame = 0.0;
		double GameMs = 0.0;
		bool bObserved = false;
		double Value = 0.0;
	};

	// Moves a check on by one frame. A hold check is moved on by the run, which also decides when it times out.
	void AdvanceClipCheck(FClipCheckRun& Run, const FClipCheckFrame& F);
	// Settles a check still running or pending when the run ends.
	void SettleClipCheckAtEnd(FClipCheckRun& Run, const FString& Reason);
	TSharedRef<FJsonObject> ClipCheckRunToJson(const FClipCheckRun& Run);

	// Region results from the checks and the per-frame drift (Frame, position delta cm, velocity delta cm/s).
	struct FClipDriftSample
	{
		double Frame = 0.0;
		float PositionCm = 0.f;
		float VelocityCms = 0.f;
	};
	TArray<FClipRegionResult> SummarizeClipRegions(const FClip& Clip, const TArray<FClipCheckRun>& Checks,
		const TArray<FClipDriftSample>& Drift, float ThrPosCm, float ThrVelCms);
	// Whether a check starting at Start belongs to the region at RegionIndex: from its start up to its end, where the
	// next region begins, and up to and including the end of the last region. A run of one region and the region
	// results both decide membership here, so a check is never judged in one region and reported in another.
	bool ClipCheckStartsInRegion(const FClip& Clip, int32 RegionIndex, double Start);
}
