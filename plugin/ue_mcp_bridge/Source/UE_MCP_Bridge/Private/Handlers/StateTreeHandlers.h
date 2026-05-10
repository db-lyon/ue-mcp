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

	// Lifecycle
	static TSharedPtr<FJsonValue> CompileStateTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ValidateStateTree(const TSharedPtr<FJsonObject>& Params);
};
