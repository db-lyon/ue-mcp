#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FBlueprintHandlers
{
public:
	// Register all blueprint handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> CreateBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddVariable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddBlueprintInterface(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CompileBlueprint(const TSharedPtr<FJsonObject>& Params);

	// Helper functions
	static class UBlueprint* LoadBlueprint(const FString& AssetPath);
	static struct FEdGraphPinType MakePinType(const FString& TypeStr);
};
