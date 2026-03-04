#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FAssetHandlers
{
public:
	// Register all asset handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> ListAssets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SaveAsset(const TSharedPtr<FJsonObject>& Params);
};
