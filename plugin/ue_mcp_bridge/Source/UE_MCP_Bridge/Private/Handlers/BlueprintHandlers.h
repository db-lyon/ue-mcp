#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FBlueprintHandlers
{
public:
	// Register all blueprint handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

	// Reusable type-string -> pin-type resolver. Shared with other categories
	// (e.g. AssetHandlers UserDefinedStruct authoring, #735) so a single mapping
	// of "bool"/"int"/"Vector"/"Actor*"/etc. serves every authoring surface.
	static struct FEdGraphPinType MakePinType(const FString& TypeStr);

	// The container layer over MakePinType, and its inverse. ParsePinTypeSpec
	// accepts everything MakePinType does plus "int[]", "array<int>",
	// "set<Name>" and "map<Name,int>"; PinTypeSpec reports a pin type in
	// exactly that vocabulary, so a read can be handed straight back as a
	// write. Both live here rather than in a file-local helper because the
	// user-type and depth handlers need the identical spelling, and the module
	// is a unity build where a copied helper is a redefinition (C2084).
	// Defined in BlueprintHandlers_UserTypes.cpp.
	static bool ParsePinTypeSpec(const FString& TypeStr, struct FEdGraphPinType& OutType, FString& OutError);
	static FString PinTypeSpec(const struct FEdGraphPinType& PinType, bool& bOutRoundTrips);

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
	// #688: override an inherited interface / parent (virtual) function with the
	// correct signature so it binds as the override; plus list what can be overridden.
	static TSharedPtr<FJsonValue> OverrideFunction(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListOverridableFunctions(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectPins(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetNodeProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListGraphs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ResolveGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetComponentProperty(const TSharedPtr<FJsonObject>& Params);
	// #442: dedicated OverrideMaterials writer for mesh-component templates.
	static TSharedPtr<FJsonValue> SetComponentOverrideMaterials(const TSharedPtr<FJsonObject>& Params);
	// #457: timeline track authoring (float / vector / linear-color / event).
	static TSharedPtr<FJsonValue> AddTimelineTrack(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetClassDefault(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteVariable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddFunctionParameter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetVariableDefault(const TSharedPtr<FJsonObject>& Params);
	// #902: read the RESOLVED default off the generated class CDO, so a
	// write-compile-readback loop can verify itself. Reports whether that value
	// has reached the package as well as what it is (#931).
	static TSharedPtr<FJsonValue> GetVariableDefault(const TSharedPtr<FJsonObject>& Params);

	// v0.7.8 - agent-ergonomics additions (stubs)
	static TSharedPtr<FJsonValue> ReadBlueprintGraphSummary(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetBlueprintExecutionFlow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetBlueprintDependencies(const TSharedPtr<FJsonObject>& Params);

	// v0.7.11 - BP authoring depth
	static TSharedPtr<FJsonValue> DuplicateBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddLocalVariable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListLocalVariables(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ValidateBlueprint(const TSharedPtr<FJsonObject>& Params);

	// v0.7.11 - issue fixes
	static TSharedPtr<FJsonValue> ReadComponentProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadNodeProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReparentComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetActorTickSettings(const TSharedPtr<FJsonObject>& Params);

	// v0.7.12 - issue #128 - read single component property (inherited-aware)
	static TSharedPtr<FJsonValue> GetComponentProperty(const TSharedPtr<FJsonObject>& Params);

	// v0.7.17 issue #130: bulk graph node import via T3D copy/paste
	static TSharedPtr<FJsonValue> ExportNodesT3D(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportNodesT3D(const TSharedPtr<FJsonObject>& Params);

	// v0.7.18 issue #138: reparent a Blueprint to a new parent class.
	static TSharedPtr<FJsonValue> ReparentBlueprint(const TSharedPtr<FJsonObject>& Params);
	// #580 flush orphaned InheritableComponentHandler override records
	static TSharedPtr<FJsonValue> FlushInheritableComponentHandler(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> FlushComponentTemplates(const TSharedPtr<FJsonObject>& Params);

	// issues #182/#183: C++ class CDO property access
	static TSharedPtr<FJsonValue> SetCdoProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCdoProperties(const TSharedPtr<FJsonObject>& Params);

	// issue #195: run construction script and inspect resulting components
	static TSharedPtr<FJsonValue> RunConstructionScript(const TSharedPtr<FJsonObject>& Params);

	// v1.0.0-rc.15 - agent-friendly BP authoring (#284 #285 #267 #277)
	static TSharedPtr<FJsonValue> CompileBlueprints(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CleanupGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectPinsBatch(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetNodePosition(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AutoLayoutGraph(const TSharedPtr<FJsonObject>& Params);
	// #945: project-wide search for authored function call sites, narrowed by
	// the Asset Registry before any package is loaded. Defined in
	// BlueprintHandlers_Search.cpp.
	static TSharedPtr<FJsonValue> SearchCallSites(const TSharedPtr<FJsonObject>& Params);

	// #419: SetCapsuleSize on CapsuleComponent BP templates (UFUNCTION setter
	// path; raw property writes leave the visualizer stale)
	static TSharedPtr<FJsonValue> SetCapsuleSize(const TSharedPtr<FJsonObject>& Params);

	// V9 Blueprint depth. Defined in BlueprintHandlers_Depth.cpp. Each of these
	// closes a hole the shipping source already documented, or authors graph
	// state that no UPROPERTY write can reach; see that file's header comment
	// for the audit that decided the list.
	static TSharedPtr<FJsonValue> ListBlueprintInterfaces(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveBlueprintInterface(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetFunctionProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListGraphParameters(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> EditGraphParameters(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RenameBlueprintVariable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetBlueprintVariableMetadata(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetBlueprintVariableMetadata(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> EditLocalVariable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListEventDispatchers(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveEventDispatcher(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddCustomEvent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateMacro(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteMacro(const TSharedPtr<FJsonObject>& Params);

	// V14 user-type authoring. Defined in BlueprintHandlers_UserTypes.cpp.
	// Creation and the coarse entry CRUD stay in the asset category; these are
	// the parts of a UserDefinedEnum or UserDefinedStruct that only
	// FEnumEditorUtils / FStructureEditorUtils can author, plus the whole-
	// definition read-back.
	static TSharedPtr<FJsonValue> ReadUserDefinedEnum(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReorderEnumValues(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetEnumMetadata(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadUserDefinedStruct(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetStructFieldDefault(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReorderStructFields(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> EditStructMetadata(const TSharedPtr<FJsonObject>& Params);

	// Helper functions
	static class UBlueprint* LoadBlueprint(const FString& AssetPath);
	static class UEdGraph* FindGraph(class UBlueprint* Blueprint, const FString& GraphName);
	static class UEdGraphNode* FindNodeByGuidOrName(class UEdGraph* Graph, const FString& NodeId);
};
