#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * A clip (#2146): a reusable performance for one performer kind, in frames at its rate. A player clip holds Input
 * Action lanes (`input`); an actor clip holds move, action and value lanes (`lanes`); a player clip may also hold
 * action and value lanes, which act on the pawn it possesses. A clip never asserts: checks live on an arrangement's
 * Checks track (PIEArrangement.h).
 *
 * The plugin reads no hub file. The daemon hands a clip over as JSON in the hub's shape (src/contract/hub.ts):
 *
 *   { rate, length, input: [{ action, type, notes: [{ id, start, length, value? }], keys: [{ id, t, v, interp? }] }],
 *     lanes: [{ id, kind: move, points: [{ id, t, at, radius? }] }
 *           | { id, kind: action, calls: [{ id, t, fn, args? }], watch? }
 *           | { id, kind: value, target, type: axis1d|bool, keys: [{ id, t, v, interp? }] }] }
 *
 * A performer is a tagged union on its kind (#2137): a player (the editor's own, or remote client N) or an actor in
 * the level. Lanes are typed by capability and each kind declares the capabilities it takes. A player hands each
 * lane to a driver for its performer's kind (PIEClipDrivers.h), so a new kind is a new driver.
 */
namespace MCPPIE
{
	enum class EClipValueType : uint8 { Bool, Axis1D, Axis2D, Axis3D };
	enum class EClipInterp : uint8 { Linear, Constant, Cubic };

	FString ClipValueTypeToString(EClipValueType Type);
	bool ParseClipValueType(const FString& Str, EClipValueType& Out);
	FString ClipInterpToString(EClipInterp Interp);
	bool ParseClipInterp(const FString& Str, EClipInterp& Out);
	int32 ClipValueDims(EClipValueType Type);

	// An Input Action given as a package path (/Game/Input/IA_Jump) as its object path (/Game/Input/IA_Jump.IA_Jump).
	FString NormalizeInputActionPath(const FString& Path);

	// ── Performers and capabilities (#2137) ─────────────────────────────

	enum class EClipLaneCapability : uint8 { Input, Move, Action, Value };
	enum class EClipPerformerKind : uint8 { Player, Actor };

	FString ClipLaneCapabilityToString(EClipLaneCapability Capability);
	bool ParseClipLaneCapability(const FString& Str, EClipLaneCapability& Out);
	FString ClipPerformerKindToString(EClipPerformerKind Kind);
	bool ParseClipPerformerKind(const FString& Str, EClipPerformerKind& Out);

	/** What a track drives. A player: Client 0 the editor's own, N remote client N. An actor: named by label, object name or path. */
	struct FClipPerformer
	{
		EClipPerformerKind Kind = EClipPerformerKind::Player;
		int32 Client = 0;
		FString Actor;

		static FClipPerformer Player(int32 InClient) { FClipPerformer P; P.Client = InClient; return P; }
		static FClipPerformer ForActor(const FString& InActor) { FClipPerformer P; P.Kind = EClipPerformerKind::Actor; P.Actor = InActor; return P; }
		/** One key per performer: two references to the same one are equal. */
		FString Key() const;
		/** "Editor player", "Client 2", or the actor's name, for messages. */
		FString Label() const;
		bool operator==(const FClipPerformer& Other) const { return Key() == Other.Key(); }
	};

	/** The capabilities a performer kind takes: a player input, action and value; an actor move, action and value. */
	TArray<EClipLaneCapability> ClipPerformerCapabilities(EClipPerformerKind Kind);
	bool ClipPerformerAccepts(EClipPerformerKind Kind, EClipLaneCapability Capability);
	bool ClipPerformerFromJson(const TSharedPtr<FJsonValue>& Value, FClipPerformer& Out, FString& OutError);
	TSharedRef<FJsonObject> ClipPerformerToJson(const FClipPerformer& Performer);

	struct FClipNote
	{
		FString Id;
		double Start = 0.0;
		double Length = 1.0;
		// Ignored on a Boolean lane. On an axis lane, the value held over the note.
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

	/** One Input Action's lane: the raw value that reaches the action, after its mapping's modifiers. */
	struct FClipTrack
	{
		// Input Action object path, /Game/Input/IA_Jump.IA_Jump.
		FString Action;
		EClipValueType Type = EClipValueType::Bool;
		// The player the lane drives, set when an arrangement places the clip: 0 the editor's own, N remote client N.
		int32 Client = 0;
		TArray<FClipNote> Notes;
		TArray<FClipKey> Keys;
	};

	// A move lane's point: from frame Time the performer walks to Location through its controller.
	struct FClipMovePoint
	{
		FString Id;
		double Time = 0.0;
		FVector Location = FVector::ZeroVector;
		// How close counts as there, in centimetres; below 0 the controller's own acceptance radius.
		double Radius = -1.0;
	};

	// An action lane's call: Function on the performer (or "Component.Function") at frame Time, Args imported in order.
	struct FClipActionCall
	{
		FString Id;
		double Time = 0.0;
		FString Function;
		TArray<TSharedPtr<FJsonValue>> Args;
	};

	/** A move, action or value lane. Its performer is the track's, set when an arrangement places the clip. */
	struct FClipLane
	{
		FString Id;
		EClipLaneCapability Capability = EClipLaneCapability::Move;
		FClipPerformer Performer;
		// Move.
		TArray<FClipMovePoint> Points;
		// Action. Watch names more functions a recording captures besides those its calls name.
		TArray<FClipActionCall> Calls;
		TArray<FString> Watch;
		// Value: a property path on the performer ("Health", "Stats.Stamina") or "bb:<Key>" on its AI controller's
		// blackboard, axis1d (a number) or bool, as curve keys written from the first key through the last.
		FString Target;
		EClipValueType Type = EClipValueType::Axis1D;
		TArray<FClipKey> Keys;
	};

	struct FClip
	{
		// Frames per second of game time.
		double Rate = 60.0;
		// Frames. 0 means it ends at the last thing on it.
		double Length = 0.0;
		TArray<FClipTrack> Input;
		TArray<FClipLane> Lanes;

		// Length, or the end of the last thing on the clip when that is later.
		double EffectiveLength() const;
		FClipTrack* FindInput(const FString& Action);
		FClipLane* FindLane(const FString& Id);
	};

	// ── JSON ────────────────────────────────────────────────────────────

	// Reads the hub's clip JSON. False with the first problem when a field does not read.
	bool ClipFromJson(const TSharedPtr<FJsonObject>& Obj, FClip& Out, FString& OutError);
	// { rate, length, input, lanes } in the hub's shape, ids filled in where missing.
	TSharedRef<FJsonObject> ClipToJson(const FClip& Clip);
	// Ids filled in where missing (n1, k1, p1, a1 by kind, after the highest already used).
	void AssignClipIds(FClip& Clip);

	// ── Playback values ─────────────────────────────────────────────────

	// The raw value of an input lane at a frame: a note covering it, else the curve, else zero. bOutActive is false
	// where nothing covers the frame, so the player feeds nothing there.
	FVector EvaluateClipTrack(const FClipTrack& Track, double Frame, bool& bOutActive);
	// A value lane's value at a frame: the curve between its first and last key, inactive outside it.
	double EvaluateClipValueLane(const FClipLane& Lane, double Frame, bool& bOutActive);

	// ── Recording ───────────────────────────────────────────────────────

	// One frame of a take: the clip frame it fell on and each armed action's raw value.
	struct FClipTakeSample
	{
		double Frame = 0.0;
		TMap<FString, FVector> Values;
	};

	/**
	 * Replaces the armed input lanes over [Start, End) with what was recorded there, the way a DAW punches in: what
	 * the range held is dropped. Boolean lanes become notes from the value's edges; axis lanes are reduced to keys
	 * with FRichCurve.
	 */
	void PunchClipInput(FClip& Clip, const TArray<FString>& ArmedActions, const TMap<FString, EClipValueType>& Types,
		double Start, double End, const TArray<FClipTakeSample>& Samples);

	// One frame of an armed move or value lane: where its performer stood, or the value it held.
	struct FClipLaneSample
	{
		double Frame = 0.0;
		bool bHasLocation = false;
		FVector Location = FVector::ZeroVector;
		bool bHasValue = false;
		double Value = 0.0;
	};

	// What an armed lane recorded: per-frame samples for a move or value lane, the calls seen for an action lane.
	struct FClipLaneRecording
	{
		FString LaneId;
		TArray<FClipLaneSample> Samples;
		TArray<FClipActionCall> Calls;
	};

	/**
	 * Replaces the armed lanes over [Start, End) with what their performers did there. A move lane becomes the route
	 * the performer walked: stationary stretches collapse to the moment it left, corners are kept (Douglas-Peucker,
	 * 25 cm), and each point is set at the frame the performer left the point before, so a replay sets off when the
	 * recording did. A value lane becomes keys reduced with FRichCurve. An action lane takes the calls seen.
	 */
	void PunchClipLanes(FClip& Clip, const TArray<FClipLaneRecording>& Recordings, double Start, double End);

	// The points a move lane recording reduces to, by the rule above.
	TArray<FClipMovePoint> ReduceClipMoveSamples(const TArray<FClipLaneSample>& Samples);
}
