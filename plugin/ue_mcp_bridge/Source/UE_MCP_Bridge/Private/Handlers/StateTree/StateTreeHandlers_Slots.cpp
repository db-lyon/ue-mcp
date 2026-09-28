// The 18 StateTree node-list handlers on the F3 slot family: tasks, enter
// conditions, transition conditions and considerations on a state, evaluators
// and global tasks on the tree. Each list is one FNodeSlot descriptor; the
// three operations after them are the only code that adds, removes or writes
// a node, and they commit through the F1 scope and write through F5.

#include "StateTreeHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#if UE_MCP_HAS_5_5_API

#include "StateTreeEditTraits.h"
#include "Families/MCPPropertyWrite.h"
#include "Families/MCPSlots.h"
#include "MCPEngineCompat.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeNodeBase.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"

/** One StateTree node list, declared once. */
struct FStateTreeHandlers::FNodeSlot
{
	enum class EOwner : uint8 { State, Transition, Tree };

	const TCHAR* Kind;
	EOwner Owner;
	/** The owner's array property, for notifications. */
	const TCHAR* Member;
	TArray<FStateTreeEditorNode>& (*List)(UStateTreeEditorData& Data, UStateTreeState* State, int32 Transition);
	/** Position param and result key; null when nodes are addressed by nodeId. */
	const TCHAR* IndexParam;
	const TCHAR* CountKey;
	const TCHAR* AddMethod;
	const TCHAR* RemoveMethod;
	const TCHAR* SetInstanceMethod;
	const TCHAR* SetNodeMethod;
	/** A node must derive from this; null accepts any node struct. */
	UScriptStruct* (*Base)();
	bool bOperand;
	/** A removal payload carries a Blueprint node's instance-object values. */
	bool bCaptureInstanceObject;
	/** A removal always emits its inverse and says what is lost inside the payload. */
	bool bLossyInPayload;
	const TCHAR* AddNote;
	const TCHAR* NativeLossy;
	const TCHAR* BlueprintLossy;
	/** Refuses an add the schema does not allow, or null. */
	TSharedPtr<FJsonValue> (*Gate)(const UStateTreeEditorData& Data);
	/** Adds what an add reports beyond the common keys, or null. */
	void (*OnAdded)(const UStateTreeState* State, const TSharedPtr<FJsonObject>& Result);
};

namespace
{
	using FStateTreeSlot = FStateTreeHandlers::FNodeSlot;

	bool StateTreeSlotParentScores(const UStateTreeState* State)
	{
		const UStateTreeState* Parent = State ? State->Parent : nullptr;
		return Parent && (Parent->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility
			|| Parent->SelectionBehavior == EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility);
	}

	const FStateTreeSlot StateTreeTaskSlot{
		.Kind = TEXT("task"), .Owner = FStateTreeSlot::EOwner::State, .Member = TEXT("Tasks"),
		.List = [](UStateTreeEditorData&, UStateTreeState* S, int32) -> TArray<FStateTreeEditorNode>& { return S->Tasks; },
		.IndexParam = TEXT("taskIndex"), .CountKey = TEXT("taskCount"),
		.AddMethod = TEXT("add_state_tree_task"), .RemoveMethod = TEXT("remove_state_tree_task"),
		.SetInstanceMethod = TEXT("set_state_tree_task_instance_property"), .SetNodeMethod = TEXT("set_state_tree_task_property"),
		.Base = nullptr, .bOperand = false, .bCaptureInstanceObject = true, .bLossyInPayload = false,
		.AddNote = TEXT("The inverse names this task by INDEX into the state's task list. A later step that removes a task BELOW it shifts it down, and statetree(remove_task)'s own rollback re-adds by APPENDING rather than reinserting, so in a flow mixing the two this index can end up naming a different task and remove the wrong one. There is no remove-task-by-nodeId action to address it by identity instead."),
		.NativeLossy = TEXT("Replaying this appends the task at the END of the list with a NEW nodeId, so its taskIndex and any property binding that named the old id are not restored. Node-struct flags written with statetree(set_task_property) are not part of the payload either."),
		.BlueprintLossy = TEXT("The removed task was Blueprint-backed. Its instance values ARE captured in the payload, but replaying adds the wrapper without its class, and statetree(set_node_class) reallocates the instance data when you set the class, discarding them. Set the class first, then write the values through editor(set_property) at the instanceObjectPath that statetree(set_node_class) returns, because the per-property setters reach a node's Instance struct only and reject a Blueprint-backed node with \"has no instance data\". It also appends at the END of the task list with a NEW nodeId, so its taskIndex and any property binding that named the old id are not restored."),
		.Gate = nullptr, .OnAdded = nullptr,
	};

	const FStateTreeSlot StateTreeEnterConditionSlot{
		.Kind = TEXT("condition"), .Owner = FStateTreeSlot::EOwner::State, .Member = TEXT("EnterConditions"),
		.List = [](UStateTreeEditorData&, UStateTreeState* S, int32) -> TArray<FStateTreeEditorNode>& { return S->EnterConditions; },
		.IndexParam = TEXT("conditionIndex"), .CountKey = TEXT("conditionCount"),
		.AddMethod = TEXT("add_state_tree_enter_condition"), .RemoveMethod = TEXT("remove_state_tree_enter_condition"),
		.SetInstanceMethod = nullptr, .SetNodeMethod = nullptr,
		.Base = nullptr, .bOperand = true, .bCaptureInstanceObject = true, .bLossyInPayload = false,
		.AddNote = TEXT("The inverse names this condition by INDEX into the state's enter-condition list. A later step that removes an enter condition BELOW it shifts it down, and statetree(remove_enter_condition)'s own rollback re-adds by APPENDING rather than reinserting, so in a flow mixing the two this index can end up naming a different condition and remove the wrong one. There is no remove-enter-condition-by-nodeId action to address it by identity instead."),
		.NativeLossy = TEXT("Replaying this appends the condition at the END of the list with a NEW nodeId, so its position in the And/Or expression and any property binding that named the old id are not restored."),
		.BlueprintLossy = TEXT("The removed condition was Blueprint-backed. Its instance values ARE captured in the payload, but replaying adds the wrapper without its class, and statetree(set_node_class) reallocates the instance data when you set the class, discarding them. Set the class first, then write the values through editor(set_property) at the instanceObjectPath that statetree(set_node_class) returns, because the per-property setters reach a node's Instance struct only and reject a Blueprint-backed node with \"has no instance data\". It also appends at the END of the list with a NEW nodeId, so its position in the And/Or expression and any binding that named the old id are not restored."),
		.Gate = nullptr, .OnAdded = nullptr,
	};

	const FStateTreeSlot StateTreeTransitionConditionSlot{
		.Kind = TEXT("condition"), .Owner = FStateTreeSlot::EOwner::Transition, .Member = TEXT("Transitions"),
		.List = [](UStateTreeEditorData&, UStateTreeState* S, int32 T) -> TArray<FStateTreeEditorNode>& { return S->Transitions[T].Conditions; },
		.IndexParam = TEXT("conditionIndex"), .CountKey = TEXT("conditionCount"),
		.AddMethod = TEXT("add_state_tree_transition_condition"), .RemoveMethod = TEXT("remove_state_tree_transition_condition"),
		.SetInstanceMethod = nullptr, .SetNodeMethod = nullptr,
		.Base = nullptr, .bOperand = true, .bCaptureInstanceObject = false, .bLossyInPayload = true,
		.AddNote = TEXT("The inverse names this condition by TWO indices: the transition's position in the state, and the condition's position in that transition. A later step that removes a transition BELOW this one, or a condition BELOW this one inside the same transition, shifts it down, and the rollbacks of statetree(remove_transition) and statetree(remove_transition_condition) re-add by APPENDING rather than reinserting, so in a flow mixing the two this pair can end up naming a different condition and remove the wrong one. There is no remove-transition-condition-by-nodeId action to address it by identity instead."),
		.NativeLossy = TEXT("Replaying this appends the condition at the END of the transition's list with a NEW nodeId, so its position in an And/Or expression and any property binding that named the old id are not restored."),
		.BlueprintLossy = TEXT("Replaying this appends the condition at the END of the transition's list with a NEW nodeId, so its position in an And/Or expression and any property binding that named the old id are not restored."),
		.Gate = nullptr, .OnAdded = nullptr,
	};

	const FStateTreeSlot StateTreeConsiderationSlot{
		.Kind = TEXT("consideration"), .Owner = FStateTreeSlot::EOwner::State, .Member = TEXT("Considerations"),
		.List = [](UStateTreeEditorData&, UStateTreeState* S, int32) -> TArray<FStateTreeEditorNode>& { return S->Considerations; },
		.IndexParam = TEXT("considerationIndex"), .CountKey = TEXT("considerationCount"),
		.AddMethod = TEXT("add_state_tree_consideration"), .RemoveMethod = TEXT("remove_state_tree_consideration"),
		.SetInstanceMethod = nullptr, .SetNodeMethod = nullptr,
		.Base = &FStateTreeConsiderationBase::StaticStruct, .bOperand = true, .bCaptureInstanceObject = false, .bLossyInPayload = true,
		.AddNote = nullptr,
		.NativeLossy = TEXT("Replaying this appends the consideration at the END of the list with a NEW nodeId, so its position in an And/Or expression and any property binding that named the old id are not restored."),
		.BlueprintLossy = TEXT("The removed consideration was Blueprint-backed, so replaying this adds the wrapper without its class; follow it with statetree(set_node_class). The node also gets a NEW nodeId, so any binding that named the old one is gone."),
		.Gate = [](const UStateTreeEditorData& Data) -> TSharedPtr<FJsonValue>
		{
			if (!Data.Schema || Data.Schema->AllowUtilityConsiderations()) return nullptr;
			return MCPError(FString::Printf(
				TEXT("Schema '%s' does not allow utility considerations, so this tree cannot score its states. statetree(list_node_types) reports allowUtilityConsiderations for the schema in use."),
				*Data.Schema->GetClass()->GetName()));
		},
		.OnAdded = [](const UStateTreeState* State, const TSharedPtr<FJsonObject>& Result)
		{
			// The score is only read when the PARENT selects by utility.
			if (!State->Parent) return;
			const bool bParentScores = StateTreeSlotParentScores(State);
			Result->SetBoolField(TEXT("parentScoresByUtility"), bParentScores);
			if (!bParentScores)
			{
				Result->SetStringField(TEXT("warning"), TEXT("The parent state's selectionBehavior does not score children by utility, so this consideration is never evaluated. Set the parent with statetree(set_state_property, propertyName=\"selectionBehavior\", value=\"TrySelectChildrenWithHighestUtility\")."));
			}
		},
	};

	const FStateTreeSlot StateTreeEvaluatorSlot{
		.Kind = TEXT("evaluator"), .Owner = FStateTreeSlot::EOwner::Tree, .Member = TEXT("Evaluators"),
		.List = [](UStateTreeEditorData& D, UStateTreeState*, int32) -> TArray<FStateTreeEditorNode>& { return D.Evaluators; },
		.IndexParam = nullptr, .CountKey = TEXT("evaluatorCount"),
		.AddMethod = TEXT("add_state_tree_evaluator"), .RemoveMethod = TEXT("remove_state_tree_evaluator"),
		.SetInstanceMethod = TEXT("set_state_tree_evaluator_instance_property"), .SetNodeMethod = TEXT("set_state_tree_evaluator_property"),
		.Base = &FStateTreeEvaluatorBase::StaticStruct, .bOperand = false, .bCaptureInstanceObject = true, .bLossyInPayload = false,
		.AddNote = nullptr,
		.NativeLossy = TEXT("Replaying this appends the evaluator at the END of the list with a NEW nodeId, so its evaluation order and any property binding that named the old id are not restored. Node-struct fields written with statetree(set_evaluator_property) are not part of the payload either."),
		.BlueprintLossy = TEXT("The removed evaluator was Blueprint-backed. Its instance values ARE captured in the payload, but replaying adds the wrapper without its class, and statetree(set_node_class) reallocates the instance data when you set the class, discarding them. Set the class first, then write the values through editor(set_property) at the instanceObjectPath that statetree(set_node_class) returns, because the per-property setters reach a node's Instance struct only and reject a Blueprint-backed node with \"has no instance data\". It also appends at the END of the evaluator list with a NEW nodeId, so its evaluation order and any property binding that named the old id are not restored."),
		.Gate = nullptr, .OnAdded = nullptr,
	};

	const FStateTreeSlot StateTreeGlobalTaskSlot{
		.Kind = TEXT("global task"), .Owner = FStateTreeSlot::EOwner::Tree, .Member = TEXT("GlobalTasks"),
		.List = [](UStateTreeEditorData& D, UStateTreeState*, int32) -> TArray<FStateTreeEditorNode>& { return D.GlobalTasks; },
		.IndexParam = nullptr, .CountKey = TEXT("globalTaskCount"),
		.AddMethod = TEXT("add_state_tree_global_task"), .RemoveMethod = TEXT("remove_state_tree_global_task"),
		.SetInstanceMethod = TEXT("set_state_tree_global_task_instance_property"), .SetNodeMethod = TEXT("set_state_tree_global_task_property"),
		.Base = &FStateTreeTaskBase::StaticStruct, .bOperand = false, .bCaptureInstanceObject = true, .bLossyInPayload = false,
		.AddNote = nullptr,
		.NativeLossy = TEXT("Replaying this appends the global task at the END of the list with a NEW nodeId, so its order and any property binding that named the old id are not restored. Node-struct fields written with statetree(set_global_task_property) are not part of the payload either."),
		.BlueprintLossy = TEXT("The removed global task was Blueprint-backed. Its instance values ARE captured in the payload, but replaying adds the wrapper without its class, and statetree(set_node_class) reallocates the instance data when you set the class, discarding them. Set the class first, then write the values through editor(set_property) at the instanceObjectPath that statetree(set_node_class) returns, because the per-property setters reach a node's Instance struct only and reject a Blueprint-backed node with \"has no instance data\". It also appends at the END of the global task list with a NEW nodeId, so its order and any property binding that named the old id are not restored."),
		.Gate = nullptr, .OnAdded = nullptr,
	};

	/** Every UPROPERTY of the node's instance data as export text. With
	 *  bObject, a Blueprint node's instance object is read instead. */
	TSharedPtr<FJsonObject> StateTreeSlotCaptureInstance(const FStateTreeEditorNode& Node, bool bObject)
	{
		auto Props = MakeShared<FJsonObject>();
		const UStruct* Struct = nullptr;
		const void* Memory = nullptr;
		if (bObject && Node.InstanceObject)
		{
			Struct = Node.InstanceObject->GetClass();
			Memory = Node.InstanceObject.Get();
		}
		else if (Node.Instance.IsValid())
		{
			Struct = Node.Instance.GetScriptStruct();
			Memory = Node.Instance.GetMemory();
		}
		if (!Struct || !Memory) return Props;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			Props->SetStringField(It->GetName(), MCPPropertyWrite::Export(*It, It->ContainerPtrToValuePtr<void>(Memory)));
		}
		return Props;
	}

	/** Give a node the instance data its node struct asks for. 5.8 has one
	 *  engine call for it; below that the two cases are handled by hand. */
	void StateTreeSlotAllocInstance(FStateTreeEditorNode& Node, const UScriptStruct* NodeStruct, UObject* Outer)
	{
#if UE_MCP_HAS_STATETREE_NODE_OUTER_INIT
		Node.InitializeAs(Outer, NodeStruct);
#else
		Node.ID = FGuid::NewGuid();
		Node.Node.InitializeAs(NodeStruct);
		const FStateTreeNodeBase* NodeBase = Node.Node.GetPtr<FStateTreeNodeBase>();
		const UStruct* InstanceType = NodeBase ? NodeBase->GetInstanceDataType() : nullptr;
		if (const UClass* InstanceClass = Cast<const UClass>(InstanceType))
		{
			Node.InstanceObject = NewObject<UObject>(Outer, const_cast<UClass*>(InstanceClass));
		}
		else if (const UScriptStruct* InstanceStruct = Cast<const UScriptStruct>(InstanceType))
		{
			Node.Instance.InitializeAs(InstanceStruct);
		}
#endif
	}

	void StateTreeSlotSetOperand(FStateTreeEditorNode& Node, const FString& Operand)
	{
		Node.ExpressionOperand = Operand.Equals(TEXT("Or"), ESearchCase::IgnoreCase)
			? EStateTreeExpressionOperand::Or
			: EStateTreeExpressionOperand::And;
	}
}

// ── The three slot operations ─────────────────────────────────────────────────

/** The params that address a slot, read before the asset loads (#1057). */
struct FStateTreeHandlers::FSlotRequest
{
	FStateRef StateRef;
	bool bHasTransition = false;
	int32 Transition = 0;
	bool bHasIndex = false;
	int32 Index = 0;
	FString NodeId;
};

void FStateTreeHandlers::ReadSlotRequest(const TSharedPtr<FJsonObject>& Params, const FNodeSlot& Slot, bool bAddressNode, FSlotRequest& Out)
{
	if (Slot.Owner != FNodeSlot::EOwner::Tree) Out.StateRef = ReadStateRef(Params);
	if (Slot.Owner == FNodeSlot::EOwner::Transition)
	{
		Out.bHasTransition = HasParam(Params, TEXT("transitionIndex"));
		Out.Transition = static_cast<int32>(OptionalNumber(Params, TEXT("transitionIndex")));
	}
	if (!bAddressNode) return;
	if (Slot.IndexParam)
	{
		Out.bHasIndex = HasParam(Params, Slot.IndexParam);
		Out.Index = static_cast<int32>(OptionalNumber(Params, Slot.IndexParam));
	}
	else
	{
		Out.NodeId = OptionalString(Params, TEXT("nodeId"));
	}
}

TSharedPtr<FJsonValue> FStateTreeHandlers::FindSlotNode(TMCPSlotList<FStateTreeEditorNode>& List, const FNodeSlot& Slot, const FSlotRequest& Request, int32& OutIndex)
{
	OutIndex = Request.Index;
	if (Slot.IndexParam)
	{
		if (!Request.bHasIndex) return MCPError(FString::Printf(TEXT("Missing required parameter '%s'"), Slot.IndexParam));
		return List.CheckIndex(OutIndex);
	}
	FGuid NodeId;
	if (!FGuid::Parse(Request.NodeId, NodeId)) return MCPError(FString::Printf(TEXT("Invalid nodeId: %s"), *Request.NodeId));
	OutIndex = List.IndexOf([&NodeId](const FStateTreeEditorNode& Node) { return Node.ID == NodeId; });
	if (OutIndex != INDEX_NONE) return nullptr;
	return MCPError(FString::Printf(TEXT("No %s with nodeId %s in this StateTree."), Slot.Kind, *Request.NodeId));
}

TSharedPtr<FJsonValue> FStateTreeHandlers::ResolveSlot(const FNodeSlot& Slot, const FSlotRequest& Request,
	UStateTreeEditorData* EditorData, UStateTreeState*& OutState)
{
	OutState = nullptr;
	if (Slot.Owner == FNodeSlot::EOwner::Tree) return nullptr;
	OutState = ResolveState(EditorData, Request.StateRef);
	if (!OutState) return MCPError(TEXT("State not found. Pass stateId or statePath."));
	if (Slot.Owner == FNodeSlot::EOwner::Transition)
	{
		if (!Request.bHasTransition) return MCPError(TEXT("Missing required parameter 'transitionIndex'"));
		TMCPSlotList<FStateTreeTransition> Transitions(OutState, TEXT("Transitions"), OutState->Transitions, TEXT("transition"), TEXT("transitionIndex"));
		if (auto Err = Transitions.CheckIndex(Request.Transition)) return Err;
	}
	return nullptr;
}

TSharedPtr<FJsonValue> FStateTreeHandlers::AddSlotNode(const TSharedPtr<FJsonObject>& Params, const FNodeSlot& Slot)
{
	FSlotRequest Request;
	ReadSlotRequest(Params, Slot, /*bAddressNode=*/false, Request);
	FString StructType;
	const TSharedPtr<FJsonValue> StructTypeErr = RequireString(Params, TEXT("structType"), StructType);
	TSharedPtr<FJsonObject> InstanceProps;
	if (HasParam(Params, TEXT("instanceProperties")))
	{
		InstanceProps = TryGetParam(Params, TEXT("instanceProperties"))->AsObject();
	}
	const FString Operand = Slot.bOperand ? OptionalString(Params, TEXT("operand")) : FString();
	const bool bHasOperand = Slot.bOperand && HasParam(Params, TEXT("operand"));
	if (StructTypeErr) return StructTypeErr;

	FMCPEditScope Edit(Params, Slot.AddMethod, MCPStateTree::StructureEdit());
	if (auto Err = Edit.Open<UStateTree>()) return Err;
	UStateTree* ST = Edit.Target<UStateTree>();
	UStateTreeEditorData* EditorData = MCPStateTree::EditorData(ST);
	UStateTreeState* State = nullptr;
	if (auto Err = ResolveSlot(Slot, Request, EditorData, State)) return Err;
	if (Slot.Gate)
	{
		if (auto Err = Slot.Gate(*EditorData)) return Err;
	}

	FString ResolveError;
	const UScriptStruct* NodeStruct = MCPResolveScriptStruct(StructType, &ResolveError, true);
	if (!NodeStruct)
	{
		return MCPError(!ResolveError.IsEmpty() ? ResolveError : FString::Printf(
			TEXT("Struct not found: '%s'. Pass the C++ struct name or a /Script/Module.Struct path; statetree(list_node_types, nodeType=\"%s\") lists every one this tree's schema allows."),
			*StructType, Slot.Kind));
	}
	const UScriptStruct* Base = Slot.Base ? Slot.Base() : nullptr;
	if (Base && !NodeStruct->IsChildOf(Base))
	{
		return MCPError(FString::Printf(
			TEXT("Struct '%s' does not derive from %s, so it cannot be used as a %s. statetree(list_node_types) lists the ones that can."),
			*NodeStruct->GetName(), *Base->GetName(), Slot.Kind));
	}

	UObject* Owner = State ? static_cast<UObject*>(State) : static_cast<UObject*>(EditorData);
	TMCPSlotList<FStateTreeEditorNode> List(Owner, Slot.Member, Slot.List(*EditorData, State, Request.Transition), Slot.Kind,
		Slot.IndexParam ? Slot.IndexParam : TEXT("nodeId"));
	FMCPEditTransaction Txn(Edit);
	FString Error;
	FStateTreeEditorNode* NewNode = List.Add([&](FStateTreeEditorNode& Node, FString& OutError)
	{
		StateTreeSlotAllocInstance(Node, NodeStruct, EditorData);
		// A key or value that does not land fails the add, and the node goes with it.
		UObject* InstanceObject = Node.InstanceObject.Get();
		const UStruct* Struct = InstanceObject ? static_cast<const UStruct*>(InstanceObject->GetClass())
			: (Node.Instance.IsValid() ? Node.Instance.GetScriptStruct() : nullptr);
		void* Container = InstanceObject ? static_cast<void*>(InstanceObject) : static_cast<void*>(Node.Instance.GetMutableMemory());
		if (!MCPPropertyWrite::SetJsonMap(nullptr, nullptr, Struct, Container, InstanceProps, OutError)) return false;
		if (bHasOperand) StateTreeSlotSetOperand(Node, Operand);
		return true;
	}, Error);
	if (!NewNode) return MCPError(Error);
	Txn.Commit();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeId"), MCPStateTree::Guid(NewNode->ID));
	Result->SetNumberField(Slot.CountKey, List.Num());
	auto Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
	if (State)
	{
		Result->SetStringField(TEXT("stateId"), MCPStateTree::Guid(State->ID));
		Result->SetNumberField(Slot.IndexParam, List.Num() - 1);
		Payload->SetStringField(TEXT("stateId"), MCPStateTree::Guid(State->ID));
		if (Slot.Owner == FNodeSlot::EOwner::Transition) Payload->SetNumberField(TEXT("transitionIndex"), Request.Transition);
		Payload->SetNumberField(Slot.IndexParam, List.Num() - 1);
		if (Slot.OnAdded) Slot.OnAdded(State, Result);
	}
	else
	{
		Payload->SetStringField(TEXT("nodeId"), MCPStateTree::Guid(NewNode->ID));
	}
	MCPSetRollback(Result, Slot.RemoveMethod, Payload);
	if (Slot.AddNote) Result->SetStringField(TEXT("rollbackNote"), Slot.AddNote);
	return Edit.Finish(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveSlotNode(const TSharedPtr<FJsonObject>& Params, const FNodeSlot& Slot)
{
	FSlotRequest Request;
	ReadSlotRequest(Params, Slot, /*bAddressNode=*/true, Request);

	FMCPEditScope Edit(Params, Slot.RemoveMethod, MCPStateTree::StructureEdit());
	if (auto Err = Edit.Open<UStateTree>()) return Err;
	UStateTree* ST = Edit.Target<UStateTree>();
	UStateTreeEditorData* EditorData = MCPStateTree::EditorData(ST);
	UStateTreeState* State = nullptr;
	if (auto Err = ResolveSlot(Slot, Request, EditorData, State)) return Err;

	UObject* Owner = State ? static_cast<UObject*>(State) : static_cast<UObject*>(EditorData);
	TMCPSlotList<FStateTreeEditorNode> List(Owner, Slot.Member, Slot.List(*EditorData, State, Request.Transition), Slot.Kind,
		Slot.IndexParam ? Slot.IndexParam : TEXT("nodeId"));
	int32 Index = INDEX_NONE;
	if (auto Err = FindSlotNode(List, Slot, Request, Index)) return Err;

	FMCPEditTransaction Txn(Edit);
	const FStateTreeEditorNode Removed = List.Remove(Index);
	Txn.Commit();

	const UScriptStruct* RemovedStruct = Removed.Node.IsValid() ? Removed.Node.GetScriptStruct() : nullptr;
	const FString StructName = RemovedStruct ? RemovedStruct->GetName() : FString();
	const bool bBlueprint = Removed.InstanceObject != nullptr;

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetStringField(TEXT("structType"), StructName);
	Result->SetNumberField(Slot.CountKey, List.Num());
	if (State) Result->SetStringField(TEXT("stateId"), MCPStateTree::Guid(State->ID));
	if (Slot.Owner == FNodeSlot::EOwner::Transition) Result->SetNumberField(TEXT("transitionIndex"), Request.Transition);

	if (StructName.IsEmpty() && !Slot.bLossyInPayload)
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The removed %s carried no node struct, so there is no structType statetree(%s) could be replayed with."), Slot.Kind, Slot.AddMethod));
		return Edit.Finish(Result);
	}

	const TCHAR* Lossy = bBlueprint ? Slot.BlueprintLossy : Slot.NativeLossy;
	auto Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
	if (State) Payload->SetStringField(TEXT("stateId"), MCPStateTree::Guid(State->ID));
	if (Slot.Owner == FNodeSlot::EOwner::Transition) Payload->SetNumberField(TEXT("transitionIndex"), Request.Transition);
	Payload->SetStringField(TEXT("structType"), StructName);
	if (Slot.bOperand)
	{
		Payload->SetStringField(TEXT("operand"), Removed.ExpressionOperand == EStateTreeExpressionOperand::Or ? TEXT("Or") : TEXT("And"));
	}
	Payload->SetObjectField(TEXT("instanceProperties"), StateTreeSlotCaptureInstance(Removed, Slot.bCaptureInstanceObject));
	if (Slot.bLossyInPayload) Payload->SetStringField(TEXT("lossy"), Lossy);
	MCPSetRollback(Result, Slot.AddMethod, Payload);
	if (!Slot.bLossyInPayload)
	{
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), Lossy);
	}
	return Edit.Finish(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::SetSlotNodeProperty(const TSharedPtr<FJsonObject>& Params, const FNodeSlot& Slot, bool bInstance)
{
	FSlotRequest Request;
	ReadSlotRequest(Params, Slot, /*bAddressNode=*/true, Request);
	const FString PropName = OptionalString(Params, TEXT("propertyName"));
	const FString Value = OptionalString(Params, TEXT("value"));
	const TCHAR* Method = bInstance ? Slot.SetInstanceMethod : Slot.SetNodeMethod;

	FMCPEditScope Edit(Params, Method, MCPStateTree::ValueEdit());
	if (auto Err = Edit.Open<UStateTree>()) return Err;
	UStateTree* ST = Edit.Target<UStateTree>();
	UStateTreeEditorData* EditorData = MCPStateTree::EditorData(ST);
	UStateTreeState* State = nullptr;
	if (auto Err = ResolveSlot(Slot, Request, EditorData, State)) return Err;

	UObject* Owner = State ? static_cast<UObject*>(State) : static_cast<UObject*>(EditorData);
	TMCPSlotList<FStateTreeEditorNode> List(Owner, Slot.Member, Slot.List(*EditorData, State, Request.Transition), Slot.Kind,
		Slot.IndexParam ? Slot.IndexParam : TEXT("nodeId"));
	int32 Index = INDEX_NONE;
	if (auto Err = FindSlotNode(List, Slot, Request, Index)) return Err;

	// Instance writes reach the node's Instance struct; node writes reach the
	// node struct's own UPROPERTYs (bTaskEnabled and the like).
	FStateTreeEditorNode& Node = List[Index];
	FInstancedStruct& Data = bInstance ? Node.Instance : Node.Node;
	const UScriptStruct* Struct = Data.IsValid() ? Data.GetScriptStruct() : nullptr;
	uint8* Memory = Data.IsValid() ? Data.GetMutableMemory() : nullptr;
	if (!Struct || !Memory)
	{
		return MCPError(FString::Printf(TEXT("This %s has no %s data to write."), Slot.Kind, bInstance ? TEXT("instance") : TEXT("node")));
	}
	FProperty* Prop = Struct->FindPropertyByName(*PropName);
	if (!Prop)
	{
		return MCPError(bInstance
			? FString::Printf(TEXT("Property not found on %s instance data '%s': %s"), Slot.Kind, *Struct->GetName(), *PropName)
			: FString::Printf(TEXT("Property not found on %s node struct '%s': %s. This action writes the node struct's own UPROPERTYs (bTaskEnabled and the like); the instance-property action writes instance data fields."),
				Slot.Kind, *Struct->GetName(), *PropName));
	}

	FMCPEditTransaction Txn(Edit);
	MCPPropertyWrite::FTarget Target{ Owner, List.MemberProperty(), Prop, Prop->ContainerPtrToValuePtr<void>(Memory) };
	const MCPPropertyWrite::FOutcome Outcome = MCPPropertyWrite::SetText(Target, Value);
	if (!Outcome.bWritten) return MCPError(Outcome.Error);
	if (Outcome.bChanged) Txn.Commit();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPPropertyWrite::NotePrevious(Result, Outcome);
	if (!bInstance)
	{
		Result->SetStringField(TEXT("structType"), Struct->GetName());
		Result->SetStringField(TEXT("propertyName"), PropName);
		Result->SetStringField(TEXT("value"), Value);
	}
	auto Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
	if (State)
	{
		Payload->SetStringField(TEXT("stateId"), MCPStateTree::Guid(State->ID));
		Payload->SetNumberField(Slot.IndexParam, Index);
	}
	else
	{
		Payload->SetStringField(TEXT("nodeId"), Request.NodeId);
	}
	Payload->SetStringField(TEXT("propertyName"), PropName);
	Payload->SetStringField(TEXT("value"), Outcome.Previous);
	MCPSetRollback(Result, Method, Payload);
	return Edit.Finish(Result);
}

// ── The handlers ──────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::AddTask(const TSharedPtr<FJsonObject>& Params) { return AddSlotNode(Params, StateTreeTaskSlot); }
TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveTask(const TSharedPtr<FJsonObject>& Params) { return RemoveSlotNode(Params, StateTreeTaskSlot); }
TSharedPtr<FJsonValue> FStateTreeHandlers::SetTaskInstanceProperty(const TSharedPtr<FJsonObject>& Params) { return SetSlotNodeProperty(Params, StateTreeTaskSlot, true); }
TSharedPtr<FJsonValue> FStateTreeHandlers::SetTaskProperty(const TSharedPtr<FJsonObject>& Params) { return SetSlotNodeProperty(Params, StateTreeTaskSlot, false); }

TSharedPtr<FJsonValue> FStateTreeHandlers::AddEnterCondition(const TSharedPtr<FJsonObject>& Params) { return AddSlotNode(Params, StateTreeEnterConditionSlot); }
TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveEnterCondition(const TSharedPtr<FJsonObject>& Params) { return RemoveSlotNode(Params, StateTreeEnterConditionSlot); }

TSharedPtr<FJsonValue> FStateTreeHandlers::AddTransitionCondition(const TSharedPtr<FJsonObject>& Params) { return AddSlotNode(Params, StateTreeTransitionConditionSlot); }
TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveTransitionCondition(const TSharedPtr<FJsonObject>& Params) { return RemoveSlotNode(Params, StateTreeTransitionConditionSlot); }

TSharedPtr<FJsonValue> FStateTreeHandlers::AddConsideration(const TSharedPtr<FJsonObject>& Params) { return AddSlotNode(Params, StateTreeConsiderationSlot); }
TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveConsideration(const TSharedPtr<FJsonObject>& Params) { return RemoveSlotNode(Params, StateTreeConsiderationSlot); }

TSharedPtr<FJsonValue> FStateTreeHandlers::AddEvaluator(const TSharedPtr<FJsonObject>& Params) { return AddSlotNode(Params, StateTreeEvaluatorSlot); }
TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveEvaluator(const TSharedPtr<FJsonObject>& Params) { return RemoveSlotNode(Params, StateTreeEvaluatorSlot); }
TSharedPtr<FJsonValue> FStateTreeHandlers::SetEvaluatorInstanceProperty(const TSharedPtr<FJsonObject>& Params) { return SetSlotNodeProperty(Params, StateTreeEvaluatorSlot, true); }
TSharedPtr<FJsonValue> FStateTreeHandlers::SetEvaluatorProperty(const TSharedPtr<FJsonObject>& Params) { return SetSlotNodeProperty(Params, StateTreeEvaluatorSlot, false); }

TSharedPtr<FJsonValue> FStateTreeHandlers::AddGlobalTask(const TSharedPtr<FJsonObject>& Params) { return AddSlotNode(Params, StateTreeGlobalTaskSlot); }
TSharedPtr<FJsonValue> FStateTreeHandlers::RemoveGlobalTask(const TSharedPtr<FJsonObject>& Params) { return RemoveSlotNode(Params, StateTreeGlobalTaskSlot); }
TSharedPtr<FJsonValue> FStateTreeHandlers::SetGlobalTaskInstanceProperty(const TSharedPtr<FJsonObject>& Params) { return SetSlotNodeProperty(Params, StateTreeGlobalTaskSlot, true); }
TSharedPtr<FJsonValue> FStateTreeHandlers::SetGlobalTaskProperty(const TSharedPtr<FJsonObject>& Params) { return SetSlotNodeProperty(Params, StateTreeGlobalTaskSlot, false); }

#endif // UE_MCP_HAS_5_5_API
