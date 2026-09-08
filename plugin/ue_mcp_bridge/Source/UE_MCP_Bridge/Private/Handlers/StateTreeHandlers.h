#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class UStateTree;
class UStateTreeEditorData;
class UStateTreeState;

class FStateTreeHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Helpers
	static UStateTree* LoadStateTree(const FString& AssetPath);
	static UStateTreeEditorData* GetEditorData(UStateTree* StateTree);
	static UStateTreeState* FindStateByID(UStateTreeEditorData* EditorData, const FGuid& StateID);
	static UStateTreeState* FindStateByPath(UStateTreeEditorData* EditorData, const FString& Path);
	static UStateTreeState* ResolveState(UStateTreeEditorData* EditorData, const TSharedPtr<FJsonObject>& Params);
	static bool CompileAndSave(UStateTree* StateTree, TSharedPtr<FJsonObject>& OutResult);
	static FString MissingEditorDataMessage(const FString& AssetPath);
	static TSharedPtr<FJsonValue> RequireSchema(UStateTree* StateTree, const FString& AssetPath);
	static TSharedPtr<FJsonObject> SerializeStateHierarchy(const UStateTreeState* State);

	// Read / Introspect
	static TSharedPtr<FJsonValue> ReadStateTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListStates(const TSharedPtr<FJsonObject>& Params);

	// State Manipulation
	static TSharedPtr<FJsonValue> AddState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetStateProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearStateNodes(const TSharedPtr<FJsonObject>& Params);

	// Task / Condition Manipulation
	static TSharedPtr<FJsonValue> AddTask(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddEnterCondition(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveEnterCondition(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveTask(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetTaskInstanceProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetTaskProperty(const TSharedPtr<FJsonObject>& Params);

	// Transition Manipulation
	static TSharedPtr<FJsonValue> AddTransition(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddTransitionCondition(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveTransition(const TSharedPtr<FJsonObject>& Params);

	// Property Bindings
	static TSharedPtr<FJsonValue> AddBinding(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveBinding(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListBindings(const TSharedPtr<FJsonObject>& Params);
	// #681: enumerate the context/bindable sources a property can bind FROM.
	static TSharedPtr<FJsonValue> ListBindableSources(const TSharedPtr<FJsonObject>& Params);

	// Evaluator Manipulation
	static TSharedPtr<FJsonValue> AddEvaluator(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveEvaluator(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetEvaluatorInstanceProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetEvaluatorProperty(const TSharedPtr<FJsonObject>& Params);

	// Global Task Manipulation
	static TSharedPtr<FJsonValue> AddGlobalTask(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveGlobalTask(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetGlobalTaskInstanceProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetGlobalTaskProperty(const TSharedPtr<FJsonObject>& Params);

	// Color Palette
	static TSharedPtr<FJsonValue> ListColors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddColor(const TSharedPtr<FJsonObject>& Params);

	// State Parameters
	static TSharedPtr<FJsonValue> ListStateParameters(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddStateParameter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveStateParameter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetStateParameter(const TSharedPtr<FJsonObject>& Params);

	// Root Parameters
	static TSharedPtr<FJsonValue> SetRootParameters(const TSharedPtr<FJsonObject>& Params);

	// Schema
	static TSharedPtr<FJsonValue> SetSchema(const TSharedPtr<FJsonObject>& Params);

	// Lifecycle
	static TSharedPtr<FJsonValue> CompileStateTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ValidateStateTree(const TSharedPtr<FJsonObject>& Params);

	// V8 depth. All in StateTreeHandlers_Depth.cpp; see that file's header for
	// why each one needs a handler rather than a property write, and for the
	// dead ends they close (no node-type discovery, no utility considerations,
	// no in-asset subtree link, no state reordering, no Blueprint node class,
	// no transition-condition removal, no way to drive a running tree).
	static TSharedPtr<FJsonValue> ListStateTreeNodeTypes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddConsideration(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveConsideration(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveTransitionCondition(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetStateLink(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetNodeClass(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadRuntime(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SendEvent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RequestTransition(const TSharedPtr<FJsonObject>& Params);
};
