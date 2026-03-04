#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FLevelHandlers
{
public:
	// Register all level handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> GetOutliner(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PlaceActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetActorDetails(const TSharedPtr<FJsonObject>& Params);
};
