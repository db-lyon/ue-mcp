#include "StateTreeHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"

// StateTree authoring depends on UStateTreeEditingSubsystem (compile +
// validate entry points) and editor property binding support, both
// introduced in UE 5.5. On 5.4 we register no handlers and emit a one-line
// log so the rest of the plugin still loads.
#if UE_MCP_HAS_5_5_API

#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeTypes.h"
#include "StateTreeNodeBase.h"

#include "Editor.h"
#include "Runtime/Launch/Resources/Version.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "PropertyBindingPath.h"
#include "PropertyBindingTypes.h"
#endif
#include "StructUtils/InstancedStruct.h"
#include "UObject/UObjectIterator.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEditorTypes.h"
#include "HandlerStateTreeSchema.h"

#define UE_MCP_HAS_STATETREE_STATE_DESCRIPTION (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6))
#define UE_MCP_HAS_STATETREE_STATE_CUSTOM_TICK_RATE (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6))
#define UE_MCP_HAS_STATETREE_COMPILER_TOKENIZED_MESSAGES (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6))
#define UE_MCP_HAS_STATETREE_EXECUTION_RUNTIME_DATA (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7))
#define UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7))

#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
using FUE_MCPStateTreePropertyPath = FPropertyBindingPath;
using FUE_MCPStateTreePropertyCreationDesc = UE::PropertyBinding::FPropertyCreationDescriptor;
#else
using FUE_MCPStateTreePropertyPath = FStateTreePropertyPath;
using FUE_MCPStateTreePropertyCreationDesc = FStateTreeEditorPropertyCreationDesc;
#endif

#endif // UE_MCP_HAS_5_5_API

void FStateTreeHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
#if !UE_MCP_HAS_5_5_API
	UE_LOG(LogTemp, Warning, TEXT("[UE_MCP_Bridge] StateTree handlers require UE 5.5+; skipped on this engine version."));
	return;
#else
	Registry.RegisterHandler(TEXT("read_state_tree"), &ReadStateTree);
	Registry.RegisterHandler(TEXT("list_state_tree_states"), &ListStates);
	Registry.RegisterHandler(TEXT("add_state_tree_state"), &AddState);
	Registry.RegisterHandler(TEXT("remove_state_tree_state"), &RemoveState);
	Registry.RegisterHandler(TEXT("set_state_tree_state_property"), &SetStateProperty);
	Registry.RegisterHandler(TEXT("clear_state_tree_state_nodes"), &ClearStateNodes);
	Registry.RegisterHandler(TEXT("add_state_tree_task"), &AddTask);
	Registry.RegisterHandler(TEXT("add_state_tree_enter_condition"), &AddEnterCondition);
	Registry.RegisterHandler(TEXT("remove_state_tree_enter_condition"), &RemoveEnterCondition);
	Registry.RegisterHandler(TEXT("remove_state_tree_task"), &RemoveTask);
	Registry.RegisterHandler(TEXT("set_state_tree_task_instance_property"), &SetTaskInstanceProperty);
	Registry.RegisterHandler(TEXT("set_state_tree_task_property"), &SetTaskProperty);
	Registry.RegisterHandler(TEXT("add_state_tree_transition"), &AddTransition);
	Registry.RegisterHandler(TEXT("add_state_tree_transition_condition"), &AddTransitionCondition);
	Registry.RegisterHandler(TEXT("remove_state_tree_transition"), &RemoveTransition);
	Registry.RegisterHandler(TEXT("add_state_tree_binding"), &AddBinding);
	Registry.RegisterHandler(TEXT("remove_state_tree_binding"), &RemoveBinding);
	Registry.RegisterHandler(TEXT("list_state_tree_bindings"), &ListBindings);
	Registry.RegisterHandler(TEXT("list_state_tree_bindable_sources"), &ListBindableSources);
	Registry.RegisterHandler(TEXT("add_state_tree_evaluator"), &AddEvaluator);
	Registry.RegisterHandler(TEXT("remove_state_tree_evaluator"), &RemoveEvaluator);
	Registry.RegisterHandler(TEXT("set_state_tree_evaluator_instance_property"), &SetEvaluatorInstanceProperty);
	Registry.RegisterHandler(TEXT("set_state_tree_evaluator_property"), &SetEvaluatorProperty);
	Registry.RegisterHandler(TEXT("add_state_tree_global_task"), &AddGlobalTask);
	Registry.RegisterHandler(TEXT("remove_state_tree_global_task"), &RemoveGlobalTask);
	Registry.RegisterHandler(TEXT("set_state_tree_global_task_instance_property"), &SetGlobalTaskInstanceProperty);
	Registry.RegisterHandler(TEXT("set_state_tree_global_task_property"), &SetGlobalTaskProperty);
	Registry.RegisterHandler(TEXT("list_state_tree_colors"), &ListColors);
	Registry.RegisterHandler(TEXT("add_state_tree_color"), &AddColor);
	Registry.RegisterHandler(TEXT("list_state_tree_state_parameters"), &ListStateParameters);
	Registry.RegisterHandler(TEXT("add_state_tree_state_parameter"), &AddStateParameter);
	Registry.RegisterHandler(TEXT("remove_state_tree_state_parameter"), &RemoveStateParameter);
	Registry.RegisterHandler(TEXT("set_state_tree_state_parameter"), &SetStateParameter);
	Registry.RegisterHandler(TEXT("set_state_tree_root_parameters"), &SetRootParameters);
	Registry.RegisterHandler(TEXT("set_state_tree_schema"), &SetSchema);
	Registry.RegisterHandler(TEXT("compile_state_tree"), &CompileStateTree);
	Registry.RegisterHandler(TEXT("validate_state_tree"), &ValidateStateTree);

	// V8 depth (StateTreeHandlers_Depth.cpp). Everything above could author a
	// tree; these close the parts of it that had no route at all.
	Registry.RegisterHandler(TEXT("list_state_tree_node_types"), &ListStateTreeNodeTypes);
	Registry.RegisterHandler(TEXT("read_state_tree_state"), &ReadState);
	Registry.RegisterHandler(TEXT("add_state_tree_consideration"), &AddConsideration);
	Registry.RegisterHandler(TEXT("remove_state_tree_consideration"), &RemoveConsideration);
	Registry.RegisterHandler(TEXT("remove_state_tree_transition_condition"), &RemoveTransitionCondition);
	Registry.RegisterHandler(TEXT("set_state_tree_state_link"), &SetStateLink);
	Registry.RegisterHandler(TEXT("move_state_tree_state"), &MoveState);
	Registry.RegisterHandler(TEXT("set_state_tree_node_class"), &SetNodeClass);
	Registry.RegisterHandler(TEXT("read_state_tree_runtime"), &ReadRuntime);
	Registry.RegisterHandler(TEXT("send_state_tree_event"), &SendEvent);
	Registry.RegisterHandler(TEXT("request_state_tree_transition"), &RequestTransition);
#endif // UE_MCP_HAS_5_5_API
}

#if UE_MCP_HAS_5_5_API

// ── Helpers ──────────────────────────────────────────────────────────────────

UStateTree* FStateTreeHandlers::LoadStateTree(const FString& AssetPath)
{
	return LoadAssetByPath<UStateTree>(AssetPath);
}

UStateTreeEditorData* FStateTreeHandlers::GetEditorData(UStateTree* StateTree)
{
	if (!StateTree) return nullptr;
	return Cast<UStateTreeEditorData>(StateTree->EditorData);
}

// #833: a StateTree with no editor data reached every authoring action as a
// bare "EditorData not found", which reads like a bug in the action rather
// than a fact about the asset. asset(create_asset_by_class) writes exactly
// this shape, and the fix is one call, so the message names it.
FString FStateTreeHandlers::MissingEditorDataMessage(const FString& AssetPath)
{
	return FString::Printf(
		TEXT("StateTree '%s' has no editor data, so it has no schema, no states, and cannot compile. ")
		TEXT("A tree created through the generic asset(create_asset_by_class) route lands in this state. ")
		TEXT("Repair it with statetree(set_schema, assetPath=\"%s\"), which attaches the editor data, a schema ")
		TEXT("and a root state, or create trees with gameplay(create_state_tree), which does that up front."),
		*AssetPath, *AssetPath);
}

// The compiler refuses a schema-less tree by logging to LogStateTreeEditor and
// returning false with an EMPTY compiler log, so a caller saw compiled=false
// and errors=[] and had nothing to act on. Answer with the reason before
// handing the asset to a compiler that cannot report it.
TSharedPtr<FJsonValue> FStateTreeHandlers::RequireSchema(UStateTree* StateTree, const FString& AssetPath)
{
	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return MCPError(MissingEditorDataMessage(AssetPath));
	}
	if (!EditorData->Schema)
	{
		return MCPError(FString::Printf(
			TEXT("StateTree '%s' has no schema, and the compiler cannot build a tree without one ")
			TEXT("(it logs \"does not have a schema\" and stops). Attach one with ")
			TEXT("statetree(set_schema, assetPath=\"%s\")."),
			*AssetPath, *AssetPath));
	}
	return nullptr;
}

UStateTreeState* FStateTreeHandlers::FindStateByID(UStateTreeEditorData* EditorData, const FGuid& StateID)
{
	if (!EditorData) return nullptr;
	return EditorData->GetMutableStateByID(StateID);
}

UStateTreeState* FStateTreeHandlers::FindStateByPath(UStateTreeEditorData* EditorData, const FString& Path)
{
	if (!EditorData || Path.IsEmpty()) return nullptr;

	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("."));
	if (Segments.Num() == 0) return nullptr;

	UStateTreeState* Current = nullptr;

	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
	{
		if (SubTree && SubTree->Name == FName(*Segments[0]))
		{
			if (Current)
			{
				return nullptr; // ambiguous
			}
			Current = SubTree;
		}
	}

	if (!Current) return nullptr;

	for (int32 i = 1; i < Segments.Num(); ++i)
	{
		UStateTreeState* Found = nullptr;
		for (const TObjectPtr<UStateTreeState>& Child : Current->Children)
		{
			if (Child && Child->Name == FName(*Segments[i]))
			{
				if (Found)
				{
					return nullptr; // ambiguous
				}
				Found = Child;
			}
		}
		if (!Found) return nullptr;
		Current = Found;
	}

	return Current;
}

UStateTreeState* FStateTreeHandlers::ResolveState(UStateTreeEditorData* EditorData, const TSharedPtr<FJsonObject>& Params)
{
	if (Params->HasField(TEXT("stateId")))
	{
		FGuid StateID;
		if (FGuid::Parse(Params->GetStringField(TEXT("stateId")), StateID))
		{
			return FindStateByID(EditorData, StateID);
		}
	}
	if (Params->HasField(TEXT("statePath")))
	{
		return FindStateByPath(EditorData, Params->GetStringField(TEXT("statePath")));
	}
	return nullptr;
}

static FString GuidToString(const FGuid& Guid)
{
	return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

static FGuid ParseGuid(const FString& Str)
{
	FGuid G;
	FGuid::Parse(Str, G);
	return G;
}

static FString GetStatePath(const UStateTreeState* State)
{
	if (!State) return TEXT("");
	return State->GetPath();
}

static TSharedPtr<FJsonObject> SerializeEditorNode(const FStateTreeEditorNode& Node)
{
	auto Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("id"), GuidToString(Node.ID));

	if (Node.Node.IsValid())
	{
		const UScriptStruct* NodeStruct = Node.Node.GetScriptStruct();
		Obj->SetStringField(TEXT("structType"), NodeStruct ? NodeStruct->GetPathName() : TEXT("None"));

		if (NodeStruct)
		{
			Obj->SetStringField(TEXT("structName"), NodeStruct->GetName());

			// Emit node-struct UPROPERTYs (FStateTreeTaskBase / FStateTreeConditionBase /
			// FStateTreeEvaluatorBase fields like bConsideredForCompletion, bTaskEnabled,
			// bShouldCallTick, etc.). Symmetric to instanceProperties below - lets
			// callers inspect/audit base-flag values.
			auto NodePropsObj = MakeShared<FJsonObject>();
			const uint8* NodeMem = Node.Node.GetMemory();
			if (NodeMem)
			{
				for (TFieldIterator<FProperty> PropIt(NodeStruct); PropIt; ++PropIt)
				{
					FProperty* Prop = *PropIt;
					FString ValueStr;
					const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(NodeMem);
					Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
					NodePropsObj->SetStringField(Prop->GetName(), ValueStr);
				}
			}
			Obj->SetObjectField(TEXT("nodeProperties"), NodePropsObj);
		}
	}

	if (Node.Instance.IsValid())
	{
		const UScriptStruct* InstStruct = Node.Instance.GetScriptStruct();
		if (InstStruct)
		{
			auto PropsObj = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> PropIt(InstStruct); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				FString ValueStr;
				const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Node.Instance.GetMemory());
				Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
				PropsObj->SetStringField(Prop->GetName(), ValueStr);
			}
			Obj->SetObjectField(TEXT("instanceProperties"), PropsObj);
		}
	}

	Obj->SetNumberField(TEXT("expressionIndent"), Node.ExpressionIndent);
	FString OperandStr;
	switch (Node.ExpressionOperand)
	{
	case EStateTreeExpressionOperand::And: OperandStr = TEXT("And"); break;
	case EStateTreeExpressionOperand::Or: OperandStr = TEXT("Or"); break;
	default: OperandStr = TEXT("Copy"); break;
	}
	Obj->SetStringField(TEXT("operand"), OperandStr);

	return Obj;
}

static FString TransitionTriggerToString(EStateTreeTransitionTrigger Trigger)
{
	TArray<FString> Parts;
	if (EnumHasAnyFlags(Trigger, EStateTreeTransitionTrigger::OnStateSucceeded)) Parts.Add(TEXT("OnStateSucceeded"));
	if (EnumHasAnyFlags(Trigger, EStateTreeTransitionTrigger::OnStateFailed)) Parts.Add(TEXT("OnStateFailed"));
	if (EnumHasAnyFlags(Trigger, EStateTreeTransitionTrigger::OnTick)) Parts.Add(TEXT("OnTick"));
	if (EnumHasAnyFlags(Trigger, EStateTreeTransitionTrigger::OnEvent)) Parts.Add(TEXT("OnEvent"));
	return Parts.Num() > 0 ? FString::Join(Parts, TEXT("|")) : TEXT("None");
}

static FString TransitionTypeToString(EStateTreeTransitionType Type)
{
	switch (Type)
	{
	case EStateTreeTransitionType::Succeeded: return TEXT("Succeeded");
	case EStateTreeTransitionType::Failed: return TEXT("Failed");
	case EStateTreeTransitionType::GotoState: return TEXT("GotoState");
	case EStateTreeTransitionType::NextState: return TEXT("NextState");
	case EStateTreeTransitionType::NextSelectableState: return TEXT("NextSelectableState");
	default: return TEXT("None");
	}
}

static FString StateTypeToString(EStateTreeStateType Type)
{
	switch (Type)
	{
	case EStateTreeStateType::State: return TEXT("State");
	case EStateTreeStateType::Group: return TEXT("Group");
	case EStateTreeStateType::Linked: return TEXT("Linked");
	case EStateTreeStateType::LinkedAsset: return TEXT("LinkedAsset");
	case EStateTreeStateType::Subtree: return TEXT("Subtree");
	default: return TEXT("Unknown");
	}
}

static FString SelectionBehaviorToString(EStateTreeStateSelectionBehavior Behavior)
{
	switch (Behavior)
	{
	case EStateTreeStateSelectionBehavior::None: return TEXT("None");
	case EStateTreeStateSelectionBehavior::TryEnterState: return TEXT("TryEnterState");
	case EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder: return TEXT("TrySelectChildrenInOrder");
	case EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom: return TEXT("TrySelectChildrenAtRandom");
	case EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility: return TEXT("TrySelectChildrenWithHighestUtility");
	case EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility: return TEXT("TrySelectChildrenAtRandomWeightedByUtility");
	case EStateTreeStateSelectionBehavior::TryFollowTransitions: return TEXT("TryFollowTransitions");
	default: return TEXT("Unknown");
	}
}

static EStateTreeStateType ParseStateType(const FString& Str)
{
	if (Str == TEXT("Group")) return EStateTreeStateType::Group;
	if (Str == TEXT("Linked")) return EStateTreeStateType::Linked;
	if (Str == TEXT("LinkedAsset")) return EStateTreeStateType::LinkedAsset;
	if (Str == TEXT("Subtree")) return EStateTreeStateType::Subtree;
	return EStateTreeStateType::State;
}

static EStateTreeStateSelectionBehavior ParseSelectionBehavior(const FString& Str)
{
	if (Str == TEXT("None")) return EStateTreeStateSelectionBehavior::None;
	if (Str == TEXT("TryEnterState")) return EStateTreeStateSelectionBehavior::TryEnterState;
	if (Str == TEXT("TrySelectChildrenInOrder")) return EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
	if (Str == TEXT("TrySelectChildrenAtRandom")) return EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom;
	if (Str == TEXT("TrySelectChildrenWithHighestUtility")) return EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility;
	if (Str == TEXT("TrySelectChildrenAtRandomWeightedByUtility")) return EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility;
	if (Str == TEXT("TryFollowTransitions")) return EStateTreeStateSelectionBehavior::TryFollowTransitions;
	return EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
}

static EStateTreeTransitionTrigger ParseTransitionTriggerSingle(const FString& Str)
{
	if (Str == TEXT("OnStateCompleted")) return EStateTreeTransitionTrigger::OnStateCompleted;
	if (Str == TEXT("OnStateSucceeded")) return EStateTreeTransitionTrigger::OnStateSucceeded;
	if (Str == TEXT("OnStateFailed")) return EStateTreeTransitionTrigger::OnStateFailed;
	if (Str == TEXT("OnTick")) return EStateTreeTransitionTrigger::OnTick;
	if (Str == TEXT("OnEvent")) return EStateTreeTransitionTrigger::OnEvent;
	return EStateTreeTransitionTrigger::OnStateCompleted;
}

static EStateTreeTransitionTrigger ParseTransitionTrigger(const FString& Str)
{
	if (Str.Contains(TEXT("|")))
	{
		EStateTreeTransitionTrigger Combined = EStateTreeTransitionTrigger::None;
		TArray<FString> Parts;
		Str.ParseIntoArray(Parts, TEXT("|"), true);
		for (const FString& Part : Parts)
		{
			const FString Trimmed = Part.TrimStartAndEnd();
			if (!Trimmed.IsEmpty())
			{
				Combined |= ParseTransitionTriggerSingle(Trimmed);
			}
		}
		return Combined != EStateTreeTransitionTrigger::None ? Combined : EStateTreeTransitionTrigger::OnStateCompleted;
	}
	return ParseTransitionTriggerSingle(Str);
}

// The priority parser AddTransition writes through, and the one RemoveTransition
// round-trips against. Deliberately ONE function rather than a writer plus a
// replica: a round trip against a copy is only as good as the copy's tracking of
// the original, and this enum already has a third, divergent parser in
// StateTreeHandlers_Depth.cpp (case-insensitive, and it REJECTS an unrecognised
// value instead of defaulting, which is a validating contract this authoring one
// deliberately does not share).
//
// Anything unrecognised maps to Normal, which is exactly why None - and any
// priority a later engine adds - does not survive the round trip.
static EStateTreeTransitionPriority ParseTransitionPriority(const FString& Str)
{
	if (Str == TEXT("Low")) return EStateTreeTransitionPriority::Low;
	if (Str == TEXT("Medium")) return EStateTreeTransitionPriority::Medium;
	if (Str == TEXT("High")) return EStateTreeTransitionPriority::High;
	if (Str == TEXT("Critical")) return EStateTreeTransitionPriority::Critical;
	return EStateTreeTransitionPriority::Normal;
}

static FString TransitionPriorityToString(EStateTreeTransitionPriority Priority)
{
	switch (Priority)
	{
	case EStateTreeTransitionPriority::Low: return TEXT("Low");
	case EStateTreeTransitionPriority::Normal: return TEXT("Normal");
	case EStateTreeTransitionPriority::Medium: return TEXT("Medium");
	case EStateTreeTransitionPriority::High: return TEXT("High");
	case EStateTreeTransitionPriority::Critical: return TEXT("Critical");
	default: return TEXT("None");
	}
}

static EStateTreeTransitionType ParseTransitionType(const FString& Str)
{
	if (Str == TEXT("Succeeded")) return EStateTreeTransitionType::Succeeded;
	if (Str == TEXT("Failed")) return EStateTreeTransitionType::Failed;
	if (Str == TEXT("GotoState")) return EStateTreeTransitionType::GotoState;
	if (Str == TEXT("NextState")) return EStateTreeTransitionType::NextState;
	if (Str == TEXT("NextSelectableState")) return EStateTreeTransitionType::NextSelectableState;
	return EStateTreeTransitionType::None;
}

TSharedPtr<FJsonObject> FStateTreeHandlers::SerializeStateHierarchy(const UStateTreeState* State)
{
	if (!State) return nullptr;

	auto Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), State->Name.ToString());
	Obj->SetStringField(TEXT("id"), GuidToString(State->ID));
	Obj->SetStringField(TEXT("path"), GetStatePath(State));
	Obj->SetStringField(TEXT("type"), StateTypeToString(State->Type));
	Obj->SetStringField(TEXT("selectionBehavior"), SelectionBehaviorToString(State->SelectionBehavior));
	Obj->SetBoolField(TEXT("bEnabled"), State->bEnabled);
#if UE_MCP_HAS_STATETREE_STATE_DESCRIPTION
	Obj->SetStringField(TEXT("description"), State->Description);
#endif
	if (State->Tag.IsValid())
	{
		Obj->SetStringField(TEXT("tag"), State->Tag.ToString());
	}
#if UE_MCP_HAS_STATETREE_STATE_CUSTOM_TICK_RATE
	if (State->bHasCustomTickRate)
	{
		Obj->SetNumberField(TEXT("customTickRate"), State->CustomTickRate);
	}
#endif
	if (State->ColorRef.ID.IsValid())
	{
		const UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(State->GetOuter());
		if (!EditorData)
		{
			EditorData = Cast<UStateTreeEditorData>(State->GetTypedOuter<UStateTreeEditorData>());
		}
		if (EditorData)
		{
			const FStateTreeEditorColor* FoundColor = EditorData->FindColor(State->ColorRef);
			if (FoundColor)
			{
				Obj->SetStringField(TEXT("color"), FoundColor->DisplayName);
				Obj->SetStringField(TEXT("colorId"), GuidToString(FoundColor->ColorRef.ID));
			}
		}
	}

	if (State->LinkedAsset)
	{
		Obj->SetStringField(TEXT("linkedAsset"), State->LinkedAsset->GetPathName());
	}

	// Tasks
	TArray<TSharedPtr<FJsonValue>> TasksArr;
	for (const FStateTreeEditorNode& Task : State->Tasks)
	{
		TasksArr.Add(MakeShared<FJsonValueObject>(SerializeEditorNode(Task)));
	}
	if (State->SingleTask.Node.IsValid())
	{
		TasksArr.Add(MakeShared<FJsonValueObject>(SerializeEditorNode(State->SingleTask)));
	}
	Obj->SetArrayField(TEXT("tasks"), TasksArr);

	// Enter Conditions
	TArray<TSharedPtr<FJsonValue>> CondArr;
	for (const FStateTreeEditorNode& Cond : State->EnterConditions)
	{
		CondArr.Add(MakeShared<FJsonValueObject>(SerializeEditorNode(Cond)));
	}
	Obj->SetArrayField(TEXT("enterConditions"), CondArr);

	// Transitions
	TArray<TSharedPtr<FJsonValue>> TransArr;
	for (int32 i = 0; i < State->Transitions.Num(); ++i)
	{
		const FStateTreeTransition& Trans = State->Transitions[i];
		auto TransObj = MakeShared<FJsonObject>();
		TransObj->SetNumberField(TEXT("index"), i);
		TransObj->SetStringField(TEXT("id"), GuidToString(Trans.ID));
		TransObj->SetStringField(TEXT("trigger"), TransitionTriggerToString(Trans.Trigger));
		TransObj->SetStringField(TEXT("transitionType"), TransitionTypeToString(Trans.State.LinkType));

		if (Trans.RequiredEvent.Tag.IsValid())
		{
			TransObj->SetStringField(TEXT("eventTag"), Trans.RequiredEvent.Tag.ToString());
		}

		if (Trans.State.ID.IsValid())
		{
			TransObj->SetStringField(TEXT("targetStateId"), GuidToString(Trans.State.ID));
			const UStateTreeEditorData* EditorData =
				Cast<UStateTreeEditorData>(State->GetTypedOuter<UStateTreeEditorData>());
			if (EditorData)
			{
				if (const UStateTreeState* TargetState =
					EditorData->GetStateByID(Trans.State.ID))
				{
					TransObj->SetStringField(
						TEXT("targetStatePath"),
						GetStatePath(TargetState));
				}
			}
		}

		TransObj->SetBoolField(TEXT("bEnabled"), Trans.bTransitionEnabled);
		TransObj->SetBoolField(TEXT("bDelayTransition"), Trans.bDelayTransition);
		TransObj->SetNumberField(TEXT("delayDuration"), Trans.DelayDuration);
		TransObj->SetNumberField(
			TEXT("delayRandomVariance"),
			Trans.DelayRandomVariance);

		TArray<TSharedPtr<FJsonValue>> TransCondArr;
		for (const FStateTreeEditorNode& TCond : Trans.Conditions)
		{
			TransCondArr.Add(MakeShared<FJsonValueObject>(SerializeEditorNode(TCond)));
		}
		TransObj->SetArrayField(TEXT("conditions"), TransCondArr);

		TransArr.Add(MakeShared<FJsonValueObject>(TransObj));
	}
	Obj->SetArrayField(TEXT("transitions"), TransArr);

	// Children
	TArray<TSharedPtr<FJsonValue>> ChildArr;
	for (const TObjectPtr<UStateTreeState>& Child : State->Children)
	{
		if (Child)
		{
			ChildArr.Add(MakeShared<FJsonValueObject>(SerializeStateHierarchy(Child)));
		}
	}
	Obj->SetArrayField(TEXT("children"), ChildArr);

	return Obj;
}

static void SetInstancePropertiesFromJson(FInstancedStruct& Instance, const TSharedPtr<FJsonObject>& Properties)
{
	if (!Instance.IsValid() || !Properties.IsValid()) return;

	const UScriptStruct* Struct = Instance.GetScriptStruct();
	uint8* Memory = Instance.GetMutableMemory();
	if (!Struct || !Memory) return;

	for (const auto& Pair : Properties->Values)
	{
		FProperty* Prop = Struct->FindPropertyByName(*Pair.Key);
		if (!Prop) continue;

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Memory);
		FString ValueStr;

		if (Pair.Value->Type == EJson::String)
		{
			ValueStr = Pair.Value->AsString();
		}
		else if (Pair.Value->Type == EJson::Number)
		{
			ValueStr = FString::SanitizeFloat(Pair.Value->AsNumber());
		}
		else if (Pair.Value->Type == EJson::Boolean)
		{
			ValueStr = Pair.Value->AsBool() ? TEXT("true") : TEXT("false");
		}
		else
		{
			continue;
		}

		Prop->ImportText_Direct(*ValueStr, ValuePtr, nullptr, PPF_None);
	}
}

// #681: set properties on a UObject-backed node instance (BP task/condition/
// evaluator wrapper's InstanceObject), mirroring SetInstancePropertiesFromJson.
static void SetObjectPropertiesFromJson(UObject* Object, const TSharedPtr<FJsonObject>& Properties)
{
	if (!Object || !Properties.IsValid()) return;
	for (const auto& Pair : Properties->Values)
	{
		FProperty* Prop = Object->GetClass()->FindPropertyByName(*Pair.Key);
		if (!Prop) continue;
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Object);
		FString ValueStr;
		if (Pair.Value->Type == EJson::String) ValueStr = Pair.Value->AsString();
		else if (Pair.Value->Type == EJson::Number) ValueStr = FString::SanitizeFloat(Pair.Value->AsNumber());
		else if (Pair.Value->Type == EJson::Boolean) ValueStr = Pair.Value->AsBool() ? TEXT("true") : TEXT("false");
		else continue;
		Prop->ImportText_Direct(*ValueStr, ValuePtr, nullptr, PPF_None);
	}
}

static UScriptStruct* ResolveStructType(const FString& StructTypeName)
{
	UScriptStruct* Result = FindObject<UScriptStruct>(nullptr, *StructTypeName);
	if (Result) return Result;

	Result = FindFirstObject<UScriptStruct>(*StructTypeName, EFindFirstObjectOptions::NativeFirst);
	return Result;
}

static bool AddEditorNodeToArray(TArray<FStateTreeEditorNode>& Arr, const FString& StructTypeName, const TSharedPtr<FJsonObject>& InstanceProperties, UObject* Outer, FStateTreeEditorNode*& OutNode, FString& OutError)
{
	UScriptStruct* NodeStruct = ResolveStructType(StructTypeName);
	if (!NodeStruct)
	{
		OutError = FString::Printf(TEXT("Struct not found: %s"), *StructTypeName);
		return false;
	}

	FStateTreeEditorNode& EditorNode = Arr.AddDefaulted_GetRef();
	EditorNode.ID = FGuid::NewGuid();

	// #681: FStateTreeEditorNode::InitializeAs(Outer, NodeStruct) sets the node
	// struct AND allocates its instance data - including an InstanceObject when
	// the node's instance data type is a UClass (UObject-backed nodes such as
	// the Blueprint task/condition/evaluator wrappers), which the previous
	// struct-only path left null, so BP tasks could not be wired.
	// The 3-arg FStateTreeEditorNode::InitializeAs(Outer, NodeStruct) - which also
	// allocates the InstanceObject for UObject-backed (Blueprint-wrapped) nodes -
	// is UE 5.8+. On 5.7 we fall back to the struct-only path; BP-wrapped nodes
	// therefore do not get their InstanceObject auto-wired on 5.7 (#681).
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
	if (Outer)
	{
		EditorNode.InitializeAs(Outer, NodeStruct);
	}
	else
#endif
	{
		EditorNode.Node.InitializeAs(NodeStruct);
		const FStateTreeNodeBase& Node = EditorNode.Node.Get<FStateTreeNodeBase>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Node.GetInstanceDataType()))
		{
			EditorNode.Instance.InitializeAs(InstanceType);
		}
	}
#if !(ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))
	(void)Outer; // unused on 5.7 (the Outer-based path above is 5.8+ only)
#endif

	if (InstanceProperties.IsValid())
	{
		if (EditorNode.InstanceObject)
		{
			SetObjectPropertiesFromJson(EditorNode.InstanceObject, InstanceProperties);
		}
		else if (EditorNode.Instance.IsValid())
		{
			SetInstancePropertiesFromJson(EditorNode.Instance, InstanceProperties);
		}
	}

	OutNode = &EditorNode;
	return true;
}

static FStateTreeEditorNode* FindEditorNodeByID(TArray<FStateTreeEditorNode>& Arr, const FGuid& NodeID)
{
	for (FStateTreeEditorNode& Node : Arr)
	{
		if (Node.ID == NodeID)
		{
			return &Node;
		}
	}
	return nullptr;
}

static bool IsStructDerivedFrom(const UScriptStruct* TestStruct, const UScriptStruct* BaseStruct)
{
	if (!TestStruct || !BaseStruct) return false;
	return TestStruct->IsChildOf(BaseStruct);
}

bool FStateTreeHandlers::CompileAndSave(UStateTree* StateTree, TSharedPtr<FJsonObject>& OutResult)
{
	FStateTreeCompilerLog Log;
	const bool bSuccess = UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log);

	OutResult->SetBoolField(TEXT("compiled"), bSuccess);

	TArray<TSharedPtr<FJsonValue>> Errors;
	TArray<TSharedPtr<FJsonValue>> Warnings;

#if UE_MCP_HAS_STATETREE_COMPILER_TOKENIZED_MESSAGES
	for (const TSharedRef<FTokenizedMessage>& Msg : Log.ToTokenizedMessages())
	{
		FString MsgText = Msg->ToText().ToString();
		if (Msg->GetSeverity() == EMessageSeverity::Error)
		{
			Errors.Add(MakeShared<FJsonValueString>(MsgText));
		}
		else if (Msg->GetSeverity() == EMessageSeverity::Warning ||
				 Msg->GetSeverity() == EMessageSeverity::PerformanceWarning)
		{
			Warnings.Add(MakeShared<FJsonValueString>(MsgText));
		}
	}
#else
	if (!bSuccess)
	{
		Errors.Add(MakeShared<FJsonValueString>(TEXT("CompileStateTree returned failure; detailed compiler diagnostics are not exposed by the UE 5.5 FStateTreeCompilerLog API.")));
	}
#endif

	OutResult->SetArrayField(TEXT("errors"), Errors);
	OutResult->SetArrayField(TEXT("warnings"), Warnings);

	if (bSuccess)
	{
		SaveAssetPackage(StateTree);
	}

	return bSuccess;
}

// ── Read / Introspect ────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::ReadStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(MissingEditorDataMessage(AssetPath));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);

	if (EditorData->Schema)
	{
		const FString SchemaPath = EditorData->Schema->GetClass()->GetPathName();
		Result->SetStringField(TEXT("schemaClass"), SchemaPath);
		Result->SetStringField(TEXT("schemaPath"), SchemaPath);
	}

	// SubTrees (state hierarchy)
	TArray<TSharedPtr<FJsonValue>> SubTreesArr;
	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
	{
		if (SubTree)
		{
			SubTreesArr.Add(MakeShared<FJsonValueObject>(SerializeStateHierarchy(SubTree)));
		}
	}
	Result->SetArrayField(TEXT("subTrees"), SubTreesArr);

	// Evaluators
	TArray<TSharedPtr<FJsonValue>> EvalArr;
	for (const FStateTreeEditorNode& Eval : EditorData->Evaluators)
	{
		EvalArr.Add(MakeShared<FJsonValueObject>(SerializeEditorNode(Eval)));
	}
	Result->SetArrayField(TEXT("evaluators"), EvalArr);

	// Global Tasks
	TArray<TSharedPtr<FJsonValue>> GTArr;
	for (const FStateTreeEditorNode& GT : EditorData->GlobalTasks)
	{
		GTArr.Add(MakeShared<FJsonValueObject>(SerializeEditorNode(GT)));
	}
	Result->SetArrayField(TEXT("globalTasks"), GTArr);

	// Root Parameters
#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
	const FInstancedPropertyBag& RootParams = EditorData->GetRootParametersPropertyBag();
#else
	const FInstancedPropertyBag& RootParams = EditorData->RootParameters.Parameters;
#endif
	if (RootParams.IsValid())
	{
		auto ParamsObj = MakeShared<FJsonObject>();
		const UPropertyBag* BagStruct = RootParams.GetPropertyBagStruct();
		const uint8* BagMem = RootParams.GetValue().GetMemory();
		if (BagStruct && BagMem)
		{
			for (TFieldIterator<FProperty> It(BagStruct); It; ++It)
			{
				FString ValStr;
				const void* ValPtr = It->ContainerPtrToValuePtr<void>(BagMem);
				It->ExportTextItem_Direct(ValStr, ValPtr, nullptr, nullptr, PPF_None);
				ParamsObj->SetStringField(It->GetName(), ValStr);
			}
		}
		Result->SetObjectField(TEXT("rootParameters"), ParamsObj);
	}

	// Editor Bindings
	const FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings();
	if (Bindings)
	{
		TArray<TSharedPtr<FJsonValue>> BindArr;
		for (const FStateTreePropertyPathBinding& B : Bindings->GetBindings())
		{
			auto BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("sourceStructId"), GuidToString(B.GetSourcePath().GetStructID()));
			BObj->SetStringField(TEXT("sourcePath"), B.GetSourcePath().ToString());
			BObj->SetStringField(TEXT("targetStructId"), GuidToString(B.GetTargetPath().GetStructID()));
			BObj->SetStringField(TEXT("targetPath"), B.GetTargetPath().ToString());
			BindArr.Add(MakeShared<FJsonValueObject>(BObj));
		}
		Result->SetArrayField(TEXT("editorBindings"), BindArr);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::ListStates(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	// T3: paged.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_state_tree_states|assetPath=%s"), *AssetPath),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	auto Result = MCPSuccess();
	TArray<MCPPagination::FPageRow> Rows;

	TFunction<void(const UStateTreeState*)> CollectStates = [&](const UStateTreeState* State)
	{
		if (!State) return;
		auto SObj = MakeShared<FJsonObject>();
		SObj->SetStringField(TEXT("name"), State->Name.ToString());
		SObj->SetStringField(TEXT("id"), GuidToString(State->ID));
		SObj->SetStringField(TEXT("path"), GetStatePath(State));
		SObj->SetStringField(TEXT("type"), StateTypeToString(State->Type));
		// The state's GUID is the page anchor: it survives a rename and a move
		// to another parent, which the state path does not.
		Rows.Add({ GuidToString(State->ID), MakeShared<FJsonValueObject>(SObj) });

		for (const TObjectPtr<UStateTreeState>& Child : State->Children)
		{
			CollectStates(Child);
		}
	};

	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
	{
		CollectStates(SubTree);
	}
	// Depth-first in the tree's own authored order, which is the order the
	// StateTree editor shows and the order selection evaluates in, so the rows
	// are deliberately NOT sorted.

	MCPPagination::EmitPage(Page, Rows, TEXT("states"), Result);
	return MCPResult(Result);
}

// ── State Manipulation ───────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::AddState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString Name = Params->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MCPError(TEXT("name is required"));

	EStateTreeStateType StateType = EStateTreeStateType::State;
	if (Params->HasField(TEXT("stateType")))
	{
		StateType = ParseStateType(Params->GetStringField(TEXT("stateType")));
	}

	EStateTreeStateSelectionBehavior SelectionBehavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
	if (Params->HasField(TEXT("selectionBehavior")))
	{
		SelectionBehavior = ParseSelectionBehavior(Params->GetStringField(TEXT("selectionBehavior")));
	}

	UStateTreeState* ParentState = ResolveState(EditorData, Params);

	UStateTreeState* NewState = nullptr;

	if (ParentState)
	{
		ParentState->Modify();
		UStateTreeState& Ref = ParentState->AddChildState(FName(*Name), StateType);
		Ref.SelectionBehavior = SelectionBehavior;
		NewState = &Ref;

		if (Params->HasField(TEXT("insertIndex")))
		{
			const int32 Idx = static_cast<int32>(Params->GetNumberField(TEXT("insertIndex")));
			const int32 LastIdx = ParentState->Children.Num() - 1;
			if (Idx >= 0 && Idx < LastIdx)
			{
				TObjectPtr<UStateTreeState> Moved = ParentState->Children.Last();
				ParentState->Children.RemoveAt(LastIdx);
				ParentState->Children.Insert(Moved, Idx);
			}
		}
	}
	else
	{
		EditorData->Modify();
		UStateTreeState& Ref = EditorData->AddSubTree(FName(*Name));
		Ref.Type = StateType;
		Ref.SelectionBehavior = SelectionBehavior;
		NewState = &Ref;
	}

	if (Params->HasField(TEXT("linkedSubtree")))
	{
		const FString SubtreePath = Params->GetStringField(TEXT("linkedSubtree"));
		UStateTree* LinkedST = LoadObject<UStateTree>(nullptr, *SubtreePath);
		if (LinkedST)
		{
			NewState->LinkedAsset = LinkedST;
		}
	}

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("stateId"), GuidToString(NewState->ID));
	Result->SetStringField(TEXT("statePath"), GetStatePath(NewState));
	Result->SetStringField(TEXT("stateName"), NewState->Name.ToString());

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(NewState->ID));
		MCPSetRollback(Result, TEXT("remove_state_tree_state"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	UStateTreeState* Parent = State->Parent;

	// Captured before the removal: the inverse has to name the parent and the
	// sibling slot, and neither is readable once the state is detached.
	const FString PriorName = State->Name.ToString();
	const FString PriorType = StateTypeToString(State->Type);
	const FString PriorBehavior = SelectionBehaviorToString(State->SelectionBehavior);
	const FString PriorParentId = Parent ? GuidToString(Parent->ID) : FString();
	const int32 PriorIndex = Parent
		? Parent->Children.IndexOfByKey(State)
		: EditorData->SubTrees.IndexOfByKey(State);

	if (Parent)
	{
		Parent->Modify();
		Parent->Children.Remove(State);
	}
	else
	{
		EditorData->Modify();
		EditorData->SubTrees.Remove(State);
	}

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("removed"), true);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		// add_state_tree_state reads `stateId` as the PARENT to add under, and
		// adds a root subtree when it is absent.
		if (!PriorParentId.IsEmpty()) Payload->SetStringField(TEXT("stateId"), PriorParentId);
		Payload->SetStringField(TEXT("name"), PriorName);
		Payload->SetStringField(TEXT("stateType"), PriorType);
		Payload->SetStringField(TEXT("selectionBehavior"), PriorBehavior);
		if (!PriorParentId.IsEmpty() && PriorIndex >= 0)
		{
			Payload->SetNumberField(TEXT("insertIndex"), PriorIndex);
		}
		MCPSetRollback(Result, TEXT("add_state_tree_state"), Payload);
	}
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	{
		FString StateNote = PriorParentId.IsEmpty()
			? TEXT("statetree(add_state) re-creates an EMPTY state with a NEW stateId, appended as a root subtree. It has no insertIndex for root subtrees, so this state's position among them is not restored. ")
			: TEXT("statetree(add_state) re-creates an EMPTY state with a NEW stateId, under the same parent and at the same name, type, selection behaviour and sibling index. ");
		StateNote += TEXT("Its tasks, enter conditions, transitions, utility considerations, state parameters and child states are not restored, nor is any binding or transition elsewhere that named its old id. ");
		// The new GUID is what breaks a multi-step rollback, not just this record.
		StateNote += TEXT("Because the id changes, a rollback record from another step in the same flow that names this state or any state that was under it fails with \"State not found\" and leaves the chain half applied. ");
		StateNote += TEXT("Read the branch with statetree(read) before removing it if it has to come back whole.");
		Result->SetStringField(TEXT("rollbackNote"), StateNote);
	}
	return MCPResult(Result);
}

// The current value of one state property, in the same string form
// set_state_tree_state_property parses, so the inverse round-trips through the
// same action. False means there is no value the inverse could replay.
static bool StateTreeCapturePriorStateProperty(const UStateTreeState* State, const FString& PropName, FString& OutValue)
{
	if (!State) return false;
	if (PropName == TEXT("name")) { OutValue = State->Name.ToString(); return true; }
	if (PropName == TEXT("type")) { OutValue = StateTypeToString(State->Type); return true; }
	if (PropName == TEXT("selectionBehavior")) { OutValue = SelectionBehaviorToString(State->SelectionBehavior); return true; }
	if (PropName == TEXT("bEnabled")) { OutValue = State->bEnabled ? TEXT("true") : TEXT("false"); return true; }
	if (PropName == TEXT("bCheckPrerequisitesWhenActivatingChildDirectly"))
	{
		OutValue = State->bCheckPrerequisitesWhenActivatingChildDirectly ? TEXT("true") : TEXT("false");
		return true;
	}
	if (PropName == TEXT("weight")) { OutValue = FString::SanitizeFloat(State->Weight); return true; }
	if (PropName == TEXT("linkedAsset"))
	{
		// The setter rejects a path it cannot load, so a state that was not
		// linked has no prior value this action can be replayed with.
		if (!State->LinkedAsset) return false;
		OutValue = State->LinkedAsset->GetPathName();
		return true;
	}
	if (PropName == TEXT("tag")) { OutValue = State->Tag.IsValid() ? State->Tag.ToString() : FString(); return true; }
	if (PropName == TEXT("color")) { OutValue = State->ColorRef.ID.IsValid() ? GuidToString(State->ColorRef.ID) : FString(); return true; }
#if UE_MCP_HAS_STATETREE_STATE_DESCRIPTION
	if (PropName == TEXT("description")) { OutValue = State->Description; return true; }
#endif
#if UE_MCP_HAS_STATETREE_STATE_CUSTOM_TICK_RATE
	if (PropName == TEXT("customTickRate"))
	{
		OutValue = State->bHasCustomTickRate ? FString::SanitizeFloat(State->CustomTickRate) : FString();
		return true;
	}
#endif
	return false;
}

TSharedPtr<FJsonValue> FStateTreeHandlers::SetStateProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	State->Modify();

	const FString PropName = Params->GetStringField(TEXT("propertyName"));
	const FString Value = Params->GetStringField(TEXT("value"));

	// Read before the write, so the inverse replays what was actually there.
	FString PriorValue;
	const bool bHasPriorValue = StateTreeCapturePriorStateProperty(State, PropName, PriorValue);

	if (PropName == TEXT("name"))
	{
		State->Name = FName(*Value);
	}
	else if (PropName == TEXT("type"))
	{
		State->Type = ParseStateType(Value);
	}
	else if (PropName == TEXT("selectionBehavior"))
	{
		State->SelectionBehavior = ParseSelectionBehavior(Value);
	}
	else if (PropName == TEXT("bEnabled"))
	{
		State->bEnabled = Value.ToBool();
	}
	else if (PropName == TEXT("bCheckPrerequisitesWhenActivatingChildDirectly"))
	{
		State->bCheckPrerequisitesWhenActivatingChildDirectly = Value.ToBool();
	}
	else if (PropName == TEXT("weight"))
	{
		State->Weight = FCString::Atof(*Value);
	}
	else if (PropName == TEXT("linkedAsset"))
	{
		UStateTree* LinkedST = LoadObject<UStateTree>(nullptr, *Value);
		if (!LinkedST)
		{
			return MCPError(FString::Printf(TEXT("Linked StateTree not found: %s"), *Value));
		}
		State->LinkedAsset = LinkedST;
	}
	else if (PropName == TEXT("description"))
	{
#if UE_MCP_HAS_STATETREE_STATE_DESCRIPTION
		State->Description = Value;
#else
		return MCPError(TEXT("State description is not available in this UE version"));
#endif
	}
	else if (PropName == TEXT("tag"))
	{
		if (Value.IsEmpty())
		{
			State->Tag = FGameplayTag();
		}
		else
		{
			FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Value), /*bErrorIfNotFound=*/ false);
			if (!Tag.IsValid())
			{
				return MCPError(FString::Printf(TEXT("Gameplay tag not found: %s"), *Value));
			}
			State->Tag = Tag;
		}
	}
	else if (PropName == TEXT("customTickRate"))
	{
#if UE_MCP_HAS_STATETREE_STATE_CUSTOM_TICK_RATE
		if (Value.IsEmpty())
		{
			State->bHasCustomTickRate = false;
			State->CustomTickRate = 0.f;
		}
		else
		{
			State->bHasCustomTickRate = true;
			State->CustomTickRate = FCString::Atof(*Value);
		}
#else
		return MCPError(TEXT("State customTickRate is not available in this UE version"));
#endif
	}
	else if (PropName == TEXT("color"))
	{
		if (Value.IsEmpty())
		{
			State->ColorRef = FStateTreeEditorColorRef();
		}
		else
		{
			FGuid ColorGuid;
			if (FGuid::Parse(Value, ColorGuid))
			{
				const FStateTreeEditorColor* Found = EditorData->FindColor(FStateTreeEditorColorRef(ColorGuid));
				if (!Found)
				{
					return MCPError(FString::Printf(TEXT("Color not found with GUID: %s"), *Value));
				}
				State->ColorRef = Found->ColorRef;
			}
			else
			{
				const FStateTreeEditorColor* FoundByName = nullptr;
				int32 MatchCount = 0;
				for (const FStateTreeEditorColor& C : EditorData->Colors)
				{
					if (C.DisplayName == Value)
					{
						FoundByName = &C;
						++MatchCount;
					}
				}
				if (MatchCount == 0)
				{
					return MCPError(FString::Printf(TEXT("Color not found with name: %s. Use list_colors to see available colors."), *Value));
				}
				if (MatchCount > 1)
				{
					return MCPError(FString::Printf(TEXT("Ambiguous color name '%s' matches %d palette entries. Use the GUID instead."), *Value, MatchCount));
				}
				State->ColorRef = FoundByName->ColorRef;
			}
		}
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown state property: %s"), *PropName));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	if (bHasPriorValue)
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetStringField(TEXT("propertyName"), PropName);
		Payload->SetStringField(TEXT("value"), PriorValue);
		MCPSetRollback(Result, TEXT("set_state_tree_state_property"), Payload);
	}
	else
	{
		// Only reachable for linkedAsset: every other property name either has
		// a prior value or was rejected above.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This state had no linkedAsset before the call, and statetree(set_state_property) cannot be replayed with an empty one, so there is no inverse value. ")
			TEXT("Clear the link with statetree(set_state_link, linkType=\"none\")."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::ClearStateNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const int32 ClearedTasks = State->Tasks.Num();
	const int32 ClearedEnterConditions = State->EnterConditions.Num();
	const int32 ClearedTransitions = State->Transitions.Num();
	// SingleTask.Reset() clears Node, Instance, InstanceObject and ID together, so
	// testing Node alone would let a malformed slot survive under unchanged:true.
	const bool bHadSingleTask =
		State->SingleTask.Node.IsValid()
		|| State->SingleTask.Instance.IsValid()
		|| State->SingleTask.InstanceObject != nullptr
		|| State->SingleTask.ID.IsValid();

	// Nothing to clear means nothing to do: Modify() and ValidateStateTree would
	// dirty the package, which would make the unchanged report below untrue.
	const bool bAnythingToClear =
		(ClearedTasks + ClearedEnterConditions + ClearedTransitions) > 0 || bHadSingleTask;
	if (bAnythingToClear)
	{
		State->Modify();
		State->Tasks.Empty();
		State->EnterConditions.Empty();
		State->Transitions.Empty();
		State->SingleTask.Reset();

		UStateTreeEditingSubsystem::ValidateStateTree(ST);
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("cleared"), bAnythingToClear);
	Result->SetNumberField(TEXT("clearedTasks"), ClearedTasks);
	Result->SetNumberField(TEXT("clearedEnterConditions"), ClearedEnterConditions);
	Result->SetNumberField(TEXT("clearedTransitions"), ClearedTransitions);
	if (bAnythingToClear)
	{
		MCPSetUpdated(Result);
	}
	else
	{
		Result->SetBoolField(TEXT("unchanged"), true);
	}

	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("This drops every task, enter condition and transition plus the single-task slot in one call, and no single action rebuilds them: their struct types, instance values, operands, order and node ids are all gone. ")
		TEXT("Read the state with statetree(read_state) BEFORE clearing it and replay statetree(add_task) / statetree(add_enter_condition) / statetree(add_transition) from that."));
	return MCPResult(Result);
}

// ── Task / Condition Manipulation ────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::AddTask(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const FString StructType = Params->GetStringField(TEXT("structType"));
	if (StructType.IsEmpty()) return MCPError(TEXT("structType is required"));

	TSharedPtr<FJsonObject> InstanceProps;
	if (Params->HasField(TEXT("instanceProperties")))
	{
		InstanceProps = Params->GetObjectField(TEXT("instanceProperties"));
	}

	State->Modify();

	FStateTreeEditorNode* NewNode = nullptr;
	FString Error;
	if (!AddEditorNodeToArray(State->Tasks, StructType, InstanceProps, EditorData, NewNode, Error))
	{
		return MCPError(Error);
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeId"), GuidToString(NewNode->ID));
	Result->SetNumberField(TEXT("taskIndex"), State->Tasks.Num() - 1);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetNumberField(TEXT("taskIndex"), State->Tasks.Num() - 1);
		MCPSetRollback(Result, TEXT("remove_state_tree_task"), Payload);
	}
	// remove_state_tree_task addresses a task by position, and the bridge has no
	// remove-by-nodeId action to address it by identity instead.
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The inverse names this task by INDEX into the state's task list. ")
		TEXT("A later step that removes a task BELOW it shifts it down, and statetree(remove_task)'s own rollback re-adds by APPENDING rather than reinserting, so in a flow mixing the two this index can end up naming a different task and remove the wrong one. ")
		TEXT("There is no remove-task-by-nodeId action to address it by identity instead."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::AddEnterCondition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const FString StructType = Params->GetStringField(TEXT("structType"));
	if (StructType.IsEmpty()) return MCPError(TEXT("structType is required"));

	TSharedPtr<FJsonObject> InstanceProps;
	if (Params->HasField(TEXT("instanceProperties")))
	{
		InstanceProps = Params->GetObjectField(TEXT("instanceProperties"));
	}

	State->Modify();

	FStateTreeEditorNode* NewNode = nullptr;
	FString Error;
	if (!AddEditorNodeToArray(State->EnterConditions, StructType, InstanceProps, EditorData, NewNode, Error))
	{
		return MCPError(Error);
	}

	if (Params->HasField(TEXT("operand")))
	{
		const FString Op = Params->GetStringField(TEXT("operand"));
		if (Op == TEXT("Or")) NewNode->ExpressionOperand = EStateTreeExpressionOperand::Or;
		else NewNode->ExpressionOperand = EStateTreeExpressionOperand::And;
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeId"), GuidToString(NewNode->ID));
	Result->SetNumberField(TEXT("conditionIndex"), State->EnterConditions.Num() - 1);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetNumberField(TEXT("conditionIndex"), State->EnterConditions.Num() - 1);
		MCPSetRollback(Result, TEXT("remove_state_tree_enter_condition"), Payload);
	}
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The inverse names this condition by INDEX into the state's enter-condition list. ")
		TEXT("A later step that removes an enter condition BELOW it shifts it down, and statetree(remove_enter_condition)'s own rollback re-adds by APPENDING rather than reinserting, so in a flow mixing the two this index can end up naming a different condition and remove the wrong one. ")
		TEXT("There is no remove-enter-condition-by-nodeId action to address it by identity instead."));
	return MCPResult(Result);
}

// Every UPROPERTY of a node's instance data, in the string form the
// instanceProperties map accepts. UObject-backed (Blueprint-wrapped) nodes keep
// their values on InstanceObject rather than in the Instance struct, and
// AddEditorNodeToArray writes both back through that same map, so reading only
// the struct returned an empty map for every Blueprint node.
static TSharedPtr<FJsonObject> StateTreeCaptureNodeInstanceProperties(const FStateTreeEditorNode& Node)
{
	auto Props = MakeShared<FJsonObject>();
	if (UObject* InstanceObj = Node.InstanceObject)
	{
		for (TFieldIterator<FProperty> It(InstanceObj->GetClass()); It; ++It)
		{
			FString ValueStr;
			It->ExportTextItem_Direct(ValueStr, It->ContainerPtrToValuePtr<void>(InstanceObj), nullptr, nullptr, PPF_None);
			Props->SetStringField(It->GetName(), ValueStr);
		}
		return Props;
	}
	if (Node.Instance.IsValid() && Node.Instance.GetScriptStruct() && Node.Instance.GetMemory())
	{
		for (TFieldIterator<FProperty> It(Node.Instance.GetScriptStruct()); It; ++It)
		{
			FString ValueStr;
			It->ExportTextItem_Direct(ValueStr, It->ContainerPtrToValuePtr<void>(Node.Instance.GetMemory()), nullptr, nullptr, PPF_None);
			Props->SetStringField(It->GetName(), ValueStr);
		}
	}
	return Props;
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveEnterCondition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const int32 ConditionIndex = static_cast<int32>(Params->GetNumberField(TEXT("conditionIndex")));
	if (!State->EnterConditions.IsValidIndex(ConditionIndex))
	{
		return MCPError(FString::Printf(TEXT("Invalid conditionIndex: %d (state has %d enter conditions)"),
			ConditionIndex, State->EnterConditions.Num()));
	}

	// Captured before the removal so the inverse can rebuild the node.
	const FStateTreeEditorNode& DoomedCondition = State->EnterConditions[ConditionIndex];
	const UScriptStruct* DoomedCondStruct = DoomedCondition.Node.IsValid() ? DoomedCondition.Node.GetScriptStruct() : nullptr;
	const FString DoomedCondStructName = DoomedCondStruct ? DoomedCondStruct->GetName() : FString();
	const FString DoomedCondOperand = DoomedCondition.ExpressionOperand == EStateTreeExpressionOperand::Or ? TEXT("Or") : TEXT("And");
	const bool bCondHadInstanceObject = DoomedCondition.InstanceObject != nullptr;
	auto DoomedCondProps = StateTreeCaptureNodeInstanceProperties(DoomedCondition);

	State->Modify();
	State->EnterConditions.RemoveAt(ConditionIndex);
	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetStringField(TEXT("structType"), DoomedCondStructName);
	Result->SetNumberField(TEXT("conditionCount"), State->EnterConditions.Num());

	if (!DoomedCondStructName.IsEmpty())
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetStringField(TEXT("structType"), DoomedCondStructName);
		Payload->SetStringField(TEXT("operand"), DoomedCondOperand);
		Payload->SetObjectField(TEXT("instanceProperties"), DoomedCondProps);
		MCPSetRollback(Result, TEXT("add_state_tree_enter_condition"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), bCondHadInstanceObject
			? TEXT("The removed condition was Blueprint-backed. Its instance values ARE captured in the payload, but replaying adds the wrapper without its class, and statetree(set_node_class) reallocates the instance data when you set the class, discarding them. Set the class first, then write the values through editor(set_property) at the instanceObjectPath that statetree(set_node_class) returns, because the per-property setters reach a node's Instance struct only and reject a Blueprint-backed node with \"has no instance data\". It also appends at the END of the list with a NEW nodeId, so its position in the And/Or expression and any binding that named the old id are not restored.")
			: TEXT("Replaying this appends the condition at the END of the list with a NEW nodeId, so its position in the And/Or expression and any property binding that named the old id are not restored."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The removed condition carried no node struct, so there is no structType statetree(add_enter_condition) could be replayed with."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveTask(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const int32 TaskIndex = static_cast<int32>(Params->GetNumberField(TEXT("taskIndex")));
	if (!State->Tasks.IsValidIndex(TaskIndex))
	{
		return MCPError(FString::Printf(TEXT("Invalid taskIndex: %d (state has %d tasks)"), TaskIndex, State->Tasks.Num()));
	}

	// Captured before the removal so the inverse can rebuild the node.
	const FStateTreeEditorNode& DoomedTask = State->Tasks[TaskIndex];
	const UScriptStruct* DoomedTaskStruct = DoomedTask.Node.IsValid() ? DoomedTask.Node.GetScriptStruct() : nullptr;
	const FString DoomedTaskStructName = DoomedTaskStruct ? DoomedTaskStruct->GetName() : FString();
	const bool bTaskHadInstanceObject = DoomedTask.InstanceObject != nullptr;
	auto DoomedTaskProps = StateTreeCaptureNodeInstanceProperties(DoomedTask);

	State->Modify();
	State->Tasks.RemoveAt(TaskIndex);
	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetStringField(TEXT("structType"), DoomedTaskStructName);
	Result->SetNumberField(TEXT("taskCount"), State->Tasks.Num());

	if (!DoomedTaskStructName.IsEmpty())
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetStringField(TEXT("structType"), DoomedTaskStructName);
		Payload->SetObjectField(TEXT("instanceProperties"), DoomedTaskProps);
		MCPSetRollback(Result, TEXT("add_state_tree_task"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), bTaskHadInstanceObject
			? TEXT("The removed task was Blueprint-backed. Its instance values ARE captured in the payload, but replaying adds the wrapper without its class, and statetree(set_node_class) reallocates the instance data when you set the class, discarding them. Set the class first, then write the values through editor(set_property) at the instanceObjectPath that statetree(set_node_class) returns, because the per-property setters reach a node's Instance struct only and reject a Blueprint-backed node with \"has no instance data\". It also appends at the END of the task list with a NEW nodeId, so its taskIndex and any property binding that named the old id are not restored.")
			: TEXT("Replaying this appends the task at the END of the list with a NEW nodeId, so its taskIndex and any property binding that named the old id are not restored. Node-struct flags written with statetree(set_task_property) are not part of the payload either."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The removed task carried no node struct, so there is no structType statetree(add_task) could be replayed with."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::SetTaskInstanceProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const int32 TaskIndex = static_cast<int32>(Params->GetNumberField(TEXT("taskIndex")));
	if (!State->Tasks.IsValidIndex(TaskIndex))
	{
		return MCPError(FString::Printf(TEXT("Invalid taskIndex: %d"), TaskIndex));
	}

	FStateTreeEditorNode& TaskNode = State->Tasks[TaskIndex];
	if (!TaskNode.Instance.IsValid())
	{
		return MCPError(TEXT("Task has no instance data"));
	}

	State->Modify();

	const FString PropName = Params->GetStringField(TEXT("propertyName"));
	const FString Value = Params->GetStringField(TEXT("value"));

	const UScriptStruct* InstStruct = TaskNode.Instance.GetScriptStruct();
	uint8* InstMem = TaskNode.Instance.GetMutableMemory();
	FProperty* Prop = InstStruct->FindPropertyByName(*PropName);
	if (!Prop)
	{
		return MCPError(FString::Printf(TEXT("Property not found on instance data: %s"), *PropName));
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(InstMem);
	// Exported before the import so the inverse replays the value that was there.
	FString PriorValue;
	Prop->ExportTextItem_Direct(PriorValue, ValuePtr, nullptr, nullptr, PPF_None);
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetNumberField(TEXT("taskIndex"), TaskIndex);
		Payload->SetStringField(TEXT("propertyName"), PropName);
		Payload->SetStringField(TEXT("value"), PriorValue);
		MCPSetRollback(Result, TEXT("set_state_tree_task_instance_property"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::SetTaskProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const int32 TaskIndex = static_cast<int32>(Params->GetNumberField(TEXT("taskIndex")));
	if (!State->Tasks.IsValidIndex(TaskIndex))
	{
		return MCPError(FString::Printf(TEXT("Invalid taskIndex: %d (state has %d tasks)"), TaskIndex, State->Tasks.Num()));
	}

	FStateTreeEditorNode& TaskNode = State->Tasks[TaskIndex];
	if (!TaskNode.Node.IsValid())
	{
		return MCPError(TEXT("Task has no node data"));
	}

	const UScriptStruct* NodeStruct = TaskNode.Node.GetScriptStruct();
	uint8* NodeMem = TaskNode.Node.GetMutableMemory();
	if (!NodeStruct || !NodeMem)
	{
		return MCPError(TEXT("Task node struct/memory unavailable"));
	}

	const FString PropName = Params->GetStringField(TEXT("propertyName"));
	const FString Value = Params->GetStringField(TEXT("value"));

	FProperty* Prop = NodeStruct->FindPropertyByName(*PropName);
	if (!Prop)
	{
		return MCPError(FString::Printf(
			TEXT("Property not found on task node struct '%s': %s. Note: this action targets FStateTreeTaskBase-level UPROPERTYs (e.g. bConsideredForCompletion, bTaskEnabled). Use set_task_instance_property for instance data fields."),
			*NodeStruct->GetName(), *PropName));
	}

	State->Modify();

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(NodeMem);
	// Exported before the import so the inverse replays the value that was there.
	FString PriorValue;
	Prop->ExportTextItem_Direct(PriorValue, ValuePtr, nullptr, nullptr, PPF_None);
	const TCHAR* ImportResult = Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);
	if (!ImportResult)
	{
		return MCPError(FString::Printf(TEXT("Failed to parse value '%s' for property '%s' (type %s)"),
			*Value, *PropName, *Prop->GetClass()->GetName()));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("structType"), NodeStruct->GetName());
	Result->SetStringField(TEXT("propertyName"), PropName);
	Result->SetStringField(TEXT("value"), Value);
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetNumberField(TEXT("taskIndex"), TaskIndex);
		Payload->SetStringField(TEXT("propertyName"), PropName);
		Payload->SetStringField(TEXT("value"), PriorValue);
		MCPSetRollback(Result, TEXT("set_state_tree_task_property"), Payload);
	}
	return MCPResult(Result);
}

// ── Evaluator Manipulation ───────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::AddEvaluator(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString StructType = Params->GetStringField(TEXT("structType"));
	if (StructType.IsEmpty()) return MCPError(TEXT("structType is required"));

	UScriptStruct* NodeStruct = ResolveStructType(StructType);
	if (!NodeStruct)
	{
		return MCPError(FString::Printf(TEXT("Struct not found: %s"), *StructType));
	}
	if (!IsStructDerivedFrom(NodeStruct, FStateTreeEvaluatorBase::StaticStruct()))
	{
		return MCPError(FString::Printf(TEXT("Struct '%s' does not derive from FStateTreeEvaluatorBase"), *StructType));
	}

	TSharedPtr<FJsonObject> InstanceProps;
	if (Params->HasField(TEXT("instanceProperties")))
	{
		InstanceProps = Params->GetObjectField(TEXT("instanceProperties"));
	}

	EditorData->Modify();

	FStateTreeEditorNode* NewNode = nullptr;
	FString Error;
	if (!AddEditorNodeToArray(EditorData->Evaluators, StructType, InstanceProps, EditorData, NewNode, Error))
	{
		return MCPError(Error);
	}

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeId"), GuidToString(NewNode->ID));

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("nodeId"), GuidToString(NewNode->ID));
		MCPSetRollback(Result, TEXT("remove_state_tree_evaluator"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveEvaluator(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString NodeIdStr = Params->GetStringField(TEXT("nodeId"));
	FGuid NodeId;
	if (!FGuid::Parse(NodeIdStr, NodeId))
	{
		return MCPError(FString::Printf(TEXT("Invalid nodeId: %s"), *NodeIdStr));
	}

	for (int32 i = 0; i < EditorData->Evaluators.Num(); ++i)
	{
		if (EditorData->Evaluators[i].ID == NodeId)
		{
			// Captured before the removal so the inverse can rebuild the node.
			const FStateTreeEditorNode& Doomed = EditorData->Evaluators[i];
			const UScriptStruct* DoomedStruct = Doomed.Node.IsValid() ? Doomed.Node.GetScriptStruct() : nullptr;
			const FString DoomedStructName = DoomedStruct ? DoomedStruct->GetName() : FString();
			const bool bHadInstanceObject = Doomed.InstanceObject != nullptr;
			auto DoomedProps = StateTreeCaptureNodeInstanceProperties(Doomed);

			EditorData->Modify();
			EditorData->Evaluators.RemoveAt(i);
			UStateTreeEditingSubsystem::ValidateStateTree(ST);

			auto Result = MCPSuccess();
			MCPSetUpdated(Result);
			Result->SetBoolField(TEXT("removed"), true);
			Result->SetStringField(TEXT("structType"), DoomedStructName);
			Result->SetNumberField(TEXT("evaluatorCount"), EditorData->Evaluators.Num());

			if (!DoomedStructName.IsEmpty())
			{
				auto Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
				Payload->SetStringField(TEXT("structType"), DoomedStructName);
				Payload->SetObjectField(TEXT("instanceProperties"), DoomedProps);
				MCPSetRollback(Result, TEXT("add_state_tree_evaluator"), Payload);
				Result->SetBoolField(TEXT("rollbackLossy"), true);
				Result->SetStringField(TEXT("rollbackNote"), bHadInstanceObject
					? TEXT("The removed evaluator was Blueprint-backed. Its instance values ARE captured in the payload, but replaying adds the wrapper without its class, and statetree(set_node_class) reallocates the instance data when you set the class, discarding them. Set the class first, then write the values through editor(set_property) at the instanceObjectPath that statetree(set_node_class) returns, because the per-property setters reach a node's Instance struct only and reject a Blueprint-backed node with \"has no instance data\". It also appends at the END of the evaluator list with a NEW nodeId, so its evaluation order and any property binding that named the old id are not restored.")
					: TEXT("Replaying this appends the evaluator at the END of the list with a NEW nodeId, so its evaluation order and any property binding that named the old id are not restored. Node-struct fields written with statetree(set_evaluator_property) are not part of the payload either."));
			}
			else
			{
				Result->SetBoolField(TEXT("rollbackPossible"), false);
				Result->SetStringField(TEXT("rollbackNote"),
					TEXT("The removed evaluator carried no node struct, so there is no structType statetree(add_evaluator) could be replayed with."));
			}
			return MCPResult(Result);
		}
	}

	return MCPError(FString::Printf(TEXT("Evaluator node not found: %s"), *NodeIdStr));
}

TSharedPtr<FJsonValue> FStateTreeHandlers::SetEvaluatorInstanceProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString NodeIdStr = Params->GetStringField(TEXT("nodeId"));
	FGuid NodeId;
	if (!FGuid::Parse(NodeIdStr, NodeId))
	{
		return MCPError(FString::Printf(TEXT("Invalid nodeId: %s"), *NodeIdStr));
	}

	FStateTreeEditorNode* FoundNode = FindEditorNodeByID(EditorData->Evaluators, NodeId);
	if (!FoundNode)
	{
		return MCPError(FString::Printf(TEXT("Evaluator node not found: %s"), *NodeIdStr));
	}

	if (!FoundNode->Instance.IsValid())
	{
		return MCPError(TEXT("Evaluator has no instance data"));
	}

	const FString PropName = Params->GetStringField(TEXT("propertyName"));
	const FString Value = Params->GetStringField(TEXT("value"));

	const UScriptStruct* InstStruct = FoundNode->Instance.GetScriptStruct();
	uint8* InstMem = FoundNode->Instance.GetMutableMemory();
	FProperty* Prop = InstStruct->FindPropertyByName(*PropName);
	if (!Prop)
	{
		return MCPError(FString::Printf(TEXT("Property not found on evaluator instance data: %s"), *PropName));
	}

	EditorData->Modify();
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(InstMem);
	// Exported before the import so the inverse replays the value that was there.
	FString PriorValue;
	Prop->ExportTextItem_Direct(PriorValue, ValuePtr, nullptr, nullptr, PPF_None);
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
		Payload->SetStringField(TEXT("propertyName"), PropName);
		Payload->SetStringField(TEXT("value"), PriorValue);
		MCPSetRollback(Result, TEXT("set_state_tree_evaluator_instance_property"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::SetEvaluatorProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString NodeIdStr = Params->GetStringField(TEXT("nodeId"));
	FGuid NodeId;
	if (!FGuid::Parse(NodeIdStr, NodeId))
	{
		return MCPError(FString::Printf(TEXT("Invalid nodeId: %s"), *NodeIdStr));
	}

	FStateTreeEditorNode* FoundNode = FindEditorNodeByID(EditorData->Evaluators, NodeId);
	if (!FoundNode)
	{
		return MCPError(FString::Printf(TEXT("Evaluator node not found: %s"), *NodeIdStr));
	}

	if (!FoundNode->Node.IsValid())
	{
		return MCPError(TEXT("Evaluator has no node data"));
	}

	const UScriptStruct* NodeStruct = FoundNode->Node.GetScriptStruct();
	uint8* NodeMem = FoundNode->Node.GetMutableMemory();
	if (!NodeStruct || !NodeMem)
	{
		return MCPError(TEXT("Evaluator node struct/memory unavailable"));
	}

	const FString PropName = Params->GetStringField(TEXT("propertyName"));
	const FString Value = Params->GetStringField(TEXT("value"));

	FProperty* Prop = NodeStruct->FindPropertyByName(*PropName);
	if (!Prop)
	{
		return MCPError(FString::Printf(TEXT("Property not found on evaluator node struct '%s': %s"), *NodeStruct->GetName(), *PropName));
	}

	EditorData->Modify();
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(NodeMem);
	// Exported before the import so the inverse replays the value that was there.
	FString PriorValue;
	Prop->ExportTextItem_Direct(PriorValue, ValuePtr, nullptr, nullptr, PPF_None);
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("structType"), NodeStruct->GetName());
	Result->SetStringField(TEXT("propertyName"), PropName);
	Result->SetStringField(TEXT("value"), Value);
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
		Payload->SetStringField(TEXT("propertyName"), PropName);
		Payload->SetStringField(TEXT("value"), PriorValue);
		MCPSetRollback(Result, TEXT("set_state_tree_evaluator_property"), Payload);
	}
	return MCPResult(Result);
}

// ── Global Task Manipulation ────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::AddGlobalTask(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString StructType = Params->GetStringField(TEXT("structType"));
	if (StructType.IsEmpty()) return MCPError(TEXT("structType is required"));

	UScriptStruct* NodeStruct = ResolveStructType(StructType);
	if (!NodeStruct)
	{
		return MCPError(FString::Printf(TEXT("Struct not found: %s"), *StructType));
	}
	if (!IsStructDerivedFrom(NodeStruct, FStateTreeTaskBase::StaticStruct()))
	{
		return MCPError(FString::Printf(TEXT("Struct '%s' does not derive from FStateTreeTaskBase"), *StructType));
	}

	TSharedPtr<FJsonObject> InstanceProps;
	if (Params->HasField(TEXT("instanceProperties")))
	{
		InstanceProps = Params->GetObjectField(TEXT("instanceProperties"));
	}

	EditorData->Modify();

	FStateTreeEditorNode* NewNode = nullptr;
	FString Error;
	if (!AddEditorNodeToArray(EditorData->GlobalTasks, StructType, InstanceProps, EditorData, NewNode, Error))
	{
		return MCPError(Error);
	}

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeId"), GuidToString(NewNode->ID));

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("nodeId"), GuidToString(NewNode->ID));
		MCPSetRollback(Result, TEXT("remove_state_tree_global_task"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveGlobalTask(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString NodeIdStr = Params->GetStringField(TEXT("nodeId"));
	FGuid NodeId;
	if (!FGuid::Parse(NodeIdStr, NodeId))
	{
		return MCPError(FString::Printf(TEXT("Invalid nodeId: %s"), *NodeIdStr));
	}

	for (int32 i = 0; i < EditorData->GlobalTasks.Num(); ++i)
	{
		if (EditorData->GlobalTasks[i].ID == NodeId)
		{
			// Captured before the removal so the inverse can rebuild the node.
			const FStateTreeEditorNode& Doomed = EditorData->GlobalTasks[i];
			const UScriptStruct* DoomedStruct = Doomed.Node.IsValid() ? Doomed.Node.GetScriptStruct() : nullptr;
			const FString DoomedStructName = DoomedStruct ? DoomedStruct->GetName() : FString();
			const bool bHadInstanceObject = Doomed.InstanceObject != nullptr;
			auto DoomedProps = StateTreeCaptureNodeInstanceProperties(Doomed);

			EditorData->Modify();
			EditorData->GlobalTasks.RemoveAt(i);
			UStateTreeEditingSubsystem::ValidateStateTree(ST);

			auto Result = MCPSuccess();
			MCPSetUpdated(Result);
			Result->SetBoolField(TEXT("removed"), true);
			Result->SetStringField(TEXT("structType"), DoomedStructName);
			Result->SetNumberField(TEXT("globalTaskCount"), EditorData->GlobalTasks.Num());

			if (!DoomedStructName.IsEmpty())
			{
				auto Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
				Payload->SetStringField(TEXT("structType"), DoomedStructName);
				Payload->SetObjectField(TEXT("instanceProperties"), DoomedProps);
				MCPSetRollback(Result, TEXT("add_state_tree_global_task"), Payload);
				Result->SetBoolField(TEXT("rollbackLossy"), true);
				Result->SetStringField(TEXT("rollbackNote"), bHadInstanceObject
					? TEXT("The removed global task was Blueprint-backed. Its instance values ARE captured in the payload, but replaying adds the wrapper without its class, and statetree(set_node_class) reallocates the instance data when you set the class, discarding them. Set the class first, then write the values through editor(set_property) at the instanceObjectPath that statetree(set_node_class) returns, because the per-property setters reach a node's Instance struct only and reject a Blueprint-backed node with \"has no instance data\". It also appends at the END of the global task list with a NEW nodeId, so its order and any property binding that named the old id are not restored.")
					: TEXT("Replaying this appends the global task at the END of the list with a NEW nodeId, so its order and any property binding that named the old id are not restored. Node-struct fields written with statetree(set_global_task_property) are not part of the payload either."));
			}
			else
			{
				Result->SetBoolField(TEXT("rollbackPossible"), false);
				Result->SetStringField(TEXT("rollbackNote"),
					TEXT("The removed global task carried no node struct, so there is no structType statetree(add_global_task) could be replayed with."));
			}
			return MCPResult(Result);
		}
	}

	return MCPError(FString::Printf(TEXT("Global task node not found: %s"), *NodeIdStr));
}

TSharedPtr<FJsonValue> FStateTreeHandlers::SetGlobalTaskInstanceProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString NodeIdStr = Params->GetStringField(TEXT("nodeId"));
	FGuid NodeId;
	if (!FGuid::Parse(NodeIdStr, NodeId))
	{
		return MCPError(FString::Printf(TEXT("Invalid nodeId: %s"), *NodeIdStr));
	}

	FStateTreeEditorNode* FoundNode = FindEditorNodeByID(EditorData->GlobalTasks, NodeId);
	if (!FoundNode)
	{
		return MCPError(FString::Printf(TEXT("Global task node not found: %s"), *NodeIdStr));
	}

	if (!FoundNode->Instance.IsValid())
	{
		return MCPError(TEXT("Global task has no instance data"));
	}

	const FString PropName = Params->GetStringField(TEXT("propertyName"));
	const FString Value = Params->GetStringField(TEXT("value"));

	const UScriptStruct* InstStruct = FoundNode->Instance.GetScriptStruct();
	uint8* InstMem = FoundNode->Instance.GetMutableMemory();
	FProperty* Prop = InstStruct->FindPropertyByName(*PropName);
	if (!Prop)
	{
		return MCPError(FString::Printf(TEXT("Property not found on global task instance data: %s"), *PropName));
	}

	EditorData->Modify();
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(InstMem);
	// Exported before the import so the inverse replays the value that was there.
	FString PriorValue;
	Prop->ExportTextItem_Direct(PriorValue, ValuePtr, nullptr, nullptr, PPF_None);
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
		Payload->SetStringField(TEXT("propertyName"), PropName);
		Payload->SetStringField(TEXT("value"), PriorValue);
		MCPSetRollback(Result, TEXT("set_state_tree_global_task_instance_property"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::SetGlobalTaskProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString NodeIdStr = Params->GetStringField(TEXT("nodeId"));
	FGuid NodeId;
	if (!FGuid::Parse(NodeIdStr, NodeId))
	{
		return MCPError(FString::Printf(TEXT("Invalid nodeId: %s"), *NodeIdStr));
	}

	FStateTreeEditorNode* FoundNode = FindEditorNodeByID(EditorData->GlobalTasks, NodeId);
	if (!FoundNode)
	{
		return MCPError(FString::Printf(TEXT("Global task node not found: %s"), *NodeIdStr));
	}

	if (!FoundNode->Node.IsValid())
	{
		return MCPError(TEXT("Global task has no node data"));
	}

	const UScriptStruct* NodeStruct = FoundNode->Node.GetScriptStruct();
	uint8* NodeMem = FoundNode->Node.GetMutableMemory();
	if (!NodeStruct || !NodeMem)
	{
		return MCPError(TEXT("Global task node struct/memory unavailable"));
	}

	const FString PropName = Params->GetStringField(TEXT("propertyName"));
	const FString Value = Params->GetStringField(TEXT("value"));

	FProperty* Prop = NodeStruct->FindPropertyByName(*PropName);
	if (!Prop)
	{
		return MCPError(FString::Printf(TEXT("Property not found on global task node struct '%s': %s"), *NodeStruct->GetName(), *PropName));
	}

	EditorData->Modify();
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(NodeMem);
	// Exported before the import so the inverse replays the value that was there.
	FString PriorValue;
	Prop->ExportTextItem_Direct(PriorValue, ValuePtr, nullptr, nullptr, PPF_None);
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("structType"), NodeStruct->GetName());
	Result->SetStringField(TEXT("propertyName"), PropName);
	Result->SetStringField(TEXT("value"), Value);
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
		Payload->SetStringField(TEXT("propertyName"), PropName);
		Payload->SetStringField(TEXT("value"), PriorValue);
		MCPSetRollback(Result, TEXT("set_state_tree_global_task_property"), Payload);
	}
	return MCPResult(Result);
}

// ── Transition Manipulation ──────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::AddTransition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const FString TriggerStr = Params->GetStringField(TEXT("trigger"));
	const EStateTreeTransitionTrigger Trigger = ParseTransitionTrigger(TriggerStr);

	const FString TypeStr = Params->GetStringField(TEXT("transitionType"));
	const EStateTreeTransitionType TransType = ParseTransitionType(TypeStr);

	State->Modify();

	FStateTreeTransition* Trans = nullptr;

	if (Params->HasField(TEXT("eventTag")) && EnumHasAnyFlags(Trigger, EStateTreeTransitionTrigger::OnEvent))
	{
		FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(FName(*Params->GetStringField(TEXT("eventTag"))));
		Trans = &State->AddTransition(Trigger, EventTag, TransType, nullptr);
	}
	else
	{
		Trans = &State->AddTransition(Trigger, TransType, nullptr);
	}

	// Resolve target state for GotoState
	if (TransType == EStateTreeTransitionType::GotoState)
	{
		UStateTreeState* TargetState = nullptr;
		if (Params->HasField(TEXT("targetStateId")))
		{
			FGuid TargetId = ParseGuid(Params->GetStringField(TEXT("targetStateId")));
			TargetState = FindStateByID(EditorData, TargetId);
		}
		else if (Params->HasField(TEXT("targetStatePath")))
		{
			TargetState = FindStateByPath(EditorData, Params->GetStringField(TEXT("targetStatePath")));
		}

		if (TargetState)
		{
			Trans->State = TargetState->GetLinkToState();
		}
	}

	if (Params->HasField(TEXT("priority")))
	{
		Trans->Priority = ParseTransitionPriority(Params->GetStringField(TEXT("priority")));
	}

	if (Params->HasField(TEXT("bDelayTransition")))
	{
		Trans->bDelayTransition = Params->GetBoolField(TEXT("bDelayTransition"));
	}
	if (Params->HasField(TEXT("delayDuration")))
	{
		Trans->DelayDuration = static_cast<float>(Params->GetNumberField(TEXT("delayDuration")));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("transitionId"), GuidToString(Trans->ID));
	Result->SetNumberField(TEXT("transitionIndex"), State->Transitions.Num() - 1);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetNumberField(TEXT("transitionIndex"), State->Transitions.Num() - 1);
		MCPSetRollback(Result, TEXT("remove_state_tree_transition"), Payload);
	}
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The inverse names this transition by INDEX into the state's transition list. ")
		TEXT("A later step that removes a transition BELOW it shifts it down, and statetree(remove_transition)'s own rollback re-adds by APPENDING rather than reinserting, so in a flow mixing the two this index can end up naming a different transition and remove the wrong one. ")
		TEXT("There is no remove-transition-by-transitionId action to address it by identity instead."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::AddTransitionCondition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const int32 TransIndex = static_cast<int32>(Params->GetNumberField(TEXT("transitionIndex")));
	if (!State->Transitions.IsValidIndex(TransIndex))
	{
		return MCPError(FString::Printf(TEXT("Invalid transitionIndex: %d"), TransIndex));
	}

	const FString StructType = Params->GetStringField(TEXT("structType"));
	if (StructType.IsEmpty()) return MCPError(TEXT("structType is required"));

	TSharedPtr<FJsonObject> InstanceProps;
	if (Params->HasField(TEXT("instanceProperties")))
	{
		InstanceProps = Params->GetObjectField(TEXT("instanceProperties"));
	}

	State->Modify();

	FStateTreeEditorNode* NewNode = nullptr;
	FString Error;
	if (!AddEditorNodeToArray(State->Transitions[TransIndex].Conditions, StructType, InstanceProps, EditorData, NewNode, Error))
	{
		return MCPError(Error);
	}

	if (Params->HasField(TEXT("operand")))
	{
		const FString Op = Params->GetStringField(TEXT("operand"));
		if (Op == TEXT("Or")) NewNode->ExpressionOperand = EStateTreeExpressionOperand::Or;
		else NewNode->ExpressionOperand = EStateTreeExpressionOperand::And;
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeId"), GuidToString(NewNode->ID));
	Result->SetNumberField(TEXT("conditionIndex"), State->Transitions[TransIndex].Conditions.Num() - 1);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetNumberField(TEXT("transitionIndex"), TransIndex);
		Payload->SetNumberField(TEXT("conditionIndex"), State->Transitions[TransIndex].Conditions.Num() - 1);
		MCPSetRollback(Result, TEXT("remove_state_tree_transition_condition"), Payload);
	}
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The inverse names this condition by TWO indices: the transition's position in the state, and the condition's position in that transition. ")
		TEXT("A later step that removes a transition BELOW this one, or a condition BELOW this one inside the same transition, shifts it down, and the rollbacks of statetree(remove_transition) and statetree(remove_transition_condition) re-add by APPENDING rather than reinserting, so in a flow mixing the two this pair can end up naming a different condition and remove the wrong one. ")
		TEXT("There is no remove-transition-condition-by-nodeId action to address it by identity instead."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveTransition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const int32 TransIndex = static_cast<int32>(Params->GetNumberField(TEXT("transitionIndex")));
	if (!State->Transitions.IsValidIndex(TransIndex))
	{
		return MCPError(FString::Printf(TEXT("Invalid transitionIndex: %d"), TransIndex));
	}

	// Captured before the removal so the inverse can rebuild the transition.
	const FStateTreeTransition& Doomed = State->Transitions[TransIndex];
	const FString DoomedTrigger = TransitionTriggerToString(Doomed.Trigger);
	// The trigger is replayed as a STRING, and the string form covers only the
	// four flags TransitionTriggerToString knows. Round-tripping it here is what
	// catches a trigger carrying anything else - OnDelegate, or a flag a later
	// engine adds: OnTick|OnDelegate stringifies to "OnTick" and would replay as
	// a transition that no longer fires on the delegate.
	const bool bTriggerRoundTrips = ParseTransitionTrigger(DoomedTrigger) == Doomed.Trigger;
	// The same round trip for the priority. None sorts BELOW Low and replays as
	// Normal, which is a silent escalation; testing the round trip rather than
	// naming None catches a priority a later engine adds as well.
	const FString DoomedPriority = TransitionPriorityToString(Doomed.Priority);
	const bool bPriorityRoundTrips = ParseTransitionPriority(DoomedPriority) == Doomed.Priority;
	const FString DoomedType = TransitionTypeToString(Doomed.State.LinkType);
	const FString DoomedTargetId = Doomed.State.ID.IsValid() ? GuidToString(Doomed.State.ID) : FString();
	const FString DoomedEventTag = Doomed.RequiredEvent.Tag.IsValid() ? Doomed.RequiredEvent.Tag.ToString() : FString();
	const bool bDoomedDelay = Doomed.bDelayTransition;
	const float DoomedDelayDuration = Doomed.DelayDuration;
	const int32 DoomedConditionCount = Doomed.Conditions.Num();

	State->Modify();
	State->Transitions.RemoveAt(TransIndex);
	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetNumberField(TEXT("transitionCount"), State->Transitions.Num());

	if (bTriggerRoundTrips)
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetStringField(TEXT("trigger"), DoomedTrigger);
		Payload->SetStringField(TEXT("transitionType"), DoomedType);
		if (!DoomedTargetId.IsEmpty()) Payload->SetStringField(TEXT("targetStateId"), DoomedTargetId);
		if (!DoomedEventTag.IsEmpty()) Payload->SetStringField(TEXT("eventTag"), DoomedEventTag);
		Payload->SetStringField(TEXT("priority"), bPriorityRoundTrips ? DoomedPriority : TEXT("Normal"));
		Payload->SetBoolField(TEXT("bDelayTransition"), bDoomedDelay);
		Payload->SetNumberField(TEXT("delayDuration"), DoomedDelayDuration);
		MCPSetRollback(Result, TEXT("add_state_tree_transition"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);

		FString LossyNote =
			TEXT("Replaying this appends the transition at the END of the state's list with a NEW transitionId, so its transitionIndex is not restored. ")
			TEXT("The payload carries the trigger, transition type, target state, event tag, priority, bDelayTransition and delayDuration, and nothing else on the transition: ")
			TEXT("its conditions, its delegate listener, RequiredEvent.PayloadStruct, RequiredEvent.bConsumeEventOnSelect, DelayRandomVariance, bTransitionEnabled and ReactivateTargetState are all left at their defaults.");
		if (DoomedConditionCount > 0)
		{
			LossyNote += FString::Printf(
				TEXT(" The %d condition(s) on it are gone; replay each with statetree(add_transition_condition)."), DoomedConditionCount);
		}
		if (!bPriorityRoundTrips)
		{
			LossyNote += FString::Printf(
				TEXT(" Its priority ('%s') does not survive the string form statetree(add_transition) takes, which reads any unrecognised priority back as Normal, so the restored transition takes Normal and outranks anything at Low."),
				*DoomedPriority);
		}
		Result->SetStringField(TEXT("rollbackNote"), LossyNote);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("This transition's trigger does not survive the string form statetree(add_transition) takes: it stringifies to '%s', which reads back as a DIFFERENT trigger. ")
			TEXT("A trigger carrying OnDelegate, or carrying no flags at all, has that shape. Replaying an inverse here would create a transition that fires on different conditions, so none is offered: rebuild it by hand."),
			*DoomedTrigger));
	}
	return MCPResult(Result);
}

// ── Property Bindings ────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::AddBinding(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString SourceStructIdStr = Params->GetStringField(TEXT("sourceStructId"));
	const FString SourcePathStr = Params->GetStringField(TEXT("sourcePath"));
	const FString TargetStructIdStr = Params->GetStringField(TEXT("targetStructId"));
	const FString TargetPathStr = Params->GetStringField(TEXT("targetPath"));

	FUE_MCPStateTreePropertyPath SourcePath;
	SourcePath.SetStructID(ParseGuid(SourceStructIdStr));
	if (!SourcePath.FromString(SourcePathStr))
	{
		return MCPError(FString::Printf(TEXT("Failed to parse source path: %s"), *SourcePathStr));
	}

	FUE_MCPStateTreePropertyPath TargetPath;
	TargetPath.SetStructID(ParseGuid(TargetStructIdStr));
	if (!TargetPath.FromString(TargetPathStr))
	{
		return MCPError(FString::Printf(TEXT("Failed to parse target path: %s"), *TargetPathStr));
	}

	// A target property carries at most one binding, so adding over an existing
	// one replaces it. Read before the write: the inverse clears this binding
	// and cannot put the earlier one back.
	FString ReplacedSourceStructId;
	FString ReplacedSourcePath;
	bool bReplacedBinding = false;
	if (const FStateTreeEditorPropertyBindings* ExistingBindings = EditorData->GetPropertyEditorBindings())
	{
		const FGuid TargetStructGuid = ParseGuid(TargetStructIdStr);
		const FString TargetPathString = TargetPath.ToString();
		for (const FStateTreePropertyPathBinding& B : ExistingBindings->GetBindings())
		{
			if (B.GetTargetPath().GetStructID() == TargetStructGuid
				&& B.GetTargetPath().ToString() == TargetPathString)
			{
				bReplacedBinding = true;
				ReplacedSourceStructId = GuidToString(B.GetSourcePath().GetStructID());
				ReplacedSourcePath = B.GetSourcePath().ToString();
				break;
			}
		}
	}

	EditorData->Modify();
	EditorData->AddPropertyBinding(SourcePath, TargetPath);

	// #681: notify the target node its binding changed so nodes that adapt their
	// instance data to the source (e.g. FStateTreeCompareEnumCondition's
	// FStateTreeAnyEnum Left, which takes on the bound enum type) relink instead
	// of staying an unresolved wildcard.
	bool bNotified = false;
	{
		FStateTreeBindingLookup Lookup(EditorData);
		const FGuid TargetID = ParseGuid(TargetStructIdStr);
		auto TryNotify = [&](TArray<FStateTreeEditorNode>& Arr) -> bool
		{
			for (FStateTreeEditorNode& EN : Arr)
			{
				// FStateTreeEditorNode::GetInstanceDataID() is UE 5.8+. On 5.7 the
				// node's editor ID keys the instance data, so match on that.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
				if (EN.GetInstanceDataID() != TargetID) continue;
#else
				if (EN.ID != TargetID) continue;
#endif
				if (FStateTreeNodeBase* NodeBase = EN.Node.GetMutablePtr<FStateTreeNodeBase>())
				{
					NodeBase->OnBindingChanged(EN.ID, EN.GetInstance(), SourcePath, TargetPath, Lookup);
					return true;
				}
			}
			return false;
		};
		if (!bNotified) bNotified = TryNotify(EditorData->Evaluators);
		if (!bNotified) bNotified = TryNotify(EditorData->GlobalTasks);
		if (!bNotified)
		{
			TArray<UStateTreeState*> Stack;
			for (UStateTreeState* Root : EditorData->SubTrees) if (Root) Stack.Add(Root);
			while (Stack.Num() > 0 && !bNotified)
			{
				UStateTreeState* S = Stack.Pop();
				if (!S) continue;
				if (TryNotify(S->Tasks) || TryNotify(S->EnterConditions)) { bNotified = true; break; }
				for (FStateTreeTransition& Tr : S->Transitions) { if (TryNotify(Tr.Conditions)) { bNotified = true; break; } }
				for (UStateTreeState* Child : S->Children) if (Child) Stack.Add(Child);
			}
		}
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetBoolField(TEXT("bindingChangedNotified"), bNotified);
	Result->SetBoolField(TEXT("replacedExistingBinding"), bReplacedBinding);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("targetStructId"), TargetStructIdStr);
		Payload->SetStringField(TEXT("targetPath"), TargetPathStr);
		MCPSetRollback(Result, TEXT("remove_state_tree_binding"), Payload);
	}
	if (bReplacedBinding)
	{
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("This target property was already bound to %s:%s and that binding was replaced. statetree(remove_binding) clears the binding on this target without restoring the earlier one; put it back with statetree(add_binding, sourceStructId=\"%s\", sourcePath=\"%s\")."),
			*ReplacedSourceStructId, *ReplacedSourcePath, *ReplacedSourceStructId, *ReplacedSourcePath));
	}
	return MCPResult(Result);
}

// #681: enumerate the context/bindable sources in a StateTree so a caller knows
// what a property can bind FROM (context objects from the schema, parameters,
// evaluators, global tasks, and per-state nodes) with their struct IDs.
TSharedPtr<FJsonValue> FStateTreeHandlers::ListBindableSources(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	// T3: paged.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_state_tree_bindable_sources|assetPath=%s"), *AssetPath),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	TMap<FGuid, const FStateTreeDataView> AllValues;
	EditorData->GetAllStructValues(AllValues);

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(AllValues.Num());
	for (const TPair<FGuid, const FStateTreeDataView>& Pair : AllValues)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("structId"), Pair.Key.ToString());
		const UStruct* S = Pair.Value.GetStruct();
		Obj->SetStringField(TEXT("structType"), S ? S->GetName() : TEXT("(none)"));
		Obj->SetStringField(TEXT("structPath"), S ? S->GetPathName() : TEXT(""));
		// The struct GUID is the page anchor: it is what add_binding addresses
		// and it names one source across two enumerations.
		Rows.Add({ Pair.Key.ToString(), MakeShared<FJsonValueObject>(Obj) });
	}
	// TMap iterates in hash order, which is not a contract and moves as nodes
	// are added, so the rows are sorted by struct id before paging.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	MCPPagination::EmitPage(Page, Rows, TEXT("sources"), Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveBinding(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString TargetStructIdStr = Params->GetStringField(TEXT("targetStructId"));
	const FString TargetPathStr = Params->GetStringField(TEXT("targetPath"));

	FUE_MCPStateTreePropertyPath TargetPath;
	TargetPath.SetStructID(ParseGuid(TargetStructIdStr));
	if (!TargetPath.FromString(TargetPathStr))
	{
		return MCPError(FString::Printf(TEXT("Failed to parse target path: %s"), *TargetPathStr));
	}

	FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings();
	if (!Bindings)
	{
		return MCPError(TEXT("No bindings found on EditorData"));
	}

	// Read the bindings this is about to drop, so the inverse can name the
	// source each one came from.
	const FGuid TargetStructGuid = ParseGuid(TargetStructIdStr);
	const FString TargetPathString = TargetPath.ToString();
	TArray<TSharedPtr<FJsonValue>> RemovedBindings;
	FString FirstSourceStructId;
	FString FirstSourcePath;
	for (const FStateTreePropertyPathBinding& B : Bindings->GetBindings())
	{
		if (B.GetTargetPath().GetStructID() != TargetStructGuid
			|| B.GetTargetPath().ToString() != TargetPathString)
		{
			continue;
		}
		if (FirstSourceStructId.IsEmpty())
		{
			FirstSourceStructId = GuidToString(B.GetSourcePath().GetStructID());
			FirstSourcePath = B.GetSourcePath().ToString();
		}
		auto BObj = MakeShared<FJsonObject>();
		BObj->SetStringField(TEXT("sourceStructId"), GuidToString(B.GetSourcePath().GetStructID()));
		BObj->SetStringField(TEXT("sourcePath"), B.GetSourcePath().ToString());
		RemovedBindings.Add(MakeShared<FJsonValueObject>(BObj));
	}

	EditorData->Modify();
#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
	Bindings->RemoveBindings(TargetPath);
#else
	Bindings->RemovePropertyBindings(TargetPath);
#endif

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetArrayField(TEXT("removedBindings"), RemovedBindings);

	if (RemovedBindings.Num() == 0)
	{
		// Nothing was bound to this target, so there is no source for an
		// inverse to name.
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("No binding on this target path was found to remove, so there is no source path statetree(add_binding) could be replayed with."));
	}
	else
	{
		MCPSetUpdated(Result);
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("sourceStructId"), FirstSourceStructId);
		Payload->SetStringField(TEXT("sourcePath"), FirstSourcePath);
		Payload->SetStringField(TEXT("targetStructId"), TargetStructIdStr);
		Payload->SetStringField(TEXT("targetPath"), TargetPathStr);
		MCPSetRollback(Result, TEXT("add_state_tree_binding"), Payload);
		if (RemovedBindings.Num() > 1)
		{
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("%d bindings targeted this property and statetree(add_binding) restores one, the first, because a target carries a single binding. The rest are listed in removedBindings."),
				RemovedBindings.Num()));
		}
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::ListBindings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings();
	auto Result = MCPSuccess();

	TArray<TSharedPtr<FJsonValue>> BindArr;
	if (Bindings)
	{
		FGuid FilterStructId;
		if (Params->HasField(TEXT("structId")))
		{
			FilterStructId = ParseGuid(Params->GetStringField(TEXT("structId")));
		}

		for (const FStateTreePropertyPathBinding& B : Bindings->GetBindings())
		{
			if (FilterStructId.IsValid() &&
				B.GetSourcePath().GetStructID() != FilterStructId &&
				B.GetTargetPath().GetStructID() != FilterStructId)
			{
				continue;
			}

			auto BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("sourceStructId"), GuidToString(B.GetSourcePath().GetStructID()));
			BObj->SetStringField(TEXT("sourcePath"), B.GetSourcePath().ToString());
			BObj->SetStringField(TEXT("targetStructId"), GuidToString(B.GetTargetPath().GetStructID()));
			BObj->SetStringField(TEXT("targetPath"), B.GetTargetPath().ToString());
			BindArr.Add(MakeShared<FJsonValueObject>(BObj));
		}
	}

	Result->SetArrayField(TEXT("bindings"), BindArr);
	return MCPResult(Result);
}

// ── Color Palette ────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::ListColors(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	TArray<TSharedPtr<FJsonValue>> ColorsArr;
	for (const FStateTreeEditorColor& C : EditorData->Colors)
	{
		auto CObj = MakeShared<FJsonObject>();
		CObj->SetStringField(TEXT("id"), GuidToString(C.ColorRef.ID));
		CObj->SetStringField(TEXT("displayName"), C.DisplayName);
		CObj->SetStringField(TEXT("color"), FString::Printf(TEXT("(R=%f,G=%f,B=%f,A=%f)"), C.Color.R, C.Color.G, C.Color.B, C.Color.A));
		ColorsArr.Add(MakeShared<FJsonValueObject>(CObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("colors"), ColorsArr);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::AddColor(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	const FString DisplayName = Params->GetStringField(TEXT("displayName"));
	if (DisplayName.IsEmpty()) return MCPError(TEXT("displayName is required"));

	for (const FStateTreeEditorColor& C : EditorData->Colors)
	{
		if (C.DisplayName == DisplayName)
		{
			return MCPError(FString::Printf(TEXT("A color with name '%s' already exists"), *DisplayName));
		}
	}

	FStateTreeEditorColor NewColor;
	NewColor.DisplayName = DisplayName;

	if (Params->HasField(TEXT("color")))
	{
		const FString ColorStr = Params->GetStringField(TEXT("color"));
		NewColor.Color.InitFromString(ColorStr);
	}

	EditorData->Modify();
	EditorData->Colors.Add(NewColor);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("colorId"), GuidToString(NewColor.ColorRef.ID));
	Result->SetStringField(TEXT("displayName"), NewColor.DisplayName);

	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The bridge has no action that removes a palette entry, so there is no inverse to emit. The added colour is inert until a state references it: clear that reference with statetree(set_state_property, propertyName=\"color\", value=\"\")."));
	return MCPResult(Result);
}

// ── State Parameters ────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::ListStateParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const FInstancedPropertyBag& Bag = State->Parameters.Parameters;
	TArray<TSharedPtr<FJsonValue>> ParamsArr;

	if (const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct())
	{
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			auto PObj = MakeShared<FJsonObject>();
			PObj->SetStringField(TEXT("name"), Desc.Name.ToString());
			PObj->SetStringField(TEXT("id"), GuidToString(Desc.ID));

			FString TypeStr;
			switch (Desc.ValueType)
			{
			case EPropertyBagPropertyType::Bool:   TypeStr = TEXT("Bool"); break;
			case EPropertyBagPropertyType::Byte:   TypeStr = TEXT("Byte"); break;
			case EPropertyBagPropertyType::Int32:   TypeStr = TEXT("Int32"); break;
			case EPropertyBagPropertyType::Int64:   TypeStr = TEXT("Int64"); break;
			case EPropertyBagPropertyType::Float:   TypeStr = TEXT("Float"); break;
			case EPropertyBagPropertyType::Double:  TypeStr = TEXT("Double"); break;
			case EPropertyBagPropertyType::Name:    TypeStr = TEXT("Name"); break;
			case EPropertyBagPropertyType::String:  TypeStr = TEXT("String"); break;
			case EPropertyBagPropertyType::Text:    TypeStr = TEXT("Text"); break;
			case EPropertyBagPropertyType::Struct:  TypeStr = TEXT("Struct"); break;
			case EPropertyBagPropertyType::Object:  TypeStr = TEXT("Object"); break;
			case EPropertyBagPropertyType::Enum:    TypeStr = TEXT("Enum"); break;
			default: TypeStr = TEXT("Unknown"); break;
			}
			PObj->SetStringField(TEXT("type"), TypeStr);

			if (const uint8* BagMem = Bag.GetValue().GetMemory())
			{
				if (FProperty* Prop = BagStruct->FindPropertyByName(Desc.Name))
				{
					FString ValueStr;
					const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(BagMem);
					Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
					PObj->SetStringField(TEXT("value"), ValueStr);
				}
			}

			ParamsArr.Add(MakeShared<FJsonValueObject>(PObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("parameters"), ParamsArr);
	Result->SetBoolField(TEXT("bFixedLayout"), State->Parameters.bFixedLayout);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::AddStateParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	if (State->Parameters.bFixedLayout)
	{
		return MCPError(TEXT("Cannot add parameters to a state with fixed layout (linked state). Use set_state_parameter to override values instead."));
	}

	const FString ParamName = Params->GetStringField(TEXT("paramName"));
	if (ParamName.IsEmpty()) return MCPError(TEXT("paramName is required"));

	const FString ParamType = Params->GetStringField(TEXT("paramType"));
	if (ParamType.IsEmpty()) return MCPError(TEXT("paramType is required"));

	EPropertyBagPropertyType BagType;
	if (ParamType == TEXT("Bool")) BagType = EPropertyBagPropertyType::Bool;
	else if (ParamType == TEXT("Int32")) BagType = EPropertyBagPropertyType::Int32;
	else if (ParamType == TEXT("Int64")) BagType = EPropertyBagPropertyType::Int64;
	else if (ParamType == TEXT("Float")) BagType = EPropertyBagPropertyType::Float;
	else if (ParamType == TEXT("Double")) BagType = EPropertyBagPropertyType::Double;
	else if (ParamType == TEXT("Name")) BagType = EPropertyBagPropertyType::Name;
	else if (ParamType == TEXT("String")) BagType = EPropertyBagPropertyType::String;
	else if (ParamType == TEXT("Text")) BagType = EPropertyBagPropertyType::Text;
	else
	{
		return MCPError(FString::Printf(TEXT("Unsupported parameter type: %s. Supported: Bool, Int32, Int64, Float, Double, Name, String, Text"), *ParamType));
	}

	State->Modify();
	TArray<FPropertyBagPropertyDesc> NewDescs;
	NewDescs.Add(FPropertyBagPropertyDesc(FName(*ParamName), BagType));
#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
	State->Parameters.Parameters.AddProperties(NewDescs, /*bOverwrite=*/ false);
#else
	State->Parameters.Parameters.AddProperties(NewDescs);
#endif

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("paramName"), ParamName);
	Result->SetStringField(TEXT("paramType"), ParamType);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetStringField(TEXT("paramName"), ParamName);
		MCPSetRollback(Result, TEXT("remove_state_tree_state_parameter"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveStateParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	if (State->Parameters.bFixedLayout)
	{
		return MCPError(TEXT("Cannot remove parameters from a state with fixed layout (linked state)."));
	}

	const FString ParamName = Params->GetStringField(TEXT("paramName"));
	if (ParamName.IsEmpty()) return MCPError(TEXT("paramName is required"));

	// Read the descriptor and the current value before the removal: the inverse
	// has to name the type, and the value is worth reporting since the inverse
	// cannot carry it.
	FString PriorType;
	FString PriorValue;
	bool bParamExisted = false;
	{
		const FInstancedPropertyBag& Bag = State->Parameters.Parameters;
		if (const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct())
		{
			if (const FPropertyBagPropertyDesc* Desc = BagStruct->FindPropertyDescByName(FName(*ParamName)))
			{
				bParamExisted = true;
				// Only the types add_state_tree_state_parameter accepts can be
				// replayed; anything else leaves PriorType empty on purpose.
				switch (Desc->ValueType)
				{
				case EPropertyBagPropertyType::Bool:   PriorType = TEXT("Bool"); break;
				case EPropertyBagPropertyType::Int32:  PriorType = TEXT("Int32"); break;
				case EPropertyBagPropertyType::Int64:  PriorType = TEXT("Int64"); break;
				case EPropertyBagPropertyType::Float:  PriorType = TEXT("Float"); break;
				case EPropertyBagPropertyType::Double: PriorType = TEXT("Double"); break;
				case EPropertyBagPropertyType::Name:   PriorType = TEXT("Name"); break;
				case EPropertyBagPropertyType::String: PriorType = TEXT("String"); break;
				case EPropertyBagPropertyType::Text:   PriorType = TEXT("Text"); break;
				default: break;
				}
				if (const uint8* BagMem = Bag.GetValue().GetMemory())
				{
					if (FProperty* Prop = BagStruct->FindPropertyByName(Desc->Name))
					{
						Prop->ExportTextItem_Direct(PriorValue, Prop->ContainerPtrToValuePtr<void>(BagMem), nullptr, nullptr, PPF_None);
					}
				}
			}
		}
	}

	State->Modify();
	State->Parameters.Parameters.RemovePropertyByName(FName(*ParamName));

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("removed"), true);

	if (!bParamExisted)
	{
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			FString::Printf(TEXT("The state had no parameter named '%s', so this call changed nothing and there is nothing to restore."), *ParamName));
	}
	else
	{
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("paramType"), PriorType);
		Result->SetStringField(TEXT("priorValue"), PriorValue);
		if (!PriorType.IsEmpty())
		{
			auto Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
			Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
			Payload->SetStringField(TEXT("paramName"), ParamName);
			Payload->SetStringField(TEXT("paramType"), PriorType);
			MCPSetRollback(Result, TEXT("add_state_tree_state_parameter"), Payload);
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("statetree(add_state_parameter) recreates the parameter at its type default and appends it to the bag, so the value ('%s', reported as priorValue) and the parameter's position and id do not come back. ")
				TEXT("Follow the rollback with statetree(set_state_parameter, paramName=\"%s\", value=\"%s\"), and re-add any binding that named the old parameter id."),
				*PriorValue, *ParamName, *PriorValue));
		}
		else
		{
			Result->SetBoolField(TEXT("rollbackPossible"), false);
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("statetree(add_state_parameter) creates Bool, Int32, Int64, Float, Double, Name, String and Text parameters only, and this parameter is none of those, so it cannot recreate it. ")
				TEXT("Its type is reported as `type` by statetree(list_state_parameters); rebuild it in the editor or through editor(set_property) on the state's parameter bag."));
		}
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::SetStateParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found"));

	const FString ParamName = Params->GetStringField(TEXT("paramName"));
	if (ParamName.IsEmpty()) return MCPError(TEXT("paramName is required"));

	const FString Value = Params->GetStringField(TEXT("value"));

	FInstancedPropertyBag& Bag = State->Parameters.Parameters;
	const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
	if (!BagStruct)
	{
		return MCPError(TEXT("State has no parameter bag"));
	}

	FProperty* Prop = BagStruct->FindPropertyByName(FName(*ParamName));
	if (!Prop)
	{
		return MCPError(FString::Printf(TEXT("Parameter not found: %s"), *ParamName));
	}

	State->Modify();
	uint8* BagMem = Bag.GetMutableValue().GetMemory();
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(BagMem);
	// Exported before the import so the inverse replays the value that was there.
	FString PriorValue;
	Prop->ExportTextItem_Direct(PriorValue, ValuePtr, nullptr, nullptr, PPF_None);
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	const bool bFixedLayout = State->Parameters.bFixedLayout;
	if (bFixedLayout)
	{
		const FPropertyBagPropertyDesc* Desc = BagStruct->FindPropertyDescByName(FName(*ParamName));
		if (Desc)
		{
			State->SetParametersPropertyOverridden(Desc->ID, true);
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), GuidToString(State->ID));
		Payload->SetStringField(TEXT("paramName"), ParamName);
		Payload->SetStringField(TEXT("value"), PriorValue);
		MCPSetRollback(Result, TEXT("set_state_tree_state_parameter"), Payload);
	}
	if (bFixedLayout)
	{
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This is a fixed-layout (linked) state, so the call also marked the parameter as overridden. The inverse restores the value and marks it overridden again: it does not return the parameter to inheriting from the linked target."));
	}
	return MCPResult(Result);
}

// ── Root Parameters ──────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::SetRootParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found"));

	if (!Params->HasField(TEXT("parameters")))
	{
		return MCPError(TEXT("parameters array is required"));
	}

	const TArray<TSharedPtr<FJsonValue>>& ParamsArr = Params->GetArrayField(TEXT("parameters"));

	TArray<FUE_MCPStateTreePropertyCreationDesc> Descs;
	for (const TSharedPtr<FJsonValue>& PVal : ParamsArr)
	{
		const TSharedPtr<FJsonObject>& PObj = PVal->AsObject();
		if (!PObj) continue;

		const FString PropName = PObj->GetStringField(TEXT("name"));
		const FString TypeStr = PObj->GetStringField(TEXT("type"));

		EPropertyBagPropertyType BagType = EPropertyBagPropertyType::Float;
		if (TypeStr == TEXT("float")) BagType = EPropertyBagPropertyType::Float;
		else if (TypeStr == TEXT("int32") || TypeStr == TEXT("int")) BagType = EPropertyBagPropertyType::Int32;
		else if (TypeStr == TEXT("bool")) BagType = EPropertyBagPropertyType::Bool;
		else if (TypeStr == TEXT("name")) BagType = EPropertyBagPropertyType::Name;
		else if (TypeStr == TEXT("string")) BagType = EPropertyBagPropertyType::String;
		else if (TypeStr == TEXT("double")) BagType = EPropertyBagPropertyType::Double;

		FUE_MCPStateTreePropertyCreationDesc& Desc = Descs.AddDefaulted_GetRef();
		Desc.PropertyDesc = FPropertyBagPropertyDesc(FName(*PropName), BagType);
	}

	EditorData->Modify();
#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
	EditorData->CreateRootProperties(Descs);
#else
	EditorData->CreateParameters(EditorData->RootParameters.ID, Descs);
#endif

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetNumberField(TEXT("parameterCount"), Descs.Num());

	// CreateRootProperties adds the named properties to the root bag. Nothing
	// in the surface removes a root parameter, so replaying this action with
	// the previous list would leave the newly created ones in place rather than
	// undo them, and no other action gets back to the previous bag.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("There is no action that removes a root parameter, so the parameters this call created cannot be taken back out. ")
		TEXT("Read the tree with statetree(read) before calling it if the previous root parameter list has to be recoverable."));
	return MCPResult(Result);
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::CompileStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	if (auto SchemaErr = RequireSchema(ST, AssetPath)) return SchemaErr;

	auto Result = MCPSuccess();
	// CompileAndSave gates SaveAssetPackage on success, so `saved` is a fact this
	// handler can state. Whether a FAILED compile left the asset's in-memory
	// compiled data intact is not: the compiler resets it before rebuilding, and
	// nothing here can observe the outcome, so no claim is made about it.
	const bool bCompiled = CompileAndSave(ST, Result);
	Result->SetBoolField(TEXT("saved"), bCompiled);
	if (bCompiled)
	{
		MCPSetUpdated(Result);
	}

	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Compiling regenerates the asset's runtime data from its editor data and saves the package. There is no action that restores the previous compiled data, and recompiling produces this same result rather than the earlier one."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::ValidateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	if (auto SchemaErr = RequireSchema(ST, AssetPath)) return SchemaErr;

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), ST->GetPathName());
	if (UStateTreeEditorData* EditorData = GetEditorData(ST))
	{
		if (EditorData->Schema)
		{
			const FString SchemaPath =
				EditorData->Schema->GetClass()->GetPathName();
			Result->SetStringField(TEXT("schemaClass"), SchemaPath);
			Result->SetStringField(TEXT("schemaPath"), SchemaPath);
		}
	}
	Result->SetBoolField(TEXT("validated"), true);
	return MCPResult(Result);
}

// #833: set_state_tree_schema. The schema is what makes a StateTree compilable,
// and until now nothing in the surface could write one: every Schema reference
// in these handlers read it back. A tree that arrived without one - from
// asset(create_asset_by_class), or from any route that did not go through
// gameplay(create_state_tree) - was unrepairable through the bridge and had to
// be rebuilt by hand in the editor.
TSharedPtr<FJsonValue> FStateTreeHandlers::SetSchema(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	MCPStateTreeSchema::FResolution Schema = MCPStateTreeSchema::Resolve(
		OptionalString(Params, TEXT("schema")));
	if (!Schema.SchemaClass)
	{
		return MCPStateTreeSchema::UnresolvedError(Schema);
	}

	ST->Modify();
	const MCPStateTreeSchema::FAttachOutcome Attach =
		MCPStateTreeSchema::AttachSchema(ST, Schema.SchemaClass);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), ST->GetPathName());
	Result->SetStringField(TEXT("schema"), Schema.SchemaClass->GetPathName());
	Result->SetStringField(TEXT("schemaSource"), Schema.Source);
	Result->SetBoolField(TEXT("createdEditorData"), Attach.bCreatedEditorData);
	Result->SetBoolField(TEXT("createdRootState"), Attach.bCreatedRootState);
	Result->SetStringField(TEXT("previousSchema"), Attach.PreviousSchemaPath);
	if (!Schema.Note.IsEmpty())
	{
		Result->SetStringField(TEXT("schemaNote"), Schema.Note);
		Result->SetArrayField(TEXT("availableSchemas"), MCPStateTreeSchema::ConcreteSchemaPathsJson());
	}

	// Compile here rather than leaving it to the caller: the point of writing a
	// schema is that the tree can be built, and an asset saved without compiled
	// data still reports "failed to link" to anything that loads it.
	// CompileAndSave saves only on success. The schema landed either way, and a
	// tree that does not compile yet is a normal in-progress shape (an empty
	// root state with no tasks), so the write is saved regardless - otherwise
	// the repair would be lost on the next reload.
	const bool bCompiled = CompileAndSave(ST, Result);
	if (!bCompiled) SaveAssetPackage(ST);
	Result->SetBoolField(TEXT("saved"), true);
	if (Attach.bSchemaChanged || Attach.bCreatedEditorData) MCPSetUpdated(Result);
	else Result->SetBoolField(TEXT("updated"), false);

	if (Attach.PreviousSchemaPath.IsEmpty())
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The tree had no schema before this call, and nothing in this surface removes a schema: a StateTree ")
			TEXT("without one cannot compile, so putting it back is not a state worth restoring. Delete the asset if ")
			TEXT("the create was wrong."));
	}
	else
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("schema"), Attach.PreviousSchemaPath);
		MCPSetRollback(Result, TEXT("set_state_tree_schema"), Payload);
	}
	return MCPResult(Result);
}

#endif // UE_MCP_HAS_5_5_API
