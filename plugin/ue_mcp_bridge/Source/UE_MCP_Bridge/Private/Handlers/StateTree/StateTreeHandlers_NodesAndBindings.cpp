// StateTree authoring by node ID, and binding introspection.
//
// UE 5.9 gives StateTree a scripting surface: K2_ add/move/remove for every
// node list on a state and on the editor data, a binding API on the editor
// data (bindable properties, binding options, path resolution, a validity
// check, promote to parameter), and node-level reads and writes on the editing
// subsystem (template and instance data, task enabled, considered for
// completion, condition evaluation mode). Most of it already had a native
// route. What did not:
//
//   1. NODES COULD NOT BE REORDERED. Tasks run in list order, enter conditions
//      and transition conditions combine in list order through their operand
//      and indent, and considerations multiply in list order, so a node added
//      in the wrong place meant remove and re-add with a new ID, which also
//      drops every binding into it. move_state_tree_node moves any node, found
//      by the ID the add_* actions return, within the list it is in.
//   2. A NODE COULD ONLY BE READ AS PART OF ITS STATE, and conditions and
//      considerations had no setter at all: set_task_property and its kin are
//      addressed by task index or by evaluator and global task ID.
//      read_state_tree_node and set_state_tree_node_data address any node by
//      ID; set_state_tree_node_flags writes the three typed flags the editor
//      exposes (task enabled, considered for completion, condition evaluation
//      mode) with the editor's own rules.
//   3. BINDINGS COULD BE WRITTEN BUT NOT CHECKED. add_binding takes two struct
//      IDs and two paths and stores whatever it is given, so a typo, a source
//      the target cannot see, or an incompatible type surfaced only as a
//      compile error. list_state_tree_bindable_properties lists what a node
//      can be bound on and, for one target, every compatible source in scope;
//      check_state_tree_binding resolves both ends and grades the pair;
//      promote_state_tree_property_to_parameter is the details panel's
//      Promote to Parameter.
//
// Everything here is native against UStateTreeState and UStateTreeEditorData,
// so one code path serves every engine the category supports (5.5 and later;
// see StateTreeHandlers.cpp). The binding actions read the property-binding
// types that became general in 5.7 (FPropertyBindingPath,
// FPropertyBindingBindableStructDescriptor), and answer with a clear error on
// 5.5 and 5.6, where those types are StateTree-specific.
//
// The file-local helpers carry a StateTreeNB prefix: the module is a unity
// build, and a helper name shared with another StateTree .cpp in the same blob
// is a redefinition (C2084).

#include "StateTreeHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#if UE_MCP_HAS_5_5_API

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MCPEngineCompat.h"
#include "Misc/StringOutputDevice.h"
#include "UObject/UnrealType.h"

#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTypes.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
#include "PropertyBindingBindableStructDescriptor.h"
#include "PropertyBindingDataView.h"
#include "PropertyBindingPath.h"
#include "PropertyBindingTypes.h"
#endif

namespace
{
	FString StateTreeNBGuid(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	/** Accepts the hyphenated form the add_* actions return and the bare
	 *  32-digit form list_bindable_sources prints. */
	FGuid StateTreeNBParseGuid(const FString& Str)
	{
		FGuid G;
		FGuid::Parse(Str, G);
		return G;
	}

	/** Where a node lives: the list that holds it (null for a state's single
	 *  task), the object that owns that list, and how to address it. */
	struct FStateTreeNBNodeLocation
	{
		FStateTreeEditorNode* Node = nullptr;
		TArray<FStateTreeEditorNode>* List = nullptr;
		int32 Index = INDEX_NONE;
		UObject* Owner = nullptr;
		UStateTreeState* State = nullptr;
		int32 TransitionIndex = INDEX_NONE;
		/** tasks, singleTask, enterConditions, considerations, transitionConditions, evaluators or globalTasks */
		FString Container;
	};

	bool StateTreeNBFindInList(TArray<FStateTreeEditorNode>& List, const FGuid& Id, UObject* Owner, UStateTreeState* State,
		const TCHAR* Container, int32 TransitionIndex, FStateTreeNBNodeLocation& Out)
	{
		for (int32 i = 0; i < List.Num(); ++i)
		{
			if (List[i].ID == Id)
			{
				Out.Node = &List[i];
				Out.List = &List;
				Out.Index = i;
				Out.Owner = Owner;
				Out.State = State;
				Out.TransitionIndex = TransitionIndex;
				Out.Container = Container;
				return true;
			}
		}
		return false;
	}

	bool StateTreeNBFindInState(UStateTreeState* State, const FGuid& Id, FStateTreeNBNodeLocation& Out)
	{
		if (!State) return false;
		if (StateTreeNBFindInList(State->Tasks, Id, State, State, TEXT("tasks"), INDEX_NONE, Out)) return true;
		if (StateTreeNBFindInList(State->EnterConditions, Id, State, State, TEXT("enterConditions"), INDEX_NONE, Out)) return true;
		if (StateTreeNBFindInList(State->Considerations, Id, State, State, TEXT("considerations"), INDEX_NONE, Out)) return true;
		for (int32 t = 0; t < State->Transitions.Num(); ++t)
		{
			if (StateTreeNBFindInList(State->Transitions[t].Conditions, Id, State, State, TEXT("transitionConditions"), t, Out)) return true;
		}
		if (State->SingleTask.ID == Id && Id.IsValid())
		{
			Out.Node = &State->SingleTask;
			Out.List = nullptr;
			Out.Index = 0;
			Out.Owner = State;
			Out.State = State;
			Out.Container = TEXT("singleTask");
			return true;
		}
		for (UStateTreeState* Child : State->Children)
		{
			if (StateTreeNBFindInState(Child, Id, Out)) return true;
		}
		return false;
	}

	bool StateTreeNBFindNode(UStateTreeEditorData* EditorData, const FGuid& Id, FStateTreeNBNodeLocation& Out)
	{
		if (!EditorData || !Id.IsValid()) return false;
		if (StateTreeNBFindInList(EditorData->Evaluators, Id, EditorData, nullptr, TEXT("evaluators"), INDEX_NONE, Out)) return true;
		if (StateTreeNBFindInList(EditorData->GlobalTasks, Id, EditorData, nullptr, TEXT("globalTasks"), INDEX_NONE, Out)) return true;
		for (UStateTreeState* Root : EditorData->SubTrees)
		{
			if (StateTreeNBFindInState(Root, Id, Out)) return true;
		}
		return false;
	}

	FString StateTreeNBNodeKind(const FStateTreeEditorNode& Node)
	{
		const UScriptStruct* S = Node.Node.GetScriptStruct();
		if (!S) return TEXT("unknown");
		if (S->IsChildOf(FStateTreeTaskBase::StaticStruct())) return TEXT("task");
		if (S->IsChildOf(FStateTreeConditionBase::StaticStruct())) return TEXT("condition");
		if (S->IsChildOf(FStateTreeConsiderationBase::StaticStruct())) return TEXT("consideration");
		if (S->IsChildOf(FStateTreeEvaluatorBase::StaticStruct())) return TEXT("evaluator");
		return TEXT("unknown");
	}

	TSharedPtr<FJsonObject> StateTreeNBExportProps(const UStruct* Struct, const void* Memory)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Struct || !Memory) return Obj;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FString Value;
			It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(Memory), nullptr, nullptr, PPF_None);
			Obj->SetStringField(It->GetName(), Value);
		}
		return Obj;
	}

	/** A struct (or object) and the memory a property path is written into. */
	struct FStateTreeNBDataTarget
	{
		const UStruct* Struct = nullptr;
		void* Memory = nullptr;
		UObject* Object = nullptr;
	};

	/** target = node | instance | executionRuntime. Instance data held as a
	 *  UObject (a Blueprint node) resolves to that object. */
	bool StateTreeNBResolveDataTarget(FStateTreeEditorNode& Node, const FString& Target, FStateTreeNBDataTarget& Out, FString& OutError)
	{
		if (Target.Equals(TEXT("node"), ESearchCase::IgnoreCase))
		{
			Out.Struct = Node.Node.GetScriptStruct();
			Out.Memory = Node.Node.GetMutableMemory();
		}
		else if (Target.IsEmpty() || Target.Equals(TEXT("instance"), ESearchCase::IgnoreCase))
		{
			if (Node.InstanceObject)
			{
				Out.Object = Node.InstanceObject;
				Out.Struct = Node.InstanceObject->GetClass();
				Out.Memory = Node.InstanceObject;
			}
			else
			{
				Out.Struct = Node.Instance.GetScriptStruct();
				Out.Memory = Node.Instance.GetMutableMemory();
			}
		}
		else if (Target.Equals(TEXT("executionRuntime"), ESearchCase::IgnoreCase))
		{
#if UE_MCP_HAS_STATETREE_EXECUTION_RUNTIME_DATA
			if (Node.ExecutionRuntimeDataObject)
			{
				Out.Object = Node.ExecutionRuntimeDataObject;
				Out.Struct = Node.ExecutionRuntimeDataObject->GetClass();
				Out.Memory = Node.ExecutionRuntimeDataObject;
			}
			else
			{
				Out.Struct = Node.ExecutionRuntimeData.GetScriptStruct();
				Out.Memory = Node.ExecutionRuntimeData.GetMutableMemory();
			}
#else
			OutError = TEXT("Execution runtime data is a UE 5.7 addition to StateTree nodes; this engine's nodes have none.");
			return false;
#endif
		}
		else
		{
			OutError = FString::Printf(TEXT("Unknown target '%s'. Use node, instance or executionRuntime."), *Target);
			return false;
		}
		if (!Out.Struct || !Out.Memory)
		{
			OutError = FString::Printf(TEXT("This node has no %s data to write."), Target.IsEmpty() ? TEXT("instance") : *Target);
			return false;
		}
		return true;
	}

	FBoolProperty* StateTreeNBBoolProp(const UStruct* Struct, const TCHAR* Name)
	{
		return Struct ? FindFProperty<FBoolProperty>(Struct, Name) : nullptr;
	}

	/** The consideredForCompletion flags live on the node struct, except on a
	 *  Blueprint task, where the editor reads and writes the copy on the
	 *  Blueprint instance (StateTreeEditorNodeUtils does the same). */
	FStateTreeNBDataTarget StateTreeNBCompletionFlagTarget(FStateTreeEditorNode& Node)
	{
		FStateTreeNBDataTarget T;
		if (Node.Node.GetPtr<FStateTreeBlueprintTaskWrapper>() && Node.InstanceObject)
		{
			T.Struct = Node.InstanceObject->GetClass();
			T.Memory = Node.InstanceObject;
			T.Object = Node.InstanceObject;
		}
		else
		{
			T.Struct = Node.Node.GetScriptStruct();
			T.Memory = Node.Node.GetMutableMemory();
		}
		return T;
	}

	bool StateTreeNBReadBool(const FStateTreeNBDataTarget& T, const TCHAR* Name, bool& Out)
	{
		FBoolProperty* P = StateTreeNBBoolProp(T.Struct, Name);
		if (!P || !T.Memory) return false;
		Out = P->GetPropertyValue_InContainer(T.Memory);
		return true;
	}

	bool StateTreeNBWriteBool(const FStateTreeNBDataTarget& T, const TCHAR* Name, bool bValue)
	{
		FBoolProperty* P = StateTreeNBBoolProp(T.Struct, Name);
		if (!P || !T.Memory) return false;
		P->SetPropertyValue_InContainer(T.Memory, bValue);
		return true;
	}

	/** FStateTreeConditionBase::EvaluationMode, through reflection so an engine
	 *  whose conditions lack the field answers instead of failing to build. */
	FEnumProperty* StateTreeNBEvalModeProp(const FStateTreeEditorNode& Node)
	{
		const UScriptStruct* S = Node.Node.GetScriptStruct();
		if (!S || !S->IsChildOf(FStateTreeConditionBase::StaticStruct())) return nullptr;
		return FindFProperty<FEnumProperty>(S, TEXT("EvaluationMode"));
	}

	bool StateTreeNBReadEvalMode(FStateTreeEditorNode& Node, FString& Out)
	{
		FEnumProperty* P = StateTreeNBEvalModeProp(Node);
		void* Mem = Node.Node.GetMutableMemory();
		if (!P || !Mem || !P->GetEnum()) return false;
		const int64 V = P->GetUnderlyingProperty()->GetSignedIntPropertyValue(P->ContainerPtrToValuePtr<void>(Mem));
		Out = P->GetEnum()->GetNameStringByValue(V);
		return true;
	}

	void StateTreeNBTouch(UStateTree* ST)
	{
#if UE_MCP_HAS_5_9_API
		UStateTreeEditingSubsystem::MarkAsModified(ST);
#else
		UStateTreeEditingSubsystem::ValidateStateTree(ST);
#endif
	}

#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
	FString StateTreeNBPathString(const FPropertyBindingPath& Path)
	{
		FString Out;
		for (const FPropertyBindingPathSegment& Segment : Path.GetSegments())
		{
			if (!Out.IsEmpty()) Out += TEXT(".");
			Out += Segment.GetName().ToString();
			if (Segment.GetArrayIndex() != INDEX_NONE) Out += FString::Printf(TEXT("[%d]"), Segment.GetArrayIndex());
		}
		return Out;
	}

	FString StateTreeNBPropertyType(const FProperty* Prop)
	{
		if (!Prop) return FString();
		if (const FStructProperty* SP = CastField<FStructProperty>(Prop)) return SP->Struct ? SP->Struct->GetName() : FString();
		if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Prop)) return OP->PropertyClass ? OP->PropertyClass->GetName() : FString();
		if (const FEnumProperty* EP = CastField<FEnumProperty>(Prop)) return EP->GetEnum() ? EP->GetEnum()->GetName() : FString();
		return Prop->GetCPPType();
	}

	FString StateTreeNBCompatibility(UE::PropertyBinding::EPropertyCompatibility C)
	{
		switch (C)
		{
		case UE::PropertyBinding::EPropertyCompatibility::Compatible: return TEXT("compatible");
		case UE::PropertyBinding::EPropertyCompatibility::Promotable: return TEXT("promotable");
		default: return TEXT("incompatible");
		}
	}

	/** One property reachable from a struct, with the dotted path that names it. */
	struct FStateTreeNBPropEntry
	{
		FString Path;
		const FProperty* Property = nullptr;
	};

	/** Every property of Struct, descending into value structs, MaxDepth levels
	 *  deep (1 = top level only, <= 0 = unlimited). Instanced structs and
	 *  objects are not descended: their contents depend on the value. */
	void StateTreeNBCollectProps(const UStruct* Struct, const FString& Prefix, int32 Depth, int32 MaxDepth, TArray<FStateTreeNBPropEntry>& Out)
	{
		if (!Struct || Depth > 16) return;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* P = *It;
			if (P->HasAnyPropertyFlags(CPF_Deprecated)) continue;
			const FString Path = Prefix.IsEmpty() ? P->GetName() : Prefix + TEXT(".") + P->GetName();
			Out.Add({ Path, P });
			if (MaxDepth > 0 && Depth + 1 >= MaxDepth) continue;
			if (const FStructProperty* SP = CastField<FStructProperty>(P))
			{
				if (SP->Struct && SP->Struct != FInstancedStruct::StaticStruct())
				{
					StateTreeNBCollectProps(SP->Struct, Path, Depth + 1, MaxDepth, Out);
				}
			}
		}
	}

	/** Resolve StructId + Path to its leaf property against the struct's
	 *  current value. An empty path names the struct itself. */
	bool StateTreeNBResolve(UStateTreeEditorData* EditorData, const FGuid& StructId, const FString& PathStr,
		FPropertyBindingPath& OutPath, FPropertyBindingDataView& OutView, TArray<FPropertyBindingPathIndirection>& OutIndirections, FString& OutError)
	{
		if (!StructId.IsValid())
		{
			OutError = TEXT("struct ID is not a GUID");
			return false;
		}
		if (!EditorData->GetBindingDataViewByID(StructId, OutView) || !OutView.GetStruct())
		{
			OutError = FString::Printf(TEXT("no bindable struct with ID %s in this tree (list them with statetree(list_bindable_sources))"), *StateTreeNBGuid(StructId));
			return false;
		}
		OutPath = FPropertyBindingPath();
		OutPath.SetStructID(StructId);
		if (!PathStr.IsEmpty() && !OutPath.FromString(PathStr))
		{
			OutError = FString::Printf(TEXT("could not parse property path '%s'"), *PathStr);
			return false;
		}
		if (PathStr.IsEmpty()) return true;
		FString ResolveError;
		if (!OutPath.ResolveIndirectionsWithValue(OutView, OutIndirections, &ResolveError) || OutIndirections.Num() == 0)
		{
			OutError = FString::Printf(TEXT("'%s' does not resolve on %s: %s"), *PathStr, *OutView.GetStruct()->GetName(), *ResolveError);
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonObject> StateTreeNBExistingBinding(UStateTreeEditorData* EditorData, const FGuid& TargetId, const FString& TargetPath)
	{
		if (const FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings())
		{
			for (const FStateTreePropertyPathBinding& B : Bindings->GetBindings())
			{
				if (B.GetTargetPath().GetStructID() == TargetId && StateTreeNBPathString(B.GetTargetPath()) == TargetPath)
				{
					TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetStringField(TEXT("sourceStructId"), StateTreeNBGuid(B.GetSourcePath().GetStructID()));
					Obj->SetStringField(TEXT("sourcePath"), StateTreeNBPathString(B.GetSourcePath()));
					return Obj;
				}
			}
		}
		return nullptr;
	}
#endif // UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
}

// ── move_state_tree_node ───────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::MoveNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	// Every parameter is read before the asset load (#1057).
	FString NodeIdStr;
	const TSharedPtr<FJsonValue> NodeErr = RequireString(Params, TEXT("nodeId"), NodeIdStr);
	int32 NewIndex = INDEX_NONE;
	const TSharedPtr<FJsonValue> IndexErr = RequireNumber(Params, TEXT("newIndex"), NewIndex);
	if (NodeErr) return NodeErr;
	if (IndexErr) return IndexErr;

	UStateTree* ST = nullptr;
	UStateTreeEditorData* EditorData = nullptr;
	if (auto LoadErr = LoadForEdit(AssetPath, ST, EditorData)) return LoadErr;

	FStateTreeNBNodeLocation Loc;
	if (!StateTreeNBFindNode(EditorData, StateTreeNBParseGuid(NodeIdStr), Loc))
	{
		return MCPError(FString::Printf(TEXT("No node with ID '%s' in this tree. Node IDs are what the add_* actions return and read_state reports as nodeId."), *NodeIdStr));
	}
	if (!Loc.List)
	{
		return MCPError(TEXT("That node is the state's single task, which is not in a list and has no order to change."));
	}
	if (NewIndex < 0 || NewIndex >= Loc.List->Num())
	{
		return MCPError(FString::Printf(TEXT("newIndex %d is out of range: the %s list holding this node has %d entries (0 to %d)."),
			NewIndex, *Loc.Container, Loc.List->Num(), Loc.List->Num() - 1));
	}

	const int32 OldIndex = Loc.Index;
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("nodeId"), StateTreeNBGuid(Loc.Node->ID));
	Result->SetStringField(TEXT("container"), Loc.Container);
	if (Loc.State) Result->SetStringField(TEXT("stateId"), StateTreeNBGuid(Loc.State->ID));
	if (Loc.TransitionIndex != INDEX_NONE) Result->SetNumberField(TEXT("transitionIndex"), Loc.TransitionIndex);
	Result->SetNumberField(TEXT("previousIndex"), OldIndex);
	Result->SetNumberField(TEXT("newIndex"), NewIndex);

	if (OldIndex == NewIndex)
	{
		Result->SetBoolField(TEXT("moved"), false);
		MCPSetNoRollback(Result, TEXT("The node was already at that index, so nothing changed and there is nothing to undo."));
		return MCPResult(Result);
	}

	// The same remove-then-insert the 5.9 K2_Move* functions do: the node keeps
	// its ID, so bindings into and out of it survive the move.
	Loc.Owner->Modify();
	FStateTreeEditorNode Moved = MoveTemp((*Loc.List)[OldIndex]);
	Loc.List->RemoveAt(OldIndex);
	Loc.List->Insert(MoveTemp(Moved), NewIndex);
	StateTreeNBTouch(ST);

	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("moved"), true);
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
		Payload->SetNumberField(TEXT("newIndex"), OldIndex);
		MCPSetRollback(Result, TEXT("move_state_tree_node"), Payload);
	}
	return MCPResult(Result);
}

// ── read_state_tree_node ───────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::ReadNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString NodeIdStr;
	const TSharedPtr<FJsonValue> NodeErr = RequireString(Params, TEXT("nodeId"), NodeIdStr);
	if (NodeErr) return NodeErr;

	UStateTree* ST = nullptr;
	UStateTreeEditorData* EditorData = nullptr;
	if (auto LoadErr = LoadForEdit(AssetPath, ST, EditorData)) return LoadErr;

	FStateTreeNBNodeLocation Loc;
	if (!StateTreeNBFindNode(EditorData, StateTreeNBParseGuid(NodeIdStr), Loc))
	{
		return MCPError(FString::Printf(TEXT("No node with ID '%s' in this tree."), *NodeIdStr));
	}
	FStateTreeEditorNode& Node = *Loc.Node;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("nodeId"), StateTreeNBGuid(Node.ID));
	Result->SetStringField(TEXT("kind"), StateTreeNBNodeKind(Node));
	Result->SetStringField(TEXT("container"), Loc.Container);
	Result->SetNumberField(TEXT("index"), Loc.Index);
	if (Loc.List) Result->SetNumberField(TEXT("siblingCount"), Loc.List->Num());
	if (Loc.State)
	{
		Result->SetStringField(TEXT("stateId"), StateTreeNBGuid(Loc.State->ID));
		Result->SetStringField(TEXT("statePath"), StateDotPath(Loc.State));
	}
	if (Loc.TransitionIndex != INDEX_NONE) Result->SetNumberField(TEXT("transitionIndex"), Loc.TransitionIndex);

	const UScriptStruct* NodeStruct = Node.Node.GetScriptStruct();
	Result->SetStringField(TEXT("structType"), NodeStruct ? NodeStruct->GetName() : FString());
	Result->SetStringField(TEXT("structPath"), NodeStruct ? NodeStruct->GetPathName() : FString());
	Result->SetObjectField(TEXT("nodeProperties"), StateTreeNBExportProps(NodeStruct, Node.Node.GetMemory()));

	if (Node.InstanceObject)
	{
		Result->SetStringField(TEXT("instanceType"), Node.InstanceObject->GetClass()->GetPathName());
		Result->SetStringField(TEXT("instanceObjectPath"), Node.InstanceObject->GetPathName());
		Result->SetObjectField(TEXT("instanceProperties"), StateTreeNBExportProps(Node.InstanceObject->GetClass(), Node.InstanceObject));
	}
	else if (const UScriptStruct* InstStruct = Node.Instance.GetScriptStruct())
	{
		Result->SetStringField(TEXT("instanceType"), InstStruct->GetPathName());
		Result->SetObjectField(TEXT("instanceProperties"), StateTreeNBExportProps(InstStruct, Node.Instance.GetMemory()));
	}
#if UE_MCP_HAS_STATETREE_EXECUTION_RUNTIME_DATA
	if (Node.ExecutionRuntimeDataObject)
	{
		Result->SetStringField(TEXT("executionRuntimeType"), Node.ExecutionRuntimeDataObject->GetClass()->GetPathName());
		Result->SetObjectField(TEXT("executionRuntimeProperties"), StateTreeNBExportProps(Node.ExecutionRuntimeDataObject->GetClass(), Node.ExecutionRuntimeDataObject));
	}
	else if (const UScriptStruct* RtStruct = Node.ExecutionRuntimeData.GetScriptStruct())
	{
		Result->SetStringField(TEXT("executionRuntimeType"), RtStruct->GetPathName());
		Result->SetObjectField(TEXT("executionRuntimeProperties"), StateTreeNBExportProps(RtStruct, Node.ExecutionRuntimeData.GetMemory()));
	}
#endif
#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
	// The ID bindings into this node's instance data use: the node ID before
	// 5.8, a derived ID from 5.8 on.
#if UE_MCP_HAS_5_8_API
	Result->SetStringField(TEXT("instanceStructId"), StateTreeNBGuid(Node.GetInstanceDataID()));
#else
	Result->SetStringField(TEXT("instanceStructId"), StateTreeNBGuid(Node.ID));
#endif
#endif
#if UE_MCP_HAS_5_7_API
	Result->SetStringField(TEXT("description"), EditorData->GetNodeDescription(Node).ToString());
#endif

	// The typed flags, with the editor's rules for what can change.
	FStateTreeNBDataTarget NodeT;
	NodeT.Struct = NodeStruct;
	NodeT.Memory = Node.Node.GetMutableMemory();
	bool bFlag = false;
	if (StateTreeNBReadBool(NodeT, TEXT("bTaskEnabled"), bFlag)) Result->SetBoolField(TEXT("taskEnabled"), bFlag);
	const FStateTreeNBDataTarget CompletionT = StateTreeNBCompletionFlagTarget(Node);
	if (StateTreeNBReadBool(CompletionT, TEXT("bConsideredForCompletion"), bFlag)) Result->SetBoolField(TEXT("consideredForCompletion"), bFlag);
	bool bCanEdit = false;
	if (StateTreeNBReadBool(CompletionT, TEXT("bCanEditConsideredForCompletion"), bCanEdit))
	{
		bool bEnabled = true;
		StateTreeNBReadBool(NodeT, TEXT("bTaskEnabled"), bEnabled);
		Result->SetBoolField(TEXT("canEditConsideredForCompletion"), bCanEdit && bEnabled);
	}
	FString EvalMode;
	if (StateTreeNBReadEvalMode(Node, EvalMode)) Result->SetStringField(TEXT("evaluationMode"), EvalMode);

	Result->SetNumberField(TEXT("expressionIndent"), Node.ExpressionIndent);
	Result->SetStringField(TEXT("operand"),
		Node.ExpressionOperand == EStateTreeExpressionOperand::Or ? TEXT("Or")
		: (Node.ExpressionOperand == EStateTreeExpressionOperand::And ? TEXT("And") : TEXT("Copy")));
	return MCPResult(Result);
}

// ── set_state_tree_node_flags ──────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::SetNodeFlags(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString NodeIdStr;
	const TSharedPtr<FJsonValue> NodeErr = RequireString(Params, TEXT("nodeId"), NodeIdStr);
	bool bTaskEnabled = false;
	const bool bHasTaskEnabled = TryGetBoolParam(Params, TEXT("taskEnabled"), bTaskEnabled);
	bool bConsidered = false;
	const bool bHasConsidered = TryGetBoolParam(Params, TEXT("consideredForCompletion"), bConsidered);
	const bool bHasEvalMode = HasParam(Params, TEXT("evaluationMode"));
	const FString EvalModeStr = OptionalString(Params, TEXT("evaluationMode"));
	if (NodeErr) return NodeErr;
	if (!bHasTaskEnabled && !bHasConsidered && !bHasEvalMode)
	{
		return MCPError(TEXT("Pass at least one of taskEnabled, consideredForCompletion or evaluationMode."));
	}

	UStateTree* ST = nullptr;
	UStateTreeEditorData* EditorData = nullptr;
	if (auto LoadErr = LoadForEdit(AssetPath, ST, EditorData)) return LoadErr;

	FStateTreeNBNodeLocation Loc;
	if (!StateTreeNBFindNode(EditorData, StateTreeNBParseGuid(NodeIdStr), Loc))
	{
		return MCPError(FString::Printf(TEXT("No node with ID '%s' in this tree."), *NodeIdStr));
	}
	FStateTreeEditorNode& Node = *Loc.Node;
	FStateTreeNBDataTarget NodeT;
	NodeT.Struct = Node.Node.GetScriptStruct();
	NodeT.Memory = Node.Node.GetMutableMemory();
	const FStateTreeNBDataTarget CompletionT = StateTreeNBCompletionFlagTarget(Node);

	// Validate everything before writing anything.
	bool bPriorEnabled = true;
	const bool bIsTask = StateTreeNBReadBool(NodeT, TEXT("bTaskEnabled"), bPriorEnabled);
	if ((bHasTaskEnabled || bHasConsidered) && !bIsTask)
	{
		return MCPError(FString::Printf(TEXT("taskEnabled and consideredForCompletion apply to tasks; this node is a %s (%s)."),
			*StateTreeNBNodeKind(Node), NodeT.Struct ? *NodeT.Struct->GetName() : TEXT("no struct")));
	}
	bool bPriorConsidered = false;
	if (bHasConsidered)
	{
		const bool bEnabledAfter = bHasTaskEnabled ? bTaskEnabled : bPriorEnabled;
		bool bCanEdit = false;
		if (!StateTreeNBReadBool(CompletionT, TEXT("bConsideredForCompletion"), bPriorConsidered)
			|| !StateTreeNBReadBool(CompletionT, TEXT("bCanEditConsideredForCompletion"), bCanEdit))
		{
			return MCPError(TEXT("This engine's task nodes carry no considered-for-completion flag."));
		}
		if (!bEnabledAfter)
		{
			return MCPError(TEXT("The task is disabled; enable it (taskEnabled=true) before changing whether it is considered for completion, as the editor requires."));
		}
		if (!bCanEdit)
		{
			return MCPError(TEXT("This task does not let its considered-for-completion setting be changed (bCanEditConsideredForCompletion is false on it)."));
		}
	}
	FEnumProperty* EvalProp = nullptr;
	int64 NewEvalValue = 0;
	FString PriorEvalMode;
	if (bHasEvalMode)
	{
		EvalProp = StateTreeNBEvalModeProp(Node);
		if (!EvalProp || !EvalProp->GetEnum())
		{
			return MCPError(FString::Printf(TEXT("evaluationMode applies to conditions; this node is a %s, or this engine's conditions have no evaluation mode."), *StateTreeNBNodeKind(Node)));
		}
		NewEvalValue = EvalProp->GetEnum()->GetValueByNameString(EvalModeStr);
		if (NewEvalValue == INDEX_NONE)
		{
			return MCPError(FString::Printf(TEXT("Unknown evaluationMode '%s'. Use Evaluated, ForcedTrue or ForcedFalse."), *EvalModeStr));
		}
		StateTreeNBReadEvalMode(Node, PriorEvalMode);
	}

	Loc.Owner->Modify();
	if (CompletionT.Object) CompletionT.Object->Modify();
	if (bHasTaskEnabled) StateTreeNBWriteBool(NodeT, TEXT("bTaskEnabled"), bTaskEnabled);
	if (bHasConsidered) StateTreeNBWriteBool(CompletionT, TEXT("bConsideredForCompletion"), bConsidered);
	if (EvalProp)
	{
		EvalProp->GetUnderlyingProperty()->SetIntPropertyValue(EvalProp->ContainerPtrToValuePtr<void>(NodeT.Memory), NewEvalValue);
	}
	StateTreeNBTouch(ST);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("nodeId"), StateTreeNBGuid(Node.ID));
	auto Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
	Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
	if (bHasTaskEnabled)
	{
		Result->SetBoolField(TEXT("taskEnabled"), bTaskEnabled);
		Payload->SetBoolField(TEXT("taskEnabled"), bPriorEnabled);
	}
	if (bHasConsidered)
	{
		Result->SetBoolField(TEXT("consideredForCompletion"), bConsidered);
		Payload->SetBoolField(TEXT("consideredForCompletion"), bPriorConsidered);
	}
	if (bHasEvalMode)
	{
		Result->SetStringField(TEXT("evaluationMode"), EvalProp->GetEnum()->GetNameStringByValue(NewEvalValue));
		Payload->SetStringField(TEXT("evaluationMode"), PriorEvalMode);
	}
	MCPSetRollback(Result, TEXT("set_state_tree_node_flags"), Payload);
	if (bHasTaskEnabled && bHasConsidered && !bPriorEnabled)
	{
		// The inverse disables the task and restores the completion flag in one
		// call, which the validation above refuses for a disabled result.
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The task was disabled before this call, and a disabled task's completion flag cannot be set, so replaying the inverse in one call is refused. Restore consideredForCompletion first, then taskEnabled=false, as two calls."));
	}
	return MCPResult(Result);
}

// ── set_state_tree_node_data ───────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::SetNodeData(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString NodeIdStr;
	const TSharedPtr<FJsonValue> NodeErr = RequireString(Params, TEXT("nodeId"), NodeIdStr);
	const FString Target = OptionalString(Params, TEXT("target"), TEXT("instance"));
	const FString PropName = OptionalString(Params, TEXT("propertyName"));
	FString Value;
	const TSharedPtr<FJsonValue> ValueErr = RequireString(Params, TEXT("value"), Value);
	if (NodeErr) return NodeErr;
	if (ValueErr) return ValueErr;

	UStateTree* ST = nullptr;
	UStateTreeEditorData* EditorData = nullptr;
	if (auto LoadErr = LoadForEdit(AssetPath, ST, EditorData)) return LoadErr;

	FStateTreeNBNodeLocation Loc;
	if (!StateTreeNBFindNode(EditorData, StateTreeNBParseGuid(NodeIdStr), Loc))
	{
		return MCPError(FString::Printf(TEXT("No node with ID '%s' in this tree."), *NodeIdStr));
	}

	FStateTreeNBDataTarget T;
	FString TargetError;
	if (!StateTreeNBResolveDataTarget(*Loc.Node, Target, T, TargetError)) return MCPError(TargetError);

	FString Prior;
	FStringOutputDevice ImportErrors;
	Loc.Owner->Modify();
	if (T.Object) T.Object->Modify();
	if (PropName.IsEmpty())
	{
		// The whole struct, as K2_SetNode*Data replaces it. A UObject instance is
		// written property by property through ImportText on its class.
		const UScriptStruct* SS = Cast<UScriptStruct>(T.Struct);
		if (!SS)
		{
			return MCPError(TEXT("This node's data is a UObject (a Blueprint node), which cannot be replaced whole; pass propertyName to write one property."));
		}
		SS->ExportText(Prior, T.Memory, nullptr, nullptr, PPF_None, nullptr);
		const TCHAR* Parsed = SS->ImportText(*Value, T.Memory, nullptr, PPF_None, &ImportErrors, SS->GetName());
		if (!Parsed || !ImportErrors.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("Could not parse '%s' as %s: %s"), *Value, *SS->GetName(), *ImportErrors));
		}
	}
	else
	{
		FProperty* Prop = T.Struct->FindPropertyByName(*PropName);
		if (!Prop)
		{
			TArray<FString> Names;
			for (TFieldIterator<FProperty> It(T.Struct); It; ++It) Names.Add(It->GetName());
			return MCPError(FString::Printf(TEXT("%s has no property '%s'. Its properties: %s"),
				*T.Struct->GetName(), *PropName, *FString::Join(Names, TEXT(", "))));
		}
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(T.Memory);
		Prop->ExportTextItem_Direct(Prior, ValuePtr, nullptr, nullptr, PPF_None);
		const TCHAR* Parsed = Prop->ImportText_Direct(*Value, ValuePtr, T.Object, PPF_None, &ImportErrors);
		if (!Parsed || !ImportErrors.IsEmpty())
		{
			return MCPError(FString::Printf(TEXT("Could not parse '%s' for %s (%s): %s"), *Value, *PropName, *Prop->GetCPPType(), *ImportErrors));
		}
	}
	StateTreeNBTouch(ST);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("nodeId"), StateTreeNBGuid(Loc.Node->ID));
	Result->SetStringField(TEXT("target"), Target);
	Result->SetStringField(TEXT("dataType"), T.Struct->GetName());
	if (!PropName.IsEmpty()) Result->SetStringField(TEXT("propertyName"), PropName);
	Result->SetStringField(TEXT("previousValue"), Prior);
	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
		Payload->SetStringField(TEXT("target"), Target);
		if (!PropName.IsEmpty()) Payload->SetStringField(TEXT("propertyName"), PropName);
		Payload->SetStringField(TEXT("value"), Prior);
		MCPSetRollback(Result, TEXT("set_state_tree_node_data"), Payload);
	}
	return MCPResult(Result);
}

// ── list_state_tree_bindable_properties ────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::ListBindableProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString StructIdStr;
	const TSharedPtr<FJsonValue> StructErr = RequireString(Params, TEXT("structId"), StructIdStr);
	const FString TargetPathStr = OptionalString(Params, TEXT("targetPath"));
	const bool bExactOnly = OptionalBool(Params, TEXT("exactOnly"), false);
	const int32 MaxDepth = OptionalInt(Params, TEXT("maxDepth"), 3);
	if (StructErr) return StructErr;

#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
	UStateTree* ST = nullptr;
	UStateTreeEditorData* EditorData = nullptr;
	if (auto LoadErr = LoadForEdit(AssetPath, ST, EditorData)) return LoadErr;

	const FGuid StructId = StateTreeNBParseGuid(StructIdStr);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("structId"), StateTreeNBGuid(StructId));

	if (TargetPathStr.IsEmpty())
	{
		// GetBindableProperties: every property of the owner, with its binding.
		FPropertyBindingDataView View;
		if (!StructId.IsValid() || !EditorData->GetBindingDataViewByID(StructId, View) || !View.GetStruct())
		{
			return MCPError(FString::Printf(TEXT("No bindable struct with ID '%s' in this tree (list them with statetree(list_bindable_sources))."), *StructIdStr));
		}
		Result->SetStringField(TEXT("structType"), View.GetStruct()->GetName());
		TArray<FStateTreeNBPropEntry> Entries;
		StateTreeNBCollectProps(View.GetStruct(), FString(), 0, MaxDepth, Entries);
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const FStateTreeNBPropEntry& E : Entries)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("path"), E.Path);
			Row->SetStringField(TEXT("type"), StateTreeNBPropertyType(E.Property));
#if WITH_METADATA
			const FString Category = E.Property->GetMetaData(TEXT("Category"));
			if (!Category.IsEmpty()) Row->SetStringField(TEXT("category"), Category);
#endif
			if (TSharedPtr<FJsonObject> Bound = StateTreeNBExistingBinding(EditorData, StructId, E.Path))
			{
				Row->SetObjectField(TEXT("binding"), Bound);
			}
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		Result->SetArrayField(TEXT("properties"), Rows);
		return MCPResult(Result);
	}

	// GetBindingOptions: every compatible source in scope for one target.
	FPropertyBindingPath TargetPath;
	FPropertyBindingDataView TargetView;
	TArray<FPropertyBindingPathIndirection> TargetInd;
	FString ResolveError;
	if (!StateTreeNBResolve(EditorData, StructId, TargetPathStr, TargetPath, TargetView, TargetInd, ResolveError))
	{
		return MCPError(FString::Printf(TEXT("Target: %s"), *ResolveError));
	}
	const FProperty* TargetProp = TargetInd.Last().GetProperty();
	Result->SetStringField(TEXT("targetPath"), TargetPathStr);
	Result->SetStringField(TEXT("targetType"), StateTreeNBPropertyType(TargetProp));

	TArray<TInstancedStruct<FPropertyBindingBindableStructDescriptor>> Accessible;
	EditorData->GetBindableStructs(StructId, Accessible);

	TArray<TSharedPtr<FJsonValue>> Exact;
	TArray<TSharedPtr<FJsonValue>> Promoted;
	for (const TInstancedStruct<FPropertyBindingBindableStructDescriptor>& DescStruct : Accessible)
	{
		const FPropertyBindingBindableStructDescriptor* Desc = DescStruct.GetPtr<FPropertyBindingBindableStructDescriptor>();
		if (!Desc || !Desc->Struct || Desc->ID == StructId) continue;
		TArray<FStateTreeNBPropEntry> Entries;
		StateTreeNBCollectProps(Desc->Struct, FString(), 0, MaxDepth, Entries);
		for (const FStateTreeNBPropEntry& E : Entries)
		{
			const UE::PropertyBinding::EPropertyCompatibility C = UE::PropertyBinding::GetPropertyCompatibility(E.Property, TargetProp);
			if (C == UE::PropertyBinding::EPropertyCompatibility::Incompatible) continue;
			if (bExactOnly && C != UE::PropertyBinding::EPropertyCompatibility::Compatible) continue;
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("sourceStructId"), StateTreeNBGuid(Desc->ID));
			Row->SetStringField(TEXT("sourceName"), Desc->Name.ToString());
			Row->SetStringField(TEXT("sourceStructType"), Desc->Struct->GetName());
			Row->SetStringField(TEXT("sourcePath"), E.Path);
			Row->SetStringField(TEXT("type"), StateTreeNBPropertyType(E.Property));
			Row->SetStringField(TEXT("compatibility"), StateTreeNBCompatibility(C));
			(C == UE::PropertyBinding::EPropertyCompatibility::Compatible ? Exact : Promoted).Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	// Exact matches before numeric promotions, as the engine's own list orders them.
	Exact.Append(Promoted);
	Result->SetArrayField(TEXT("options"), Exact);
	if (TSharedPtr<FJsonObject> Bound = StateTreeNBExistingBinding(EditorData, StructId, TargetPathStr))
	{
		Result->SetObjectField(TEXT("currentBinding"), Bound);
	}
	return MCPResult(Result);
#else
	(void)TargetPathStr; (void)bExactOnly; (void)MaxDepth;
	return MCPError(TEXT("Binding introspection reads the general property-binding types StateTree adopted in UE 5.7; on this engine, list sources with statetree(list_bindable_sources) and bind with statetree(add_binding)."));
#endif
}

// ── check_state_tree_binding ───────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::CheckBinding(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString TargetIdStr;
	const TSharedPtr<FJsonValue> TargetIdErr = RequireString(Params, TEXT("targetStructId"), TargetIdStr);
	const FString TargetPathStr = OptionalString(Params, TEXT("targetPath"));
	const FString SourceIdStr = OptionalString(Params, TEXT("sourceStructId"));
	const FString SourcePathStr = OptionalString(Params, TEXT("sourcePath"));
	if (TargetIdErr) return TargetIdErr;

#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
	UStateTree* ST = nullptr;
	UStateTreeEditorData* EditorData = nullptr;
	if (auto LoadErr = LoadForEdit(AssetPath, ST, EditorData)) return LoadErr;

	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> Reasons;

	const FGuid TargetId = StateTreeNBParseGuid(TargetIdStr);
	FPropertyBindingPath TargetPath;
	FPropertyBindingDataView TargetView;
	TArray<FPropertyBindingPathIndirection> TargetInd;
	FString Err;
	const bool bTargetOk = StateTreeNBResolve(EditorData, TargetId, TargetPathStr, TargetPath, TargetView, TargetInd, Err);
	TSharedPtr<FJsonObject> TargetObj = MakeShared<FJsonObject>();
	TargetObj->SetStringField(TEXT("structId"), StateTreeNBGuid(TargetId));
	TargetObj->SetStringField(TEXT("path"), TargetPathStr);
	TargetObj->SetBoolField(TEXT("resolved"), bTargetOk);
	const FProperty* TargetProp = nullptr;
	if (bTargetOk)
	{
		TargetObj->SetStringField(TEXT("structType"), TargetView.GetStruct()->GetName());
		if (TargetInd.Num() > 0)
		{
			TargetProp = TargetInd.Last().GetProperty();
			TargetObj->SetStringField(TEXT("type"), StateTreeNBPropertyType(TargetProp));
		}
	}
	else
	{
		Reasons.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("target: %s"), *Err)));
	}
	Result->SetObjectField(TEXT("target"), TargetObj);
	if (TSharedPtr<FJsonObject> Bound = bTargetOk ? StateTreeNBExistingBinding(EditorData, TargetId, TargetPathStr) : nullptr)
	{
		Result->SetObjectField(TEXT("currentBinding"), Bound);
	}

	const bool bHasSource = !SourceIdStr.IsEmpty();
	if (!bHasSource)
	{
		// Resolve only: the ResolveBindingPath half of the API.
		Result->SetBoolField(TEXT("resolved"), bTargetOk);
		Result->SetArrayField(TEXT("reasons"), Reasons);
		return MCPResult(Result);
	}

	const FGuid SourceId = StateTreeNBParseGuid(SourceIdStr);
	FPropertyBindingPath SourcePath;
	FPropertyBindingDataView SourceView;
	TArray<FPropertyBindingPathIndirection> SourceInd;
	const bool bSourceOk = StateTreeNBResolve(EditorData, SourceId, SourcePathStr, SourcePath, SourceView, SourceInd, Err);
	TSharedPtr<FJsonObject> SourceObj = MakeShared<FJsonObject>();
	SourceObj->SetStringField(TEXT("structId"), StateTreeNBGuid(SourceId));
	SourceObj->SetStringField(TEXT("path"), SourcePathStr);
	SourceObj->SetBoolField(TEXT("resolved"), bSourceOk);
	const FProperty* SourceProp = nullptr;
	if (bSourceOk)
	{
		SourceObj->SetStringField(TEXT("structType"), SourceView.GetStruct()->GetName());
		if (SourceInd.Num() > 0)
		{
			SourceProp = SourceInd.Last().GetProperty();
			SourceObj->SetStringField(TEXT("type"), StateTreeNBPropertyType(SourceProp));
		}
	}
	else
	{
		Reasons.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("source: %s"), *Err)));
	}
	Result->SetObjectField(TEXT("source"), SourceObj);

	bool bInScope = false;
	if (bTargetOk && bSourceOk)
	{
		TArray<TInstancedStruct<FPropertyBindingBindableStructDescriptor>> Accessible;
		EditorData->GetBindableStructs(TargetId, Accessible);
		for (const TInstancedStruct<FPropertyBindingBindableStructDescriptor>& D : Accessible)
		{
			const FPropertyBindingBindableStructDescriptor* Desc = D.GetPtr<FPropertyBindingBindableStructDescriptor>();
			if (Desc && Desc->ID == SourceId) { bInScope = true; break; }
		}
		if (SourceId == TargetId)
		{
			bInScope = false;
			Reasons.Add(MakeShared<FJsonValueString>(TEXT("a struct cannot bind to itself")));
		}
		else if (!bInScope)
		{
			Reasons.Add(MakeShared<FJsonValueString>(TEXT("the source is not in the target's scope: a node can read the context, the tree parameters, evaluators, global tasks, and nodes of its own state and the states above it that run before it")));
		}
	}
	Result->SetBoolField(TEXT("inScope"), bInScope);

	FString Compat = TEXT("incompatible");
	if (SourceProp && TargetProp)
	{
		Compat = StateTreeNBCompatibility(UE::PropertyBinding::GetPropertyCompatibility(SourceProp, TargetProp));
		if (Compat == TEXT("incompatible"))
		{
			Reasons.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s cannot be copied into %s"),
				*StateTreeNBPropertyType(SourceProp), *StateTreeNBPropertyType(TargetProp))));
		}
	}
	else if (bTargetOk && bSourceOk)
	{
		Reasons.Add(MakeShared<FJsonValueString>(TEXT("both ends need a property path; a whole-struct binding is not graded here")));
	}
	Result->SetStringField(TEXT("compatibility"), Compat);
	Result->SetBoolField(TEXT("canBind"), bTargetOk && bSourceOk && bInScope && Compat != TEXT("incompatible"));
	Result->SetArrayField(TEXT("reasons"), Reasons);
	return MCPResult(Result);
#else
	(void)TargetPathStr; (void)SourceIdStr; (void)SourcePathStr;
	return MCPError(TEXT("Binding checks read the general property-binding types StateTree adopted in UE 5.7, which this engine predates."));
#endif
}

// ── promote_state_tree_property_to_parameter ───────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::PromoteToParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	const FStateRef StateRef = ReadStateRef(Params);
	FString TargetIdStr;
	const TSharedPtr<FJsonValue> TargetIdErr = RequireString(Params, TEXT("targetStructId"), TargetIdStr);
	FString TargetPathStr;
	const TSharedPtr<FJsonValue> TargetPathErr = RequireString(Params, TEXT("targetPath"), TargetPathStr);
	const FString ParameterName = OptionalString(Params, TEXT("parameterName"));
	if (TargetIdErr) return TargetIdErr;
	if (TargetPathErr) return TargetPathErr;

#if UE_MCP_HAS_STATETREE_GENERAL_PROPERTY_BINDING
	UStateTree* ST = nullptr;
	UStateTreeEditorData* EditorData = nullptr;
	if (auto LoadErr = LoadForEdit(AssetPath, ST, EditorData)) return LoadErr;

	const FGuid TargetId = StateTreeNBParseGuid(TargetIdStr);
	FPropertyBindingPath TargetPath;
	FPropertyBindingDataView TargetView;
	TArray<FPropertyBindingPathIndirection> TargetInd;
	FString Err;
	if (!StateTreeNBResolve(EditorData, TargetId, TargetPathStr, TargetPath, TargetView, TargetInd, Err))
	{
		return MCPError(FString::Printf(TEXT("Target: %s"), *Err));
	}
	const FPropertyBindingPathIndirection& Leaf = TargetInd.Last();
	const FProperty* LeafProp = Leaf.GetProperty();

	// The parameter goes on the named state (the target's state or an ancestor,
	// neither linked nor a linked asset), or on the tree parameters.
	UStateTreeState* ParamState = nullptr;
	if (StateRef.bHasId || StateRef.bHasPath)
	{
		ParamState = ResolveState(EditorData, StateRef);
		if (!ParamState) return MCPError(TEXT("Parameter state not found"));
		const UStateTreeState* TargetState = EditorData->GetStateByStructID(TargetId);
		bool bAncestor = false;
		for (const UStateTreeState* S = TargetState; S; S = S->Parent)
		{
			if (S == ParamState) { bAncestor = true; break; }
		}
		if (!bAncestor)
		{
			return MCPError(FString::Printf(TEXT("State '%s' is not the target's state or one of its ancestors, so the target could not read a parameter placed there."), *StateDotPath(ParamState)));
		}
		if (ParamState->Parameters.bFixedLayout || ParamState->Type == EStateTreeStateType::Linked || ParamState->Type == EStateTreeStateType::LinkedAsset)
		{
			return MCPError(TEXT("A linked or linked-asset state takes its parameters from what it links to, so a parameter cannot be added to it. Pick a state above it, or omit stateId/statePath for a tree parameter."));
		}
	}

	TSharedPtr<FJsonObject> Replaced = StateTreeNBExistingBinding(EditorData, TargetId, TargetPathStr);

	UE::PropertyBinding::FPropertyCreationDescriptor Desc;
	const FName BaseName = ParameterName.IsEmpty() ? LeafProp->GetFName() : FName(*ParameterName);
	Desc.PropertyDesc = FPropertyBagPropertyDesc(BaseName, LeafProp);
	Desc.SourceProperty = LeafProp;
	Desc.SourceContainerAddress = Leaf.GetContainerAddress();
	TArray<UE::PropertyBinding::FPropertyCreationDescriptor> Descs;
	Descs.Add(Desc);

	FGuid ParamStructId;
	if (ParamState)
	{
		ParamState->Modify();
		UE::PropertyBinding::CreateUniquelyNamedPropertiesInPropertyBag(Descs, ParamState->Parameters.Parameters);
		ParamStructId = ParamState->Parameters.ID;
	}
	else
	{
		EditorData->Modify();
		EditorData->CreateRootProperties(Descs);
		ParamStructId = EditorData->GetRootParametersGuid();
	}
	const FName CreatedName = Descs[0].PropertyDesc.Name;

	FPropertyBindingPath SourcePath;
	SourcePath.SetStructID(ParamStructId);
	SourcePath.FromString(CreatedName.ToString());
	EditorData->Modify();
	EditorData->AddPropertyBinding(SourcePath, TargetPath);
	StateTreeNBTouch(ST);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("parameterName"), CreatedName.ToString());
	Result->SetStringField(TEXT("parameterStructId"), StateTreeNBGuid(ParamStructId));
	Result->SetStringField(TEXT("parameterType"), StateTreeNBPropertyType(LeafProp));
	Result->SetStringField(TEXT("scope"), ParamState ? TEXT("state") : TEXT("tree"));
	if (ParamState) Result->SetStringField(TEXT("stateId"), StateTreeNBGuid(ParamState->ID));
	if (Replaced) Result->SetObjectField(TEXT("replacedBinding"), Replaced);

	{
		auto Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ST->GetPathName());
		Payload->SetStringField(TEXT("targetStructId"), TargetIdStr);
		Payload->SetStringField(TEXT("targetPath"), TargetPathStr);
		MCPSetRollback(Result, TEXT("remove_state_tree_binding"), Payload);
	}
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	FString Note = TEXT("The inverse removes the binding to the new parameter; the parameter itself stays. ");
	Note += ParamState
		? FString::Printf(TEXT("Remove it with statetree(remove_state_parameter, stateId=\"%s\", paramName=\"%s\"). "), *StateTreeNBGuid(ParamState->ID), *CreatedName.ToString())
		: FString(TEXT("No action removes a tree parameter, so it stays on the tree. "));
	if (Replaced)
	{
		Note += FString::Printf(TEXT("The target was bound to %s:%s before; put that back with statetree(add_binding)."),
			*Replaced->GetStringField(TEXT("sourceStructId")), *Replaced->GetStringField(TEXT("sourcePath")));
	}
	Result->SetStringField(TEXT("rollbackNote"), Note.TrimEnd());
	return MCPResult(Result);
#else
	(void)ParameterName;
	return MCPError(TEXT("Promote to parameter uses the general property-binding types StateTree adopted in UE 5.7, which this engine predates. Add the parameter with statetree(add_state_parameter) and bind it with statetree(add_binding)."));
#endif
}

#endif // UE_MCP_HAS_5_5_API
