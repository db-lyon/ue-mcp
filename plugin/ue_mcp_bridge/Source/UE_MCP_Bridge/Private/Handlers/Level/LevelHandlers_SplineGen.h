#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/** Spline generators: a spline actor from points, a chain of spline meshes
 *  along a spline, and meshes scattered along one. Generated output is tagged
 *  with its generator name and its parameters are kept on the spline's actor,
 *  so running a generator again replaces what it made last time. Filed under
 *  the level category; registered from FLevelHandlers. */
class FLevelSplineGenHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> CreateSplineActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BuildSplineMeshes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ScatterAlongSpline(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearSplineGenerated(const TSharedPtr<FJsonObject>& Params);
};
