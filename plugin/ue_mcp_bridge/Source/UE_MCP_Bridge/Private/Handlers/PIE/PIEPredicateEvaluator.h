// Assertion layer (Roadmap v2, Phase A). Evaluates a list of predicates over a
// finalised per-frame series (observation.csv / recording.csv), plus the run's
// session_errors.json and manifest markers. A predicate binds a channel or an
// event to an operator under a temporal quantifier (hold), optionally windowed.
// The evaluator is pure file+JSON: no editor, no PIE runtime, so it is unit
// testable and reusable by test_scaffold (range derivation) and test_run.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

namespace MCPPIE
{
	enum class EPredOp : uint8
	{
		Eq, Ne, Lt, Lte, Gt, Gte, Approx, Between, Changed, Increased, Decreased, Crossed
	};

	enum class EPredHold : uint8
	{
		Always, Never, Eventually, AtEnd, OnceThenAlways
	};

	enum class EPredKind : uint8
	{
		Channel, Montage, Marker, Error
	};

	enum class EPredSeverity : uint8
	{
		Error, Warn
	};

	// One parsed assertion.
	struct FPredicate
	{
		FString        Name;
		EPredKind      Kind = EPredKind::Channel;
		EPredHold      Hold = EPredHold::Always;
		EPredSeverity  Severity = EPredSeverity::Error;

		// Channel predicates.
		FString  Channel;
		EPredOp  Op = EPredOp::Eq;
		double   Value = 0.0;
		double   Min = 0.0;
		double   Max = 0.0;
		double   Tol = 0.0;
		bool     bHasValue = false;
		bool     bHasMinMax = false;

		// Event predicates.
		FString  Montage;         // Montage kind: asset/section name to match.
		FString  MontagePhase;    // "started" | "playing" | "completed" (default playing).
		FString  Marker;          // Marker kind: label to match.
		FString  ErrorCategory;   // Error kind: category filter (optional).
		FString  ErrorContains;   // Error kind: message substring filter (optional).

		// Window (all optional).
		bool     bHasFromFrame = false, bHasFromS = false;
		bool     bHasToFrame = false,   bHasToS = false;
		int64    FromFrame = 0, ToFrame = 0;
		double   FromS = 0.0,   ToS = 0.0;
		FString  AfterMarker;     // Anchor the window start to this marker's frame.

		// Deadline for Eventually.
		bool     bHasWithinFrames = false, bHasWithinS = false;
		int64    WithinFrames = 0;
		double   WithinS = 0.0;
	};

	struct FPredicateResult
	{
		FString        Name;
		bool           bPassed = false;
		EPredSeverity  Severity = EPredSeverity::Error;
		int64          WitnessFrame = -1;   // Proof frame: first violation / first satisfaction.
		double         WitnessTime = -1.0;
		double         Actual = 0.0;
		bool           bHasActual = false;
		FString        Expected;            // Human-readable expectation.
		FString        Message;             // One-line verdict for this predicate.
	};

	// Parsed per-frame table from a series CSV. Reused by scaffold range derivation.
	struct FSeriesTable
	{
		TArray<int64>            Frames;
		TArray<double>           Times;
		TMap<FString, int32>     ColIndex;   // header name -> column
		TArray<TArray<FString>>  Cells;       // one entry per data row

		int32 ResolveColumn(const FString& Channel) const;   // exact, else "t:"+name, else INDEX_NONE
		bool  HasRows() const { return Cells.Num() > 0; }
	};

	class FPIEPredicateEvaluator
	{
	public:
		// Parse a JSON assertions array into predicates. Returns false + OutError on a
		// malformed entry (unknown op/hold, missing required field).
		static bool ParsePredicates(const TArray<TSharedPtr<FJsonValue>>& In,
		                            TArray<FPredicate>& Out, FString& OutError);

		// Load a series CSV (skips '#' comment lines, first non-# line is the header).
		static bool LoadTable(const FString& CsvPath, FSeriesTable& Out, FString& OutError);

		// Derive a starter predicate set from a recorded series: range invariants for
		// pos_z and each tracked value, an "eventually" per montage seen, and a
		// no-errors invariant. Shared by test_scaffold and scenario_scaffold. Returns
		// an empty array if the CSV cannot be read.
		static TArray<TSharedPtr<FJsonValue>> DeriveStarterPredicates(const FString& CsvPath, int32 MaxErrors);

		// Evaluate predicates against a run. ErrorsJsonPath / ManifestPath may be empty
		// (error and marker predicates then degrade to a clear failure message).
		static bool Evaluate(const FString& CsvPath, const FString& ErrorsJsonPath,
		                     const FString& ManifestPath, const TArray<FPredicate>& Preds,
		                     TArray<FPredicateResult>& Out, FString& OutError);

		// Assemble the verdict block. bAllErrorsPassed is true iff every Error-severity
		// result passed (Warn results never fail the verdict).
		static TSharedRef<FJsonObject> ResultsToJson(const TArray<FPredicateResult>& Results,
		                                             bool& bAllErrorsPassed);
	};
}
