#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FSplineHandlers
{
public:
	// Register all spline handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> CreateSplineActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadSpline(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSplinePoints(const TSharedPtr<FJsonObject>& Params);
};
