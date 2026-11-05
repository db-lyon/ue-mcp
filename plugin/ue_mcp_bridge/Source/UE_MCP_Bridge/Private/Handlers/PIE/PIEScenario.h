// Declarative scenario (Roadmap v2, F2). A committed arrange / act / assert
// document: the reusable unit an agent authors, not a disposable recording. Parse
// and Validate are pure (no PIE) and unit tested; ApplyArrange dispatches the
// arrange steps against a live PIE world via FPIEActorPuppet.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class UWorld;

namespace MCPPIE
{
	struct FScenario
	{
		FString Name;
		FString SourceRecordingDir;
		TArray<TSharedPtr<FJsonValue>> Arrange;   // raw step objects: {kind: spawn|set|call, ...}
		TArray<TSharedPtr<FJsonValue>> Act;        // raw step objects: {kind: replay|inject_tape|drive|goto|face|follow, ...}
		TArray<TSharedPtr<FJsonValue>> Assert;     // predicate objects (Phase A schema)
	};

	class FPIEScenario
	{
	public:
		// Parse a scenario document. Accepts 'assert' or 'assertions' for the predicate
		// list. Never fails on shape here; structural checks live in Validate.
		static bool Parse(const TSharedPtr<FJsonObject>& Root, FScenario& Out, FString& OutError);

		// Structural + semantic validation: known step kinds, required per-kind fields,
		// and every assert entry parsing as a valid predicate. Fills OutErrors with one
		// message per problem; returns true iff OutErrors is empty.
		static bool Validate(const FScenario& S, TArray<FString>& OutErrors);

		// The arrange checks Validate applies, for arrange steps outside a scenario (a test script step's).
		// Each message starts with Where, then [i].
		static void ValidateArrange(const TArray<TSharedPtr<FJsonValue>>& Arrange, const FString& Where, TArray<FString>& OutErrors);

		static TSharedRef<FJsonObject> ToJson(const FScenario& S);

		// Execute the arrange steps against a live PIE world. Live PIE required.
		static bool ApplyArrange(UWorld* World, const FScenario& S, int32& OutApplied, FString& OutError);
	};
}
