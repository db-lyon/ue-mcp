#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UWorld;

namespace UEMCPInstanceProjection
{
	constexpr int32 DefaultMaxInstances = 10000;
	constexpr int32 HardMaxInstances = 50000;
	constexpr int32 MaxReportedSamples = 32;
	constexpr int32 MaxFilteredSurfaceHits = 64;

	enum class EMissPolicy : uint8
	{
		Error,
		Skip,
	};

	struct FProjectionPlanEntry
	{
		int32 InstanceIndex = INDEX_NONE;
		FTransform Before = FTransform::Identity;
		FTransform After = FTransform::Identity;
		bool bHit = false;
		FString HitActorLabel;
		FString HitActorClass;
		FString MissReason;
	};

	struct FProjectionPlan
	{
		TArray<FProjectionPlanEntry> Entries;
		int32 HitCount = 0;
		int32 MissCount = 0;
		double MinDistance = TNumericLimits<double>::Max();
		double MaxDistance = 0.0;
	};

	bool ParseMissPolicy(const FString& InValue, EMissPolicy& OutPolicy);

	FVector ApplySurfaceOffset(const FVector& ImpactPoint, const FVector& ImpactNormal, double SurfaceOffset);

	/**
	 * Testable core for the registered editor-world handler.
	 *
	 * Raw bridge parameters:
	 * - actorLabel (required), componentName (required only when ambiguous)
	 * - instanceIndices (optional; omitted means all), maxInstances (default
	 *   10000, hard limit 50000)
	 * - direction, traceStartOffset, traceDistance, channel, traceComplex
	 * - surfaceActorClass, surfaceActorLabels, surfaceOffset
	 * - onMiss (error or skip), dryRun (default true)
	 *
	 * A commit still runs the complete preflight in the same game-thread call,
	 * then applies all hits in one editor transaction without saving a package.
	 */
	TSharedPtr<FJsonValue> SnapInstancesToSurfaceInWorld(
		UWorld* World,
		const TSharedPtr<FJsonObject>& Params);
}
