#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * A clip (#2146): a reusable performance for a player, in frames at its rate: Input Action lanes (`input`) only. A
 * clip never asserts: checks live on an arrangement's Checks track (PIEArrangement.h).
 *
 * The plugin reads no hub file. The daemon hands a clip over as JSON in the hub's shape (src/contract/hub.ts):
 *
 *   { rate, length, input: [{ action, type, notes: [{ id, start, length, value? }], keys: [{ id, t, v, interp? }] }] }
 *
 * A performer is a tagged union on its kind with one case for now: a player (the editor's own, or remote client N).
 * The player hands each lane to a driver for its performer's kind (PIEClipDrivers.h), so a new kind is a new case and
 * a new driver.
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

	// ── Performers ──────────────────────────────────────────────────────

	enum class EClipPerformerKind : uint8 { Player };

	FString ClipPerformerKindToString(EClipPerformerKind Kind);
	bool ParseClipPerformerKind(const FString& Str, EClipPerformerKind& Out);

	/** What a track drives: a player, Client 0 the editor's own and N remote client N. */
	struct FClipPerformer
	{
		EClipPerformerKind Kind = EClipPerformerKind::Player;
		int32 Client = 0;

		static FClipPerformer Player(int32 InClient) { FClipPerformer P; P.Client = InClient; return P; }
		/** One key per performer: two references to the same one are equal. */
		FString Key() const;
		/** "Editor player" or "Client 2", for messages. */
		FString Label() const;
		bool operator==(const FClipPerformer& Other) const { return Key() == Other.Key(); }
	};

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

	struct FClip
	{
		// Frames per second of game time.
		double Rate = 60.0;
		// Frames. 0 means it ends at the last thing on it.
		double Length = 0.0;
		TArray<FClipTrack> Input;

		// Length, or the end of the last thing on the clip when that is later.
		double EffectiveLength() const;
		FClipTrack* FindInput(const FString& Action);
	};

	// ── JSON ────────────────────────────────────────────────────────────

	// Reads the hub's clip JSON. False with the first problem when a field does not read, or when it holds lanes.
	bool ClipFromJson(const TSharedPtr<FJsonObject>& Obj, FClip& Out, FString& OutError);
	// { rate, length, input } in the hub's shape, ids filled in where missing.
	TSharedRef<FJsonObject> ClipToJson(const FClip& Clip);
	// Ids filled in where missing (n1, k1 by kind, after the highest already used).
	void AssignClipIds(FClip& Clip);

	// ── Playback values ─────────────────────────────────────────────────

	// The raw value of an input lane at a frame: a note covering it, else the curve, else zero. bOutActive is false
	// where nothing covers the frame, so the player feeds nothing there.
	FVector EvaluateClipTrack(const FClipTrack& Track, double Frame, bool& bOutActive);

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
}
