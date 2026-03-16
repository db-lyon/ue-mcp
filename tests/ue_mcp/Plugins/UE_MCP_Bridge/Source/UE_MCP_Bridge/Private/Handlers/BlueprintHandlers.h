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
	static TSharedPtr<FJsonValue> SearchNodeTypes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListNodeTypes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListBlueprintVariables(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetVariableProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateFunction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListBlueprintFunctions(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadBlueprintGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddEventDispatcher(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RenameFunction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteFunction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateBlueprintInterface(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListNodeTypesDetailed(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SearchCallableFunctions(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectPins(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetNodeProperty(const TSharedPtr<FJsonObject>& Params);

	// Helper functions
	static class UBlueprint* LoadBlueprint(const FString& AssetPath);
	static struct FEdGraphPinType MakePinType(const FString& TypeStr);
	static class UEdGraph* FindGraph(class UBlueprint* Blueprint, const FString& GraphName);
	static class UEdGraphNode* FindNodeByGuidOrName(class UEdGraph* Graph, const FString& NodeId);
};
