#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * asset(bulk_read_properties) (#909).
 *
 * Its own class rather than another FAssetHandlers member, with its own
 * registration entry in BridgeServer.cpp, so this lands without touching
 * AssetHandlers.cpp at all.
 */
class FAssetBulkReadHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> BulkReadAssetProperties(const TSharedPtr<FJsonObject>& Params);
};
