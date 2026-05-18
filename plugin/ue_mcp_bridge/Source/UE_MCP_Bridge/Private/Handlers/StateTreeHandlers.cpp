#include "StateTreeHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeTypes.h"
#include "StateTreeNodeBase.h"
#include "PropertyBindingPath.h"
#include "PropertyBindingTypes.h"

#include "Editor.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UObjectIterator.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEditorTypes.h"

void FStateTreeHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
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
	Registry.RegisterHandler(TEXT("compile_state_tree"), &CompileStateTree);
	Registry.RegisterHandler(TEXT("validate_state_tree"), &ValidateStateTree);
}

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
	Obj->SetStringField(TEXT("description"), State->Description);
	if (State->Tag.IsValid())
	{
		Obj->SetStringField(TEXT("tag"), State->Tag.ToString());
	}
	if (State->bHasCustomTickRate)
	{
		Obj->SetNumberField(TEXT("customTickRate"), State->CustomTickRate);
	}
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
		}

		TransObj->SetBoolField(TEXT("bEnabled"), Trans.bTransitionEnabled);

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

static UScriptStruct* ResolveStructType(const FString& StructTypeName)
{
	UScriptStruct* Result = FindObject<UScriptStruct>(nullptr, *StructTypeName);
	if (Result) return Result;

	Result = FindFirstObject<UScriptStruct>(*StructTypeName, EFindFirstObjectOptions::NativeFirst);
	return Result;
}

static bool AddEditorNodeToArray(TArray<FStateTreeEditorNode>& Arr, const FString& StructTypeName, const TSharedPtr<FJsonObject>& InstanceProperties, FStateTreeEditorNode*& OutNode, FString& OutError)
{
	UScriptStruct* NodeStruct = ResolveStructType(StructTypeName);
	if (!NodeStruct)
	{
		OutError = FString::Printf(TEXT("Struct not found: %s"), *StructTypeName);
		return false;
	}

	FStateTreeEditorNode& EditorNode = Arr.AddDefaulted_GetRef();
	EditorNode.ID = FGuid::NewGuid();
	EditorNode.Node.InitializeAs(NodeStruct);

	const FStateTreeNodeBase& Node = EditorNode.Node.Get<FStateTreeNodeBase>();

	if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Node.GetInstanceDataType()))
	{
		EditorNode.Instance.InitializeAs(InstanceType);
	}

	if (const UScriptStruct* ExecType = Cast<const UScriptStruct>(Node.GetExecutionRuntimeDataType()))
	{
		EditorNode.ExecutionRuntimeData.InitializeAs(ExecType);
	}

	if (InstanceProperties.IsValid() && EditorNode.Instance.IsValid())
	{
		SetInstancePropertiesFromJson(EditorNode.Instance, InstanceProperties);
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
	if (!EditorData) return MCPError(TEXT("EditorData not found on StateTree"));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);

	if (EditorData->Schema)
	{
		Result->SetStringField(TEXT("schemaClass"), EditorData->Schema->GetClass()->GetPathName());
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
	const FInstancedPropertyBag& RootParams = EditorData->GetRootParametersPropertyBag();
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

	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> StatesArr;

	TFunction<void(const UStateTreeState*)> CollectStates = [&](const UStateTreeState* State)
	{
		if (!State) return;
		auto SObj = MakeShared<FJsonObject>();
		SObj->SetStringField(TEXT("name"), State->Name.ToString());
		SObj->SetStringField(TEXT("id"), GuidToString(State->ID));
		SObj->SetStringField(TEXT("path"), GetStatePath(State));
		SObj->SetStringField(TEXT("type"), StateTypeToString(State->Type));
		StatesArr.Add(MakeShared<FJsonValueObject>(SObj));

		for (const TObjectPtr<UStateTreeState>& Child : State->Children)
		{
			CollectStates(Child);
		}
	};

	for (const TObjectPtr<UStateTreeState>& SubTree : EditorData->SubTrees)
	{
		CollectStates(SubTree);
	}

	Result->SetArrayField(TEXT("states"), StatesArr);
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
	Result->SetBoolField(TEXT("removed"), true);
	return MCPResult(Result);
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
		State->Description = Value;
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

	State->Modify();
	State->Tasks.Empty();
	State->EnterConditions.Empty();
	State->Transitions.Empty();
	State->SingleTask.Reset();

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("cleared"), true);
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
	if (!AddEditorNodeToArray(State->Tasks, StructType, InstanceProps, NewNode, Error))
	{
		return MCPError(Error);
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeId"), GuidToString(NewNode->ID));
	Result->SetNumberField(TEXT("taskIndex"), State->Tasks.Num() - 1);
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
	if (!AddEditorNodeToArray(State->EnterConditions, StructType, InstanceProps, NewNode, Error))
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
	return MCPResult(Result);
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

	State->Modify();
	State->EnterConditions.RemoveAt(ConditionIndex);
	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("removed"), true);
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

	State->Modify();
	State->Tasks.RemoveAt(TaskIndex);
	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("removed"), true);
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
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
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
	if (!AddEditorNodeToArray(EditorData->Evaluators, StructType, InstanceProps, NewNode, Error))
	{
		return MCPError(Error);
	}

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeId"), GuidToString(NewNode->ID));
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
			EditorData->Modify();
			EditorData->Evaluators.RemoveAt(i);
			UStateTreeEditingSubsystem::ValidateStateTree(ST);

			auto Result = MCPSuccess();
			Result->SetBoolField(TEXT("removed"), true);
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
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
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
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("structType"), NodeStruct->GetName());
	Result->SetStringField(TEXT("propertyName"), PropName);
	Result->SetStringField(TEXT("value"), Value);
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
	if (!AddEditorNodeToArray(EditorData->GlobalTasks, StructType, InstanceProps, NewNode, Error))
	{
		return MCPError(Error);
	}

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("nodeId"), GuidToString(NewNode->ID));
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
			EditorData->Modify();
			EditorData->GlobalTasks.RemoveAt(i);
			UStateTreeEditingSubsystem::ValidateStateTree(ST);

			auto Result = MCPSuccess();
			Result->SetBoolField(TEXT("removed"), true);
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
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
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
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("structType"), NodeStruct->GetName());
	Result->SetStringField(TEXT("propertyName"), PropName);
	Result->SetStringField(TEXT("value"), Value);
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
		const FString PriorityStr = Params->GetStringField(TEXT("priority"));
		if (PriorityStr == TEXT("Low")) Trans->Priority = EStateTreeTransitionPriority::Low;
		else if (PriorityStr == TEXT("Medium")) Trans->Priority = EStateTreeTransitionPriority::Medium;
		else if (PriorityStr == TEXT("High")) Trans->Priority = EStateTreeTransitionPriority::High;
		else if (PriorityStr == TEXT("Critical")) Trans->Priority = EStateTreeTransitionPriority::Critical;
		else Trans->Priority = EStateTreeTransitionPriority::Normal;
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
	if (!AddEditorNodeToArray(State->Transitions[TransIndex].Conditions, StructType, InstanceProps, NewNode, Error))
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

	State->Modify();
	State->Transitions.RemoveAt(TransIndex);
	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("removed"), true);
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

	FPropertyBindingPath SourcePath;
	SourcePath.SetStructID(ParseGuid(SourceStructIdStr));
	if (!SourcePath.FromString(SourcePathStr))
	{
		return MCPError(FString::Printf(TEXT("Failed to parse source path: %s"), *SourcePathStr));
	}

	FPropertyBindingPath TargetPath;
	TargetPath.SetStructID(ParseGuid(TargetStructIdStr));
	if (!TargetPath.FromString(TargetPathStr))
	{
		return MCPError(FString::Printf(TEXT("Failed to parse target path: %s"), *TargetPathStr));
	}

	EditorData->Modify();
	EditorData->AddPropertyBinding(SourcePath, TargetPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
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

	FPropertyBindingPath TargetPath;
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

	EditorData->Modify();
	Bindings->RemoveBindings(TargetPath);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("removed"), true);
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
	State->Parameters.Parameters.AddProperties(NewDescs, /*bOverwrite=*/ false);

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("paramName"), ParamName);
	Result->SetStringField(TEXT("paramType"), ParamType);
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

	State->Modify();
	State->Parameters.Parameters.RemovePropertyByName(FName(*ParamName));

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("removed"), true);
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
	Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None);

	if (State->Parameters.bFixedLayout)
	{
		const FPropertyBagPropertyDesc* Desc = BagStruct->FindPropertyDescByName(FName(*ParamName));
		if (Desc)
		{
			State->SetParametersPropertyOverridden(Desc->ID, true);
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
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

	TArray<UE::PropertyBinding::FPropertyCreationDescriptor> Descs;
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

		UE::PropertyBinding::FPropertyCreationDescriptor& Desc = Descs.AddDefaulted_GetRef();
		Desc.PropertyDesc = FPropertyBagPropertyDesc(FName(*PropName), BagType);
	}

	EditorData->Modify();
	EditorData->CreateRootProperties(Descs);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetNumberField(TEXT("parameterCount"), Descs.Num());
	return MCPResult(Result);
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FStateTreeHandlers::CompileStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	auto Result = MCPSuccess();
	CompileAndSave(ST, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FStateTreeHandlers::ValidateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	UStateTree* ST = LoadStateTree(AssetPath);
	if (!ST) return MCPError(FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));

	UStateTreeEditingSubsystem::ValidateStateTree(ST);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("validated"), true);
	return MCPResult(Result);
}
