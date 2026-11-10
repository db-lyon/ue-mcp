// Learning Agents observations: the observation types the engine offers, and
// the observation schema an interactor Blueprint builds in its
// SpecifyAgentObservation override, read and extended node by node.
//
// An interactor's observations are not data on an asset. They are whatever its
// SpecifyAgentObservation function returns, built by calling the static
// ULearningAgentsObservations::Specify* functions, so the schema is authored as
// that function's graph. The plugin is optional and experimental, so it is
// reached by class name and every Specify* signature is read from reflection,
// which also picks up the cross-attention QueryElement inputs UE 5.9 added to
// the union, set, array and map observations without naming a version.

#include "MachineLearningHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Interfaces/IPluginManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* const LAObservationsClassPath = TEXT("/Script/LearningAgents.LearningAgentsObservations");
	const TCHAR* const LAInteractorClassPath = TEXT("/Script/LearningAgents.LearningAgentsInteractor");
	const TCHAR* const LASpecifyFunction = TEXT("SpecifyAgentObservation");
	const TCHAR* const LASchemaInputPin = TEXT("InObservationSchema");
	const TCHAR* const LAResultPin = TEXT("OutObservationSchemaElement");
	const TCHAR* const LAPluginOffError =
		TEXT("Learning Agents is not available: enable the Learning Agents plugin (LearningAgents) for this project and restart the editor.");

	UClass* LAFindClass(const TCHAR* Path)
	{
		return FindObject<UClass>(nullptr, Path);
	}

	UClass* LAObservationsClass()
	{
		if (UClass* Found = LAFindClass(LAObservationsClassPath)) return Found;
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LearningAgents"));
		if (!Plugin.IsValid() || !Plugin->IsEnabled()) return nullptr;
		FModuleManager::Get().LoadModule(TEXT("LearningAgents"));
		return LAFindClass(LAObservationsClassPath);
	}

	FString LAObservationTypeName(const FString& FunctionName)
	{
		FString Type = FunctionName;
		Type.RemoveFromStart(TEXT("Specify"));
		return Type;
	}

	/** Float, FloatObservation and SpecifyFloatObservation all name SpecifyFloatObservation. */
	UFunction* LAResolveSpecify(UClass* ObservationsClass, const FString& Requested)
	{
		const FString Candidates[] = {
			Requested,
			TEXT("Specify") + Requested,
			TEXT("Specify") + Requested + TEXT("Observation"),
		};
		for (const FString& Candidate : Candidates)
		{
			if (!Candidate.StartsWith(TEXT("Specify"))) continue;
			if (UFunction* Func = ObservationsClass->FindFunctionByName(FName(*Candidate), EIncludeSuperFlag::ExcludeSuper)) return Func;
		}
		return nullptr;
	}

	bool LAIsInputParam(const FProperty* Prop)
	{
		return Prop->HasAnyPropertyFlags(CPF_Parm) && !Prop->HasAnyPropertyFlags(CPF_ReturnParm)
			&& (!Prop->HasAnyPropertyFlags(CPF_OutParm) || Prop->HasAnyPropertyFlags(CPF_ReferenceParm | CPF_ConstParm));
	}

	TSharedPtr<FJsonObject> LAFunctionJson(UFunction* Func, bool bIncludeParameters)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		const FString Name = Func->GetName();
		Row->SetStringField(TEXT("function"), Name);
		Row->SetStringField(TEXT("observationType"), LAObservationTypeName(Name));
		Row->SetBoolField(TEXT("supportsCrossAttention"), Func->FindPropertyByName(TEXT("QueryElement")) != nullptr);
#if WITH_EDITOR
		const FString Tooltip = Func->GetToolTipText().ToString();
		if (!Tooltip.IsEmpty()) Row->SetStringField(TEXT("description"), Tooltip.Left(600));
#endif
		if (bIncludeParameters)
		{
			TArray<TSharedPtr<FJsonValue>> Parameters;
			for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				const FProperty* Prop = *It;
				if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					Row->SetStringField(TEXT("returns"), Prop->GetCPPType());
					continue;
				}
				TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
				Param->SetStringField(TEXT("name"), Prop->GetName());
				Param->SetStringField(TEXT("type"), Prop->GetCPPType());
#if WITH_EDITOR
				const FString Default = Func->GetMetaData(*(TEXT("CPP_Default_") + Prop->GetName()));
				if (!Default.IsEmpty()) Param->SetStringField(TEXT("default"), Default);
#endif
				if (Prop->GetName() == TEXT("Schema")) Param->SetBoolField(TEXT("wiredAutomatically"), true);
				Parameters.Add(MakeShared<FJsonValueObject>(Param));
			}
			Row->SetArrayField(TEXT("parameters"), Parameters);
		}
		return Row;
	}

	UEdGraph* LAFindSpecifyGraph(UBlueprint* Blueprint)
	{
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == FName(LASpecifyFunction)) return Graph;
		}
		return nullptr;
	}

	template <typename TNode>
	TNode* LAFindNodeOfClass(UEdGraph* Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (TNode* Typed = Cast<TNode>(Node)) return Typed;
		}
		return nullptr;
	}

	UEdGraphNode* LAFindNodeById(UEdGraph* Graph, const FString& NodeId)
	{
		FGuid Guid;
		if (!Graph || !FGuid::Parse(NodeId, Guid)) return nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == Guid) return Node;
		}
		return nullptr;
	}

	bool LAIsObservationNode(const UEdGraphNode* Node, const UClass* ObservationsClass, UFunction*& OutFunction)
	{
		const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
		OutFunction = Call ? Call->GetTargetFunction() : nullptr;
		return OutFunction && OutFunction->GetOwnerClass() == ObservationsClass && OutFunction->GetName().StartsWith(TEXT("Specify"));
	}

	TArray<TSharedPtr<FJsonValue>> LALinksJson(const UEdGraphPin* Pin)
	{
		TArray<TSharedPtr<FJsonValue>> Links;
		for (const UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (!Linked || !Linked->GetOwningNode()) continue;
			TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
			Link->SetStringField(TEXT("nodeId"), Linked->GetOwningNode()->NodeGuid.ToString());
			Link->SetStringField(TEXT("pin"), Linked->PinName.ToString());
			Links.Add(MakeShared<FJsonValueObject>(Link));
		}
		return Links;
	}

	TSharedPtr<FJsonObject> LANodeJson(UEdGraphNode* Node, UFunction* Function)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
		Row->SetStringField(TEXT("function"), Function->GetName());
		Row->SetStringField(TEXT("observationType"), LAObservationTypeName(Function->GetName()));
		TArray<TSharedPtr<FJsonValue>> Inputs;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
			if (Pin->Direction == EGPD_Output)
			{
				Row->SetArrayField(TEXT("outputLinkedTo"), LALinksJson(Pin));
				continue;
			}
			TSharedPtr<FJsonObject> PinRow = MakeShared<FJsonObject>();
			PinRow->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinRow->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
			if (Pin->LinkedTo.Num() > 0)
			{
				PinRow->SetArrayField(TEXT("linkedTo"), LALinksJson(Pin));
			}
			else
			{
				PinRow->SetStringField(TEXT("default"), Pin->GetDefaultAsString());
			}
			Inputs.Add(MakeShared<FJsonValueObject>(PinRow));
		}
		Row->SetArrayField(TEXT("inputs"), Inputs);
		return Row;
	}

	FString LAJsonToPinText(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid()) return FString();
		switch (Value->Type)
		{
		case EJson::Boolean: return Value->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Number:
		{
			const double Number = Value->AsNumber();
			return FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)) && FMath::Abs(Number) < 2147483647.0
				? FString::Printf(TEXT("%lld"), static_cast<int64>(FMath::RoundToDouble(Number)))
				: FString::SanitizeFloat(Number);
		}
		default: return Value->AsString();
		}
	}

	/** The interactor Blueprint at the path, or the error to answer with. */
	TSharedPtr<FJsonValue> LALoadInteractor(const FString& BlueprintPath, UBlueprint*& OutBlueprint)
	{
		OutBlueprint = LoadAssetByPath<UBlueprint>(BlueprintPath);
		if (!OutBlueprint) return MCPAssetLoadError(BlueprintPath, TEXT("Blueprint"));
		UClass* Interactor = LAFindClass(LAInteractorClassPath);
		if (!Interactor || !OutBlueprint->ParentClass || !OutBlueprint->ParentClass->IsChildOf(Interactor))
		{
			return MCPError(FString::Printf(TEXT("'%s' is not a Learning Agents interactor: its parent class must derive from LearningAgentsInteractor."), *BlueprintPath));
		}
		return nullptr;
	}
}

TSharedPtr<FJsonValue> FMachineLearningHandlers::LearningAgentsListObservationTypes(const TSharedPtr<FJsonObject>& Params)
{
	const FString NameFilter = OptionalString(Params, TEXT("nameFilter"), TEXT(""));
	const bool bIncludeParameters = OptionalBool(Params, TEXT("includeParameters"), true);

	UClass* ObservationsClass = LAObservationsClass();
	if (!ObservationsClass) return MCPErrorWithCode(TEXT("plugin_not_enabled"), LAPluginOffError);

	TArray<UFunction*> Functions;
	for (TFieldIterator<UFunction> It(ObservationsClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		const FString Name = It->GetName();
		if (!Name.StartsWith(TEXT("Specify"))) continue;
		if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter)) continue;
		Functions.Add(*It);
	}
	Functions.Sort([](const UFunction& A, const UFunction& B) { return A.GetName() < B.GetName(); });

	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<TSharedPtr<FJsonValue>> CrossAttention;
	for (UFunction* Func : Functions)
	{
		Rows.Add(MakeShared<FJsonValueObject>(LAFunctionJson(Func, bIncludeParameters)));
		if (Func->FindPropertyByName(TEXT("QueryElement"))) CrossAttention.Add(MakeShared<FJsonValueString>(Func->GetName()));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
	Result->SetArrayField(TEXT("observationTypes"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	Result->SetBoolField(TEXT("crossAttention"), CrossAttention.Num() > 0);
	Result->SetArrayField(TEXT("crossAttentionTypes"), CrossAttention);
	Result->SetStringField(TEXT("authoring"), TEXT("An interactor Blueprint returns its schema from SpecifyAgentObservation; add these with gameplay(learning_agents_add_observation) and read them back with learning_agents_read_observations."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMachineLearningHandlers::LearningAgentsReadObservations(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	UClass* ObservationsClass = LAObservationsClass();
	if (!ObservationsClass) return MCPErrorWithCode(TEXT("plugin_not_enabled"), LAPluginOffError);
	UBlueprint* Blueprint = nullptr;
	if (auto Err = LALoadInteractor(BlueprintPath, Blueprint)) return Err;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	UEdGraph* Graph = LAFindSpecifyGraph(Blueprint);
	Result->SetBoolField(TEXT("hasOverride"), Graph != nullptr);
	TArray<TSharedPtr<FJsonValue>> Rows;
	if (Graph)
	{
		Result->SetStringField(TEXT("graphName"), Graph->GetName());
		int32 OtherNodes = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UFunction* Function = nullptr;
			if (LAIsObservationNode(Node, ObservationsClass, Function))
			{
				Rows.Add(MakeShared<FJsonValueObject>(LANodeJson(Node, Function)));
			}
			else if (Node && !Node->IsA<UK2Node_FunctionEntry>() && !Node->IsA<UK2Node_FunctionResult>())
			{
				++OtherNodes;
			}
		}
		if (UK2Node_FunctionResult* ResultNode = LAFindNodeOfClass<UK2Node_FunctionResult>(Graph))
		{
			if (UEdGraphPin* Pin = ResultNode->FindPin(LAResultPin))
			{
				if (Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0] && Pin->LinkedTo[0]->GetOwningNode())
				{
					Result->SetStringField(TEXT("rootNodeId"), Pin->LinkedTo[0]->GetOwningNode()->NodeGuid.ToString());
				}
			}
		}
		Result->SetNumberField(TEXT("otherNodes"), OtherNodes);
	}
	Result->SetArrayField(TEXT("observations"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	if (!Result->HasField(TEXT("rootNodeId")))
	{
		Result->SetStringField(TEXT("note"), TEXT("No observation feeds OutObservationSchemaElement, so the interactor returns an empty schema."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FMachineLearningHandlers::LearningAgentsAddObservation(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;
	FString ObservationType;
	if (auto Err = RequireString(Params, TEXT("observationType"), ObservationType)) return Err;
	const TSharedPtr<FJsonObject>* Settings = nullptr;
	const bool bHasSettings = TryGetObjectParam(Params, TEXT("settings"), Settings) && Settings && Settings->IsValid();
	const FString ConnectTo = OptionalString(Params, TEXT("connectTo"), TEXT(""));
	const FString ConnectPin = OptionalString(Params, TEXT("connectPin"), TEXT(""));
	const int32 PosX = OptionalInt(Params, TEXT("posX"), 0);
	const bool bHasPosY = HasParam(Params, TEXT("posY"));
	const int32 PosY = OptionalInt(Params, TEXT("posY"), 0);

	UClass* ObservationsClass = LAObservationsClass();
	if (!ObservationsClass) return MCPErrorWithCode(TEXT("plugin_not_enabled"), LAPluginOffError);
	UFunction* Function = LAResolveSpecify(ObservationsClass, ObservationType);
	if (!Function)
	{
		return MCPError(FString::Printf(TEXT("No observation type '%s'. gameplay(learning_agents_list_observation_types) lists them, e.g. Float, Location, Struct, Set."), *ObservationType));
	}

	// Every setting names an input of the Specify function.
	TArray<TPair<FString, FString>> PinValues;
	if (bHasSettings)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Settings)->Values)
		{
			const FProperty* Prop = Function->FindPropertyByName(FName(*Entry.Key));
			if (!Prop || !LAIsInputParam(Prop) || Entry.Key == TEXT("Schema"))
			{
				TArray<FString> Names;
				for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
				{
					if (LAIsInputParam(*It) && It->GetName() != TEXT("Schema")) Names.Add(It->GetName());
				}
				return MCPError(FString::Printf(TEXT("'%s' is not an input of %s. Inputs: %s."), *Entry.Key, *Function->GetName(), *FString::Join(Names, TEXT(", "))));
			}
			PinValues.Emplace(Entry.Key, LAJsonToPinText(Entry.Value));
		}
	}
	const bool bToResult = ConnectTo.Equals(TEXT("result"), ESearchCase::IgnoreCase);
	if (!ConnectTo.IsEmpty() && !bToResult && ConnectPin.IsEmpty())
	{
		return MCPError(TEXT("connectTo names a node, so connectPin must name its input to feed, such as Element or QueryElement."));
	}

	UBlueprint* Blueprint = nullptr;
	if (auto Err = LALoadInteractor(BlueprintPath, Blueprint)) return Err;

	UEdGraph* Graph = LAFindSpecifyGraph(Blueprint);
	UEdGraphPin* TargetPin = nullptr;
	if (!ConnectTo.IsEmpty() && !bToResult)
	{
		UEdGraphNode* Target = LAFindNodeById(Graph, ConnectTo);
		if (!Target) return MCPError(FString::Printf(TEXT("No node '%s' in %s's SpecifyAgentObservation graph."), *ConnectTo, *BlueprintPath));
		TargetPin = Target->FindPin(FName(*ConnectPin), EGPD_Input);
		if (!TargetPin) return MCPError(FString::Printf(TEXT("Node '%s' has no input pin '%s'."), *ConnectTo, *ConnectPin));
		if (TargetPin->LinkedTo.Num() > 0) return MCPError(FString::Printf(TEXT("Input '%s' of node '%s' is already connected."), *ConnectPin, *ConnectTo));
	}

	bool bCreatedOverride = false;
	if (!Graph)
	{
		UFunction* OverrideFunc = nullptr;
		UClass* OverrideClass = FBlueprintEditorUtils::GetOverrideFunctionClass(Blueprint, FName(LASpecifyFunction), &OverrideFunc);
		if (!OverrideClass || !OverrideFunc) return MCPError(TEXT("The interactor's parent class has no overridable SpecifyAgentObservation."));
		Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(LASpecifyFunction), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		if (!Graph) return MCPError(TEXT("Could not create the SpecifyAgentObservation override graph."));
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, /*bIsUserCreated=*/false, OverrideClass);
		bCreatedOverride = true;
	}
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UK2Node_FunctionEntry* Entry = LAFindNodeOfClass<UK2Node_FunctionEntry>(Graph);
	UK2Node_FunctionResult* ResultNode = LAFindNodeOfClass<UK2Node_FunctionResult>(Graph);
	if (bToResult)
	{
		UEdGraphPin* Pin = ResultNode ? ResultNode->FindPin(LAResultPin, EGPD_Input) : nullptr;
		if (!Pin) return MCPError(TEXT("The SpecifyAgentObservation graph has no return node with OutObservationSchemaElement."));
		if (Pin->LinkedTo.Num() > 0) return MCPError(TEXT("OutObservationSchemaElement is already fed by another observation; feed that one instead, or delete it first."));
		TargetPin = Pin;
	}

	Graph->Modify();
	FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
	UK2Node_CallFunction* Node = Creator.CreateNode(false);
	Node->SetFromFunction(Function);
	Node->NodePosX = PosX;
	Node->NodePosY = bHasPosY ? PosY : 200 * Graph->Nodes.Num();
	Creator.Finalize();

	auto Discard = [Graph, Node]()
	{
		Node->BreakAllNodeLinks();
		Graph->RemoveNode(Node);
	};

	for (const TPair<FString, FString>& Value : PinValues)
	{
		UEdGraphPin* Pin = Node->FindPin(FName(*Value.Key), EGPD_Input);
		if (!Pin)
		{
			Discard();
			return MCPError(FString::Printf(TEXT("The node has no input pin '%s'."), *Value.Key));
		}
		const FString Invalid = Schema->IsPinDefaultValid(Pin, Value.Value, nullptr, FText::GetEmpty());
		if (!Invalid.IsEmpty())
		{
			Discard();
			return MCPError(FString::Printf(TEXT("'%s' is not a valid value for %s: %s"), *Value.Value, *Value.Key, *Invalid));
		}
		Schema->TrySetDefaultValue(*Pin, Value.Value);
	}

	if (UEdGraphPin* SchemaPin = Node->FindPin(TEXT("Schema"), EGPD_Input))
	{
		UEdGraphPin* EntrySchema = Entry ? Entry->FindPin(LASchemaInputPin, EGPD_Output) : nullptr;
		if (!EntrySchema || !Schema->TryCreateConnection(EntrySchema, SchemaPin))
		{
			Discard();
			return MCPError(TEXT("Could not wire the node's Schema input to InObservationSchema on the function entry."));
		}
	}

	UEdGraphPin* Output = Node->GetReturnValuePin();
	if (TargetPin)
	{
		if (!Output || !Schema->TryCreateConnection(Output, TargetPin))
		{
			Discard();
			return MCPError(FString::Printf(TEXT("Could not connect the new observation to %s."), bToResult ? LAResultPin : *ConnectPin));
		}
		// The return node runs only on the exec path from the entry.
		if (bToResult && Entry && ResultNode)
		{
			UEdGraphPin* Then = Entry->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* Exec = ResultNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			if (Then && Exec && Then->LinkedTo.Num() == 0 && Exec->LinkedTo.Num() == 0) Schema->TryCreateConnection(Then, Exec);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FString SaveError;
	const bool bSaved = SaveAssetPackageChecked(Blueprint, SaveError);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPNoteSaveOutcome(Result, BlueprintPath, bSaved, SaveError);
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("graphName"), Graph->GetName());
	Result->SetBoolField(TEXT("createdOverride"), bCreatedOverride);
	Result->SetObjectField(TEXT("observation"), LANodeJson(Node, Function));
	Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
	if (TargetPin) Result->SetStringField(TEXT("connectedTo"), bToResult ? FString(TEXT("result")) : ConnectTo + TEXT(".") + ConnectPin);
	Result->SetBoolField(TEXT("compiled"), Blueprint->Status != BS_Error);
	if (Blueprint->Status == BS_Error)
	{
		Result->SetStringField(TEXT("warning"), TEXT("The Blueprint has compile errors after the edit; blueprint(compile) lists them."));
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), BlueprintPath);
	Payload->SetStringField(TEXT("graphName"), Graph->GetName());
	Payload->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
	MCPSetRollback(Result, TEXT("delete_node"), Payload);
	return MCPResult(Result);
}
