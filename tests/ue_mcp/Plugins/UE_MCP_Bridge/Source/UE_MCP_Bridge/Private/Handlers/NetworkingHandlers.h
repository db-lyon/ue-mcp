#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FNetworkingHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> GetNetworkingInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetReplicates(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConfigureNetUpdateFrequency(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetNetDormancy(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetAlwaysRelevant(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetNetPriority(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetReplicateMovement(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetVariableReplication(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetReplicationInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetOwnerOnlyRelevant(const TSharedPtr<FJsonObject>& Params);

	// Helper to load blueprint CDO as AActor
	static AActor* LoadBlueprintCDO(const FString& BlueprintPath, TSharedPtr<FJsonObject>& OutResult);
	static void SaveBlueprint(UBlueprint* Blueprint);
};
