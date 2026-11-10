#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class UWorld;
class ULandscapeSplinesComponent;

namespace MCPLandscapeSplines
{
	/** Every landscape splines component in the world: the ones on landscape
	 *  actors and streaming proxies, and the ones on World Partition
	 *  LandscapeSplineActors. Sorted by path so a listing is stable. */
	void CollectSplineComponents(UWorld* World, TArray<ULandscapeSplinesComponent*>& Out);

	/** One row per control point and one per segment across every splines
	 *  component in the world, in the shape the spline writes return. */
	void DescribeAll(UWorld* World, TArray<TSharedPtr<FJsonValue>>& OutPoints, TArray<TSharedPtr<FJsonValue>>& OutSegments);
}

/** Landscape spline authoring: control points, segments, segment meshes, and
 *  applying the network to the landscape's heights and paint layers. Filed
 *  under the landscape category; registered from FLandscapeHandlers. */
class FLandscapeSplineHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> CreatePoint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> UpdatePoint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeletePoint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectPoints(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> UpdateSegment(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSegmentMeshes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteSegment(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ApplySplines(const TSharedPtr<FJsonObject>& Params);
};
