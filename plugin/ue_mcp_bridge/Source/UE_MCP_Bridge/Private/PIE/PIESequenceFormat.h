#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * On-disk format for the PIE record / replay system.
 *
 * A recording lives in <ProjectSavedDir>/MCPRecordings/<id>/ and contains:
 *   - manifest.json   Top-level metadata, action list, markers, file pointers
 *   - sequence.json   Replay-ready step list (input_tape / hold / mark / capture / console)
 *   - recording.csv   Per-frame samples for analysis (pandas/jq friendly)
 *   - drift.json      Per-frame deltas vs source recording (only after a replay)
 *
 * Schema is versioned at the top of every JSON file. The current version is 1.
 *
 * This header defines the in-memory structs and pure read/write helpers. There
 * are no UObject lifetimes here; safe to call from any thread that owns its
 * own copies of the structs.
 */
namespace UEMCPPIE
{
	constexpr int32 kFormatVersion = 1;

	enum class EActionValueType : uint8
	{
		Boolean,
		Axis1D,
		Axis2D,
		Axis3D
	};

	FString ActionValueTypeToString(EActionValueType Type);
	bool ParseActionValueType(const FString& Str, EActionValueType& Out);

	struct FActionSpec
	{
		FString Name;
		FString Path;
		EActionValueType ValueType = EActionValueType::Boolean;
	};

	struct FTrackedValueSpec
	{
		FString Path;
		FString Type;
	};

	struct FMarker
	{
		uint64 Frame = 0;
		double Time = 0.0;
		FString Label;
		TSharedPtr<FJsonObject> Metadata;
	};

	struct FManifest
	{
		int32 Version = kFormatVersion;
		FString Id;
		FString StartedAt;
		FString EndedAt;
		double DurationSeconds = 0.0;
		int32 TotalFrames = 0;
		int32 SampleHz = 60;
		int32 PinMaxFPS = 60;
		int64 RngSeed = 0;
		FString PIEWorld;
		FString PawnClass;
		float AxisThreshold = 0.15f;
		TArray<FActionSpec> Actions;
		TArray<FTrackedValueSpec> TrackedValues;
		TArray<FString> TrackedActorIds;
		TArray<FMarker> Markers;
		FString CSVFile = TEXT("recording.csv");
		FString SequenceFile = TEXT("sequence.json");
		FString DriftFile;
		FString TrackedActorsFile;
	};

	// Per-frame position / rotation / velocity for a single tracked actor.
	// Resolved=false means no actor matched the user's id this frame; deltas
	// against an unresolved frame are skipped rather than treated as zero.
	struct FActorState
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector Velocity = FVector::ZeroVector;
		bool bResolved = false;
	};

	struct FTrackedActorRow
	{
		uint64 Frame = 0;
		double Time = 0.0;
		TMap<FString, FActorState> Actors;
	};

	enum class EStepType : uint8
	{
		Input,
		Hold,
		Capture,
		Console,
		InputTape,
		Mark
	};

	FString StepTypeToString(EStepType Type);
	bool ParseStepType(const FString& Str, EStepType& Out);

	struct FStep
	{
		EStepType Type = EStepType::Input;
		FString Action;
		int32 DelayMs = 0;
		double ValueX = 0.0;
		double ValueY = 0.0;
		double ValueZ = 0.0;
		int32 DurationMs = 0;
		FString CaptureName;
		FString Command;
		FString Label;
		TArray<FVector> TapeValues;
	};

	struct FSequence
	{
		int32 Version = kFormatVersion;
		FString SourceRecordingId;
		int32 SettleMs = 500;
		int32 SampleHz = 60;
		int64 RngSeed = 0;
		TArray<FStep> Steps;
	};

	struct FDriftFrameEntry
	{
		uint64 Frame = 0;
		float PositionDeltaCm = 0.f;
		float VelocityDeltaCms = 0.f;
		float RotationDeltaDeg = 0.f;
	};

	// Aggregated per-tracked-actor drift across all sampled frames.
	struct FActorDrift
	{
		float MaxPositionCm = 0.f;
		float MaxRotationDeg = 0.f;
		float MaxVelocityCms = 0.f;
		int32 FramesUnresolvedInSource = 0;
		int32 FramesUnresolvedInReplay = 0;
	};

	struct FDriftReport
	{
		int32 Version = kFormatVersion;
		FString SourceRecordingId;
		FString ReplayStartedAt;
		int32 FramesCompared = 0;
		int32 FramesMissingInReplay = 0;
		float MaxPositionDriftCm = 0.f;
		uint64 MaxPositionDriftFrame = 0;
		float MaxVelocityDriftCms = 0.f;
		float MaxRotationDriftDeg = 0.f;
		int32 MontageSectionMismatches = 0;
		TMap<FString, float> TrackedValueMaxDeltas;
		TMap<FString, FActorDrift> ActorDrift;
		TArray<FDriftFrameEntry> FramesOverThreshold;
	};

	// CSV row used by both the recorder (writing) and the replayer / diff
	// (reading). Action values are keyed by action name; reflection-tracked
	// values are keyed by their dotted path.
	struct FCSVRow
	{
		uint64 Frame = 0;
		double Time = 0.0;
		double Dt = 0.0;
		FVector PawnLocation = FVector::ZeroVector;
		FRotator PawnRotation = FRotator::ZeroRotator;
		FVector PawnVelocity = FVector::ZeroVector;
		float Speed2D = 0.f;
		FString MontageSection;
		TMap<FString, FVector> ActionValues;
		TMap<FString, double> TrackedValues;
		TArray<FString> EdgeEvents;
	};

	// ── JSON serialization ───────────────────────────────────────────────

	TSharedRef<FJsonObject> ManifestToJson(const FManifest& M);
	bool ManifestFromJson(const TSharedRef<FJsonObject>& Obj, FManifest& Out, FString& OutError);

	TSharedRef<FJsonObject> SequenceToJson(const FSequence& S);
	bool SequenceFromJson(const TSharedRef<FJsonObject>& Obj, FSequence& Out, FString& OutError);

	TSharedRef<FJsonObject> DriftToJson(const FDriftReport& D);
	bool DriftFromJson(const TSharedRef<FJsonObject>& Obj, FDriftReport& Out, FString& OutError);

	// ── File IO (UTF-8, no BOM) ──────────────────────────────────────────

	bool SaveManifest(const FString& Path, const FManifest& M, FString& OutError);
	bool LoadManifest(const FString& Path, FManifest& Out, FString& OutError);

	bool SaveSequence(const FString& Path, const FSequence& S, FString& OutError);
	bool LoadSequence(const FString& Path, FSequence& Out, FString& OutError);

	bool SaveDrift(const FString& Path, const FDriftReport& D, FString& OutError);
	bool LoadDrift(const FString& Path, FDriftReport& Out, FString& OutError);

	// tracked.jsonl: one JSON object per line, ordered by frame.
	bool SaveTrackedActorsJSONL(const FString& Path, const TArray<FTrackedActorRow>& Rows, FString& OutError);
	bool LoadTrackedActorsJSONL(const FString& Path, TArray<FTrackedActorRow>& OutRows, FString& OutError);

	// ── CSV writing ──────────────────────────────────────────────────────

	struct FCSVHeader
	{
		FString RecordingId;
		int32 SampleHz = 60;
		int64 RngSeed = 0;
		TArray<FActionSpec> Actions;
		TArray<FTrackedValueSpec> TrackedValues;
	};

	// Build the two-line CSV header (a `#` comment line plus the column header).
	// The Body buffer is appended to per row; flush by calling SaveCSV.
	FString BuildCSVHeader(const FCSVHeader& H);
	void AppendCSVRow(FString& Body, const FCSVRow& Row, const FCSVHeader& H);
	bool SaveCSV(const FString& Path, const FString& HeaderAndBody, FString& OutError);

	// ── Recording-directory helpers ──────────────────────────────────────

	// Default root: <ProjectSavedDir>/MCPRecordings/
	FString DefaultRecordingsRoot();
	FString MakeRecordingId(const FString& Override = FString());
	FString MakeRecordingDir(const FString& Root, const FString& Id);
}
