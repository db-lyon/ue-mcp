#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/** Level blockout: tagged primitive actors (box, ramp, wedge, cylinder,
 *  stairs), a reader for them, ranking candidate meshes against one by
 *  bounds, and swapping a blockout for a fitted asset with a record that
 *  restores it. Filed under the level category; registered from
 *  FLevelHandlers. */
class FLevelBlockoutHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> CreateBlockout(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListBlockout(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MatchAssetToBlockout(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReplaceBlockout(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RestoreBlockout(const TSharedPtr<FJsonObject>& Params);
};
