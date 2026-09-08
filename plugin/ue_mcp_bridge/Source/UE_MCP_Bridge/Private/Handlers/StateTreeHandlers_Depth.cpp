// StateTree depth: discovering what can go in a tree, reading a state whole,
// utility considerations, in-asset subtree links, moving a state, Blueprint
// node classes, and driving a running tree.
//
// WHAT THE AUDIT FOUND (V8). The shipped StateTree surface is broad: 37
// actions covering states, tasks, enter conditions, transitions, transition
// conditions, evaluators, global tasks, bindings, the colour palette, state and
// root parameters, compile and validate. Six things were missing, and each one
// is a dead end rather than a convenience:
//
//   1. NO WAY TO DISCOVER A NODE TYPE. add_task, add_enter_condition,
//      add_transition_condition, add_evaluator and add_global_task all take a
//      `structType` C++ struct name and nothing enumerated them, so authoring
//      a tree meant guessing struct names. Behavior Trees have
//      list_bt_node_classes and EQS has list_eqs_types; StateTree had nothing.
//   2. UTILITY SELECTION WAS UNREACHABLE. set_state_property already accepts
//      selectionBehavior=TrySelectChildrenWithHighestUtility, and nothing could
//      add the considerations that produce the score, so the setting could be
//      written and could never do anything.
//   3. IN-ASSET SUBTREE LINKING WAS IMPOSSIBLE, and looked like it was not.
//      UStateTreeState has two separate link fields: LinkedSubtree (an
//      FStateTreeStateLink to a Subtree state in the SAME asset) and
//      LinkedAsset (another StateTree asset). add_state documents a
//      `linkedSubtree` parameter and assigns LinkedAsset with it, so the
//      in-asset half had no route at all. Both writes also bypassed
//      SetLinkedState / SetLinkedStateAsset, which are what call
//      UpdateParametersFromLinkedSubtree - so a linked state's parameter list
//      stayed empty and set_state_parameter had nothing to override.
//   4. STATES COULD NOT BE MOVED. add_state takes an insertIndex for a NEW
//      state; nothing could reorder or reparent an existing one. Sibling order
//      is load-bearing (TrySelectChildrenInOrder walks it, and NextState /
//      NextSelectableState transitions resolve through it), so "wrong order"
//      meant delete and rebuild.
//   5. BLUEPRINT NODES COULD NOT BE COMPLETED. A Blueprint task/condition/
//      evaluator/consideration is a native wrapper struct whose class property
//      names the Blueprint. The wrapper's GetInstanceDataType() RETURNS that
//      class, so a node added before the class is set allocates no instance
//      data, and setting the class afterwards does not go back and allocate it.
//   6. add_transition_condition HAD NO REMOVE.
//
// Runtime was half-covered: gameplay(get_state_tree_runtime) reports the active
// state names of a running StateTreeComponent and nothing else. It cannot say
// whether the tree is running, succeeded or failed, cannot show the event
// queue, and there was no way to SEND an event - which is how an event-driven
// StateTree is meant to be driven from outside at all.
//
// THERE ARE DELIBERATELY NO NEW TYPED SETTERS HERE. Transition priority,
// bTransitionEnabled, DelayDuration, DelayRandomVariance, ReactivateTargetState,
// RequiredEvent, RequiredEventToEnter, TasksCompletion, Weight and every other
// per-state or per-transition field is a UPROPERTY on the UStateTreeState
// UObject. read_state returns that state's objectPath, and editor(set_property)
// writes any of them at a dotted path such as `Transitions[0].Priority`, which
// is why read_state exists in this file and a set_transition_property does not.
//
// The 5.5 floor and the helpers on FStateTreeHandlers (LoadStateTree,
// GetEditorData, ResolveState, FindStateByID, FindStateByPath) are shared with
// StateTreeHandlers.cpp; the file-local helpers there are not, so the ones
// below carry a StateTreeDepth prefix. The module is a unity build, where two
// translation units sharing a blob merge their anonymous namespaces and a
// colliding helper name is a redefinition (C2084).

#include "StateTreeHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#if UE_MCP_HAS_5_5_API

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Runtime/Launch/Resources/Version.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "StructUtils/StructView.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorTypes.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeInstanceData.h"
#include "StateTreeNodeBase.h"
#include "StateTreeReference.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTypes.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"

// FStateTreeEditorNode::InitializeAs(Outer, Struct) and ReallocInstanceData are
// 5.8; on 5.7 the same two cases are handled by hand below.
#define UE_MCP_HAS_STATETREE_NODE_OUTER_INIT (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))
// UStateTree::GetStateHandleFromGameplayTag and its EStateGameplayTagQueryMethod
// do not exist before 5.8 (checked against the 5.7 header), so
// request_transition takes targetStateId there and says why.
#define UE_MCP_HAS_STATETREE_TAG_STATE_LOOKUP (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))

namespace
{
	FString StateTreeDepthGuid(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	FGuid StateTreeDepthParseGuid(const FString& Str)
	{
		FGuid G;
		FGuid::Parse(Str, G);
		return G;
	}

	/** Resolve a node struct by short name ("FStateTreeDelayTask"), by
	 *  /Script path, or by the display name list_node_types reports. */
	UScriptStruct* StateTreeDepthResolveStruct(const FString& StructTypeName)
	{
		if (StructTypeName.IsEmpty()) return nullptr;
		if (UScriptStruct* Direct = FindObject<UScriptStruct>(nullptr, *StructTypeName))
		{
			return Direct;
		}
		return FindFirstObject<UScriptStruct>(*StructTypeName, EFindFirstObjectOptions::NativeFirst);
	}

	/** The four node families a StateTree state or tree can hold, keyed by the
	 *  spelling every action in this category uses. */
	const UScriptStruct* StateTreeDepthBaseForKind(const FString& Kind)
	{
		if (Kind.Equals(TEXT("task"), ESearchCase::IgnoreCase)) return FStateTreeTaskBase::StaticStruct();
		if (Kind.Equals(TEXT("condition"), ESearchCase::IgnoreCase)) return FStateTreeConditionBase::StaticStruct();
		if (Kind.Equals(TEXT("evaluator"), ESearchCase::IgnoreCase)) return FStateTreeEvaluatorBase::StaticStruct();
		if (Kind.Equals(TEXT("consideration"), ESearchCase::IgnoreCase)) return FStateTreeConsiderationBase::StaticStruct();
		return nullptr;
	}

	/** The Blueprint base class whose subclasses are authored through each
	 *  family's native wrapper struct. */
	UClass* StateTreeDepthBlueprintBaseForKind(const FString& Kind)
	{
		if (Kind.Equals(TEXT("task"), ESearchCase::IgnoreCase)) return UStateTreeTaskBlueprintBase::StaticClass();
		if (Kind.Equals(TEXT("condition"), ESearchCase::IgnoreCase)) return UStateTreeConditionBlueprintBase::StaticClass();
		if (Kind.Equals(TEXT("evaluator"), ESearchCase::IgnoreCase)) return UStateTreeEvaluatorBlueprintBase::StaticClass();
		if (Kind.Equals(TEXT("consideration"), ESearchCase::IgnoreCase)) return UStateTreeConsiderationBlueprintBase::StaticClass();
		return nullptr;
	}

	/** The wrapper struct that carries a Blueprint node of each family, and the
	 *  name of the class property on it that names the Blueprint. */
	void StateTreeDepthWrapperForKind(const FString& Kind, const UScriptStruct*& OutStruct, FName& OutClassProperty)
	{
		OutStruct = nullptr;
		OutClassProperty = NAME_None;
		if (Kind.Equals(TEXT("task"), ESearchCase::IgnoreCase))
		{
			OutStruct = FStateTreeBlueprintTaskWrapper::StaticStruct();
			OutClassProperty = TEXT("TaskClass");
		}
		else if (Kind.Equals(TEXT("condition"), ESearchCase::IgnoreCase))
		{
			OutStruct = FStateTreeBlueprintConditionWrapper::StaticStruct();
			OutClassProperty = TEXT("ConditionClass");
		}
		else if (Kind.Equals(TEXT("evaluator"), ESearchCase::IgnoreCase))
		{
			OutStruct = FStateTreeBlueprintEvaluatorWrapper::StaticStruct();
			OutClassProperty = TEXT("EvaluatorClass");
		}
		else if (Kind.Equals(TEXT("consideration"), ESearchCase::IgnoreCase))
		{
			OutStruct = FStateTreeBlueprintConsiderationWrapper::StaticStruct();
			OutClassProperty = TEXT("ConsiderationClass");
		}
	}

	/** The instance data type a node struct declares, read off a throwaway
	 *  default-constructed node. This is the only way to answer "what keys does
	 *  instanceProperties take", because the type is a virtual function on the
	 *  node rather than metadata on the struct. */
	const UStruct* StateTreeDepthInstanceDataType(const UScriptStruct* NodeStruct)
	{
		if (!NodeStruct) return nullptr;
		FInstancedStruct Temp;
		Temp.InitializeAs(NodeStruct);
		if (!Temp.IsValid()) return nullptr;
		const FStateTreeNodeBase& NodeBase = Temp.Get<FStateTreeNodeBase>();
		return NodeBase.GetInstanceDataType();
	}

	/** Name, C++ type and current default of every property on a struct or
	 *  class, so a caller can fill instanceProperties without guessing. */
	TArray<TSharedPtr<FJsonValue>> StateTreeDepthDescribeProperties(const UStruct* Struct)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		if (!Struct) return Out;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Prop = *It;
			auto Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Prop->GetName());
			Obj->SetStringField(TEXT("type"), Prop->GetCPPType());
			Obj->SetStringField(TEXT("class"), Prop->GetClass()->GetName());
#if WITH_EDITOR
			const FString Tooltip = Prop->GetToolTipText().ToString();
			if (!Tooltip.IsEmpty()) Obj->SetStringField(TEXT("description"), Tooltip);
#endif
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		return Out;
	}

	/** Serialize one editor node in the shape the depth reads use: the same
	 *  fields the tree-wide read emits, plus the array position, which is what
	 *  every remove_* action in this category consumes. */
	TSharedPtr<FJsonObject> StateTreeDepthSerializeNode(const FStateTreeEditorNode& Node, int32 Index)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), Index);
		Obj->SetStringField(TEXT("nodeId"), StateTreeDepthGuid(Node.ID));

		const UScriptStruct* NodeStruct = Node.Node.IsValid() ? Node.Node.GetScriptStruct() : nullptr;
		Obj->SetStringField(TEXT("structType"), NodeStruct ? NodeStruct->GetName() : TEXT("None"));
		Obj->SetStringField(TEXT("structPath"), NodeStruct ? NodeStruct->GetPathName() : TEXT(""));

		if (NodeStruct && Node.Node.GetMemory())
		{
			auto NodeProps = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(NodeStruct); It; ++It)
			{
				FString ValueStr;
				It->ExportTextItem_Direct(ValueStr, It->ContainerPtrToValuePtr<void>(Node.Node.GetMemory()), nullptr, nullptr, PPF_None);
				NodeProps->SetStringField(It->GetName(), ValueStr);
			}
			Obj->SetObjectField(TEXT("nodeProperties"), NodeProps);
		}

		// Held as a raw pointer: TObjectPtr reaches ContainerPtrToValuePtr only
		// through two conversions, which makes the const/non-const overload
		// choice ambiguous.
		if (UObject* InstObj = Node.InstanceObject.Get())
		{
			Obj->SetStringField(TEXT("instanceObjectPath"), InstObj->GetPathName());
			Obj->SetStringField(TEXT("instanceClass"), InstObj->GetClass()->GetPathName());
			auto Props = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(InstObj->GetClass()); It; ++It)
			{
				FString ValueStr;
				It->ExportTextItem_Direct(ValueStr, It->ContainerPtrToValuePtr<void>(InstObj), nullptr, nullptr, PPF_None);
				Props->SetStringField(It->GetName(), ValueStr);
			}
			Obj->SetObjectField(TEXT("instanceProperties"), Props);
		}
		else if (Node.Instance.IsValid())
		{
			const UScriptStruct* InstStruct = Node.Instance.GetScriptStruct();
			Obj->SetStringField(TEXT("instanceStruct"), InstStruct ? InstStruct->GetName() : TEXT(""));
			auto Props = MakeShared<FJsonObject>();
			if (InstStruct && Node.Instance.GetMemory())
			{
				for (TFieldIterator<FProperty> It(InstStruct); It; ++It)
				{
					FString ValueStr;
					It->ExportTextItem_Direct(ValueStr, It->ContainerPtrToValuePtr<void>(Node.Instance.GetMemory()), nullptr, nullptr, PPF_None);
					Props->SetStringField(It->GetName(), ValueStr);
				}
			}
			Obj->SetObjectField(TEXT("instanceProperties"), Props);
		}
		else
		{
			Obj->SetBoolField(TEXT("instanceMissing"), true);
			Obj->SetStringField(TEXT("problem"), TEXT("This node has no instance data. For a Blueprint wrapper struct that means its class property is unset, or was set after the node was added; statetree(set_node_class) sets the class AND reallocates the instance."));
		}

		Obj->SetNumberField(TEXT("expressionIndent"), Node.ExpressionIndent);
		Obj->SetStringField(TEXT("operand"),
			Node.ExpressionOperand == EStateTreeExpressionOperand::Or ? TEXT("Or")
			: Node.ExpressionOperand == EStateTreeExpressionOperand::And ? TEXT("And")
			: TEXT("Copy"));
		return Obj;
	}

	/** Give an editor node the instance data its (possibly just-assigned) node
	 *  struct asks for. On 5.8 the engine has one call for it; below that the
	 *  same two cases are handled by hand, which is what that call does. */
	void StateTreeDepthReallocInstance(FStateTreeEditorNode& Node, UObject* Outer)
	{
#if UE_MCP_HAS_STATETREE_NODE_OUTER_INIT
		if (Outer)
		{
			Node.ReallocInstanceData(Outer);
			return;
		}
#endif
		const FStateTreeNodeBase* NodeBase = Node.Node.GetPtr<FStateTreeNodeBase>();
		const UStruct* InstType = NodeBase ? NodeBase->GetInstanceDataType() : nullptr;
		Node.Instance.Reset();
		Node.InstanceObject = nullptr;
		if (const UClass* InstClass = Cast<const UClass>(InstType))
		{
			if (Outer)
			{
				Node.InstanceObject = NewObject<UObject>(Outer, const_cast<UClass*>(InstClass));
			}
		}
		else if (const UScriptStruct* InstStruct = Cast<const UScriptStruct>(InstType))
		{
			Node.Instance.InitializeAs(InstStruct);
		}
	}

	/** Append a node of StructTypeName to Arr, allocating its instance data and
	 *  applying instanceProperties. Returns false with a reason that lists what
	 *  IS acceptable, since a wrong struct name is the common failure. */
	bool StateTreeDepthAddNode(
		TArray<FStateTreeEditorNode>& Arr,
		const FString& StructTypeName,
		const UScriptStruct* RequiredBase,
		const TCHAR* KindLabel,
		const TSharedPtr<FJsonObject>& InstanceProperties,
		UObject* Outer,
		FStateTreeEditorNode*& OutNode,
		FString& OutError)
	{
		UScriptStruct* NodeStruct = StateTreeDepthResolveStruct(StructTypeName);
		if (!NodeStruct)
		{
			OutError = FString::Printf(
				TEXT("Struct not found: '%s'. Pass the C++ struct name (for example FStateTreeConstantConsideration) or a /Script/Module.Struct path; statetree(list_node_types, nodeType=\"%s\") lists every one this tree's schema allows."),
				*StructTypeName, KindLabel);
			return false;
		}
		if (RequiredBase && !NodeStruct->IsChildOf(RequiredBase))
		{
			OutError = FString::Printf(
				TEXT("Struct '%s' does not derive from %s, so it cannot be used as a %s. statetree(list_node_types, nodeType=\"%s\") lists the ones that can."),
				*NodeStruct->GetName(), *RequiredBase->GetName(), KindLabel, KindLabel);
			return false;
		}

		FStateTreeEditorNode& EditorNode = Arr.AddDefaulted_GetRef();
#if UE_MCP_HAS_STATETREE_NODE_OUTER_INIT
		if (Outer)
		{
			EditorNode.InitializeAs(Outer, NodeStruct);
		}
		else
#endif
		{
			EditorNode.ID = FGuid::NewGuid();
			EditorNode.Node.InitializeAs(NodeStruct);
			StateTreeDepthReallocInstance(EditorNode, Outer);
		}

		if (InstanceProperties.IsValid())
		{
			for (const auto& Pair : InstanceProperties->Values)
			{
				FString ValueStr;
				if (Pair.Value->Type == EJson::String) ValueStr = Pair.Value->AsString();
				else if (Pair.Value->Type == EJson::Number) ValueStr = FString::SanitizeFloat(Pair.Value->AsNumber());
				else if (Pair.Value->Type == EJson::Boolean) ValueStr = Pair.Value->AsBool() ? TEXT("true") : TEXT("false");
				else continue;

				if (UObject* InstObj = EditorNode.InstanceObject.Get())
				{
					if (FProperty* Prop = InstObj->GetClass()->FindPropertyByName(*Pair.Key))
					{
						Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(InstObj), nullptr, PPF_None);
					}
				}
				else if (EditorNode.Instance.IsValid())
				{
					const UScriptStruct* InstStruct = EditorNode.Instance.GetScriptStruct();
					if (FProperty* Prop = InstStruct ? InstStruct->FindPropertyByName(*Pair.Key) : nullptr)
					{
						Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(EditorNode.Instance.GetMutableMemory()), nullptr, PPF_None);
					}
				}
			}
		}

		OutNode = &EditorNode;
		return true;
	}

	/** Find one editor node anywhere in the tree by its ID, and report which
	 *  array it lives in so an error can name the right remove action. */
	FStateTreeEditorNode* StateTreeDepthFindNodeById(
		UStateTreeEditorData* EditorData, const FGuid& NodeId, FString& OutLocation, UStateTreeState*& OutState)
	{
		OutLocation.Reset();
		OutState = nullptr;
		auto Search = [&](TArray<FStateTreeEditorNode>& Arr, const TCHAR* Label, UStateTreeState* State) -> FStateTreeEditorNode*
		{
			for (int32 i = 0; i < Arr.Num(); ++i)
			{
				if (Arr[i].ID == NodeId)
				{
					OutLocation = FString::Printf(TEXT("%s[%d]"), Label, i);
					OutState = State;
					return &Arr[i];
				}
			}
			return nullptr;
		};

		if (FStateTreeEditorNode* Found = Search(EditorData->Evaluators, TEXT("evaluators"), nullptr)) return Found;
		if (FStateTreeEditorNode* Found = Search(EditorData->GlobalTasks, TEXT("globalTasks"), nullptr)) return Found;

		TArray<UStateTreeState*> Stack;
		for (UStateTreeState* Root : EditorData->SubTrees) if (Root) Stack.Add(Root);
		while (Stack.Num() > 0)
		{
			UStateTreeState* S = Stack.Pop();
			if (!S) continue;
			if (FStateTreeEditorNode* Found = Search(S->Tasks, TEXT("tasks"), S)) return Found;
			if (FStateTreeEditorNode* Found = Search(S->EnterConditions, TEXT("enterConditions"), S)) return Found;
			if (FStateTreeEditorNode* Found = Search(S->Considerations, TEXT("considerations"), S)) return Found;
			for (int32 t = 0; t < S->Transitions.Num(); ++t)
			{
				const FString Label = FString::Printf(TEXT("transitions[%d].conditions"), t);
				if (FStateTreeEditorNode* Found = Search(S->Transitions[t].Conditions, *Label, S)) return Found;
			}
			if (S->SingleTask.ID == NodeId)
			{
				OutLocation = TEXT("singleTask");
				OutState = S;
				return &S->SingleTask;
			}
			for (UStateTreeState* Child : S->Children) if (Child) Stack.Add(Child);
		}
		return nullptr;
	}

	bool StateTreeDepthIsDescendant(const UStateTreeState* Candidate, const UStateTreeState* Ancestor)
	{
		for (const UStateTreeState* Walk = Candidate; Walk; Walk = Walk->Parent)
		{
			if (Walk == Ancestor) return true;
		}
		return false;
	}

	FString StateTreeDepthRunStatus(EStateTreeRunStatus Status)
	{
		switch (Status)
		{
		case EStateTreeRunStatus::Running:   return TEXT("Running");
		case EStateTreeRunStatus::Stopped:   return TEXT("Stopped");
		case EStateTreeRunStatus::Succeeded: return TEXT("Succeeded");
		case EStateTreeRunStatus::Failed:    return TEXT("Failed");
		default:                             return TEXT("Unset");
		}
	}

	/** A running StateTree lives on a component that keeps an
	 *  FStateTreeInstanceData 'InstanceData' and an FStateTreeReference
	 *  'StateTreeRef'. Both UStateTreeComponent and UStateTreeAIComponent are in
	 *  GameplayStateTreeModule, which this plugin does not link, so the
	 *  component is recognised by the shape of its properties instead. */
	struct FStateTreeDepthRuntime
	{
		UActorComponent* Component = nullptr;
		const UStateTree* StateTree = nullptr;
		FStateTreeInstanceData* InstanceData = nullptr;
	};

	TSharedPtr<FJsonValue> StateTreeDepthResolveRuntime(
		const TSharedPtr<FJsonObject>& Params, FStateTreeDepthRuntime& Out, FString& OutActorLabel)
	{
		const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("pie"));
		UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
		if (!World)
		{
			return MCPError(FString::Printf(
				TEXT("No world for scope '%s'. A StateTree only runs in a game world, so start PIE first (editor(play_in_editor)) and pass world=\"pie\" (the default), or world=\"auto\"."),
				*WorldScope));
		}

		FMCPActorSelector Selector;
		Selector.Match = EMCPActorMatch::LabelNameOrPath;
		Selector.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
		TSharedPtr<FJsonValue> ActorErr;
		AActor* Actor = MCPResolveActor(World, Params, ActorErr, Selector);
		if (!Actor) return ActorErr;
		OutActorLabel = Actor->GetActorLabel();

		const FString CompName = OptionalString(Params, TEXT("componentName"));
		TArray<FString> Candidates;
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (!Comp) continue;
			FStructProperty* IP = CastField<FStructProperty>(Comp->GetClass()->FindPropertyByName(TEXT("InstanceData")));
			FStructProperty* RP = CastField<FStructProperty>(Comp->GetClass()->FindPropertyByName(TEXT("StateTreeRef")));
			const bool bShaped = IP && IP->Struct == FStateTreeInstanceData::StaticStruct()
				&& RP && RP->Struct == FStateTreeReference::StaticStruct();
			if (!bShaped) continue;
			Candidates.Add(Comp->GetName());
			if (!CompName.IsEmpty() && Comp->GetName() != CompName) continue;

			FStateTreeReference* Ref = RP->ContainerPtrToValuePtr<FStateTreeReference>(Comp);
			Out.Component = Comp;
			Out.StateTree = Ref ? Ref->GetStateTree() : nullptr;
			Out.InstanceData = IP->ContainerPtrToValuePtr<FStateTreeInstanceData>(Comp);
			break;
		}

		if (!Out.Component)
		{
			if (Candidates.Num() > 0 && !CompName.IsEmpty())
			{
				return MCPError(FString::Printf(
					TEXT("'%s' has no StateTree component named '%s'. It has: %s."),
					*OutActorLabel, *CompName, *FString::Join(Candidates, TEXT(", "))));
			}
			return MCPError(FString::Printf(
				TEXT("No StateTree component on '%s'. Add one with gameplay(add_state_tree_component) and point its StateTreeRef at an asset."),
				*OutActorLabel));
		}
		if (!Out.StateTree)
		{
			return MCPError(FString::Printf(
				TEXT("Component '%s' on '%s' has no StateTree asset in its StateTreeRef. Set it with editor(set_property) at the component's object path."),
				*Out.Component->GetName(), *OutActorLabel));
		}
		if (!Out.InstanceData || Out.InstanceData->Num() == 0)
		{
			return MCPError(FString::Printf(
				TEXT("The StateTree on '%s' has not started, so it has no instance data to read or drive. It starts on BeginPlay in PIE; gameplay(get_state_tree_runtime) reports the same state as running=false."),
				*OutActorLabel));
		}
		return nullptr;
	}

	/** The post-call snapshot every runtime action returns, so a send or a
	 *  transition request can be checked without a second call. */
	void StateTreeDepthFillRuntimeSnapshot(FStateTreeExecutionContext& Context, TSharedPtr<FJsonObject> Result)
	{
		Result->SetStringField(TEXT("runStatus"), StateTreeDepthRunStatus(Context.GetStateTreeRunStatus()));
		Result->SetStringField(TEXT("lastTickStatus"), StateTreeDepthRunStatus(Context.GetLastTickStatus()));
		Result->SetNumberField(TEXT("stateChangeCount"), Context.GetStateChangeCount());
		Result->SetStringField(TEXT("activeState"), Context.GetActiveStateName());

		TArray<TSharedPtr<FJsonValue>> Names;
		for (const FName& N : Context.GetActiveStateNames())
		{
			Names.Add(MakeShared<FJsonValueString>(N.ToString()));
		}
		Result->SetArrayField(TEXT("activeStates"), Names);

		TArray<TSharedPtr<FJsonValue>> Events;
		for (const FStateTreeSharedEvent& Shared : Context.GetEventsToProcessView())
		{
			const FStateTreeEvent* Event = Shared.Get();
			if (!Event) continue;
			auto EObj = MakeShared<FJsonObject>();
			EObj->SetStringField(TEXT("tag"), Event->Tag.ToString());
			EObj->SetStringField(TEXT("origin"), Event->Origin.ToString());
			if (Event->Payload.IsValid() && Event->Payload.GetScriptStruct())
			{
				EObj->SetStringField(TEXT("payloadStruct"), Event->Payload.GetScriptStruct()->GetName());
			}
			Events.Add(MakeShared<FJsonValueObject>(EObj));
		}
		Result->SetArrayField(TEXT("pendingEvents"), Events);
		Result->SetNumberField(TEXT("pendingEventCount"), Events.Num());
	}
}

// ── Discovery ────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::ListStateTreeNodeTypes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found on StateTree"));

	const FString NodeTypeParam = OptionalString(Params, TEXT("nodeType"), TEXT("all"));
	const FString Filter = OptionalString(Params, TEXT("filter"));
	const bool bIncludeInstanceProperties = OptionalBool(Params, TEXT("includeInstanceProperties"), true);
	const bool bSchemaAllowedOnly = OptionalBool(Params, TEXT("schemaAllowedOnly"), true);

	static const TCHAR* AllKinds[] = { TEXT("task"), TEXT("condition"), TEXT("evaluator"), TEXT("consideration") };
	TArray<FString> Kinds;
	if (NodeTypeParam.Equals(TEXT("all"), ESearchCase::IgnoreCase))
	{
		for (const TCHAR* K : AllKinds) Kinds.Add(K);
	}
	else if (StateTreeDepthBaseForKind(NodeTypeParam))
	{
		Kinds.Add(NodeTypeParam);
	}
	else
	{
		return MCPError(FString::Printf(
			TEXT("Unknown nodeType '%s'. Valid values: task, condition, evaluator, consideration, all (default)."),
			*NodeTypeParam));
	}

	const UStateTreeSchema* Schema = EditorData->Schema;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), ST->GetPathName());
	if (Schema)
	{
		auto SchemaObj = MakeShared<FJsonObject>();
		SchemaObj->SetStringField(TEXT("schemaClass"), Schema->GetClass()->GetPathName());
		SchemaObj->SetBoolField(TEXT("allowEnterConditions"), Schema->AllowEnterConditions());
		SchemaObj->SetBoolField(TEXT("allowUtilityConsiderations"), Schema->AllowUtilityConsiderations());
		SchemaObj->SetBoolField(TEXT("allowEvaluators"), Schema->AllowEvaluators());
		SchemaObj->SetBoolField(TEXT("allowMultipleTasks"), Schema->AllowMultipleTasks());
		SchemaObj->SetBoolField(TEXT("allowGlobalParameters"), Schema->AllowGlobalParameters());
		SchemaObj->SetBoolField(TEXT("allowTasksCompletion"), Schema->AllowTasksCompletion());
		SchemaObj->SetBoolField(TEXT("scheduledTickAllowed"), Schema->IsScheduledTickAllowed());

		TArray<TSharedPtr<FJsonValue>> ContextData;
		for (const FStateTreeExternalDataDesc& Desc : Schema->GetContextDataDescs())
		{
			auto CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("name"), Desc.Name.ToString());
			CObj->SetStringField(TEXT("struct"), Desc.Struct ? Desc.Struct->GetPathName() : TEXT(""));
			ContextData.Add(MakeShared<FJsonValueObject>(CObj));
		}
		SchemaObj->SetArrayField(TEXT("contextData"), ContextData);
		Result->SetObjectField(TEXT("schema"), SchemaObj);
	}
	else
	{
		Result->SetStringField(TEXT("schemaWarning"), TEXT("This StateTree has no schema, so nothing is filtered and a node the runtime cannot use may still be listed."));
	}

	int32 Total = 0;
	for (const FString& Kind : Kinds)
	{
		const UScriptStruct* Base = StateTreeDepthBaseForKind(Kind);
		if (!Base) continue;

		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* Candidate = *It;
			if (!Candidate || Candidate == Base) continue;
			if (!Candidate->IsChildOf(Base)) continue;
			// The engine marks the abstract family bases Hidden; they are not
			// addable and listing them is the same as listing nothing.
			if (Candidate->HasMetaData(TEXT("Hidden"))) continue;
			if (!Filter.IsEmpty() && !Candidate->GetName().Contains(Filter)) continue;
			if (bSchemaAllowedOnly && Schema && !Schema->IsStructAllowed(Candidate)) continue;

			auto Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("structType"), Candidate->GetName());
			Obj->SetStringField(TEXT("structPath"), Candidate->GetPathName());
#if WITH_EDITOR
			Obj->SetStringField(TEXT("displayName"), Candidate->GetDisplayNameText().ToString());
			const FString Tooltip = Candidate->GetToolTipText().ToString();
			if (!Tooltip.IsEmpty()) Obj->SetStringField(TEXT("description"), Tooltip);
#endif
			const UStruct* InstType = StateTreeDepthInstanceDataType(Candidate);
			Obj->SetStringField(TEXT("instanceDataType"), InstType ? InstType->GetName() : TEXT(""));
			Obj->SetBoolField(TEXT("instanceDataIsClass"), InstType != nullptr && InstType->IsA<UClass>());
			if (bIncludeInstanceProperties)
			{
				Obj->SetArrayField(TEXT("instanceProperties"), StateTreeDepthDescribeProperties(InstType));
			}
			Obj->SetArrayField(TEXT("nodeProperties"), StateTreeDepthDescribeProperties(Candidate));
			Nodes.Add(MakeShared<FJsonValueObject>(Obj));
			++Total;
		}

		Nodes.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
		{
			return A->AsObject()->GetStringField(TEXT("structType")) < B->AsObject()->GetStringField(TEXT("structType"));
		});

		// Blueprint-authored nodes are not structs. Each family has one native
		// wrapper struct that carries a Blueprint class, so they are reported
		// separately with the two calls that actually author one.
		TArray<TSharedPtr<FJsonValue>> BlueprintNodes;
		if (UClass* BPBase = StateTreeDepthBlueprintBaseForKind(Kind))
		{
			for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
			{
				UClass* Candidate = *ClassIt;
				if (!Candidate || Candidate == BPBase) continue;
				if (!Candidate->IsChildOf(BPBase)) continue;
				if (Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
				if (!Filter.IsEmpty() && !Candidate->GetName().Contains(Filter)) continue;
				if (bSchemaAllowedOnly && Schema && !Schema->IsClassAllowed(Candidate)) continue;

				auto Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("classPath"), Candidate->GetPathName());
				Obj->SetStringField(TEXT("className"), Candidate->GetName());
				if (bIncludeInstanceProperties)
				{
					Obj->SetArrayField(TEXT("instanceProperties"), StateTreeDepthDescribeProperties(Candidate));
				}
				BlueprintNodes.Add(MakeShared<FJsonValueObject>(Obj));
			}
		}

		const UScriptStruct* WrapperStruct = nullptr;
		FName ClassPropertyName = NAME_None;
		StateTreeDepthWrapperForKind(Kind, WrapperStruct, ClassPropertyName);

		auto KindObj = MakeShared<FJsonObject>();
		KindObj->SetStringField(TEXT("baseStruct"), Base->GetName());
		KindObj->SetNumberField(TEXT("count"), Nodes.Num());
		KindObj->SetArrayField(TEXT("nodeTypes"), Nodes);
		KindObj->SetNumberField(TEXT("blueprintCount"), BlueprintNodes.Num());
		KindObj->SetArrayField(TEXT("blueprintNodeClasses"), BlueprintNodes);
		if (WrapperStruct)
		{
			KindObj->SetStringField(TEXT("blueprintWrapperStruct"), WrapperStruct->GetName());
			KindObj->SetStringField(TEXT("blueprintClassProperty"), ClassPropertyName.ToString());
			KindObj->SetStringField(TEXT("blueprintRecipe"), FString::Printf(
				TEXT("Add the wrapper with structType=\"%s\", then statetree(set_node_class, nodeId=<the returned nodeId>, nodeClass=<classPath>). The second call is required: the wrapper reports the Blueprint class AS its instance data type, so a node added before the class is set has no instance data at all."),
				*WrapperStruct->GetName()));
		}
		Result->SetObjectField(*Kind, KindObj);
	}

	Result->SetNumberField(TEXT("count"), Total);
	Result->SetStringField(TEXT("usage"), TEXT("structType is what add_task, add_enter_condition, add_transition_condition, add_consideration, add_evaluator and add_global_task take. instanceProperties names the keys those actions' instanceProperties map accepts, and nodeProperties names the keys set_task_property / set_evaluator_property / set_global_task_property accept."));
	return MCPResult(Result);
}

// ── Whole-state read ─────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::ReadState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found on StateTree"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State)
	{
		return MCPError(TEXT("State not found. Pass stateId (a GUID from statetree(list_states)) or statePath (the dot-path that action also returns)."));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), ST->GetPathName());
	Result->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
	Result->SetStringField(TEXT("statePath"), State->GetPath());
	Result->SetStringField(TEXT("name"), State->Name.ToString());
	// The whole point of this action: every field below that has no typed
	// setter is a UPROPERTY at this path.
	Result->SetStringField(TEXT("objectPath"), State->GetPathName());
	Result->SetStringField(TEXT("propertyWriteNote"), TEXT("Fields with no typed setter (Transitions[i].Priority, Transitions[i].bTransitionEnabled, Transitions[i].DelayRandomVariance, Transitions[i].ReactivateTargetState, RequiredEventToEnter, bHasRequiredEventToEnter, TasksCompletion) are UPROPERTYs on this object: write them with editor(set_property, objectPath=<objectPath>, propertyName=\"Transitions[0].Priority\", ...)."));

	Result->SetStringField(TEXT("type"), StaticEnum<EStateTreeStateType>()
		? StaticEnum<EStateTreeStateType>()->GetNameStringByValue(static_cast<int64>(State->Type))
		: FString());
	Result->SetStringField(TEXT("selectionBehavior"), StaticEnum<EStateTreeStateSelectionBehavior>()
		? StaticEnum<EStateTreeStateSelectionBehavior>()->GetNameStringByValue(static_cast<int64>(State->SelectionBehavior))
		: FString());
	Result->SetStringField(TEXT("tasksCompletion"), StaticEnum<EStateTreeTaskCompletionType>()
		? StaticEnum<EStateTreeTaskCompletionType>()->GetNameStringByValue(static_cast<int64>(State->TasksCompletion))
		: FString());
	Result->SetBoolField(TEXT("bEnabled"), State->bEnabled);
	Result->SetNumberField(TEXT("weight"), State->Weight);
	Result->SetStringField(TEXT("description"), State->Description);
	Result->SetStringField(TEXT("tag"), State->Tag.IsValid() ? State->Tag.ToString() : FString());
	Result->SetStringField(TEXT("parentStateId"), State->Parent ? StateTreeDepthGuid(State->Parent->ID) : FString());
	Result->SetNumberField(TEXT("childCount"), State->Children.Num());

	// Linking, both halves, because they are separate fields and one of them
	// had no route at all before set_state_link.
	{
		auto LinkObj = MakeShared<FJsonObject>();
		LinkObj->SetStringField(TEXT("linkedAsset"), State->LinkedAsset ? State->LinkedAsset->GetPathName() : FString());
		LinkObj->SetStringField(TEXT("linkedSubtreeStateId"), State->LinkedSubtree.ID.IsValid() ? StateTreeDepthGuid(State->LinkedSubtree.ID) : FString());
		LinkObj->SetStringField(TEXT("linkedSubtreeName"), State->LinkedSubtree.Name.ToString());
		if (State->LinkedSubtree.ID.IsValid())
		{
			if (const UStateTreeState* Target = EditorData->GetStateByID(State->LinkedSubtree.ID))
			{
				LinkObj->SetStringField(TEXT("linkedSubtreeStatePath"), Target->GetPath());
			}
			else
			{
				LinkObj->SetStringField(TEXT("problem"), TEXT("LinkedSubtree names a state ID that no longer exists in this asset. Repoint it with statetree(set_state_link) or clear it with linkType=\"none\"."));
			}
		}
		Result->SetObjectField(TEXT("link"), LinkObj);
	}

	// Required event to enter: a state-level gate with no typed setter, and
	// invisible in the tree-wide read.
	{
		auto EventObj = MakeShared<FJsonObject>();
		EventObj->SetBoolField(TEXT("enabled"), State->bHasRequiredEventToEnter);
		EventObj->SetStringField(TEXT("tag"), State->RequiredEventToEnter.Tag.ToString());
		EventObj->SetStringField(TEXT("payloadStruct"), State->RequiredEventToEnter.PayloadStruct
			? State->RequiredEventToEnter.PayloadStruct->GetPathName() : FString());
		Result->SetObjectField(TEXT("requiredEventToEnter"), EventObj);
	}

	auto SerializeArray = [](const TArray<FStateTreeEditorNode>& Arr)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < Arr.Num(); ++i)
		{
			Out.Add(MakeShared<FJsonValueObject>(StateTreeDepthSerializeNode(Arr[i], i)));
		}
		return Out;
	};

	Result->SetArrayField(TEXT("tasks"), SerializeArray(State->Tasks));
	Result->SetArrayField(TEXT("enterConditions"), SerializeArray(State->EnterConditions));
	Result->SetArrayField(TEXT("considerations"), SerializeArray(State->Considerations));
	if (State->SingleTask.Node.IsValid())
	{
		Result->SetObjectField(TEXT("singleTask"), StateTreeDepthSerializeNode(State->SingleTask, 0));
	}

	TArray<TSharedPtr<FJsonValue>> Transitions;
	for (int32 i = 0; i < State->Transitions.Num(); ++i)
	{
		const FStateTreeTransition& T = State->Transitions[i];
		auto TObj = MakeShared<FJsonObject>();
		TObj->SetNumberField(TEXT("index"), i);
		TObj->SetStringField(TEXT("transitionId"), StateTreeDepthGuid(T.ID));
		TObj->SetStringField(TEXT("trigger"), StaticEnum<EStateTreeTransitionTrigger>()
			? StaticEnum<EStateTreeTransitionTrigger>()->GetNameStringByValue(static_cast<int64>(T.Trigger))
			: FString());
		TObj->SetStringField(TEXT("transitionType"), StaticEnum<EStateTreeTransitionType>()
			? StaticEnum<EStateTreeTransitionType>()->GetNameStringByValue(static_cast<int64>(T.State.LinkType))
			: FString());
		TObj->SetStringField(TEXT("priority"), StaticEnum<EStateTreeTransitionPriority>()
			? StaticEnum<EStateTreeTransitionPriority>()->GetNameStringByValue(static_cast<int64>(T.Priority))
			: FString());
		TObj->SetBoolField(TEXT("bTransitionEnabled"), T.bTransitionEnabled);
		TObj->SetBoolField(TEXT("bDelayTransition"), T.bDelayTransition);
		TObj->SetNumberField(TEXT("delayDuration"), T.DelayDuration);
		TObj->SetNumberField(TEXT("delayRandomVariance"), T.DelayRandomVariance);
		TObj->SetStringField(TEXT("eventTag"), T.RequiredEvent.Tag.ToString());
		TObj->SetStringField(TEXT("eventPayloadStruct"), T.RequiredEvent.PayloadStruct
			? T.RequiredEvent.PayloadStruct->GetPathName() : FString());
		TObj->SetStringField(TEXT("targetStateId"), T.State.ID.IsValid() ? StateTreeDepthGuid(T.State.ID) : FString());
		if (T.State.ID.IsValid())
		{
			if (const UStateTreeState* Target = EditorData->GetStateByID(T.State.ID))
			{
				TObj->SetStringField(TEXT("targetStatePath"), Target->GetPath());
			}
			else
			{
				TObj->SetStringField(TEXT("problem"), TEXT("This transition targets a state ID that is no longer in the asset, so it will fail to compile."));
			}
		}
		TObj->SetStringField(TEXT("propertyPathPrefix"), FString::Printf(TEXT("Transitions[%d]"), i));
		TObj->SetArrayField(TEXT("conditions"), SerializeArray(T.Conditions));
		Transitions.Add(MakeShared<FJsonValueObject>(TObj));
	}
	Result->SetArrayField(TEXT("transitions"), Transitions);

	// Utility selection only does anything when both halves are present, and
	// the two ways to get it wrong are silent.
	TArray<TSharedPtr<FJsonValue>> Problems;
	const bool bUtilitySelection =
		State->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility
		|| State->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility;
	if (bUtilitySelection && State->Children.Num() > 0)
	{
		bool bAnyChildScores = false;
		for (const UStateTreeState* Child : State->Children)
		{
			if (Child && Child->Considerations.Num() > 0) { bAnyChildScores = true; break; }
		}
		if (!bAnyChildScores)
		{
			Problems.Add(MakeShared<FJsonValueString>(TEXT("selectionBehavior scores children by utility, but no child state has any consideration, so every child scores the same and selection falls back to order. Add one with statetree(add_consideration) on the CHILD states.")));
		}
	}
	if (!bUtilitySelection && State->Considerations.Num() > 0 && State->Parent)
	{
		const bool bParentScores =
			State->Parent->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility
			|| State->Parent->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility;
		if (!bParentScores)
		{
			Problems.Add(MakeShared<FJsonValueString>(TEXT("This state has considerations, but its parent's selectionBehavior does not score children by utility, so the score is never read. Set the PARENT's selectionBehavior to TrySelectChildrenWithHighestUtility or TrySelectChildrenAtRandomWeightedByUtility.")));
		}
	}
	if (State->Type == EStateTreeStateType::Linked && !State->LinkedSubtree.ID.IsValid())
	{
		Problems.Add(MakeShared<FJsonValueString>(TEXT("Type is Linked but LinkedSubtree points at nothing. Set it with statetree(set_state_link, linkType=\"subtree\").")));
	}
	if (State->Type == EStateTreeStateType::LinkedAsset && !State->LinkedAsset)
	{
		Problems.Add(MakeShared<FJsonValueString>(TEXT("Type is LinkedAsset but LinkedAsset is unset. Set it with statetree(set_state_link, linkType=\"asset\").")));
	}
	Result->SetArrayField(TEXT("problems"), Problems);

	return MCPResult(Result);
}

// ── Utility considerations ───────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::AddConsideration(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found on StateTree"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found. Pass stateId or statePath."));

	FString StructType;
	if (auto Err = RequireString(Params, TEXT("structType"), StructType)) return Err;

	if (EditorData->Schema && !EditorData->Schema->AllowUtilityConsiderations())
	{
		return MCPError(FString::Printf(
			TEXT("Schema '%s' does not allow utility considerations, so this tree cannot score its states. statetree(list_node_types) reports allowUtilityConsiderations for the schema in use."),
			*EditorData->Schema->GetClass()->GetName()));
	}

	TSharedPtr<FJsonObject> InstanceProps;
	if (Params->HasField(TEXT("instanceProperties")))
	{
		InstanceProps = Params->GetObjectField(TEXT("instanceProperties"));
	}

	State->Modify();
	FStateTreeEditorNode* NewNode = nullptr;
	FString Error;
	if (!StateTreeDepthAddNode(State->Considerations, StructType,
			FStateTreeConsiderationBase::StaticStruct(), TEXT("consideration"),
			InstanceProps, EditorData, NewNode, Error))
	{
		return MCPError(Error);
	}

	if (Params->HasField(TEXT("operand")))
	{
		const FString Op = Params->GetStringField(TEXT("operand"));
		NewNode->ExpressionOperand = Op.Equals(TEXT("Or"), ESearchCase::IgnoreCase)
			? EStateTreeExpressionOperand::Or
			: EStateTreeExpressionOperand::And;
	}

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeId"), StateTreeDepthGuid(NewNode->ID));
	Result->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
	Result->SetNumberField(TEXT("considerationIndex"), State->Considerations.Num() - 1);
	Result->SetNumberField(TEXT("considerationCount"), State->Considerations.Num());

	// The score is only read when the PARENT selects by utility, so say so at
	// the point of authoring rather than letting it fail silently.
	if (State->Parent)
	{
		const bool bParentScores =
			State->Parent->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility
			|| State->Parent->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility;
		Result->SetBoolField(TEXT("parentScoresByUtility"), bParentScores);
		if (!bParentScores)
		{
			Result->SetStringField(TEXT("warning"), TEXT("The parent state's selectionBehavior does not score children by utility, so this consideration is never evaluated. Set the parent with statetree(set_state_property, propertyName=\"selectionBehavior\", value=\"TrySelectChildrenWithHighestUtility\")."));
		}
	}

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
		Payload->SetNumberField(TEXT("considerationIndex"), State->Considerations.Num() - 1);
		MCPSetRollback(Result, TEXT("remove_state_tree_consideration"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveConsideration(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found on StateTree"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found. Pass stateId or statePath."));

	if (!Params->HasField(TEXT("considerationIndex")))
	{
		return MCPError(FString::Printf(
			TEXT("Missing required parameter 'considerationIndex'. This state has %d consideration(s); statetree(read_state) lists each one with its index."),
			State->Considerations.Num()));
	}
	const int32 Index = static_cast<int32>(Params->GetNumberField(TEXT("considerationIndex")));
	if (!State->Considerations.IsValidIndex(Index))
	{
		return MCPError(FString::Printf(
			TEXT("Invalid considerationIndex %d. Valid values: %s."),
			Index,
			State->Considerations.Num() > 0
				? *FString::Printf(TEXT("0 to %d"), State->Considerations.Num() - 1)
				: TEXT("none, the state has no considerations")));
	}

	// Capture what is about to be lost so the inverse can rebuild it.
	const FStateTreeEditorNode& Doomed = State->Considerations[Index];
	const UScriptStruct* DoomedStruct = Doomed.Node.IsValid() ? Doomed.Node.GetScriptStruct() : nullptr;
	auto PriorProps = MakeShared<FJsonObject>();
	if (Doomed.Instance.IsValid() && Doomed.Instance.GetScriptStruct() && Doomed.Instance.GetMemory())
	{
		for (TFieldIterator<FProperty> It(Doomed.Instance.GetScriptStruct()); It; ++It)
		{
			FString ValueStr;
			It->ExportTextItem_Direct(ValueStr, It->ContainerPtrToValuePtr<void>(Doomed.Instance.GetMemory()), nullptr, nullptr, PPF_None);
			PriorProps->SetStringField(It->GetName(), ValueStr);
		}
	}
	const FString DoomedStructName = DoomedStruct ? DoomedStruct->GetName() : FString();
	const FString DoomedOperand = Doomed.ExpressionOperand == EStateTreeExpressionOperand::Or ? TEXT("Or") : TEXT("And");
	const bool bHadInstanceObject = Doomed.InstanceObject != nullptr;

	State->Modify();
	State->Considerations.RemoveAt(Index);
	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
	Result->SetStringField(TEXT("structType"), DoomedStructName);
	Result->SetNumberField(TEXT("considerationCount"), State->Considerations.Num());

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
		Payload->SetStringField(TEXT("structType"), DoomedStructName);
		Payload->SetStringField(TEXT("operand"), DoomedOperand);
		Payload->SetObjectField(TEXT("instanceProperties"), PriorProps);
		Payload->SetStringField(TEXT("lossy"), bHadInstanceObject
			? TEXT("The removed consideration was Blueprint-backed, so replaying this adds the wrapper without its class; follow it with statetree(set_node_class). The node also gets a NEW nodeId, so any binding that named the old one is gone.")
			: TEXT("Replaying this appends the consideration at the END of the list with a NEW nodeId, so its position in an And/Or expression and any property binding that named the old id are not restored."));
		MCPSetRollback(Result, TEXT("add_state_tree_consideration"), Payload);
	}
	return MCPResult(Result);
}

// ── Transition condition removal ─────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveTransitionCondition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found on StateTree"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found. Pass stateId or statePath."));

	if (!Params->HasField(TEXT("transitionIndex")))
	{
		return MCPError(FString::Printf(
			TEXT("Missing required parameter 'transitionIndex'. This state has %d transition(s); statetree(read_state) lists each with its index and conditions."),
			State->Transitions.Num()));
	}
	const int32 TransIndex = static_cast<int32>(Params->GetNumberField(TEXT("transitionIndex")));
	if (!State->Transitions.IsValidIndex(TransIndex))
	{
		return MCPError(FString::Printf(
			TEXT("Invalid transitionIndex %d. Valid values: %s."),
			TransIndex,
			State->Transitions.Num() > 0
				? *FString::Printf(TEXT("0 to %d"), State->Transitions.Num() - 1)
				: TEXT("none, the state has no transitions")));
	}

	FStateTreeTransition& Transition = State->Transitions[TransIndex];
	if (!Params->HasField(TEXT("conditionIndex")))
	{
		return MCPError(FString::Printf(
			TEXT("Missing required parameter 'conditionIndex'. Transition %d has %d condition(s)."),
			TransIndex, Transition.Conditions.Num()));
	}
	const int32 CondIndex = static_cast<int32>(Params->GetNumberField(TEXT("conditionIndex")));
	if (!Transition.Conditions.IsValidIndex(CondIndex))
	{
		return MCPError(FString::Printf(
			TEXT("Invalid conditionIndex %d on transition %d. Valid values: %s."),
			CondIndex, TransIndex,
			Transition.Conditions.Num() > 0
				? *FString::Printf(TEXT("0 to %d"), Transition.Conditions.Num() - 1)
				: TEXT("none, the transition has no conditions")));
	}

	const FStateTreeEditorNode& Doomed = Transition.Conditions[CondIndex];
	const UScriptStruct* DoomedStruct = Doomed.Node.IsValid() ? Doomed.Node.GetScriptStruct() : nullptr;
	const FString DoomedStructName = DoomedStruct ? DoomedStruct->GetName() : FString();
	const FString DoomedOperand = Doomed.ExpressionOperand == EStateTreeExpressionOperand::Or ? TEXT("Or") : TEXT("And");
	auto PriorProps = MakeShared<FJsonObject>();
	if (Doomed.Instance.IsValid() && Doomed.Instance.GetScriptStruct() && Doomed.Instance.GetMemory())
	{
		for (TFieldIterator<FProperty> It(Doomed.Instance.GetScriptStruct()); It; ++It)
		{
			FString ValueStr;
			It->ExportTextItem_Direct(ValueStr, It->ContainerPtrToValuePtr<void>(Doomed.Instance.GetMemory()), nullptr, nullptr, PPF_None);
			PriorProps->SetStringField(It->GetName(), ValueStr);
		}
	}

	State->Modify();
	Transition.Conditions.RemoveAt(CondIndex);
	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
	Result->SetNumberField(TEXT("transitionIndex"), TransIndex);
	Result->SetStringField(TEXT("structType"), DoomedStructName);
	Result->SetNumberField(TEXT("conditionCount"), Transition.Conditions.Num());

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
		Payload->SetNumberField(TEXT("transitionIndex"), TransIndex);
		Payload->SetStringField(TEXT("structType"), DoomedStructName);
		Payload->SetStringField(TEXT("operand"), DoomedOperand);
		Payload->SetObjectField(TEXT("instanceProperties"), PriorProps);
		Payload->SetStringField(TEXT("lossy"), TEXT("Replaying this appends the condition at the END of the transition's list with a NEW nodeId, so its position in an And/Or expression and any property binding that named the old id are not restored."));
		MCPSetRollback(Result, TEXT("add_state_tree_transition_condition"), Payload);
	}
	return MCPResult(Result);
}

// ── State linking ────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::SetStateLink(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found on StateTree"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found. Pass stateId or statePath."));

	FString LinkType;
	if (auto Err = RequireString(Params, TEXT("linkType"), LinkType)) return Err;
	const bool bSubtree = LinkType.Equals(TEXT("subtree"), ESearchCase::IgnoreCase);
	const bool bAsset = LinkType.Equals(TEXT("asset"), ESearchCase::IgnoreCase);
	const bool bNone = LinkType.Equals(TEXT("none"), ESearchCase::IgnoreCase);
	if (!bSubtree && !bAsset && !bNone)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown linkType '%s'. Valid values: subtree (link to a Subtree state in THIS asset, via targetStateId or targetStatePath), asset (run another StateTree asset, via linkedAsset), none (clear both and return the state to a plain State)."),
			*LinkType));
	}

	// Capture the previous link for the inverse before anything changes.
	const EStateTreeStateType PrevType = State->Type;
	const FGuid PrevSubtreeId = State->LinkedSubtree.ID;
	const FString PrevAssetPath = State->LinkedAsset ? State->LinkedAsset->GetPathName() : FString();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), ST->GetPathName());
	Result->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
	Result->SetStringField(TEXT("statePath"), State->GetPath());
	Result->SetStringField(TEXT("linkType"), LinkType);

	if (bSubtree)
	{
		UStateTreeState* Target = nullptr;
		if (Params->HasField(TEXT("targetStateId")))
		{
			Target = FindStateByID(EditorData, StateTreeDepthParseGuid(Params->GetStringField(TEXT("targetStateId"))));
		}
		else if (Params->HasField(TEXT("targetStatePath")))
		{
			Target = FindStateByPath(EditorData, Params->GetStringField(TEXT("targetStatePath")));
		}
		if (!Target)
		{
			TArray<FString> Subtrees;
			for (const TObjectPtr<UStateTreeState>& Root : EditorData->SubTrees)
			{
				if (Root && Root->Type == EStateTreeStateType::Subtree) Subtrees.Add(Root->Name.ToString());
			}
			return MCPError(FString::Printf(
				TEXT("linkType=\"subtree\" needs targetStateId or targetStatePath naming a Subtree state in this asset. Subtree states at the root: %s. Make one with statetree(add_state, stateType=\"Subtree\")."),
				Subtrees.Num() > 0 ? *FString::Join(Subtrees, TEXT(", ")) : TEXT("none")));
		}
		if (Target->Type != EStateTreeStateType::Subtree)
		{
			return MCPError(FString::Printf(
				TEXT("State '%s' is of type %s, and only a Subtree state can be linked to. Change it with statetree(set_state_property, propertyName=\"type\", value=\"Subtree\")."),
				*Target->Name.ToString(),
				StaticEnum<EStateTreeStateType>()
					? *StaticEnum<EStateTreeStateType>()->GetNameStringByValue(static_cast<int64>(Target->Type))
					: TEXT("unknown")));
		}
		if (Target == State)
		{
			return MCPError(TEXT("A state cannot link to itself."));
		}

		State->Modify();
		State->Type = EStateTreeStateType::Linked;
		State->LinkedAsset = nullptr;
		// SetLinkedState, not a raw assignment: it is what runs
		// UpdateParametersFromLinkedSubtree, which fills this state's fixed
		// parameter layout from the subtree. Without it set_state_parameter has
		// nothing to override.
		State->SetLinkedState(Target->GetLinkToState());

		Result->SetStringField(TEXT("targetStateId"), StateTreeDepthGuid(Target->ID));
		Result->SetStringField(TEXT("targetStatePath"), Target->GetPath());
	}
	else if (bAsset)
	{
		FString LinkedAssetPath;
		if (auto Err = RequireString(Params, TEXT("linkedAsset"), LinkedAssetPath)) return Err;
		UStateTree* Linked = LoadAssetByPath<UStateTree>(LinkedAssetPath);
		if (!Linked)
		{
			return MCPAssetLoadError(LinkedAssetPath, TEXT("StateTree"));
		}
		if (Linked == ST)
		{
			return MCPError(TEXT("A StateTree cannot link to itself as an asset. Use linkType=\"subtree\" to reuse a Subtree state inside this asset."));
		}

		State->Modify();
		State->Type = EStateTreeStateType::LinkedAsset;
		State->LinkedSubtree = FStateTreeStateLink();
		State->SetLinkedStateAsset(Linked);
		Result->SetStringField(TEXT("linkedAsset"), Linked->GetPathName());
	}
	else
	{
		State->Modify();
		State->Type = EStateTreeStateType::State;
		State->SetLinkedState(FStateTreeStateLink());
		State->SetLinkedStateAsset(nullptr);
	}

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	const bool bChanged = (PrevType != State->Type)
		|| (PrevSubtreeId != State->LinkedSubtree.ID)
		|| (PrevAssetPath != (State->LinkedAsset ? State->LinkedAsset->GetPathName() : FString()));
	if (bChanged)
	{
		MCPSetUpdated(Result);
	}
	else
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("note"), TEXT("The state already carried exactly this link."));
	}
	Result->SetNumberField(TEXT("parameterCount"), State->Parameters.Parameters.GetNumPropertiesInBag());
	Result->SetBoolField(TEXT("parametersFixedLayout"), State->Parameters.bFixedLayout);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
		if (PrevSubtreeId.IsValid())
		{
			Payload->SetStringField(TEXT("linkType"), TEXT("subtree"));
			Payload->SetStringField(TEXT("targetStateId"), StateTreeDepthGuid(PrevSubtreeId));
		}
		else if (!PrevAssetPath.IsEmpty())
		{
			Payload->SetStringField(TEXT("linkType"), TEXT("asset"));
			Payload->SetStringField(TEXT("linkedAsset"), PrevAssetPath);
		}
		else
		{
			Payload->SetStringField(TEXT("linkType"), TEXT("none"));
			Payload->SetStringField(TEXT("lossy"), FString::Printf(
				TEXT("The state was not linked before this call; the inverse clears the link and sets type back to State, which is not the same as its previous type (%s) if that was Group or Subtree. Restore that with statetree(set_state_property, propertyName=\"type\")."),
				StaticEnum<EStateTreeStateType>()
					? *StaticEnum<EStateTreeStateType>()->GetNameStringByValue(static_cast<int64>(PrevType))
					: TEXT("unknown")));
		}
		MCPSetRollback(Result, TEXT("set_state_tree_state_link"), Payload);
	}
	return MCPResult(Result);
}

// ── Moving a state ───────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::MoveState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found on StateTree"));

	UStateTreeState* State = ResolveState(EditorData, Params);
	if (!State) return MCPError(TEXT("State not found. Pass stateId or statePath."));

	// Where it is now, for the inverse and for the idempotency answer.
	UStateTreeState* OldParent = State->Parent;
	TArray<TObjectPtr<UStateTreeState>>& OldArray = OldParent ? OldParent->Children : EditorData->SubTrees;
	const int32 OldIndex = OldArray.IndexOfByKey(State);
	if (OldIndex == INDEX_NONE)
	{
		return MCPError(FString::Printf(
			TEXT("State '%s' is not in its recorded parent's child list, so the asset's hierarchy is inconsistent and moving it would make that worse. Recompile with statetree(compile) and read it back with statetree(read)."),
			*State->Name.ToString()));
	}

	// The new parent: named explicitly, or the root when toRoot is set. Absent
	// both, the state keeps its parent and only its position changes.
	UStateTreeState* NewParent = OldParent;
	const bool bToRoot = OptionalBool(Params, TEXT("toRoot"), false);
	const bool bHasParentSelector =
		Params->HasField(TEXT("newParentStateId")) || Params->HasField(TEXT("newParentStatePath"));
	if (bToRoot && bHasParentSelector)
	{
		return MCPError(TEXT("toRoot=true and newParentStateId / newParentStatePath ask for two different destinations. Pass exactly one: toRoot for the top level, or a parent selector for a state to go under."));
	}
	if (bToRoot)
	{
		NewParent = nullptr;
	}
	else if (bHasParentSelector)
	{
		if (Params->HasField(TEXT("newParentStateId")))
		{
			NewParent = FindStateByID(EditorData, StateTreeDepthParseGuid(Params->GetStringField(TEXT("newParentStateId"))));
		}
		else
		{
			NewParent = FindStateByPath(EditorData, Params->GetStringField(TEXT("newParentStatePath")));
		}
		if (!NewParent)
		{
			return MCPError(TEXT("newParentStateId / newParentStatePath names no state in this asset. statetree(list_states) lists every id and path; pass toRoot=true to move the state to the top level instead."));
		}
	}

	if (NewParent == State)
	{
		return MCPError(TEXT("A state cannot be its own parent."));
	}
	if (NewParent && StateTreeDepthIsDescendant(NewParent, State))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is a descendant of '%s', so moving '%s' under it would detach the whole branch from the tree."),
			*NewParent->Name.ToString(), *State->Name.ToString(), *State->Name.ToString()));
	}

	TArray<TObjectPtr<UStateTreeState>>& NewArray = NewParent ? NewParent->Children : EditorData->SubTrees;
	const int32 TargetMax = (&NewArray == &OldArray) ? NewArray.Num() - 1 : NewArray.Num();
	int32 InsertIndex = TargetMax;
	if (Params->HasField(TEXT("insertIndex")))
	{
		InsertIndex = static_cast<int32>(Params->GetNumberField(TEXT("insertIndex")));
		if (InsertIndex < 0 || InsertIndex > TargetMax)
		{
			return MCPError(FString::Printf(
				TEXT("Invalid insertIndex %d. Valid values: 0 to %d (the destination holds %d sibling(s); omit insertIndex to append)."),
				InsertIndex, TargetMax, NewArray.Num()));
		}
	}

	const bool bSameParent = (NewParent == OldParent);
	const bool bChanged = !bSameParent || (InsertIndex != OldIndex);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), ST->GetPathName());
	Result->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
	Result->SetStringField(TEXT("previousParentStateId"), OldParent ? StateTreeDepthGuid(OldParent->ID) : FString());
	Result->SetNumberField(TEXT("previousIndex"), OldIndex);

	if (bChanged)
	{
		EditorData->Modify();
		if (OldParent) OldParent->Modify();
		if (NewParent) NewParent->Modify();
		State->Modify();

		OldArray.RemoveAt(OldIndex);
		// insertIndex is the FINAL position among siblings, which is why the
		// same-parent bound above is Num()-1: after the removal the array is one
		// shorter, so inserting at insertIndex lands the state exactly there.
		const int32 EffectiveIndex = FMath::Clamp(InsertIndex, 0, NewArray.Num());
		NewArray.Insert(State, EffectiveIndex);
		State->Parent = NewParent;

		MCPSetUpdated(Result);
		Result->SetNumberField(TEXT("index"), EffectiveIndex);
	}
	else
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetNumberField(TEXT("index"), OldIndex);
		Result->SetStringField(TEXT("note"), TEXT("The state is already this parent's child at this position, so nothing moved."));
	}

	UStateTreeEditingSubsystem::ValidateStateTree(ST);
	Result->SetStringField(TEXT("statePath"), State->GetPath());
	Result->SetStringField(TEXT("parentStateId"), NewParent ? StateTreeDepthGuid(NewParent->ID) : FString());
	Result->SetBoolField(TEXT("atRoot"), NewParent == nullptr);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("stateId"), StateTreeDepthGuid(State->ID));
		if (OldParent)
		{
			Payload->SetStringField(TEXT("newParentStateId"), StateTreeDepthGuid(OldParent->ID));
		}
		else
		{
			Payload->SetBoolField(TEXT("toRoot"), true);
		}
		Payload->SetNumberField(TEXT("insertIndex"), OldIndex);
		MCPSetRollback(Result, TEXT("move_state_tree_state"), Payload);
	}
	return MCPResult(Result);
}

// ── Blueprint node classes ───────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::SetNodeClass(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) return MCPError(TEXT("EditorData not found on StateTree"));

	FString NodeIdStr;
	if (auto Err = RequireString(Params, TEXT("nodeId"), NodeIdStr)) return Err;
	FGuid NodeId;
	if (!FGuid::Parse(NodeIdStr, NodeId))
	{
		return MCPError(FString::Printf(TEXT("Invalid nodeId '%s'. It is the GUID every add_* action in this category returns, and read_state / read report for each node."), *NodeIdStr));
	}

	FString Location;
	UStateTreeState* OwningState = nullptr;
	FStateTreeEditorNode* Node = StateTreeDepthFindNodeById(EditorData, NodeId, Location, OwningState);
	if (!Node)
	{
		return MCPError(FString::Printf(TEXT("No node with id '%s' anywhere in this StateTree. statetree(read) lists every node with its id."), *NodeIdStr));
	}

	const UScriptStruct* NodeStruct = Node->Node.IsValid() ? Node->Node.GetScriptStruct() : nullptr;
	if (!NodeStruct)
	{
		return MCPError(FString::Printf(TEXT("Node '%s' (%s) has no node struct, so there is no class property to set."), *NodeIdStr, *Location));
	}

	// The class property is named per family. Find it by looking for the one
	// FClassProperty on the wrapper struct rather than by hard-coding a name
	// per version.
	FClassProperty* ClassProp = nullptr;
	for (TFieldIterator<FProperty> It(NodeStruct); It; ++It)
	{
		if (FClassProperty* AsClass = CastField<FClassProperty>(*It))
		{
			ClassProp = AsClass;
			break;
		}
	}
	if (!ClassProp)
	{
		return MCPError(FString::Printf(
			TEXT("Node struct '%s' has no class property, so it is a native node rather than a Blueprint wrapper. Configure it with statetree(set_task_instance_property) / set_task_property, or the evaluator and global-task equivalents. statetree(list_node_types) reports each family's blueprintWrapperStruct."),
			*NodeStruct->GetName()));
	}

	FString ClassSpec;
	if (auto Err = RequireString(Params, TEXT("nodeClass"), ClassSpec)) return Err;
	UClass* Resolved = MCPResolveClass(ClassSpec, /*bAllowLoad=*/ true);
	if (!Resolved)
	{
		return MCPClassNotFoundError(ClassSpec, TEXT("nodeClass"));
	}
	UClass* MetaClass = ClassProp->MetaClass;
	if (MetaClass && !Resolved->IsChildOf(MetaClass))
	{
		return MCPError(FString::Printf(
			TEXT("Class '%s' does not derive from %s, which is what '%s' on '%s' requires. statetree(list_node_types) lists the Blueprint classes each family accepts under blueprintNodeClasses."),
			*Resolved->GetPathName(), *MetaClass->GetName(), *ClassProp->GetName(), *NodeStruct->GetName()));
	}

	uint8* NodeMem = Node->Node.GetMutableMemory();
	if (!NodeMem)
	{
		return MCPError(TEXT("Node memory unavailable."));
	}
	void* ValuePtr = ClassProp->ContainerPtrToValuePtr<void>(NodeMem);
	UObject* Previous = ClassProp->GetObjectPropertyValue(ValuePtr);
	const FString PreviousPath = Previous ? Previous->GetPathName() : FString();
	const bool bChanged = (Previous != Resolved) || (Node->InstanceObject == nullptr && Node->Instance.IsValid() == false);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), ST->GetPathName());
	Result->SetStringField(TEXT("nodeId"), NodeIdStr);
	Result->SetStringField(TEXT("location"), Location);
	Result->SetStringField(TEXT("structType"), NodeStruct->GetName());
	Result->SetStringField(TEXT("classProperty"), ClassProp->GetName());
	Result->SetStringField(TEXT("previousClass"), PreviousPath);
	Result->SetStringField(TEXT("nodeClass"), Resolved->GetPathName());

	if (bChanged)
	{
		if (OwningState) { OwningState->Modify(); } else { EditorData->Modify(); }
		ClassProp->SetObjectPropertyValue(ValuePtr, Resolved);
		// The wrapper reports the class AS its instance data type, so the
		// instance has to be reallocated after the class changes. This is the
		// step that has no property-write equivalent, and skipping it is what
		// leaves a Blueprint node with no instance data at all.
		StateTreeDepthReallocInstance(*Node, EditorData);
		MCPSetUpdated(Result);
	}
	else
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("note"), TEXT("The node already named this class and already had its instance data, so nothing was reallocated."));
	}

	Result->SetBoolField(TEXT("hasInstanceObject"), Node->InstanceObject != nullptr);
	Result->SetStringField(TEXT("instanceObjectPath"), Node->InstanceObject ? Node->InstanceObject->GetPathName() : FString());
	Result->SetStringField(TEXT("instancePropertyNote"), TEXT("The Blueprint's own variables live on instanceObjectPath: write them with editor(set_property) there, which reaches nested structs and arrays that the flat instanceProperties map cannot."));

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
		Payload->SetStringField(TEXT("nodeClass"), PreviousPath);
		Payload->SetStringField(TEXT("lossy"), PreviousPath.IsEmpty()
			? TEXT("The node had no class before this call, so the inverse cannot be replayed as-is: nodeClass would be empty. Remove the node instead.")
			: TEXT("The inverse restores the previous class and reallocates instance data from scratch, so any value set on the current instance object is discarded."));
		MCPSetRollback(Result, TEXT("set_state_tree_node_class"), Payload);
	}
	return MCPResult(Result);
}

// ── Runtime ──────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::ReadRuntime(const TSharedPtr<FJsonObject>& Params)
{
	FStateTreeDepthRuntime RT;
	FString ActorLabel;
	if (auto Err = StateTreeDepthResolveRuntime(Params, RT, ActorLabel)) return Err;

	FStateTreeExecutionContext Context(*RT.Component, *RT.StateTree, *RT.InstanceData);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("component"), RT.Component->GetName());
	Result->SetStringField(TEXT("componentPath"), RT.Component->GetPathName());
	Result->SetStringField(TEXT("stateTree"), RT.StateTree->GetPathName());
	Result->SetBoolField(TEXT("running"), Context.GetStateTreeRunStatus() == EStateTreeRunStatus::Running);
	StateTreeDepthFillRuntimeSnapshot(Context, Result);

	// The active states as HANDLES too, because that is what a transition
	// request needs and the names alone cannot be turned back into one.
	TArray<TSharedPtr<FJsonValue>> Frames;
	for (const FStateTreeExecutionFrame& Frame : Context.GetActiveFrames())
	{
		auto FObj = MakeShared<FJsonObject>();
		FObj->SetStringField(TEXT("stateTree"), Frame.StateTree ? Frame.StateTree->GetPathName() : FString());
		FObj->SetStringField(TEXT("rootState"), Frame.RootState.Describe());
		Frames.Add(MakeShared<FJsonValueObject>(FObj));
	}
	Result->SetArrayField(TEXT("frames"), Frames);

	if (OptionalBool(Params, TEXT("includeDebugStrings"), false))
	{
		Result->SetStringField(TEXT("debugInfo"), Context.GetDebugInfoString());
	}
	Result->SetStringField(TEXT("note"), TEXT("gameplay(get_state_tree_runtime) answers the same question with active state names only; this adds run status, last tick status, the state change count, the execution frames and the pending event queue, which is what tells you whether an event you sent has been consumed."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::SendEvent(const TSharedPtr<FJsonObject>& Params)
{
	FString EventTagStr;
	if (auto Err = RequireString(Params, TEXT("eventTag"), EventTagStr)) return Err;

	FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(FName(*EventTagStr), /*ErrorIfNotFound=*/ false);
	if (!EventTag.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("Gameplay tag '%s' is not registered, so no StateTree transition could ever match it. Register it first with gameplay(add_gameplay_tag); a transition's eventTag has to name a real tag."),
			*EventTagStr));
	}

	FStateTreeDepthRuntime RT;
	FString ActorLabel;
	if (auto Err = StateTreeDepthResolveRuntime(Params, RT, ActorLabel)) return Err;

	const FName Origin(*OptionalString(Params, TEXT("origin"), TEXT("ue-mcp")));

	FStateTreeExecutionContext Context(*RT.Component, *RT.StateTree, *RT.InstanceData);
	const int32 ChangesBefore = Context.GetStateChangeCount();
	Context.SendEvent(EventTag, FConstStructView(), Origin);

	auto Result = MCPSuccess();
	// An event is queued, not applied: the tree consumes it on its next tick.
	// Saying "updated" and reporting the count is the honest version of that.
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("component"), RT.Component->GetName());
	Result->SetStringField(TEXT("stateTree"), RT.StateTree->GetPathName());
	Result->SetStringField(TEXT("eventTag"), EventTag.ToString());
	Result->SetStringField(TEXT("origin"), Origin.ToString());
	Result->SetNumberField(TEXT("stateChangeCountBefore"), ChangesBefore);
	StateTreeDepthFillRuntimeSnapshot(Context, Result);
	Result->SetStringField(TEXT("timingNote"), TEXT("The event is QUEUED. The tree consumes it on its next tick, so activeStates here still reflect the state before it is handled; read it again with statetree(read_runtime) after a tick to see the result. A payload cannot be attached from this action; a task that needs one must read it from a bound parameter instead."));

	{
		// An event that has been queued cannot be un-queued, and one already
		// consumed cannot be undone at all. Naming a rollback that pretends
		// otherwise would be worse than admitting it.
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		if (Params->HasField(TEXT("actorPath"))) Payload->SetStringField(TEXT("actorPath"), OptionalString(Params, TEXT("actorPath")));
		if (Params->HasField(TEXT("componentName"))) Payload->SetStringField(TEXT("componentName"), OptionalString(Params, TEXT("componentName")));
		if (Params->HasField(TEXT("world"))) Payload->SetStringField(TEXT("world"), OptionalString(Params, TEXT("world")));
		Payload->SetStringField(TEXT("lossy"), TEXT("Sending an event has NO inverse. Once queued it is consumed on the next tick and whatever transition it triggered has already run; the inverse here only re-reads the runtime so a caller can see what happened. Undo the effect by requesting a transition back with statetree(request_transition), or by restarting the tree."));
		MCPSetRollback(Result, TEXT("read_state_tree_runtime"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RequestTransition(const TSharedPtr<FJsonObject>& Params)
{
	FStateTreeDepthRuntime RT;
	FString ActorLabel;
	if (auto Err = StateTreeDepthResolveRuntime(Params, RT, ActorLabel)) return Err;

	FStateTreeStateHandle Target;
	FString TargetDescription;
	if (Params->HasField(TEXT("targetStateId")))
	{
		const FString IdStr = Params->GetStringField(TEXT("targetStateId"));
		FGuid Id;
		if (!FGuid::Parse(IdStr, Id))
		{
			return MCPError(FString::Printf(TEXT("Invalid targetStateId '%s'. It is the GUID statetree(list_states) reports."), *IdStr));
		}
		Target = RT.StateTree->GetStateHandleFromId(Id);
		TargetDescription = IdStr;
	}
	else if (Params->HasField(TEXT("targetStateTag")))
	{
#if UE_MCP_HAS_STATETREE_TAG_STATE_LOOKUP
		const FString TagStr = Params->GetStringField(TEXT("targetStateTag"));
		FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), /*ErrorIfNotFound=*/ false);
		if (!Tag.IsValid())
		{
			return MCPError(FString::Printf(TEXT("Gameplay tag '%s' is not registered, so no state can carry it."), *TagStr));
		}
		Target = RT.StateTree->GetStateHandleFromGameplayTag(
			Tag, UStateTree::EStateGameplayTagQueryMethod::MatchesExact);
		TargetDescription = TagStr;
#else
		return MCPError(TEXT("targetStateTag needs UStateTree::GetStateHandleFromGameplayTag, which is UE 5.8 and later. On this engine pass targetStateId instead; statetree(list_states) reports the GUID and statetree(read_state) reports each state's tag, so a tag can be resolved to an id in one extra call."));
#endif
	}
	else
	{
		return MCPError(TEXT("Name the destination: targetStateId (a state GUID from statetree(list_states)) or targetStateTag (a state's Tag, UE 5.8 and later). Those are the only two selectors."));
	}

	if (!Target.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("'%s' resolves to no state in the COMPILED '%s'. A state added since the last compile is not in the runtime data; run statetree(compile) and restart the tree."),
			*TargetDescription, *RT.StateTree->GetPathName()));
	}

	const FString PriorityStr = OptionalString(Params, TEXT("priority"), TEXT("Normal"));
	EStateTreeTransitionPriority Priority = EStateTreeTransitionPriority::Normal;
	if (PriorityStr.Equals(TEXT("Low"), ESearchCase::IgnoreCase)) Priority = EStateTreeTransitionPriority::Low;
	else if (PriorityStr.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)) Priority = EStateTreeTransitionPriority::Normal;
	else if (PriorityStr.Equals(TEXT("Medium"), ESearchCase::IgnoreCase)) Priority = EStateTreeTransitionPriority::Medium;
	else if (PriorityStr.Equals(TEXT("High"), ESearchCase::IgnoreCase)) Priority = EStateTreeTransitionPriority::High;
	else if (PriorityStr.Equals(TEXT("Critical"), ESearchCase::IgnoreCase)) Priority = EStateTreeTransitionPriority::Critical;
	else
	{
		return MCPError(FString::Printf(
			TEXT("Unknown priority '%s'. Valid values: Low, Normal (default), Medium, High, Critical."), *PriorityStr));
	}

	const FString FallbackStr = OptionalString(Params, TEXT("fallback"), TEXT("None"));
	EStateTreeSelectionFallback Fallback = EStateTreeSelectionFallback::None;
	if (FallbackStr.Equals(TEXT("None"), ESearchCase::IgnoreCase)) Fallback = EStateTreeSelectionFallback::None;
	else if (FallbackStr.Equals(TEXT("NextSelectableSibling"), ESearchCase::IgnoreCase)) Fallback = EStateTreeSelectionFallback::NextSelectableSibling;
	else
	{
		return MCPError(FString::Printf(
			TEXT("Unknown fallback '%s'. Valid values: None (default), NextSelectableSibling."), *FallbackStr));
	}

	FStateTreeExecutionContext Context(*RT.Component, *RT.StateTree, *RT.InstanceData);
	if (Context.GetStateTreeRunStatus() != EStateTreeRunStatus::Running)
	{
		return MCPError(FString::Printf(
			TEXT("The tree on '%s' is %s, not Running, so a transition request would be dropped. Start it first (in PIE it starts on BeginPlay)."),
			*ActorLabel, *StateTreeDepthRunStatus(Context.GetStateTreeRunStatus())));
	}

	TArray<FName> Before = Context.GetActiveStateNames();
	Context.RequestTransition(Target, Priority, Fallback);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("component"), RT.Component->GetName());
	Result->SetStringField(TEXT("stateTree"), RT.StateTree->GetPathName());
	Result->SetStringField(TEXT("targetState"), Target.Describe());
	Result->SetStringField(TEXT("targetStateId"), StateTreeDepthGuid(RT.StateTree->GetStateIdFromHandle(Target)));
	Result->SetStringField(TEXT("priority"), PriorityStr);
	Result->SetStringField(TEXT("fallback"), FallbackStr);

	TArray<TSharedPtr<FJsonValue>> BeforeNames;
	for (const FName& N : Before) BeforeNames.Add(MakeShared<FJsonValueString>(N.ToString()));
	Result->SetArrayField(TEXT("activeStatesBefore"), BeforeNames);
	StateTreeDepthFillRuntimeSnapshot(Context, Result);
	Result->SetStringField(TEXT("timingNote"), TEXT("The request is QUEUED and resolved on the next tick, against every other pending transition by priority, so activeStates here still show the state before it is applied. Read it again with statetree(read_runtime) after a tick."));

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		if (Params->HasField(TEXT("actorPath"))) Payload->SetStringField(TEXT("actorPath"), OptionalString(Params, TEXT("actorPath")));
		if (Params->HasField(TEXT("componentName"))) Payload->SetStringField(TEXT("componentName"), OptionalString(Params, TEXT("componentName")));
		if (Params->HasField(TEXT("world"))) Payload->SetStringField(TEXT("world"), OptionalString(Params, TEXT("world")));
		if (Before.Num() > 0)
		{
			Payload->SetStringField(TEXT("targetStateTag"), FString());
			Payload->SetStringField(TEXT("priority"), PriorityStr);
		}
		Payload->SetStringField(TEXT("lossy"), TEXT("A transition cannot be un-taken. Entering the previous state again re-runs its tasks' EnterState from the beginning rather than resuming where they were, so this inverse needs a targetStateId filled in by the caller from activeStatesBefore, and even then it is a re-entry rather than an undo."));
		MCPSetRollback(Result, TEXT("request_state_tree_transition"), Payload);
	}
	return MCPResult(Result);
}

#endif // UE_MCP_HAS_5_5_API
