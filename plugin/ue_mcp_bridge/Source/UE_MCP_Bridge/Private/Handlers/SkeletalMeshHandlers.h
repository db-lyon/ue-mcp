#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

/** Small, editor-only skeletal mesh build-settings handlers. */
class FSkeletalMeshHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> SetOptimizeForInstancing(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadBuildSettings(const TSharedPtr<FJsonObject>& Params);
};
