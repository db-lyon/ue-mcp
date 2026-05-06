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
	static TSharedPtr<FJsonValue> ListGraphs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetComponentProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetClassDefault(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteVariable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddFunctionParameter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetVariableDefault(const TSharedPtr<FJsonObject>& Params);

	// v0.7.8 — agent-ergonomics additions (stubs)
	static TSharedPtr<FJsonValue> ReadBlueprintGraphSummary(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetBlueprintExecutionFlow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetBlueprintDependencies(const TSharedPtr<FJsonObject>& Params);

	// v0.7.11 — BP authoring depth
	static TSharedPtr<FJsonValue> DuplicateBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddLocalVariable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListLocalVariables(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ValidateBlueprint(const TSharedPtr<FJsonObject>& Params);

	// v0.7.11 — issue fixes
	static TSharedPtr<FJsonValue> ReadComponentProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadNodeProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReparentComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetActorTickSettings(const TSharedPtr<FJsonObject>& Params);

	// v0.7.12 — issue #128 — read single component property (inherited-aware)
	static TSharedPtr<FJsonValue> GetComponentProperty(const TSharedPtr<FJsonObject>& Params);

	// v0.7.17 issue #130: bulk graph node import via T3D copy/paste
	static TSharedPtr<FJsonValue> ExportNodesT3D(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportNodesT3D(const TSharedPtr<FJsonObject>& Params);

	// v0.7.18 issue #138: reparent a Blueprint to a new parent class.
	static TSharedPtr<FJsonValue> ReparentBlueprint(const TSharedPtr<FJsonObject>& Params);

	// issues #182/#183: C++ class CDO property access
	static TSharedPtr<FJsonValue> SetCdoProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCdoProperties(const TSharedPtr<FJsonObject>& Params);

	// issue #195: run construction script and inspect resulting components
	static TSharedPtr<FJsonValue> RunConstructionScript(const TSharedPtr<FJsonObject>& Params);

	// v1.0.0-rc.15 — agent-friendly BP authoring (#284 #285 #267 #277)
	static TSharedPtr<FJsonValue> CompileBlueprints(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CleanupGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectPinsBatch(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetNodePosition(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AutoLayoutGraph(const TSharedPtr<FJsonObject>& Params);

	// Helper functions
	static class UBlueprint* LoadBlueprint(const FString& AssetPath);
	static struct FEdGraphPinType MakePinType(const FString& TypeStr);
	static class UEdGraph* FindGraph(class UBlueprint* Blueprint, const FString& GraphName);
	static class UEdGraphNode* FindNodeByGuidOrName(class UEdGraph* Graph, const FString& NodeId);
};
