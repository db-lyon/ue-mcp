#include "BlueprintHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "SubobjectDataSubsystem.h"
#include "SubobjectDataHandle.h"
#include "SubobjectData.h"
#include "SubobjectDataBlueprintFunctionLibrary.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Internationalization/Text.h"
#include "UObject/TopLevelAssetPath.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallDelegate.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/MessageDialog.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetArrayLibrary.h"

// SCS component access
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "EditorAssetLibrary.h"
#include "Containers/Queue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Logging/TokenizedMessage.h"
#include "Kismet2/CompilerResultsLog.h"
#include "EdGraphUtilities.h"

void FBlueprintHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("create_blueprint"), &CreateBlueprint);
	Registry.RegisterHandler(TEXT("read_blueprint"), &ReadBlueprint);
	Registry.RegisterHandler(TEXT("add_variable"), &AddVariable);
	Registry.RegisterHandler(TEXT("add_component"), &AddComponent);
	Registry.RegisterHandler(TEXT("add_blueprint_interface"), &AddBlueprintInterface);
	Registry.RegisterHandler(TEXT("compile_blueprint"), &CompileBlueprint);
	Registry.RegisterHandler(TEXT("search_node_types"), &SearchNodeTypes);
	Registry.RegisterHandler(TEXT("list_node_types"), &ListNodeTypes);
	Registry.RegisterHandler(TEXT("list_blueprint_variables"), &ListBlueprintVariables);
	Registry.RegisterHandler(TEXT("set_variable_properties"), &SetVariableProperties);
	Registry.RegisterHandler(TEXT("create_function"), &CreateFunction);
	Registry.RegisterHandler(TEXT("list_blueprint_functions"), &ListBlueprintFunctions);
	Registry.RegisterHandler(TEXT("add_node"), &AddNode);
	Registry.RegisterHandler(TEXT("read_blueprint_graph"), &ReadBlueprintGraph);
	Registry.RegisterHandler(TEXT("add_event_dispatcher"), &AddEventDispatcher);
	Registry.RegisterHandler(TEXT("rename_function"), &RenameFunction);
	Registry.RegisterHandler(TEXT("delete_function"), &DeleteFunction);
	Registry.RegisterHandler(TEXT("create_blueprint_interface"), &CreateBlueprintInterface);
	Registry.RegisterHandler(TEXT("list_node_types_detailed"), &ListNodeTypesDetailed);
	Registry.RegisterHandler(TEXT("search_callable_functions"), &SearchCallableFunctions);
	Registry.RegisterHandler(TEXT("connect_pins"), &ConnectPins);
	Registry.RegisterHandler(TEXT("delete_node"), &DeleteNode);
	Registry.RegisterHandler(TEXT("set_node_property"), &SetNodeProperty);
	Registry.RegisterHandler(TEXT("list_blueprint_graphs"), &ListGraphs);
	Registry.RegisterHandler(TEXT("set_blueprint_component_property"), &SetComponentProperty);
	Registry.RegisterHandler(TEXT("set_class_default"), &SetClassDefault);
	Registry.RegisterHandler(TEXT("remove_component"), &RemoveComponent);
	Registry.RegisterHandler(TEXT("delete_variable"), &DeleteVariable);
	Registry.RegisterHandler(TEXT("add_function_parameter"), &AddFunctionParameter);
	Registry.RegisterHandler(TEXT("set_variable_default"), &SetVariableDefault);

	// v0.7.8 stubs
	Registry.RegisterHandler(TEXT("read_blueprint_graph_summary"), &ReadBlueprintGraphSummary);
	Registry.RegisterHandler(TEXT("get_blueprint_execution_flow"), &GetBlueprintExecutionFlow);
	Registry.RegisterHandler(TEXT("get_blueprint_dependencies"), &GetBlueprintDependencies);

	// v0.7.11 — BP authoring depth
	Registry.RegisterHandler(TEXT("duplicate_blueprint"), &DuplicateBlueprint);
	Registry.RegisterHandler(TEXT("add_local_variable"), &AddLocalVariable);
	Registry.RegisterHandler(TEXT("list_local_variables"), &ListLocalVariables);
	Registry.RegisterHandler(TEXT("validate_blueprint"), &ValidateBlueprint);

	// v0.7.11 — issue fixes
	Registry.RegisterHandler(TEXT("read_component_properties"), &ReadComponentProperties);
	Registry.RegisterHandler(TEXT("read_node_property"), &ReadNodeProperty);
	Registry.RegisterHandler(TEXT("reparent_component"), &ReparentComponent);
	Registry.RegisterHandler(TEXT("reparent_blueprint"), &ReparentBlueprint);
	Registry.RegisterHandler(TEXT("set_actor_tick_settings"), &SetActorTickSettings);

	// v0.7.12 — issue #128 — single-property read (inherited-aware)
	Registry.RegisterHandler(TEXT("get_blueprint_component_property"), &GetComponentProperty);

	// v0.7.17 issue #130: bulk graph node import via T3D copy/paste
	Registry.RegisterHandler(TEXT("export_nodes_t3d"), &ExportNodesT3D);
	Registry.RegisterHandler(TEXT("import_nodes_t3d"), &ImportNodesT3D);

	// issues #182/#183: C++ class CDO property access
	Registry.RegisterHandler(TEXT("set_cdo_property"), &SetCdoProperty);
	Registry.RegisterHandler(TEXT("get_cdo_properties"), &GetCdoProperties);

	// issue #195: run construction script and inspect resulting components
	Registry.RegisterHandler(TEXT("run_construction_script"), &RunConstructionScript);
}

// ---------------------------------------------------------------------------
// v0.7.8 STUBS — agent-ergonomics actions (Milestone A)
// Bodies intentionally minimal; flesh out one per follow-up patch.
// ---------------------------------------------------------------------------

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprintGraphSummary(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	// Nodes: id + class + concise title only. No pin defaults, no positions, no comments.
	TArray<TSharedPtr<FJsonValue>> Nodes;
	TArray<TSharedPtr<FJsonValue>> ExecEdges;
	TArray<TSharedPtr<FJsonValue>> DataEdges;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
		N->SetStringField(TEXT("id"), Node->NodeGuid.ToString(EGuidFormats::Short));
		N->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		N->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Nodes.Add(MakeShared<FJsonValueObject>(N));

		// Walk output pins only (one edge per connection, no dup).
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			const bool bExec = (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode()) continue;
				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("from"), Node->NodeGuid.ToString(EGuidFormats::Short));
				E->SetStringField(TEXT("fromPin"), Pin->PinName.ToString());
				E->SetStringField(TEXT("to"), Linked->GetOwningNode()->NodeGuid.ToString(EGuidFormats::Short));
				E->SetStringField(TEXT("toPin"), Linked->PinName.ToString());
				(bExec ? ExecEdges : DataEdges).Add(MakeShared<FJsonValueObject>(E));
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetArrayField(TEXT("nodes"), Nodes);
	Result->SetArrayField(TEXT("execEdges"), ExecEdges);
	Result->SetArrayField(TEXT("dataEdges"), DataEdges);
	Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::GetBlueprintExecutionFlow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	FString EntryPoint = OptionalString(Params, TEXT("entryPoint"), TEXT(""));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

	// Locate entry node. If EntryPoint is given, match by title. Else pick first
	// K2Node_Event / K2Node_FunctionEntry / K2Node_CustomEvent encountered.
	UEdGraphNode* Entry = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		const bool bIsEntry =
			Node->IsA<UK2Node_Event>() ||
			Node->IsA<UK2Node_FunctionEntry>() ||
			Node->IsA<UK2Node_CustomEvent>();
		if (!bIsEntry) continue;
		if (EntryPoint.IsEmpty())
		{
			Entry = Node;
			break;
		}
		if (Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Contains(EntryPoint))
		{
			Entry = Node;
			break;
		}
	}

	if (!Entry)
	{
		return MCPError(EntryPoint.IsEmpty()
			? TEXT("No event or function entry node found")
			: FString::Printf(TEXT("Entry node not found: %s"), *EntryPoint));
	}

	// BFS through exec output pins. Track visited node guids to break cycles.
	TArray<TSharedPtr<FJsonValue>> Steps;
	TSet<FGuid> Visited;
	TQueue<UEdGraphNode*> Queue;
	Queue.Enqueue(Entry);

	while (!Queue.IsEmpty())
	{
		UEdGraphNode* Cur = nullptr;
		Queue.Dequeue(Cur);
		if (!Cur || Visited.Contains(Cur->NodeGuid)) continue;
		Visited.Add(Cur->NodeGuid);

		TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("id"), Cur->NodeGuid.ToString(EGuidFormats::Short));
		Step->SetStringField(TEXT("class"), Cur->GetClass()->GetName());
		Step->SetStringField(TEXT("title"), Cur->GetNodeTitle(ENodeTitleType::ListView).ToString());

		// Enumerate exec branches from this node, one per output exec pin.
		TArray<TSharedPtr<FJsonValue>> Branches;
		for (UEdGraphPin* Pin : Cur->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;

			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode()) continue;
				UEdGraphNode* Next = Linked->GetOwningNode();

				TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
				B->SetStringField(TEXT("pin"), Pin->PinName.ToString());
				B->SetStringField(TEXT("toId"), Next->NodeGuid.ToString(EGuidFormats::Short));
				Branches.Add(MakeShared<FJsonValueObject>(B));

				if (!Visited.Contains(Next->NodeGuid))
				{
					Queue.Enqueue(Next);
				}
			}
		}
		Step->SetArrayField(TEXT("branches"), Branches);
		Steps.Add(MakeShared<FJsonValueObject>(Step));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("entryPoint"), Entry->GetNodeTitle(ENodeTitleType::ListView).ToString());
	Result->SetStringField(TEXT("entryId"), Entry->NodeGuid.ToString(EGuidFormats::Short));
	Result->SetArrayField(TEXT("steps"), Steps);
	Result->SetNumberField(TEXT("stepCount"), Steps.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::GetBlueprintDependencies(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	const bool bReverse = OptionalBool(Params, TEXT("reverse"), false);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = AssetRegistryModule.Get();
	const FName PackageName = Blueprint->GetOutermost()->GetFName();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("reverse"), bReverse);

	if (bReverse)
	{
		TArray<FName> Referencers;
		Registry.GetReferencers(PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(Referencers.Num());
		for (const FName& Ref : Referencers)
		{
			Arr.Add(MakeShared<FJsonValueString>(Ref.ToString()));
		}
		Result->SetArrayField(TEXT("referencers"), Arr);
		Result->SetNumberField(TEXT("referencerCount"), Arr.Num());
		return MCPResult(Result);
	}

	// Forward: asset-level deps from registry + class-level walk.
	TArray<FName> AssetDeps;
	Registry.GetDependencies(PackageName, AssetDeps, UE::AssetRegistry::EDependencyCategory::Package);
	TArray<TSharedPtr<FJsonValue>> AssetArr;
	AssetArr.Reserve(AssetDeps.Num());
	for (const FName& Dep : AssetDeps)
	{
		AssetArr.Add(MakeShared<FJsonValueString>(Dep.ToString()));
	}

	// Classes referenced by variables + function signatures + parent class.
	TSet<FString> Classes;
	if (UClass* ParentClass = Blueprint->ParentClass)
	{
		Classes.Add(ParentClass->GetPathName());
	}
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (UObject* Sub = Var.VarType.PinSubCategoryObject.Get())
		{
			Classes.Add(Sub->GetPathName());
		}
	}

	// Functions called via K2Node_CallFunction across all graphs.
	TSet<FString> Functions;
	auto VisitGraph = [&Functions](UEdGraph* G)
	{
		if (!G) return;
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				if (UFunction* Fn = Call->GetTargetFunction())
				{
					Functions.Add(Fn->GetPathName());
				}
			}
		}
	};
	for (UEdGraph* G : Blueprint->UbergraphPages) VisitGraph(G);
	for (UEdGraph* G : Blueprint->FunctionGraphs) VisitGraph(G);

	TArray<TSharedPtr<FJsonValue>> ClassArr;
	for (const FString& C : Classes) ClassArr.Add(MakeShared<FJsonValueString>(C));
	TArray<TSharedPtr<FJsonValue>> FnArr;
	for (const FString& F : Functions) FnArr.Add(MakeShared<FJsonValueString>(F));

	Result->SetArrayField(TEXT("assets"), AssetArr);
	Result->SetArrayField(TEXT("classes"), ClassArr);
	Result->SetArrayField(TEXT("functions"), FnArr);
	Result->SetNumberField(TEXT("assetCount"), AssetArr.Num());
	Result->SetNumberField(TEXT("classCount"), ClassArr.Num());
	Result->SetNumberField(TEXT("functionCount"), FnArr.Num());
	return MCPResult(Result);
}

UBlueprint* FBlueprintHandlers::LoadBlueprint(const FString& AssetPath)
{
	// Try exact path first (handles both package path and full object path)
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (BP) return BP;

	// If the path looks like a package path (no '.' after last '/'), try object path format
	// e.g. "/Game/Foo/BP_Bar" -> "/Game/Foo/BP_Bar.BP_Bar"
	if (!AssetPath.Contains(TEXT(".")))
	{
		FString AssetName;
		AssetPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		FString ObjectPath = AssetPath + TEXT(".") + AssetName;
		BP = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	}
	return BP;
}

// ---------------------------------------------------------------------------
// list_blueprint_graphs -- List all graphs in a blueprint (EventGraph, AnimGraph, functions, etc.)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::ListGraphs(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("class"), Graph->GetClass()->GetName());
		GraphObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("graphs"), GraphsArray);

	return MCPResult(Result);
}

FEdGraphPinType FBlueprintHandlers::MakePinType(const FString& TypeStr)
{
	FEdGraphPinType PinType;
	PinType.PinCategory = NAME_None;
	PinType.PinSubCategory = NAME_None;

	FString LowerType = TypeStr.ToLower();

	// (#140) Object-reference types: "Actor", "Actor*", "APawn*", full class paths
	// like "/Script/Engine.Actor", and soft-ref variants "SoftActor" or "SoftClassPtr<Foo>".
	// Previously these fell through to the struct resolver and ultimately defaulted to
	// PC_Real (float), breaking any function parameter that takes an object-ref.
	auto TryResolveObjectPin = [&PinType](const FString& Raw) -> bool
	{
		FString Trimmed = Raw;
		Trimmed.TrimStartAndEndInline();
		// Strip trailing asterisks (AActor*, AActor**)
		while (Trimmed.EndsWith(TEXT("*"))) Trimmed = Trimmed.LeftChop(1);
		Trimmed.TrimStartAndEndInline();

		// SoftClassPtr<Foo> / TSubclassOf<Foo> / TSoftObjectPtr<Foo>
		bool bIsSoftClass = false;
		bool bIsClass = false;
		bool bIsSoftObject = false;
		auto UnwrapTemplate = [&](const TCHAR* Prefix) -> bool
		{
			if (Trimmed.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				int32 Open = Trimmed.Find(TEXT("<"));
				int32 Close = Trimmed.Find(TEXT(">"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				if (Open != INDEX_NONE && Close != INDEX_NONE && Close > Open)
				{
					Trimmed = Trimmed.Mid(Open + 1, Close - Open - 1).TrimStartAndEnd();
					return true;
				}
			}
			return false;
		};
		if (UnwrapTemplate(TEXT("TSubclassOf"))) bIsClass = true;
		else if (UnwrapTemplate(TEXT("TSoftClassPtr")) || UnwrapTemplate(TEXT("SoftClassPtr"))) bIsSoftClass = true;
		else if (UnwrapTemplate(TEXT("TSoftObjectPtr")) || UnwrapTemplate(TEXT("SoftObjectPtr"))) bIsSoftObject = true;

		UClass* Resolved = nullptr;
		if (Trimmed.Contains(TEXT("/")) || Trimmed.Contains(TEXT(".")))
		{
			Resolved = LoadObject<UClass>(nullptr, *Trimmed);
		}
		if (!Resolved)
		{
			Resolved = FindClassByShortName(Trimmed);
		}
		if (!Resolved) return false;

		if (bIsSoftClass)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
		}
		else if (bIsClass)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		}
		else if (bIsSoftObject)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
		}
		else
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		}
		PinType.PinSubCategoryObject = Resolved;
		return true;
	};

	// If the caller passed an asterisk or a class path, treat as object-ref first.
	if (TypeStr.Contains(TEXT("*")) || TypeStr.Contains(TEXT("/")))
	{
		if (TryResolveObjectPin(TypeStr)) return PinType;
	}

	// Map type strings to pin categories
	if (LowerType == TEXT("bool") || LowerType == TEXT("boolean"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (LowerType == TEXT("int") || LowerType == TEXT("integer") || LowerType == TEXT("int32"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (LowerType == TEXT("int64"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (LowerType == TEXT("float") || LowerType == TEXT("double") || LowerType == TEXT("real"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (LowerType == TEXT("string") || LowerType == TEXT("str"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (LowerType == TEXT("name"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (LowerType == TEXT("text"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (LowerType == TEXT("object"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	}
	else if (LowerType == TEXT("class"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
	}
	else if (LowerType == TEXT("softobject") || LowerType == TEXT("softobjectreference"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
	}
	else if (LowerType == TEXT("softclass") || LowerType == TEXT("softclassreference"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
	}
	else if (LowerType == TEXT("byte"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (LowerType == TEXT("enum"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else
	{
		// Try to resolve as a struct type (FVector, FRotator, FTransform, FLinearColor, FGameplayTag, etc.)
		// Strip leading 'F' for lookup if present
		FString StructName = TypeStr;
		static const TMap<FString, FString> StructAliases = {
			{ TEXT("vector"),       TEXT("Vector") },
			{ TEXT("fvector"),      TEXT("Vector") },
			{ TEXT("rotator"),      TEXT("Rotator") },
			{ TEXT("frotator"),     TEXT("Rotator") },
			{ TEXT("transform"),    TEXT("Transform") },
			{ TEXT("ftransform"),   TEXT("Transform") },
			{ TEXT("linearcolor"),  TEXT("LinearColor") },
			{ TEXT("flinearcolor"), TEXT("LinearColor") },
			{ TEXT("color"),        TEXT("Color") },
			{ TEXT("fcolor"),       TEXT("Color") },
			{ TEXT("vector2d"),     TEXT("Vector2D") },
			{ TEXT("fvector2d"),    TEXT("Vector2D") },
			{ TEXT("gameplaytag"),      TEXT("GameplayTag") },
			{ TEXT("fgameplaytag"),     TEXT("GameplayTag") },
			{ TEXT("gameplaytagcontainer"), TEXT("GameplayTagContainer") },
			{ TEXT("fgameplaytagcontainer"), TEXT("GameplayTagContainer") },
		};

		const FString* Alias = StructAliases.Find(LowerType);
		if (Alias)
		{
			StructName = *Alias;
		}
		else if (StructName.Len() > 1 && StructName[0] == 'F' && FChar::IsUpper(StructName[1]))
		{
			StructName = StructName.Mid(1);
		}

		UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *(FString(TEXT("/Script/CoreUObject.")) + StructName));
		if (!Struct)
		{
			Struct = FindObject<UScriptStruct>(nullptr, *(FString(TEXT("/Script/GameplayTags.")) + StructName));
		}
		if (!Struct)
		{
			// Broad search
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				if (It->GetName() == StructName)
				{
					Struct = *It;
					break;
				}
			}
		}

		if (Struct)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = Struct;
		}
		else if (TryResolveObjectPin(TypeStr))
		{
			// (#140) Last-ditch: treat as a bare class name (e.g. "Actor", "Pawn", "PlayerController").
		}
		// else: PinCategory remains NAME_None — caller must check for unresolved type (#181)
	}

	return PinType;
}

UEdGraph* FBlueprintHandlers::FindGraph(UBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint) return nullptr;

	// Search ALL graphs (UbergraphPages, FunctionGraphs, AnimGraphs, etc.)
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	// #119: support indexed addressing "Transition[4]" for disambiguating the N'th graph
	// with that name (AnimBP state-machine transition graphs all share name "Transition")
	FString BaseName = GraphName;
	int32 Index = -1;
	int32 LB = GraphName.Find(TEXT("["));
	int32 RB = GraphName.Find(TEXT("]"));
	if (LB != INDEX_NONE && RB != INDEX_NONE && RB > LB)
	{
		BaseName = GraphName.Left(LB);
		FString IdxStr = GraphName.Mid(LB + 1, RB - LB - 1);
		Index = FCString::Atoi(*IdxStr);
	}

	int32 Matched = 0;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName() == BaseName)
		{
			if (Index < 0) return Graph;
			if (Matched == Index) return Graph;
			Matched++;
		}
	}

	// Also support object-path addressing "Outer.Graph" by matching suffix
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetPathName().EndsWith(TEXT(".") + GraphName))
		{
			return Graph;
		}
	}
	return nullptr;
}

UEdGraphNode* FBlueprintHandlers::FindNodeByGuidOrName(UEdGraph* Graph, const FString& NodeId)
{
	if (!Graph) return nullptr;

	// Try to parse as GUID first
	FGuid SearchGuid;
	if (FGuid::Parse(NodeId, SearchGuid))
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == SearchGuid)
			{
				return Node;
			}
		}
	}

	// Fallback: search by name/title
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (Node->GetName() == NodeId)
		{
			return Node;
		}
		if (Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString() == NodeId)
		{
			return Node;
		}
	}

	return nullptr;
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ParentClassName = OptionalString(Params, TEXT("parentClass"), TEXT("Actor"));

	// Find parent class -- try multiple resolution strategies
	UClass* ParentClass = nullptr;

	// 1. Try silent short-name search first (handles "Actor", "AActor", "UAnimInstance" etc.)
	ParentClass = FindClassByShortName(ParentClassName);

	// 2. Try as full class path (e.g. "/Script/Engine.Actor" or "/Script/MyModule.MyClass")
	if (!ParentClass)
	{
		ParentClass = LoadObject<UClass>(nullptr, *ParentClassName);
	}

	if (!ParentClass)
	{
		return MCPError(FString::Printf(
			TEXT("Parent class not found: '%s'. Try the full path (e.g. '/Script/Engine.Actor') or the class name without prefix (e.g. 'Actor', 'Pawn', 'Character')."),
			*ParentClassName));
	}

	// Create blueprint
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString PackageName;
	FString AssetName;
	AssetPath.Split(TEXT("/"), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	// Idempotent: if asset already exists, return it.
	UBlueprint* ExistingBP = LoadBlueprint(AssetPath);
	if (ExistingBP)
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("Blueprint '%s' already exists"), *AssetPath));
		}
		FString ObjectPath = ExistingBP->GetPathName();
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("objectPath"), ObjectPath);
		Result->SetStringField(TEXT("className"), ExistingBP->GetName());
		if (ExistingBP->ParentClass)
		{
			Result->SetStringField(TEXT("parentClass"), ExistingBP->ParentClass->GetPathName());
		}
		return MCPResult(Result);
	}

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = ParentClass;
	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(AssetName, PackageName, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		return MCPError(TEXT("Failed to create Blueprint"));
	}

	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	const FString ObjectPath = NewBlueprint->GetPathName();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("objectPath"), ObjectPath);
	Result->SetStringField(TEXT("className"), NewBlueprint->GetName());
	Result->SetStringField(TEXT("parentClass"), ParentClass->GetPathName());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), ObjectPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("className"), Blueprint->GetName());
	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parentClass"), Blueprint->ParentClass->GetName());
	}

	// Enumerate SCS components
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		// Build child->parent map from the tree
		TMap<USCS_Node*, USCS_Node*> ParentMap;
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node) continue;
			for (USCS_Node* Child : Node->ChildNodes)
			{
				if (Child) ParentMap.Add(Child, Node);
			}
		}

		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate) continue;

			UActorComponent* Template = Node->ComponentTemplate;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			CompObj->SetStringField(TEXT("class"), Template->GetClass()->GetName());

			// Parent component
			if (USCS_Node** ParentPtr = ParentMap.Find(Node))
			{
				CompObj->SetStringField(TEXT("parent"), (*ParentPtr)->GetVariableName().ToString());
			}

			// Transform for SceneComponents
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Template))
			{
				TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
				Loc->SetNumberField(TEXT("x"), SceneComp->GetRelativeLocation().X);
				Loc->SetNumberField(TEXT("y"), SceneComp->GetRelativeLocation().Y);
				Loc->SetNumberField(TEXT("z"), SceneComp->GetRelativeLocation().Z);
				CompObj->SetObjectField(TEXT("relativeLocation"), Loc);

				TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
				Rot->SetNumberField(TEXT("pitch"), SceneComp->GetRelativeRotation().Pitch);
				Rot->SetNumberField(TEXT("yaw"), SceneComp->GetRelativeRotation().Yaw);
				Rot->SetNumberField(TEXT("roll"), SceneComp->GetRelativeRotation().Roll);
				CompObj->SetObjectField(TEXT("relativeRotation"), Rot);

				TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
				Scale->SetNumberField(TEXT("x"), SceneComp->GetRelativeScale3D().X);
				Scale->SetNumberField(TEXT("y"), SceneComp->GetRelativeScale3D().Y);
				Scale->SetNumberField(TEXT("z"), SceneComp->GetRelativeScale3D().Z);
				CompObj->SetObjectField(TEXT("relativeScale3D"), Scale);
			}

			// StaticMesh info
			if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Template))
			{
				if (UStaticMesh* Mesh = SMC->GetStaticMesh())
				{
					CompObj->SetStringField(TEXT("staticMesh"), Mesh->GetPathName());
				}
				// Material overrides
				TArray<TSharedPtr<FJsonValue>> Mats;
				for (int32 i = 0; i < SMC->GetNumMaterials(); i++)
				{
					if (UMaterialInterface* Mat = SMC->GetMaterial(i))
					{
						Mats.Add(MakeShared<FJsonValueString>(Mat->GetPathName()));
					}
					else
					{
						Mats.Add(MakeShared<FJsonValueNull>());
					}
				}
				if (Mats.Num() > 0)
				{
					CompObj->SetArrayField(TEXT("materials"), Mats);
				}
			}

			// SkeletalMesh info
			if (USkeletalMeshComponent* SkMC = Cast<USkeletalMeshComponent>(Template))
			{
				if (USkeletalMesh* Mesh = SkMC->GetSkeletalMeshAsset())
				{
					CompObj->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
				}
				TArray<TSharedPtr<FJsonValue>> Mats;
				for (int32 i = 0; i < SkMC->GetNumMaterials(); i++)
				{
					if (UMaterialInterface* Mat = SkMC->GetMaterial(i))
					{
						Mats.Add(MakeShared<FJsonValueString>(Mat->GetPathName()));
					}
					else
					{
						Mats.Add(MakeShared<FJsonValueNull>());
					}
				}
				if (Mats.Num() > 0)
				{
					CompObj->SetArrayField(TEXT("materials"), Mats);
				}
			}

			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}
	Result->SetArrayField(TEXT("components"), ComponentsArray);

	// #116: expose actor tick settings from the CDO
	if (Blueprint->GeneratedClass)
	{
		if (AActor* CDOActor = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)))
		{
			TSharedPtr<FJsonObject> TickObj = MakeShared<FJsonObject>();
			TickObj->SetBoolField(TEXT("bCanEverTick"), CDOActor->PrimaryActorTick.bCanEverTick);
			TickObj->SetBoolField(TEXT("bStartWithTickEnabled"), CDOActor->PrimaryActorTick.bStartWithTickEnabled);
			TickObj->SetNumberField(TEXT("TickInterval"), CDOActor->PrimaryActorTick.TickInterval);
			Result->SetObjectField(TEXT("actorTick"), TickObj);
		}
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	FString VarType = OptionalString(Params, TEXT("type"), TEXT("Float"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Idempotency: if the variable already exists on the blueprint, short-circuit.
	const FName VarNameFName(*VarName);
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarNameFName)
		{
			if (OnConflict == TEXT("error"))
			{
				return MCPError(FString::Printf(TEXT("Variable '%s' already exists"), *VarName));
			}
			auto Existing = MCPSuccess();
			MCPSetExisted(Existing);
			Existing->SetStringField(TEXT("path"), AssetPath);
			Existing->SetStringField(TEXT("variableName"), VarName);
			return MCPResult(Existing);
		}
	}

	FEdGraphPinType PinType = MakePinType(VarType);

	if (PinType.PinCategory == NAME_None)
	{
		return MCPError(FString::Printf(TEXT("Unrecognized variable type: '%s'. Use a known type (Bool, Int, Float, String, Name, Text, Byte, Object, Vector, Rotator, Transform, GameplayTag, etc.) or a full class/struct path."), *VarType));
	}

	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarNameFName, PinType);

	if (bSuccess)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("variableName"), VarName);
		Result->SetStringField(TEXT("variableType"), VarType);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("variableName"), VarName);
		MCPSetRollback(Result, TEXT("delete_variable"), Payload);

		return MCPResult(Result);
	}
	else
	{
		return MCPError(TEXT("Failed to add variable - FBlueprintEditorUtils::AddMemberVariable returned false"));
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentClass;
	if (auto Err = RequireString(Params, TEXT("componentClass"), ComponentClass)) return Err;

	FString ComponentName = OptionalString(Params, TEXT("componentName"), ComponentClass);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Idempotency: existing SCS component with same name short-circuits.
	if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString() == ComponentName)
			{
				if (OnConflict == TEXT("error"))
				{
					return MCPError(FString::Printf(TEXT("Component '%s' already exists"), *ComponentName));
				}
				auto Existing = MCPSuccess();
				MCPSetExisted(Existing);
				Existing->SetStringField(TEXT("path"), AssetPath);
				Existing->SetStringField(TEXT("componentName"), ComponentName);
				Existing->SetStringField(TEXT("componentClass"), ComponentClass);
				return MCPResult(Existing);
			}
		}
	}

	// Find component class: accept full paths, short names ("StaticMeshComponent"),
	// short names with U prefix, and engine-module implicit resolution.
	// (#136, #137) Previously only literal FindObject + "U"+name worked, so standard
	// engine components like SceneComponent/SphereComponent/NiagaraComponent failed.
	UClass* CompClass = nullptr;
	if (ComponentClass.Contains(TEXT("/")) || ComponentClass.Contains(TEXT(".")))
	{
		CompClass = LoadObject<UClass>(nullptr, *ComponentClass);
	}
	if (!CompClass)
	{
		CompClass = FindClassByShortName(ComponentClass);
	}
	if (!CompClass)
	{
		CompClass = LoadObject<UClass>(nullptr, *(FString(TEXT("/Script/Engine.")) + ComponentClass));
	}

	if (!CompClass)
	{
		return MCPError(FString::Printf(TEXT("Component class not found: %s. Try the short name (e.g. 'StaticMeshComponent') or the full path ('/Script/Engine.StaticMeshComponent')."), *ComponentClass));
	}

	// #115: optional parentComponent — makes this component a child in the SCS hierarchy
	const FString ParentComponent = OptionalString(Params, TEXT("parentComponent"));

	// Try using SubobjectDataSubsystem (UE5 method)
	bool bSuccess = false;
	if (USubobjectDataSubsystem* Subsystem = GEngine->GetEngineSubsystem<USubobjectDataSubsystem>())
	{
		// Get blueprint handles using K2 function
		TArray<FSubobjectDataHandle> Handles;
		Subsystem->K2_GatherSubobjectDataForBlueprint(Blueprint, Handles);
		if (Handles.Num() > 0)
		{
			FSubobjectDataHandle RootHandle = Handles[0];

			// Resolve parentComponent to its handle if specified
			if (!ParentComponent.IsEmpty())
			{
				for (const FSubobjectDataHandle& H : Handles)
				{
					if (const FSubobjectData* Data = H.GetData())
					{
						if (UObject* Obj = const_cast<UObject*>(Data->GetObject()))
						{
							if (Obj->GetName() == ParentComponent || Obj->GetName().StartsWith(ParentComponent))
							{
								RootHandle = H;
								break;
							}
						}
					}
				}
			}

			FAddNewSubobjectParams AddParams;
			AddParams.ParentHandle = RootHandle;
			AddParams.NewClass = CompClass;
			AddParams.BlueprintContext = Blueprint;

			FText FailReason;
			FSubobjectDataHandle NewHandle = Subsystem->AddNewSubobject(AddParams, FailReason);
			if (NewHandle.IsValid())
			{
				// Rename if needed
				if (ComponentName != ComponentClass)
				{
					Subsystem->RenameSubobject(NewHandle, FText::FromString(ComponentName));
				}
				bSuccess = true;
			}
		}
	}

	if (bSuccess)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		// Save asset
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("componentClass"), ComponentClass);
		Result->SetStringField(TEXT("componentName"), ComponentName);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("componentName"), ComponentName);
		MCPSetRollback(Result, TEXT("remove_component"), Payload);

		return MCPResult(Result);
	}
	else
	{
		return MCPError(TEXT("Failed to add component via SubobjectDataSubsystem"));
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	FString InterfacePathStr;
	if (auto Err = RequireString(Params, TEXT("interfacePath"), InterfacePathStr)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
	}

	UClass* InterfaceClass = LoadObject<UClass>(nullptr, *InterfacePathStr);
	if (!InterfaceClass)
	{
		return MCPError(FString::Printf(TEXT("Interface not found: %s"), *InterfacePathStr));
	}

	// Idempotency: check if interface already implemented on this blueprint
	FTopLevelAssetPath InterfaceAssetPath(InterfaceClass->GetPathName());
	for (const FBPInterfaceDescription& Impl : Blueprint->ImplementedInterfaces)
	{
		if (Impl.Interface == InterfaceClass)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("blueprintPath"), BlueprintPath);
			Existed->SetStringField(TEXT("interfacePath"), InterfacePathStr);
			return MCPResult(Existed);
		}
	}

	// Use FBlueprintEditorUtils to add interface
	FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceAssetPath);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	// Save asset
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("interfacePath"), InterfacePathStr);
	// No rollback: no paired remove_blueprint_interface handler yet.
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SearchNodeTypes(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (auto Err = RequireString(Params, TEXT("query"), Query)) return Err;

	TArray<TSharedPtr<FJsonValue>> MatchingTypes;
	FString LowerQuery = Query.ToLower();

	// Search UFunction names across common engine classes
	TArray<UClass*> ClassesToSearch;
	ClassesToSearch.Add(AActor::StaticClass());
	ClassesToSearch.Add(UGameplayStatics::StaticClass());
	ClassesToSearch.Add(UKismetSystemLibrary::StaticClass());
	ClassesToSearch.Add(UKismetMathLibrary::StaticClass());
	ClassesToSearch.Add(UKismetStringLibrary::StaticClass());

	for (UClass* SearchClass : ClassesToSearch)
	{
		if (!SearchClass) continue;
		for (TFieldIterator<UFunction> FuncIt(SearchClass); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			if (!Func) continue;

			FString FuncName = Func->GetName();
			if (FuncName.ToLower().Contains(LowerQuery))
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), FuncName);
				Entry->SetStringField(TEXT("class"), SearchClass->GetName());
				Entry->SetStringField(TEXT("fullPath"), Func->GetPathName());
				MatchingTypes.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
	}

	// Also search AnimGraph node types and other UEdGraphNode subclasses
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UEdGraphNode::StaticClass())) continue;
		if (*It == UEdGraphNode::StaticClass()) continue;

		FString ClassName = It->GetName();
		if (ClassName.ToLower().Contains(LowerQuery))
		{
			// Avoid duplicates from function search above
			bool bAlreadyListed = false;
			for (const TSharedPtr<FJsonValue>& Existing : MatchingTypes)
			{
				if (Existing->AsObject()->GetStringField(TEXT("name")) == ClassName)
				{
					bAlreadyListed = true;
					break;
				}
			}
			if (!bAlreadyListed)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), ClassName);
				Entry->SetStringField(TEXT("class"), It->GetSuperClass() ? It->GetSuperClass()->GetName() : TEXT(""));
				Entry->SetStringField(TEXT("fullPath"), It->GetPathName());
				Entry->SetStringField(TEXT("type"), TEXT("graphNode"));
				MatchingTypes.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("results"), MatchingTypes);
	Result->SetNumberField(TEXT("count"), MatchingTypes.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListNodeTypes(const TSharedPtr<FJsonObject>& Params)
{
	FString Category = OptionalString(Params, TEXT("category"), TEXT("Utilities"));

	TArray<TSharedPtr<FJsonValue>> NodeTypes;
	FString LowerCategory = Category.ToLower();

	// Map categories to relevant classes and function sets
	TArray<UClass*> ClassesToSearch;

	if (LowerCategory == TEXT("utilities"))
	{
		ClassesToSearch.Add(UKismetSystemLibrary::StaticClass());
	}
	else if (LowerCategory == TEXT("math"))
	{
		ClassesToSearch.Add(UKismetMathLibrary::StaticClass());
	}
	else if (LowerCategory == TEXT("string"))
	{
		ClassesToSearch.Add(UKismetStringLibrary::StaticClass());
	}
	else if (LowerCategory == TEXT("gameplay"))
	{
		ClassesToSearch.Add(UGameplayStatics::StaticClass());
	}
	else if (LowerCategory == TEXT("actor"))
	{
		ClassesToSearch.Add(AActor::StaticClass());
	}
	else
	{
		// Default: search all common classes
		ClassesToSearch.Add(UKismetSystemLibrary::StaticClass());
		ClassesToSearch.Add(UKismetMathLibrary::StaticClass());
		ClassesToSearch.Add(UGameplayStatics::StaticClass());
	}

	for (UClass* SearchClass : ClassesToSearch)
	{
		if (!SearchClass) continue;
		for (TFieldIterator<UFunction> FuncIt(SearchClass); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			if (!Func) continue;
			if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure)) continue;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Func->GetName());
			Entry->SetStringField(TEXT("class"), SearchClass->GetName());
			NodeTypes.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("category"), Category);
	Result->SetArrayField(TEXT("nodeTypes"), NodeTypes);
	Result->SetNumberField(TEXT("count"), NodeTypes.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListBlueprintVariables(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> Variables;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		VarObj->SetStringField(TEXT("guid"), Var.VarGuid.ToString());

		// Check metadata
		if (Var.HasMetaData(FBlueprintMetadata::MD_Private))
		{
			VarObj->SetBoolField(TEXT("private"), true);
		}
		if (Var.HasMetaData(FBlueprintMetadata::MD_FunctionCategory))
		{
			VarObj->SetStringField(TEXT("category"), Var.GetMetaData(FBlueprintMetadata::MD_FunctionCategory));
		}
		if (Var.HasMetaData(FBlueprintMetadata::MD_Tooltip))
		{
			VarObj->SetStringField(TEXT("tooltip"), Var.GetMetaData(FBlueprintMetadata::MD_Tooltip));
		}

		VarObj->SetBoolField(TEXT("instanceEditable"),
			!Var.HasMetaData(FBlueprintMetadata::MD_Private) && (Var.PropertyFlags & CPF_Edit) != 0);

		VarObj->SetBoolField(TEXT("exposeOnSpawn"),
			Var.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn) || (Var.PropertyFlags & CPF_ExposeOnSpawn) != 0);

		Variables.Add(MakeShared<FJsonValueObject>(VarObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("variables"), Variables);
	Result->SetNumberField(TEXT("count"), Variables.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SetVariableProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the variable
	FBPVariableDescription* FoundVar = nullptr;
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName.ToString() == VarName)
		{
			FoundVar = &Var;
			break;
		}
	}

	if (!FoundVar)
	{
		return MCPError(FString::Printf(TEXT("Variable not found: %s"), *VarName));
	}

	// Capture previous values for rollback
	const bool bPrevInstanceEditable = (FoundVar->PropertyFlags & CPF_Edit) != 0;
	FString PrevCategory;
	if (FoundVar->HasMetaData(FBlueprintMetadata::MD_FunctionCategory))
	{
		PrevCategory = FoundVar->GetMetaData(FBlueprintMetadata::MD_FunctionCategory);
	}
	FString PrevTooltip;
	if (FoundVar->HasMetaData(FBlueprintMetadata::MD_Tooltip))
	{
		PrevTooltip = FoundVar->GetMetaData(FBlueprintMetadata::MD_Tooltip);
	}

	// Set expose on spawn
	bool bExposeOnSpawn = false;
	const bool bHasExposeOnSpawn = Params->TryGetBoolField(TEXT("exposeOnSpawn"), bExposeOnSpawn);
	const bool bPrevExposeOnSpawn = FoundVar->HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn);
	if (bHasExposeOnSpawn)
	{
		if (bExposeOnSpawn)
		{
			FoundVar->SetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
			FoundVar->PropertyFlags |= CPF_ExposeOnSpawn;
		}
		else
		{
			FoundVar->RemoveMetaData(FBlueprintMetadata::MD_ExposeOnSpawn);
			FoundVar->PropertyFlags &= ~CPF_ExposeOnSpawn;
		}
	}

	// Set instance editable
	bool bInstanceEditable = false;
	const bool bHasInstanceEditable = Params->TryGetBoolField(TEXT("instanceEditable"), bInstanceEditable);
	if (bHasInstanceEditable)
	{
		if (bInstanceEditable)
		{
			FoundVar->PropertyFlags |= CPF_Edit;
			FoundVar->RemoveMetaData(FBlueprintMetadata::MD_Private);
		}
		else
		{
			FoundVar->PropertyFlags &= ~CPF_Edit;
		}
	}

	// Set category
	FString CategoryStr;
	const bool bHasCategory = Params->TryGetStringField(TEXT("category"), CategoryStr);
	if (bHasCategory)
	{
		FoundVar->SetMetaData(FBlueprintMetadata::MD_FunctionCategory, *CategoryStr);
	}

	// Set tooltip
	FString TooltipStr;
	const bool bHasTooltip = Params->TryGetStringField(TEXT("tooltip"), TooltipStr);
	if (bHasTooltip)
	{
		FoundVar->SetMetaData(FBlueprintMetadata::MD_Tooltip, *TooltipStr);
	}

	// Detect no-op: nothing requested OR every requested field already matches
	const bool bAnyChanged =
		(bHasExposeOnSpawn && bExposeOnSpawn != bPrevExposeOnSpawn) ||
		(bHasInstanceEditable && bInstanceEditable != bPrevInstanceEditable) ||
		(bHasCategory && CategoryStr != PrevCategory) ||
		(bHasTooltip && TooltipStr != PrevTooltip);
	if (!bAnyChanged)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("name"), VarName);
		return MCPResult(Noop);
	}

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("variableName"), VarName);

	// Rollback: call set_variable_properties with previous values
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("name"), VarName);
	if (bHasExposeOnSpawn) Payload->SetBoolField(TEXT("exposeOnSpawn"), bPrevExposeOnSpawn);
	if (bHasInstanceEditable) Payload->SetBoolField(TEXT("instanceEditable"), bPrevInstanceEditable);
	if (bHasCategory) Payload->SetStringField(TEXT("category"), PrevCategory);
	if (bHasTooltip) Payload->SetStringField(TEXT("tooltip"), PrevTooltip);
	MCPSetRollback(Result, TEXT("set_variable_properties"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Idempotency: existing function graph short-circuits.
	for (UEdGraph* G : Blueprint->FunctionGraphs)
	{
		if (G && G->GetName() == FunctionName)
		{
			if (OnConflict == TEXT("error"))
			{
				return MCPError(FString::Printf(TEXT("Function '%s' already exists"), *FunctionName));
			}
			auto Existing = MCPSuccess();
			MCPSetExisted(Existing);
			Existing->SetStringField(TEXT("path"), AssetPath);
			Existing->SetStringField(TEXT("functionName"), FunctionName);
			return MCPResult(Existing);
		}
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);
	if (!NewGraph)
	{
		return MCPError(FString::Printf(TEXT("Failed to create function: %s"), *FunctionName));
	}

	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated=*/true, /*SignatureFromObject=*/nullptr);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("functionName"), FunctionName);
	MCPSetRollback(Result, TEXT("delete_function"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListBlueprintFunctions(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> Functions;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Graph->GetName());
		FuncObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
		Functions.Add(MakeShared<FJsonValueObject>(FuncObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetArrayField(TEXT("functions"), Functions);
	Result->SetNumberField(TEXT("count"), Functions.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString NodeClass;
	if (auto Err = RequireString(Params, TEXT("nodeClass"), NodeClass)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the target graph
	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);

	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Get optional node params
	const TSharedPtr<FJsonObject>* NodeParams = nullptr;
	Params->TryGetObjectField(TEXT("nodeParams"), NodeParams);

	// Resolve short aliases to full class names
	FString ResolvedClass = NodeClass;
	if (NodeClass == TEXT("CallFunction"))  ResolvedClass = TEXT("K2Node_CallFunction");
	else if (NodeClass == TEXT("Event"))    ResolvedClass = TEXT("K2Node_Event");
	else if (NodeClass == TEXT("GetVar"))   ResolvedClass = TEXT("K2Node_VariableGet");
	else if (NodeClass == TEXT("SetVar"))   ResolvedClass = TEXT("K2Node_VariableSet");
	else if (NodeClass == TEXT("Branch"))   ResolvedClass = TEXT("K2Node_IfThenElse");
	else if (NodeClass == TEXT("CustomEvent")) ResolvedClass = TEXT("K2Node_CustomEvent");

	// Find the UEdGraphNode subclass by name (works for K2, AnimGraph, and any other graph node types)
	UClass* NodeUClass = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->GetName() == ResolvedClass && It->IsChildOf(UEdGraphNode::StaticClass()))
		{
			NodeUClass = *It;
			break;
		}
	}

	if (!NodeUClass)
	{
		return MCPError(FString::Printf(TEXT("Node class not found: %s (must be a UEdGraphNode subclass)"), *NodeClass));
	}

	// Create node instance
	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(TargetGraph, NodeUClass);
	if (!NewNode)
	{
		return MCPError(TEXT("Failed to create node"));
	}

	// Special-case initialization for known types (must happen BEFORE AllocateDefaultPins)
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(NewNode))
	{
		if (NodeParams)
		{
			FString FunctionName;
			FString TargetClassName;

			// Accept flat params: functionName, targetClass
			if (!(*NodeParams)->TryGetStringField(TEXT("functionName"), FunctionName))
				(*NodeParams)->TryGetStringField(TEXT("memberName"), FunctionName);
			if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), TargetClassName))
				(*NodeParams)->TryGetStringField(TEXT("memberParent"), TargetClassName);

			// Also accept nested: {"FunctionReference":{"MemberName":"X","MemberParent":"Y"}}
			if (FunctionName.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* FuncRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("FunctionReference"), FuncRef))
				{
					(*FuncRef)->TryGetStringField(TEXT("MemberName"), FunctionName);
					if (TargetClassName.IsEmpty())
						(*FuncRef)->TryGetStringField(TEXT("MemberParent"), TargetClassName);
				}
			}

			// Handle full path format: "/Script/Engine.GameplayStatics:GetGameMode"
			if (!FunctionName.IsEmpty() && FunctionName.Contains(TEXT(":")))
			{
				FString ClassPath, FuncPart;
				FunctionName.Split(TEXT(":"), &ClassPath, &FuncPart);
				FunctionName = FuncPart;
				if (TargetClassName.IsEmpty())
				{
					TargetClassName = ClassPath;
				}
			}

			if (!FunctionName.IsEmpty())
			{
				UFunction* FoundFunc = nullptr;

				// 1. Try explicit target class
				if (!TargetClassName.IsEmpty())
				{
					UClass* TargetClass = LoadObject<UClass>(nullptr, *TargetClassName);
					if (!TargetClass)
					{
						TargetClass = FindClassByShortName(TargetClassName);
					}
					if (TargetClass)
					{
						FoundFunc = TargetClass->FindFunctionByName(FName(*FunctionName));
					}
				}

				// 2. Try blueprint parent class
				if (!FoundFunc && Blueprint->ParentClass)
				{
					FoundFunc = Blueprint->ParentClass->FindFunctionByName(FName(*FunctionName));
				}

				// 3. Search common library classes
				if (!FoundFunc)
				{
					static UClass* LibraryClasses[] = {
						UGameplayStatics::StaticClass(),
						UKismetSystemLibrary::StaticClass(),
						UKismetMathLibrary::StaticClass(),
						UKismetStringLibrary::StaticClass(),
						UKismetArrayLibrary::StaticClass(),
					};
					for (UClass* Lib : LibraryClasses)
					{
						FoundFunc = Lib->FindFunctionByName(FName(*FunctionName));
						if (FoundFunc) break;
					}
				}

				if (FoundFunc)
				{
					CallNode->SetFromFunction(FoundFunc);
				}
			}
		}
	}
	else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(NewNode))
	{
		if (NodeParams)
		{
			FString EventName;
			FString EventClassName;

			if (!(*NodeParams)->TryGetStringField(TEXT("eventName"), EventName))
				(*NodeParams)->TryGetStringField(TEXT("memberName"), EventName);
			if (!(*NodeParams)->TryGetStringField(TEXT("eventClass"), EventClassName))
				(*NodeParams)->TryGetStringField(TEXT("memberParent"), EventClassName);

			// Also accept nested: {"EventReference":{"MemberName":"X","MemberParent":"Y"}}
			if (EventName.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* EvtRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("EventReference"), EvtRef))
				{
					(*EvtRef)->TryGetStringField(TEXT("MemberName"), EventName);
					if (EventClassName.IsEmpty())
						(*EvtRef)->TryGetStringField(TEXT("MemberParent"), EventClassName);
				}
			}

			if (!EventName.IsEmpty())
			{

				if (!EventClassName.IsEmpty())
				{
					// Engine event override -- bind via EventReference
					UClass* EventClass = FindClassByShortName(EventClassName);
					if (!EventClass) EventClass = Blueprint->ParentClass;

					if (EventClass)
					{
						UFunction* EventFunc = EventClass->FindFunctionByName(FName(*EventName));
						if (EventFunc)
						{
							bool bIsSelf = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(EventClass);
							EventNode->EventReference.SetFromField<UFunction>(EventFunc, bIsSelf);
						}
					}
				}
				else
				{
					// Custom event -- just set the name
					EventNode->CustomFunctionName = FName(*EventName);
				}
			}
		}
	}
	else if (UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(NewNode))
	{
		if (NodeParams)
		{
			FString VarName;
			FString OwnerClass;
			if (!(*NodeParams)->TryGetStringField(TEXT("variableName"), VarName))
			{
				// Also accept {"VariableReference":{"MemberName":"X"}} format
				const TSharedPtr<FJsonObject>* VarRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("VariableReference"), VarRef))
					(*VarRef)->TryGetStringField(TEXT("MemberName"), VarName);
			}
			(*NodeParams)->TryGetStringField(TEXT("ownerClass"), OwnerClass);

			if (!VarName.IsEmpty())
			{
				if (!OwnerClass.IsEmpty())
				{
					// #118: external class member get — typed Target input pin
					UClass* Owner = LoadClass<UObject>(nullptr, *OwnerClass);
					if (!Owner) Owner = LoadObject<UClass>(nullptr, *OwnerClass);
					if (!Owner && !OwnerClass.EndsWith(TEXT("_C")))
					{
						Owner = LoadClass<UObject>(nullptr, *(OwnerClass + TEXT("_C")));
					}
					if (!Owner) Owner = FindClassByShortName(OwnerClass);
					if (Owner)
					{
						GetNode->VariableReference.SetExternalMember(FName(*VarName), Owner);
					}
					else
					{
						GetNode->VariableReference.SetSelfMember(FName(*VarName));
					}
				}
				else
				{
					GetNode->VariableReference.SetSelfMember(FName(*VarName));
				}
			}
		}
	}
	else if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(NewNode))
	{
		if (NodeParams)
		{
			FString VarName;
			if (!(*NodeParams)->TryGetStringField(TEXT("variableName"), VarName))
			{
				const TSharedPtr<FJsonObject>* VarRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("VariableReference"), VarRef))
					(*VarRef)->TryGetStringField(TEXT("MemberName"), VarName);
			}
			if (!VarName.IsEmpty())
			{
				SetNode->VariableReference.SetSelfMember(FName(*VarName));
			}
		}
	}
	else if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(NewNode))
	{
		// #101/#118: resolve BP-generated class paths as well as native classes
		if (NodeParams)
		{
			FString TargetType;
			if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), TargetType))
				(*NodeParams)->TryGetStringField(TEXT("TargetType"), TargetType);
			if (!TargetType.IsEmpty())
			{
				UClass* CastTargetClass = nullptr;
				// Try LoadClass (handles Blueprint_C paths like /Game/.../BP_Foo.BP_Foo_C)
				CastTargetClass = LoadClass<UObject>(nullptr, *TargetType);
				if (!CastTargetClass)
				{
					// Try LoadObject as UClass
					CastTargetClass = LoadObject<UClass>(nullptr, *TargetType);
				}
				if (!CastTargetClass && !TargetType.EndsWith(TEXT("_C")))
				{
					// Try appending _C for Blueprint generated classes
					FString WithSuffix = TargetType + TEXT("_C");
					CastTargetClass = LoadClass<UObject>(nullptr, *WithSuffix);
					if (!CastTargetClass) CastTargetClass = LoadObject<UClass>(nullptr, *WithSuffix);
				}
				if (!CastTargetClass)
				{
					CastTargetClass = FindClassByShortName(TargetType);
				}
				if (CastTargetClass)
				{
					CastNode->TargetType = CastTargetClass;
				}
			}
		}
	}

	// #189: K2Node_CallDelegate — bind the DelegateReference so the node resolves
	// its signature and generates correct pins for multicast delegate invocation.
	else if (UK2Node_CallDelegate* DelegateNode = Cast<UK2Node_CallDelegate>(NewNode))
	{
		if (NodeParams)
		{
			FString DelegateName;
			FString OwnerClass;

			// Accept flat params: delegateName / functionName, ownerClass / targetClass
			if (!(*NodeParams)->TryGetStringField(TEXT("delegateName"), DelegateName))
			{
				if (!(*NodeParams)->TryGetStringField(TEXT("functionName"), DelegateName))
					(*NodeParams)->TryGetStringField(TEXT("memberName"), DelegateName);
			}
			if (!(*NodeParams)->TryGetStringField(TEXT("ownerClass"), OwnerClass))
			{
				if (!(*NodeParams)->TryGetStringField(TEXT("targetClass"), OwnerClass))
					(*NodeParams)->TryGetStringField(TEXT("memberParent"), OwnerClass);
			}

			// Also accept nested: {"DelegateReference":{"MemberName":"X","MemberParent":"Y"}}
			if (DelegateName.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* DelRef = nullptr;
				if ((*NodeParams)->TryGetObjectField(TEXT("DelegateReference"), DelRef))
				{
					(*DelRef)->TryGetStringField(TEXT("MemberName"), DelegateName);
					if (OwnerClass.IsEmpty())
						(*DelRef)->TryGetStringField(TEXT("MemberParent"), OwnerClass);
				}
			}

			if (!DelegateName.IsEmpty())
			{
				if (!OwnerClass.IsEmpty())
				{
					UClass* Owner = LoadObject<UClass>(nullptr, *OwnerClass);
					if (!Owner) Owner = FindClassByShortName(OwnerClass);
					if (Owner)
					{
						// Check if the property is a multicast delegate on the owner class
						FProperty* Prop = Owner->FindPropertyByName(FName(*DelegateName));
						if (Prop)
						{
							bool bIsSelf = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(Owner);
							DelegateNode->SetFromProperty(Prop, bIsSelf, Owner);
						}
						else
						{
							// Fall back to setting the member reference directly
							bool bIsSelf = Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(Owner);
							if (bIsSelf)
								DelegateNode->DelegateReference.SetSelfMember(FName(*DelegateName));
							else
								DelegateNode->DelegateReference.SetExternalMember(FName(*DelegateName), Owner);
						}
					}
				}
				else
				{
					// Self member — delegate belongs to the Blueprint's own class
					DelegateNode->DelegateReference.SetSelfMember(FName(*DelegateName));
				}
			}
		}
	}

	// Common initialization (works for all UEdGraphNode subclasses -- K2, AnimGraph, etc.)
	TargetGraph->AddNode(NewNode, false, false);
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	// #101/#118: after AllocateDefaultPins, force ReconstructNode so typed output pin
	// ("As ClassName") appears for DynamicCast and typed pins appear for VariableGet.
	if (UK2Node* K2 = Cast<UK2Node>(NewNode))
	{
		K2->ReconstructNode();
	}

	// #152: function graphs need the structural-modification signal for the
	// skeleton class to pick up new nodes. MarkBlueprintAsStructurallyModified
	// triggers that plus invalidates cached CDO info; CompileBlueprint alone
	// was leaving nodes in newly-created function graphs in a half-initialized
	// state where pins appeared but the underlying function binding didn't.
	TargetGraph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeClass"), NewNode->GetClass()->GetName());
	const FString NodeIdStr = NewNode->NodeGuid.ToString();
	Result->SetStringField(TEXT("nodeId"), NodeIdStr);
	Result->SetStringField(TEXT("title"), NewNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

	// Return pin info so the caller knows what to connect
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : NewNode->Pins)
	{
		if (!Pin) continue;
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	Result->SetArrayField(TEXT("pins"), PinsArray);

	// Rollback: delete the node we just created by guid.
	// Note: add_node has no natural key, so we cannot short-circuit on replay.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("graphName"), GraphName);
	Payload->SetStringField(TEXT("nodeId"), NodeIdStr);
	MCPSetRollback(Result, TEXT("delete_node"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ReadBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the graph in UbergraphPages and FunctionGraphs
	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);

	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("posX"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("posY"), Node->NodePosY);
		NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);

		// List pins
		TArray<TSharedPtr<FJsonValue>> Pins;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
			PinObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
			PinObj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
			Pins.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		NodeObj->SetArrayField(TEXT("pins"), Pins);

		Nodes.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetArrayField(TEXT("nodes"), Nodes);
	Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddEventDispatcher(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	FString DispatcherName;
	if (auto Err = RequireString(Params, TEXT("name"), DispatcherName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
	}

	FName DispatcherFName(*DispatcherName);

	// Idempotency: if a variable with this name already exists, short-circuit
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == DispatcherFName)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("blueprintPath"), BlueprintPath);
			Existed->SetStringField(TEXT("name"), DispatcherName);
			return MCPResult(Existed);
		}
	}

	// Create the delegate signature graph so the compiler has a function to reference.
	// Convention: "<Name>__DelegateSignature"
	FString SigGraphName = DispatcherName + TEXT("__DelegateSignature");
	UEdGraph* SigGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FName(*SigGraphName),
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (SigGraph)
	{
		Blueprint->DelegateSignatureGraphs.AddUnique(SigGraph);
		SigGraph->SetFlags(RF_Transactional);
		// Schema creates the proper function entry node for us
		SigGraph->GetSchema()->CreateDefaultNodesForGraph(*SigGraph);
	}

	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	if (SigGraph)
	{
		PinType.PinSubCategoryMemberReference.MemberName = SigGraph->GetFName();
		PinType.PinSubCategoryMemberReference.MemberGuid = SigGraph->GraphGuid;
	}

	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, DispatcherFName, PinType);

	if (bSuccess)
	{
		// Compile and save
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Result->SetStringField(TEXT("name"), DispatcherName);

		// Rollback: delete_variable (dispatcher is a member variable under the hood)
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), BlueprintPath);
		Payload->SetStringField(TEXT("name"), DispatcherName);
		MCPSetRollback(Result, TEXT("delete_variable"), Payload);

		return MCPResult(Result);
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Failed to add event dispatcher: %s"), *DispatcherName));
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::RenameFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString OldName;
	if (auto Err = RequireString(Params, TEXT("oldName"), OldName)) return Err;

	FString NewName;
	if (auto Err = RequireString(Params, TEXT("newName"), NewName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the function graph
	UEdGraph* FoundGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == OldName)
		{
			FoundGraph = Graph;
			break;
		}
	}

	if (!FoundGraph)
	{
		return MCPError(FString::Printf(TEXT("Function not found: %s"), *OldName));
	}

	FBlueprintEditorUtils::RenameGraph(FoundGraph, NewName);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("oldName"), OldName);
	Result->SetStringField(TEXT("newName"), NewName);

	// Self-inverse: rename back.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("oldName"), NewName);
	Payload->SetStringField(TEXT("newName"), OldName);
	MCPSetRollback(Result, TEXT("rename_function"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteFunction(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* FoundGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == FunctionName)
		{
			FoundGraph = Graph;
			break;
		}
	}

	// Idempotent: no function to delete is a no-op.
	if (!FoundGraph)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("functionName"), FunctionName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	FBlueprintEditorUtils::RemoveGraph(Blueprint, FoundGraph);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetBoolField(TEXT("deleted"), true);
	// Delete of a function is not reversible by default.
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::CreateBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	// Idempotency: check if asset already exists.
	if (UBlueprint* Existing = LoadBlueprint(AssetPath))
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("Interface '%s' already exists"), *AssetPath));
		}
		auto Result = MCPSuccess();
		MCPSetExisted(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("name"), Existing->GetName());
		return MCPResult(Result);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	FString PackageName;
	FString AssetName;
	AssetPath.Split(TEXT("/"), &PackageName, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->BlueprintType = BPTYPE_Interface;
	BlueprintFactory->ParentClass = UInterface::StaticClass();

	UBlueprint* NewInterface = Cast<UBlueprint>(AssetTools.CreateAsset(AssetName, PackageName, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewInterface)
	{
		return MCPError(TEXT("Failed to create Blueprint Interface"));
	}

	FKismetEditorUtilities::CompileBlueprint(NewInterface);

	const FString ObjectPath = NewInterface->GetPathName();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("name"), NewInterface->GetName());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), ObjectPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);

	return MCPResult(Result);
}

// ============================================================================
// NEW HANDLERS
// ============================================================================

TSharedPtr<FJsonValue> FBlueprintHandlers::ListNodeTypesDetailed(const TSharedPtr<FJsonObject>& Params)
{
	// Static catalog of common K2Node types with descriptions and categories
	struct FNodeTypeEntry
	{
		const TCHAR* Name;
		const TCHAR* Category;
		const TCHAR* Description;
		const TCHAR* ClassName;
	};

	static const FNodeTypeEntry CommonNodes[] =
	{
		// Flow Control
		{ TEXT("Branch"), TEXT("Flow Control"), TEXT("If/else conditional branch"), TEXT("K2Node_IfThenElse") },
		{ TEXT("Sequence"), TEXT("Flow Control"), TEXT("Execute multiple outputs in order"), TEXT("K2Node_ExecutionSequence") },
		{ TEXT("DoOnce"), TEXT("Flow Control"), TEXT("Execute only the first time"), TEXT("K2Node_DoOnceMultiInput") },
		{ TEXT("FlipFlop"), TEXT("Flow Control"), TEXT("Alternates between two outputs"), TEXT("K2Node_FlipFlop") },
		{ TEXT("Gate"), TEXT("Flow Control"), TEXT("Open/close execution gate"), TEXT("K2Node_Gate") },
		{ TEXT("ForEachLoop"), TEXT("Flow Control"), TEXT("Loop over array elements"), TEXT("K2Node_ForEachElementInEnum") },
		{ TEXT("WhileLoop"), TEXT("Flow Control"), TEXT("Loop while condition is true"), TEXT("K2Node_WhileLoop") },
		{ TEXT("Select"), TEXT("Flow Control"), TEXT("Select output based on index"), TEXT("K2Node_Select") },
		{ TEXT("Switch"), TEXT("Flow Control"), TEXT("Switch on value (int, string, enum, name)"), TEXT("K2Node_Switch") },
		{ TEXT("Delay"), TEXT("Flow Control"), TEXT("Wait for specified time before continuing"), TEXT("K2Node_Delay") },

		// Events
		{ TEXT("EventBeginPlay"), TEXT("Events"), TEXT("Called when play begins"), TEXT("K2Node_Event") },
		{ TEXT("EventTick"), TEXT("Events"), TEXT("Called every frame"), TEXT("K2Node_Event") },
		{ TEXT("EventActorBeginOverlap"), TEXT("Events"), TEXT("Called when an actor overlaps"), TEXT("K2Node_Event") },
		{ TEXT("EventHit"), TEXT("Events"), TEXT("Called when actor is hit"), TEXT("K2Node_Event") },
		{ TEXT("EventAnyDamage"), TEXT("Events"), TEXT("Called when any damage is received"), TEXT("K2Node_Event") },
		{ TEXT("CustomEvent"), TEXT("Events"), TEXT("User-defined custom event"), TEXT("K2Node_CustomEvent") },
		{ TEXT("EventDispatcher"), TEXT("Events"), TEXT("Multicast delegate event dispatcher"), TEXT("K2Node_CreateDelegate") },
		{ TEXT("InputAction"), TEXT("Events"), TEXT("Respond to input action"), TEXT("K2Node_InputAction") },
		{ TEXT("InputKey"), TEXT("Events"), TEXT("Respond to key press/release"), TEXT("K2Node_InputKey") },

		// Functions
		{ TEXT("CallFunction"), TEXT("Functions"), TEXT("Call a function by name"), TEXT("K2Node_CallFunction") },
		{ TEXT("PrintString"), TEXT("Functions"), TEXT("Print text to screen/log"), TEXT("K2Node_CallFunction") },
		{ TEXT("SpawnActor"), TEXT("Functions"), TEXT("Spawn an actor from class"), TEXT("K2Node_SpawnActorFromClass") },
		{ TEXT("DestroyActor"), TEXT("Functions"), TEXT("Destroy an actor"), TEXT("K2Node_CallFunction") },
		{ TEXT("GetAllActorsOfClass"), TEXT("Functions"), TEXT("Get all actors of a specific class"), TEXT("K2Node_CallFunction") },
		{ TEXT("SetTimer"), TEXT("Functions"), TEXT("Set a timer by function name or event"), TEXT("K2Node_CallFunction") },
		{ TEXT("ClearTimer"), TEXT("Functions"), TEXT("Clear/invalidate a timer"), TEXT("K2Node_CallFunction") },

		// Variables
		{ TEXT("VariableGet"), TEXT("Variables"), TEXT("Get the value of a variable"), TEXT("K2Node_VariableGet") },
		{ TEXT("VariableSet"), TEXT("Variables"), TEXT("Set the value of a variable"), TEXT("K2Node_VariableSet") },
		{ TEXT("MakeArray"), TEXT("Variables"), TEXT("Construct an array from elements"), TEXT("K2Node_MakeArray") },
		{ TEXT("MakeStruct"), TEXT("Variables"), TEXT("Construct a struct from members"), TEXT("K2Node_MakeStruct") },
		{ TEXT("BreakStruct"), TEXT("Variables"), TEXT("Break a struct into its members"), TEXT("K2Node_BreakStruct") },

		// Math
		{ TEXT("Add"), TEXT("Math"), TEXT("Add two values (int, float, vector)"), TEXT("K2Node_CallFunction") },
		{ TEXT("Subtract"), TEXT("Math"), TEXT("Subtract two values"), TEXT("K2Node_CallFunction") },
		{ TEXT("Multiply"), TEXT("Math"), TEXT("Multiply two values"), TEXT("K2Node_CallFunction") },
		{ TEXT("Divide"), TEXT("Math"), TEXT("Divide two values"), TEXT("K2Node_CallFunction") },
		{ TEXT("RandomFloat"), TEXT("Math"), TEXT("Generate random float in range"), TEXT("K2Node_CallFunction") },
		{ TEXT("Clamp"), TEXT("Math"), TEXT("Clamp value between min and max"), TEXT("K2Node_CallFunction") },
		{ TEXT("Lerp"), TEXT("Math"), TEXT("Linear interpolation"), TEXT("K2Node_CallFunction") },
		{ TEXT("VectorLength"), TEXT("Math"), TEXT("Get length of a vector"), TEXT("K2Node_CallFunction") },
		{ TEXT("Normalize"), TEXT("Math"), TEXT("Normalize a vector"), TEXT("K2Node_CallFunction") },

		// Casting & Type
		{ TEXT("Cast"), TEXT("Casting"), TEXT("Cast to a specific class"), TEXT("K2Node_DynamicCast") },
		{ TEXT("IsValid"), TEXT("Casting"), TEXT("Check if object reference is valid"), TEXT("K2Node_CallFunction") },
		{ TEXT("ClassIsChildOf"), TEXT("Casting"), TEXT("Check class inheritance"), TEXT("K2Node_CallFunction") },

		// String
		{ TEXT("Format"), TEXT("String"), TEXT("Format text with arguments"), TEXT("K2Node_FormatText") },
		{ TEXT("Append"), TEXT("String"), TEXT("Concatenate strings"), TEXT("K2Node_CallFunction") },
		{ TEXT("Contains"), TEXT("String"), TEXT("Check if string contains substring"), TEXT("K2Node_CallFunction") },

		// Utility
		{ TEXT("CreateWidget"), TEXT("Utility"), TEXT("Create a UMG widget instance"), TEXT("K2Node_CreateWidget") },
		{ TEXT("Macro"), TEXT("Utility"), TEXT("Instance of a macro graph"), TEXT("K2Node_MacroInstance") },
		{ TEXT("Comment"), TEXT("Utility"), TEXT("Comment box for organizing graphs"), TEXT("EdGraphNode_Comment") },
		{ TEXT("Reroute"), TEXT("Utility"), TEXT("Reroute node for cleaner wiring"), TEXT("K2Node_Knot") },
	};

	FString FilterCategory = OptionalString(Params, TEXT("category"));
	FString LowerFilter = FilterCategory.ToLower();

	TArray<TSharedPtr<FJsonValue>> NodeTypesArray;
	for (const FNodeTypeEntry& Entry : CommonNodes)
	{
		if (!LowerFilter.IsEmpty())
		{
			FString EntryCat = FString(Entry.Category).ToLower();
			if (!EntryCat.Contains(LowerFilter))
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("name"), Entry.Name);
		NodeObj->SetStringField(TEXT("category"), Entry.Category);
		NodeObj->SetStringField(TEXT("description"), Entry.Description);
		NodeObj->SetStringField(TEXT("className"), Entry.ClassName);
		NodeTypesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("nodeTypes"), NodeTypesArray);
	Result->SetNumberField(TEXT("count"), NodeTypesArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SearchCallableFunctions(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (auto Err = RequireString(Params, TEXT("query"), Query)) return Err;

	int32 MaxResults = OptionalInt(Params, TEXT("maxResults"), 50);

	FString LowerQuery = Query.ToLower();

	// Build the list of library classes to search
	TArray<UClass*> LibraryClasses;
	LibraryClasses.Add(UKismetSystemLibrary::StaticClass());
	LibraryClasses.Add(UGameplayStatics::StaticClass());
	LibraryClasses.Add(UKismetMathLibrary::StaticClass());
	LibraryClasses.Add(UKismetStringLibrary::StaticClass());
	LibraryClasses.Add(UKismetArrayLibrary::StaticClass());
	LibraryClasses.Add(AActor::StaticClass());
	LibraryClasses.Add(APawn::StaticClass());
	LibraryClasses.Add(APlayerController::StaticClass());

	// Optionally filter by a specific class
	FString TargetClassName = OptionalString(Params, TEXT("targetClass"));
	if (!TargetClassName.IsEmpty())
	{
		UClass* TargetClass = FindObject<UClass>(nullptr, *TargetClassName);
		if (!TargetClass)
		{
			TargetClass = FindObject<UClass>(nullptr, *(TEXT("U") + TargetClassName));
		}
		if (!TargetClass)
		{
			TargetClass = FindObject<UClass>(nullptr, *(TEXT("A") + TargetClassName));
		}
		if (TargetClass)
		{
			LibraryClasses.Empty();
			LibraryClasses.Add(TargetClass);
		}
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;

	for (UClass* SearchClass : LibraryClasses)
	{
		if (!SearchClass) continue;

		for (TFieldIterator<UFunction> FuncIt(SearchClass); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			if (!Func) continue;

			// Only include blueprint-callable functions
			if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure)) continue;

			FString FuncName = Func->GetName();
			FString LowerFuncName = FuncName.ToLower();

			if (!LowerFuncName.Contains(LowerQuery)) continue;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), FuncName);
			Entry->SetStringField(TEXT("class"), SearchClass->GetName());
			Entry->SetStringField(TEXT("fullPath"), Func->GetPathName());

			// Pure vs impure
			Entry->SetBoolField(TEXT("isPure"), Func->HasAnyFunctionFlags(FUNC_BlueprintPure));
			Entry->SetBoolField(TEXT("isStatic"), Func->HasAnyFunctionFlags(FUNC_Static));

			// Collect parameters info
			TArray<TSharedPtr<FJsonValue>> ParamsArray;
			FString ReturnType;
			for (TFieldIterator<FProperty> PropIt(Func); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (!Prop) continue;

				if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					ReturnType = Prop->GetCPPType();
				}
				else if (Prop->HasAnyPropertyFlags(CPF_Parm))
				{
					TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
					ParamObj->SetStringField(TEXT("name"), Prop->GetName());
					ParamObj->SetStringField(TEXT("type"), Prop->GetCPPType());
					ParamObj->SetBoolField(TEXT("isOutput"), Prop->HasAnyPropertyFlags(CPF_OutParm));
					ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
				}
			}
			Entry->SetArrayField(TEXT("parameters"), ParamsArray);
			if (!ReturnType.IsEmpty())
			{
				Entry->SetStringField(TEXT("returnType"), ReturnType);
			}

			// Tooltip from metadata
			FString Tooltip = Func->GetMetaData(TEXT("ToolTip"));
			if (!Tooltip.IsEmpty())
			{
				Entry->SetStringField(TEXT("tooltip"), Tooltip);
			}

			ResultsArray.Add(MakeShared<FJsonValueObject>(Entry));

			if (ResultsArray.Num() >= MaxResults)
			{
				break;
			}
		}

		if (ResultsArray.Num() >= MaxResults)
		{
			break;
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetNumberField(TEXT("count"), ResultsArray.Num());
	Result->SetStringField(TEXT("query"), Query);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ConnectPins(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString SourceNodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("sourceNodeId"), TEXT("sourceNode"), SourceNodeId)) return Err;

	FString SourcePinName;
	if (auto Err = RequireStringAlt(Params, TEXT("sourcePinName"), TEXT("sourcePin"), SourcePinName)) return Err;

	FString TargetNodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("targetNodeId"), TEXT("targetNode"), TargetNodeId)) return Err;

	FString TargetPinName;
	if (auto Err = RequireStringAlt(Params, TEXT("targetPinName"), TEXT("targetPin"), TargetPinName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Find source node
	UEdGraphNode* SourceNode = FindNodeByGuidOrName(TargetGraph, SourceNodeId);
	if (!SourceNode)
	{
		return MCPError(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeId));
	}

	// Find target node
	UEdGraphNode* TargetNode = FindNodeByGuidOrName(TargetGraph, TargetNodeId);
	if (!TargetNode)
	{
		return MCPError(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeId));
	}

	// Find source pin
	UEdGraphPin* SourcePin = nullptr;
	for (UEdGraphPin* Pin : SourceNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == SourcePinName)
		{
			SourcePin = Pin;
			break;
		}
	}
	if (!SourcePin)
	{
		return MCPError(FString::Printf(TEXT("Source pin not found: '%s' on node '%s'"), *SourcePinName, *SourceNodeId));
	}

	// Find target pin
	UEdGraphPin* TargetPin = nullptr;
	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == TargetPinName)
		{
			TargetPin = Pin;
			break;
		}
	}
	if (!TargetPin)
	{
		return MCPError(FString::Printf(TEXT("Target pin not found: '%s' on node '%s'"), *TargetPinName, *TargetNodeId));
	}

	// Idempotency: if already linked between these two pins, short-circuit
	if (SourcePin->LinkedTo.Contains(TargetPin))
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("path"), AssetPath);
		Existed->SetStringField(TEXT("graphName"), GraphName);
		Existed->SetStringField(TEXT("sourceNodeId"), SourceNodeId);
		Existed->SetStringField(TEXT("sourcePinName"), SourcePinName);
		Existed->SetStringField(TEXT("targetNodeId"), TargetNodeId);
		Existed->SetStringField(TEXT("targetPinName"), TargetPinName);
		return MCPResult(Existed);
	}

	// Use the graph's own schema (K2 for EventGraphs, AnimationGraph schema for AnimGraphs, etc.)
	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	if (!Schema)
	{
		return MCPError(TEXT("Graph has no schema"));
	}

	bool bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);

	if (bConnected)
	{
		// Compile and save
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("graphName"), GraphName);
		Result->SetStringField(TEXT("sourceNodeId"), SourceNodeId);
		Result->SetStringField(TEXT("sourcePinName"), SourcePinName);
		Result->SetStringField(TEXT("targetNodeId"), TargetNodeId);
		Result->SetStringField(TEXT("targetPinName"), TargetPinName);
		// No rollback: no paired disconnect_pins handler.
		return MCPResult(Result);
	}
	else
	{
		// Get more info about why it failed
		FString ErrorMsg = TEXT("TryCreateConnection failed. Pins may be incompatible.");
		FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
		if (!Response.Message.IsEmpty())
		{
			ErrorMsg = FString::Printf(TEXT("Connection failed: %s"), *Response.Message.ToString());
		}
		return MCPError(ErrorMsg);
	}
}

TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	UEdGraphNode* NodeToDelete = FindNodeByGuidOrName(TargetGraph, NodeId);
	if (!NodeToDelete)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("graphName"), GraphName);
		Noop->SetStringField(TEXT("nodeId"), NodeId);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	NodeToDelete->BreakAllNodeLinks();
	TargetGraph->RemoveNode(NodeToDelete);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetBoolField(TEXT("deleted"), true);
	// Not reversible by default.
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::SetNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;

	FString PinName;
	if (auto Err = RequireStringAlt(Params, TEXT("pinName"), TEXT("propertyName"), PinName)) return Err;

	FString DefaultValue;
	if (auto Err = RequireStringAlt(Params, TEXT("defaultValue"), TEXT("value"), DefaultValue)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Find the node
	UEdGraphNode* TargetNode = FindNodeByGuidOrName(TargetGraph, NodeId);
	if (!TargetNode)
	{
		return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));
	}

	// First try to find a pin with this name
	UEdGraphPin* TargetPin = nullptr;
	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinName)
		{
			TargetPin = Pin;
			break;
		}
	}

	bool bSetViaPin = false;
	bool bSetViaProperty = false;
	FString PrevValue;
	bool bHasPrevValue = false;

	if (TargetPin)
	{
		// Capture previous pin default for rollback
		PrevValue = TargetPin->DefaultValue;
		bHasPrevValue = true;
		// No-op short-circuit: pin default already matches
		if (PrevValue == DefaultValue)
		{
			auto Noop = MCPSuccess();
			MCPSetExisted(Noop);
			Noop->SetStringField(TEXT("path"), AssetPath);
			Noop->SetStringField(TEXT("graphName"), GraphName);
			Noop->SetStringField(TEXT("nodeId"), NodeId);
			Noop->SetStringField(TEXT("propertyName"), PinName);
			Noop->SetStringField(TEXT("value"), DefaultValue);
			return MCPResult(Noop);
		}
		// Set pin default value using the graph's own schema
		const UEdGraphSchema* Schema = TargetGraph->GetSchema();
		if (Schema)
		{
			Schema->TrySetDefaultValue(*TargetPin, DefaultValue);
			TargetNode->PinDefaultValueChanged(TargetPin);
			bSetViaPin = true;
		}
	}

	if (!bSetViaPin)
	{
		// No pin found -- try setting as a node property via reflection.
		// Supports dotted paths like "Node.IKBone.BoneName" for AnimGraph inner structs.
		TArray<FString> PathParts;
		PinName.ParseIntoArray(PathParts, TEXT("."));

		UStruct* CurrentStruct = TargetNode->GetClass();
		void* CurrentContainer = TargetNode;
		FProperty* FinalProp = nullptr;

		for (int32 i = 0; i < PathParts.Num(); i++)
		{
			FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
			if (!Prop) break;

			if (i < PathParts.Num() - 1)
			{
				// Intermediate path segment -- drill into struct
				FStructProperty* StructProp = CastField<FStructProperty>(Prop);
				if (!StructProp) break;
				CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = StructProp->Struct;
			}
			else
			{
				FinalProp = Prop;
			}
		}

		if (FinalProp)
		{
			void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			// ImportText_Direct returns nullptr on parse failure. Previously we
			// ignored that - a malformed DefaultValue silently corrupted the
			// node's default. Now we surface the failure as an error so callers
			// see the bad input immediately rather than at compile time.
			const TCHAR* Parsed = FinalProp->ImportText_Direct(*DefaultValue, ValuePtr, nullptr, PPF_None);
			if (Parsed == nullptr)
			{
				return MCPError(FString::Printf(
					TEXT("DefaultValue '%s' is not valid for property '%s' (type %s). Use UE's text format (e.g. `(X=1,Y=2,Z=3)` for FVector)."),
					*DefaultValue, *FinalProp->GetName(), *FinalProp->GetCPPType()));
			}
			TargetNode->PostEditChange();
			bSetViaProperty = true;
		}
	}

	if (!bSetViaPin && !bSetViaProperty)
	{
		// Neither pin nor property found -- build a helpful error
		TArray<FString> PinNames;
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (Pin) PinNames.Add(Pin->PinName.ToString());
		}

		TArray<FString> PropNames;
		for (TFieldIterator<FProperty> It(TargetNode->GetClass()); It; ++It)
		{
			PropNames.Add(It->GetName());
		}

		return MCPError(FString::Printf(
			TEXT("'%s' not found as pin or property. Pins: [%s]. Properties: [%s]"),
			*PinName, *FString::Join(PinNames, TEXT(", ")), *FString::Join(PropNames, TEXT(", "))));
	}

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetStringField(TEXT("propertyName"), PinName);
	Result->SetStringField(TEXT("value"), DefaultValue);
	Result->SetStringField(TEXT("setVia"), bSetViaPin ? TEXT("pin") : TEXT("property"));

	// Rollback: self-inverse with previous pin default value (pin path only; property path has no reliable previous capture)
	if (bSetViaPin && bHasPrevValue)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("path"), AssetPath);
		Payload->SetStringField(TEXT("graphName"), GraphName);
		Payload->SetStringField(TEXT("nodeId"), NodeId);
		Payload->SetStringField(TEXT("pinName"), PinName);
		Payload->SetStringField(TEXT("defaultValue"), PrevValue);
		MCPSetRollback(Result, TEXT("set_node_property"), Payload);
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// Resolve a component template on a Blueprint by name.
//
// Child Blueprints do NOT own inherited components in their own SCS — the
// component templates live on the parent Blueprint's SCS. Writing through
// the parent's template corrupts the parent for every descendant. For
// inherited components we must route writes through the child's
// UInheritableComponentHandler, which stores per-child override templates
// (equivalent of SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint
// in Python, as opposed to the read-only shared get_object).
//
// bForWrite=true: always returns a write-safe template. For inherited
//   components this means the child's ICH override (creating one if none
//   exists yet). Never returns the shared parent template for a write.
//
// bForWrite=false: returns the child's ICH override if one exists
//   (so reads reflect what writes would mutate), otherwise falls back to
//   the parent template (which holds the effective default).
//
// OutAvailable is populated with candidate component names (own SCS +
// inherited + CDO) for error messages when the lookup fails.
static UActorComponent* ResolveComponentTemplate(
	UBlueprint* Blueprint,
	const FString& ComponentName,
	bool bForWrite,
	bool& bOutIsInherited,
	TArray<FString>& OutAvailable)
{
	bOutIsInherited = false;
	OutAvailable.Reset();
	if (!Blueprint) return nullptr;

	// 1) Own SCS — child's own components, write directly.
	if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate) continue;
			OutAvailable.AddUnique(Node->GetVariableName().ToString());
			if (Node->GetVariableName().ToString() == ComponentName ||
				Node->ComponentTemplate->GetName() == ComponentName)
			{
				return Node->ComponentTemplate;
			}
		}
	}

	// 2) Walk parent BP chain for inherited SCS components.
	USCS_Node* InheritedNode = nullptr;
	UClass* ParentClass = Blueprint->ParentClass;
	while (ParentClass && !InheritedNode)
	{
		if (UBlueprint* ParentBP = Cast<UBlueprint>(ParentClass->ClassGeneratedBy))
		{
			if (USimpleConstructionScript* ParentSCS = ParentBP->SimpleConstructionScript)
			{
				for (USCS_Node* Node : ParentSCS->GetAllNodes())
				{
					if (!Node || !Node->ComponentTemplate) continue;
					OutAvailable.AddUnique(Node->GetVariableName().ToString());
					if (Node->GetVariableName().ToString() == ComponentName ||
						Node->ComponentTemplate->GetName() == ComponentName)
					{
						InheritedNode = Node;
						break;
					}
				}
			}
			ParentClass = ParentBP->ParentClass;
		}
		else
		{
			break;
		}
	}

	if (InheritedNode)
	{
		bOutIsInherited = true;
		FComponentKey Key(InheritedNode);

		// Look up any existing override on this specific child BP.
		UInheritableComponentHandler* ICH =
			Blueprint->GetInheritableComponentHandler(/*bCreateIfNecessary=*/bForWrite);
		UActorComponent* Override = ICH ? ICH->GetOverridenComponentTemplate(Key) : nullptr;

		if (bForWrite)
		{
			// Must never write through the shared parent template — that
			// would mutate the parent and every other descendant.
			if (!Override && ICH)
			{
				Override = ICH->CreateOverridenComponentTemplate(Key);
			}
			return Override; // null only if ICH creation failed
		}
		else
		{
			return Override ? Override : ToRawPtr(InheritedNode->ComponentTemplate);
		}
	}

	// 3) CDO fallback — catches native C++ components and anything the
	// SCS walk missed. Reads only; writes to these would need different
	// plumbing and are not supported here.
	if (Blueprint->GeneratedClass)
	{
		if (AActor* ActorCDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)))
		{
			TInlineComponentArray<UActorComponent*> Components;
			ActorCDO->GetComponents(Components);
			for (UActorComponent* C : Components)
			{
				if (!C) continue;
				OutAvailable.AddUnique(C->GetName());
				if (C->GetName() == ComponentName ||
					C->GetName().StartsWith(ComponentName + TEXT("_")) ||
					C->GetFName().ToString() == ComponentName)
				{
					// Reads of native components are fine. Writes via this
					// path are unsupported — caller should check the flag.
					return C;
				}
			}
		}
	}

	return nullptr;
}

// set_component_property -- Set a property on an SCS component template
// Params: assetPath, componentName, propertyName, value
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::SetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// #152: accept any JSON value type — scalars, numbers, booleans, or structured
	// objects like {x,y,z} for FVector. Previous impl only accepted strings, so
	// RelativeLocation etc. couldn't be set without pre-formatting "(X=1,Y=2,Z=3)".
	TSharedPtr<FJsonValue> ValueField = Params->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	bool bIsInherited = false;
	TArray<FString> Available;
	UActorComponent* Template = ResolveComponentTemplate(
		Blueprint, ComponentName, /*bForWrite=*/true, bIsInherited, Available);

	if (!Template)
	{
		return MCPError(FString::Printf(
			TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Available, TEXT(", "))));
	}

	// Walk dotted path to the final property. The helper from HandlerJsonProperty.h
	// does the assignment (handles FVector objects, object refs, arrays, etc.);
	// here we duplicate the walk once so we can also capture PrevValue for rollback.
	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = Template->GetClass();
	void* CurrentContainer = Template;
	FProperty* FinalProp = nullptr;

	for (int32 i = 0; i < PathParts.Num(); i++)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop) break;

		if (i < PathParts.Num() - 1)
		{
			FStructProperty* StructProp = CastField<FStructProperty>(Prop);
			if (!StructProp) break;
			CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProp->Struct;
		}
		else
		{
			FinalProp = Prop;
		}
	}

	if (!FinalProp)
	{
		TArray<FString> PropNames;
		for (TFieldIterator<FProperty> It(Template->GetClass()); It; ++It)
		{
			PropNames.Add(It->GetName());
		}
		return MCPError(FString::Printf(
			TEXT("Property '%s' not found on %s. Properties: [%s]"),
			*PropertyName, *Template->GetClass()->GetName(),
			*FString::Join(PropNames, TEXT(", "))));
	}

	void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);

	// Capture previous value for rollback via ExportText (always a string).
	FString PrevValue;
	FinalProp->ExportText_Direct(PrevValue, ValuePtr, ValuePtr, nullptr, PPF_None);

	Template->Modify();
	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(FinalProp, ValuePtr, ValueField, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	// Re-export for the rollback payload and no-op detection.
	FString NewValue;
	FinalProp->ExportText_Direct(NewValue, ValuePtr, ValuePtr, nullptr, PPF_None);
	if (NewValue == PrevValue)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("componentName"), ComponentName);
		Noop->SetStringField(TEXT("propertyName"), PropertyName);
		Noop->SetStringField(TEXT("value"), NewValue);
		return MCPResult(Noop);
	}

	Template->PostEditChange();

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), NewValue);
	Result->SetBoolField(TEXT("inherited"), bIsInherited);

	// Rollback: self-inverse with previous value
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("componentName"), ComponentName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	MCPSetRollback(Result, TEXT("set_component_property"), Payload);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_class_default -- Set a UPROPERTY on a Blueprint's Class Default Object
// Params: assetPath, propertyName, value
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::SetClassDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// Accept value as any JSON type (string, number, bool, object, array).
	// This enables setting TArray<FStruct> with nested UObject refs via JSON
	// instead of requiring arcane ImportText format strings (#196, #199).
	TSharedPtr<FJsonValue> ValueField = Params->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UClass* GenClass = Blueprint->GeneratedClass;
	if (!GenClass)
	{
		return MCPError(TEXT("Blueprint has no GeneratedClass (needs compilation first?)"));
	}

	UObject* CDO = GenClass->GetDefaultObject();
	if (!CDO)
	{
		return MCPError(TEXT("Could not get Class Default Object"));
	}

	// Navigate dotted property paths (e.g. "EjectConfigs.Cork.Force")
	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = GenClass;
	void* CurrentContainer = CDO;
	FProperty* FinalProp = nullptr;

	for (int32 i = 0; i < PathParts.Num(); i++)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop) break;

		if (i < PathParts.Num() - 1)
		{
			FStructProperty* StructProp = CastField<FStructProperty>(Prop);
			if (!StructProp) break;
			CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProp->Struct;
		}
		else
		{
			FinalProp = Prop;
		}
	}

	if (!FinalProp)
	{
		TArray<FString> PropNames;
		for (TFieldIterator<FProperty> It(GenClass); It; ++It)
		{
			PropNames.Add(It->GetName());
		}
		return MCPError(FString::Printf(
			TEXT("Property '%s' not found on %s. Properties: [%s]"),
			*PropertyName, *GenClass->GetName(),
			*FString::Join(PropNames, TEXT(", "))));
	}

	void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);

	// Capture previous value for rollback and idempotency
	FString PrevValue;
	FinalProp->ExportText_Direct(PrevValue, ValuePtr, ValuePtr, nullptr, PPF_None);

	CDO->Modify();
	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(FinalProp, ValuePtr, ValueField, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	// Re-export for rollback payload and no-op detection
	FString NewValue;
	FinalProp->ExportText_Direct(NewValue, ValuePtr, ValuePtr, nullptr, PPF_None);
	if (NewValue == PrevValue)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("propertyName"), PropertyName);
		Noop->SetStringField(TEXT("value"), NewValue);
		return MCPResult(Noop);
	}

	CDO->PostEditChange();

	// Save
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), NewValue);

	// Rollback: self-inverse with previous value
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	MCPSetRollback(Result, TEXT("set_class_default"), Payload);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// remove_component -- Remove an SCS component from a Blueprint
// Params: assetPath, componentName
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::RemoveComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		return MCPError(TEXT("Blueprint has no SimpleConstructionScript (not an Actor blueprint?)"));
	}

	// Find the SCS node by variable name or component template name
	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (!Node || !Node->ComponentTemplate) continue;
		if (Node->GetVariableName().ToString() == ComponentName ||
			Node->ComponentTemplate->GetName() == ComponentName)
		{
			TargetNode = Node;
			break;
		}
	}

	// Idempotent: nothing to remove is a no-op.
	if (!TargetNode)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("componentName"), ComponentName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	// Remove via SubobjectDataSubsystem if available
	bool bRemoved = false;
	if (USubobjectDataSubsystem* Subsystem = GEngine->GetEngineSubsystem<USubobjectDataSubsystem>())
	{
		TArray<FSubobjectDataHandle> Handles;
		Subsystem->K2_GatherSubobjectDataForBlueprint(Blueprint, Handles);

		FSubobjectDataHandle ContextHandle = Handles.Num() > 0 ? Handles[0] : FSubobjectDataHandle();
		for (const FSubobjectDataHandle& Handle : Handles)
		{
			const FSubobjectData* Data = Handle.GetData();
			if (Data && Data->GetComponentTemplate() == TargetNode->ComponentTemplate)
			{
				TArray<FSubobjectDataHandle> ToDelete;
				ToDelete.Add(Handle);
				int32 Removed = Subsystem->DeleteSubobjects(ContextHandle, ToDelete, Blueprint);
				bRemoved = (Removed > 0);
				break;
			}
		}
	}

	// Fallback: direct SCS removal
	if (!bRemoved)
	{
		SCS->RemoveNode(TargetNode);
		bRemoved = true;
	}

	if (bRemoved)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), AssetPath);
		Result->SetStringField(TEXT("componentName"), ComponentName);
		Result->SetBoolField(TEXT("deleted"), true);
		// No rollback: component removal is not reversible by default.
		return MCPResult(Result);
	}
	else
	{
		return MCPError(TEXT("Failed to remove component"));
	}
}

// ---------------------------------------------------------------------------
// delete_variable -- Delete a member variable from a Blueprint
// Params: assetPath, name
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::DeleteVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	bool bFound = false;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName.ToString() == VarName)
		{
			bFound = true;
			break;
		}
	}

	// Idempotent: nothing to delete is a no-op.
	if (!bFound)
	{
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("variableName"), VarName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VarName));

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("variableName"), VarName);
	Result->SetBoolField(TEXT("deleted"), true);
	// No rollback: variable deletion is not reversible by default.
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// add_function_parameter -- Add an input or output parameter to a Blueprint function
// Params: assetPath, functionName, parameterName, parameterType, isOutput?
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::AddFunctionParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	FString ParamName;
	if (auto Err = RequireString(Params, TEXT("parameterName"), ParamName)) return Err;

	FString ParamType = OptionalString(Params, TEXT("parameterType"), TEXT("Float"));

	bool bIsOutput = OptionalBool(Params, TEXT("isOutput"), false);

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the function graph
	UEdGraph* FuncGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == FunctionName)
		{
			FuncGraph = Graph;
			break;
		}
	}

	if (!FuncGraph)
	{
		return MCPError(FString::Printf(TEXT("Function not found: %s"), *FunctionName));
	}

	// Find the function entry node (K2Node_FunctionEntry) or result node
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : FuncGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
		{
			EntryNode = Entry;
			break;
		}
	}

	if (!EntryNode)
	{
		return MCPError(TEXT("Function entry node not found in graph"));
	}

	// Idempotency: check if parameter already exists
	const FName ParamFName(*ParamName);
	if (!bIsOutput)
	{
		for (const TSharedPtr<FUserPinInfo>& Info : EntryNode->UserDefinedPins)
		{
			if (Info.IsValid() && Info->PinName == ParamFName)
			{
				auto Existed = MCPSuccess();
				MCPSetExisted(Existed);
				Existed->SetStringField(TEXT("path"), AssetPath);
				Existed->SetStringField(TEXT("functionName"), FunctionName);
				Existed->SetStringField(TEXT("parameterName"), ParamName);
				Existed->SetBoolField(TEXT("isOutput"), false);
				return MCPResult(Existed);
			}
		}
	}
	else
	{
		for (UEdGraphNode* Node : FuncGraph->Nodes)
		{
			if (Node && Node->GetClass()->GetName() == TEXT("K2Node_FunctionResult"))
			{
				if (UK2Node_EditablePinBase* R = Cast<UK2Node_EditablePinBase>(Node))
				{
					for (const TSharedPtr<FUserPinInfo>& Info : R->UserDefinedPins)
					{
						if (Info.IsValid() && Info->PinName == ParamFName)
						{
							auto Existed = MCPSuccess();
							MCPSetExisted(Existed);
							Existed->SetStringField(TEXT("path"), AssetPath);
							Existed->SetStringField(TEXT("functionName"), FunctionName);
							Existed->SetStringField(TEXT("parameterName"), ParamName);
							Existed->SetBoolField(TEXT("isOutput"), true);
							return MCPResult(Existed);
						}
					}
				}
			}
		}
	}

	FEdGraphPinType PinType = MakePinType(ParamType);

	if (PinType.PinCategory == NAME_None)
	{
		return MCPError(FString::Printf(TEXT("Unrecognized parameter type: '%s'. Use a known type (Bool, Int, Float, String, Name, Text, Byte, Object, Vector, Rotator, Transform, GameplayTag, etc.) or a full class/struct path."), *ParamType));
	}

	if (bIsOutput)
	{
		// For output parameters, find or create the function result node
		UK2Node_FunctionEntry* ResultAsEntry = nullptr; // K2Node_FunctionResult also inherits UK2Node_EditablePinBase
		UK2Node_EditablePinBase* ResultNode = nullptr;
		for (UEdGraphNode* Node : FuncGraph->Nodes)
		{
			if (Node->GetClass()->GetName() == TEXT("K2Node_FunctionResult"))
			{
				ResultNode = Cast<UK2Node_EditablePinBase>(Node);
				break;
			}
		}

		if (!ResultNode)
		{
			// Create a result node
			UClass* ResultNodeClass = nullptr;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName() == TEXT("K2Node_FunctionResult") && It->IsChildOf(UEdGraphNode::StaticClass()))
				{
					ResultNodeClass = *It;
					break;
				}
			}
			if (ResultNodeClass)
			{
				UEdGraphNode* NewResultNode = NewObject<UEdGraphNode>(FuncGraph, ResultNodeClass);
				FuncGraph->AddNode(NewResultNode, false, false);
				NewResultNode->CreateNewGuid();
				NewResultNode->PostPlacedNewNode();
				NewResultNode->AllocateDefaultPins();
				ResultNode = Cast<UK2Node_EditablePinBase>(NewResultNode);
			}
		}

		if (ResultNode)
		{
			TSharedPtr<FUserPinInfo> PinInfo = MakeShared<FUserPinInfo>();
			PinInfo->PinName = FName(*ParamName);
			PinInfo->PinType = PinType;
			PinInfo->DesiredPinDirection = EGPD_Input;
			ResultNode->UserDefinedPins.Add(PinInfo);
			ResultNode->ReconstructNode();
		}
	}
	else
	{
		// Input parameter: add a user-defined pin to the function entry node
		TSharedPtr<FUserPinInfo> PinInfo = MakeShared<FUserPinInfo>();
		PinInfo->PinName = FName(*ParamName);
		PinInfo->PinType = PinType;
		PinInfo->DesiredPinDirection = EGPD_Output; // Entry outputs are function inputs
		EntryNode->UserDefinedPins.Add(PinInfo);
		EntryNode->ReconstructNode();
	}

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("parameterName"), ParamName);
	Result->SetStringField(TEXT("parameterType"), ParamType);
	Result->SetBoolField(TEXT("isOutput"), bIsOutput);
	// No rollback: no paired remove_function_parameter handler yet.
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_variable_default -- Set the default value of a Blueprint variable
// Bypasses CDO restrictions by setting via FBlueprintEditorUtils on the BP variable description
// Params: assetPath, name, value
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::SetVariableDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;

	FString Value;
	if (auto Err = RequireString(Params, TEXT("value"), Value)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	// Find the variable description
	FBPVariableDescription* FoundVar = nullptr;
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName.ToString() == VarName)
		{
			FoundVar = &Var;
			break;
		}
	}

	if (!FoundVar)
	{
		TArray<FString> Names;
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			Names.Add(Var.VarName.ToString());
		}
		return MCPError(FString::Printf(
			TEXT("Variable '%s' not found. Available: [%s]"),
			*VarName, *FString::Join(Names, TEXT(", "))));
	}

	// Capture previous value for rollback and idempotency
	const FString PrevValue = FoundVar->DefaultValue;
	if (PrevValue == Value)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("path"), AssetPath);
		Noop->SetStringField(TEXT("variableName"), VarName);
		Noop->SetStringField(TEXT("value"), Value);
		return MCPResult(Noop);
	}

	// Set default value string on the variable description.
	// This is the text representation that the BP serialization system uses.
	FoundVar->DefaultValue = Value;

	// Also try to set it on the CDO property if possible (for immediate reflection)
	if (Blueprint->GeneratedClass)
	{
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		if (CDO)
		{
			FProperty* Prop = Blueprint->GeneratedClass->FindPropertyByName(FName(*VarName));
			if (Prop)
			{
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);

				if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
				{
					UClass* ClassVal = LoadObject<UClass>(nullptr, *Value);
					if (!ClassVal) ClassVal = FindClassByShortName(Value);
					if (ClassVal) ClassProp->SetObjectPropertyValue(ValuePtr, ClassVal);
				}
				else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
				{
					UObject* LoadedObj = LoadObject<UObject>(nullptr, *Value);
					if (LoadedObj) ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
				}
				else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
				{
					// For arrays (including TArray<TSubclassOf<>>), use ImportText.
					// A parse failure here just means the CDO mirror did not take;
					// the authoritative string default set on FoundVar->DefaultValue
					// above still applies on the next compile, so we do not reject
					// the whole request, but the caller gets a warning.
					if (!Prop->ImportText_Direct(*Value, ValuePtr, CDO, PPF_None))
					{
						UE_LOG(LogTemp, Warning, TEXT("set_blueprint_variable_default_value: ImportText_Direct failed for array property '%s' value '%s' - default string was still written and will take effect on recompile."), *VarName, *Value);
					}
				}
				else
				{
					if (!Prop->ImportText_Direct(*Value, ValuePtr, CDO, PPF_None))
					{
						UE_LOG(LogTemp, Warning, TEXT("set_blueprint_variable_default_value: ImportText_Direct failed for property '%s' value '%s' - default string was still written and will take effect on recompile."), *VarName, *Value);
					}
				}

				CDO->PostEditChange();
			}
		}
	}

	// Compile and save
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("variableName"), VarName);
	Result->SetStringField(TEXT("value"), Value);

	// Rollback: self-inverse with previous value
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetStringField(TEXT("name"), VarName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	MCPSetRollback(Result, TEXT("set_variable_default"), Payload);

	return MCPResult(Result);
}

// ===========================================================================
// v0.7.11 — Blueprint authoring depth
// ===========================================================================

TSharedPtr<FJsonValue> FBlueprintHandlers::DuplicateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (auto Err = RequireString(Params, TEXT("sourcePath"), SourcePath)) return Err;
	FString DestinationPath;
	if (auto Err = RequireString(Params, TEXT("destinationPath"), DestinationPath)) return Err;

	UObject* Dup = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestinationPath);
	if (!Dup) return MCPError(FString::Printf(TEXT("Failed to duplicate '%s'"), *SourcePath));

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("sourcePath"), SourcePath);
	Result->SetStringField(TEXT("destinationPath"), Dup->GetPathName());
	MCPSetDeleteAssetRollback(Result, Dup->GetPathName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::AddLocalVariable(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;
	FString VarName;
	if (auto Err = RequireString(Params, TEXT("name"), VarName)) return Err;
	FString TypeStr = OptionalString(Params, TEXT("varType"), TEXT("bool"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	// Find the function graph and its FunctionEntry node.
	UEdGraph* FuncGraph = nullptr;
	for (UEdGraph* G : Blueprint->FunctionGraphs)
	{
		if (G && G->GetName() == FunctionName) { FuncGraph = G; break; }
	}
	if (!FuncGraph) return MCPError(FString::Printf(TEXT("Function not found: %s"), *FunctionName));

	UK2Node_FunctionEntry* Entry = nullptr;
	for (UEdGraphNode* Node : FuncGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* E = Cast<UK2Node_FunctionEntry>(Node)) { Entry = E; break; }
	}
	if (!Entry) return MCPError(TEXT("Function has no entry node"));

	// Idempotency: check if local variable already exists on the entry node
	const FName VarFName(*VarName);
	for (const FBPVariableDescription& Existing : Entry->LocalVariables)
	{
		if (Existing.VarName == VarFName)
		{
			auto ExistedRes = MCPSuccess();
			MCPSetExisted(ExistedRes);
			ExistedRes->SetStringField(TEXT("path"), AssetPath);
			ExistedRes->SetStringField(TEXT("functionName"), FunctionName);
			ExistedRes->SetStringField(TEXT("name"), VarName);
			return MCPResult(ExistedRes);
		}
	}

	FEdGraphPinType PinType = MakePinType(TypeStr);

	if (PinType.PinCategory == NAME_None)
	{
		return MCPError(FString::Printf(TEXT("Unrecognized variable type: '%s'. Use a known type (Bool, Int, Float, String, Name, Text, Byte, Object, Vector, Rotator, Transform, GameplayTag, etc.) or a full class/struct path."), *TypeStr));
	}

	FBPVariableDescription NewVar;
	NewVar.VarName = VarFName;
	NewVar.VarGuid = FGuid::NewGuid();
	NewVar.VarType = PinType;
	NewVar.FriendlyName = VarName;
	Entry->Modify();
	Entry->LocalVariables.Add(NewVar);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetStringField(TEXT("name"), VarName);
	// No rollback: no paired remove_local_variable handler yet.
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ListLocalVariables(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString FunctionName;
	if (auto Err = RequireString(Params, TEXT("functionName"), FunctionName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	UEdGraph* FuncGraph = nullptr;
	for (UEdGraph* G : Blueprint->FunctionGraphs)
	{
		if (G && G->GetName() == FunctionName) { FuncGraph = G; break; }
	}
	if (!FuncGraph) return MCPError(FString::Printf(TEXT("Function not found: %s"), *FunctionName));

	UK2Node_FunctionEntry* Entry = nullptr;
	for (UEdGraphNode* Node : FuncGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* E = Cast<UK2Node_FunctionEntry>(Node)) { Entry = E; break; }
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	if (Entry)
	{
		for (const FBPVariableDescription& Var : Entry->LocalVariables)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), Var.VarName.ToString());
			O->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("functionName"), FunctionName);
	Result->SetArrayField(TEXT("variables"), Arr);
	Result->SetNumberField(TEXT("variableCount"), Arr.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ValidateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	// Run compile without saving; collect diagnostics from the compiler result log.
	FCompilerResultsLog Log;
	Log.bSilentMode = true;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave, &Log);

	TArray<TSharedPtr<FJsonValue>> Errors;
	for (TSharedRef<FTokenizedMessage> Msg : Log.Messages)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("severity"), Msg->GetSeverity() == EMessageSeverity::Error ? TEXT("Error")
			: Msg->GetSeverity() == EMessageSeverity::Warning ? TEXT("Warning") : TEXT("Info"));
		O->SetStringField(TEXT("message"), Msg->ToText().ToString());
		Errors.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetNumberField(TEXT("errorCount"), Log.NumErrors);
	Result->SetNumberField(TEXT("warningCount"), Log.NumWarnings);
	Result->SetBoolField(TEXT("valid"), Log.NumErrors == 0);
	Result->SetArrayField(TEXT("messages"), Errors);
	return MCPResult(Result);
}

// ─── #105 read_component_properties ─────────────────────────────────
TSharedPtr<FJsonValue> FBlueprintHandlers::ReadComponentProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	bool bIsInherited = false;
	TArray<FString> Available;
	UActorComponent* Template = ResolveComponentTemplate(
		Blueprint, ComponentName, /*bForWrite=*/false, bIsInherited, Available);
	if (!Template)
	{
		return MCPError(FString::Printf(
			TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Available, TEXT(", "))));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("componentName"), Template->GetName());
	Result->SetStringField(TEXT("componentClass"), Template->GetClass()->GetName());
	Result->SetBoolField(TEXT("inherited"), bIsInherited);

	TArray<TSharedPtr<FJsonValue>> PropsArr;
	for (TFieldIterator<FProperty> It(Template->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;

		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Prop->GetName());
		P->SetStringField(TEXT("type"), Prop->GetCPPType());

		FString ValueStr;
		const void* ValPtr = Prop->ContainerPtrToValuePtr<void>(Template);
		Prop->ExportText_Direct(ValueStr, ValPtr, ValPtr, Template, PPF_None);
		P->SetStringField(TEXT("value"), ValueStr);

		if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(ArrProp, ValPtr);
			P->SetNumberField(TEXT("count"), Helper.Num());
			TArray<TSharedPtr<FJsonValue>> Elems;
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				FString ElemStr;
				ArrProp->Inner->ExportText_Direct(ElemStr, Helper.GetRawPtr(i), Helper.GetRawPtr(i), Template, PPF_None);
				Elems.Add(MakeShared<FJsonValueString>(ElemStr));
			}
			P->SetArrayField(TEXT("elements"), Elems);
		}
		PropsArr.Add(MakeShared<FJsonValueObject>(P));
	}
	Result->SetArrayField(TEXT("properties"), PropsArr);
	Result->SetNumberField(TEXT("propertyCount"), PropsArr.Num());
	return MCPResult(Result);
}

// ─── #102 read_node_property ────────────────────────────────────────
TSharedPtr<FJsonValue> FBlueprintHandlers::ReadNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));
	FString NodeId;
	if (auto Err = RequireStringAlt(Params, TEXT("nodeId"), TEXT("nodeName"), NodeId)) return Err;
	FString PinOrProp;
	if (auto Err = RequireStringAlt(Params, TEXT("propertyName"), TEXT("pinName"), PinOrProp)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(TEXT("Blueprint not found"));
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph) return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	UEdGraphNode* Node = FindNodeByGuidOrName(Graph, NodeId);
	if (!Node) return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));

	auto Result = MCPSuccess();
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinOrProp)
		{
			Result->SetStringField(TEXT("pinName"), PinOrProp);
			Result->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
			if (Pin->DefaultObject)
			{
				Result->SetStringField(TEXT("defaultObject"), Pin->DefaultObject->GetPathName());
			}
			return MCPResult(Result);
		}
	}

	TArray<FString> Parts; PinOrProp.ParseIntoArray(Parts, TEXT("."));
	UStruct* Cur = Node->GetClass();
	void* Container = Node;
	FProperty* Final = nullptr;
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		FProperty* P = Cur->FindPropertyByName(FName(*Parts[i]));
		if (!P) return MCPError(FString::Printf(TEXT("Property '%s' not found"), *Parts[i]));
		if (i < Parts.Num() - 1)
		{
			FStructProperty* SP = CastField<FStructProperty>(P);
			if (!SP) return MCPError(FString::Printf(TEXT("Not a struct: %s"), *Parts[i]));
			Container = SP->ContainerPtrToValuePtr<void>(Container);
			Cur = SP->Struct;
		}
		else Final = P;
	}
	if (!Final) return MCPError(TEXT("Property path unresolved"));

	FString ValStr;
	const void* ValPtr = Final->ContainerPtrToValuePtr<void>(Container);
	Final->ExportText_Direct(ValStr, ValPtr, ValPtr, Node, PPF_None);
	Result->SetStringField(TEXT("propertyName"), PinOrProp);
	Result->SetStringField(TEXT("type"), Final->GetCPPType());
	Result->SetStringField(TEXT("value"), ValStr);
	return MCPResult(Result);
}

// ─── #115 reparent_component ────────────────────────────────────────
TSharedPtr<FJsonValue> FBlueprintHandlers::ReparentComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;
	FString NewParent;
	if (auto Err = RequireString(Params, TEXT("newParent"), NewParent)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(TEXT("Blueprint not found"));
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS) return MCPError(TEXT("Blueprint has no SCS"));

	USCS_Node* Child = nullptr; USCS_Node* Parent = nullptr;
	for (USCS_Node* N : SCS->GetAllNodes())
	{
		if (!N) continue;
		if (N->GetVariableName().ToString() == ComponentName) Child = N;
		if (N->GetVariableName().ToString() == NewParent) Parent = N;
	}
	if (!Child) return MCPError(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
	if (!Parent) return MCPError(FString::Printf(TEXT("Parent not found: %s"), *NewParent));

	SCS->RemoveNode(Child);
	Parent->AddChildNode(Child);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("newParent"), NewParent);
	return MCPResult(Result);
}

// ─── #138 reparent_blueprint ────────────────────────────────────────
// Changes a Blueprint's ParentClass (equivalent to
// unreal.BlueprintEditorLibrary.reparent_blueprint + compile + save).
TSharedPtr<FJsonValue> FBlueprintHandlers::ReparentBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString ParentClassName;
	if (auto Err = RequireString(Params, TEXT("parentClass"), ParentClassName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	// Resolve parent class: full path > short name > engine-module implicit.
	UClass* NewParent = nullptr;
	if (ParentClassName.Contains(TEXT("/")) || ParentClassName.Contains(TEXT(".")))
	{
		NewParent = LoadObject<UClass>(nullptr, *ParentClassName);
	}
	if (!NewParent)
	{
		NewParent = FindClassByShortName(ParentClassName);
	}
	if (!NewParent)
	{
		return MCPError(FString::Printf(TEXT("Parent class not found: '%s'. Try the full path ('/Script/Engine.Actor') or the bare class name."), *ParentClassName));
	}

	// Reject invalid parents to avoid engine-side asserts
	if (NewParent->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return MCPError(FString::Printf(TEXT("Parent class '%s' is deprecated or superseded"), *NewParent->GetPathName()));
	}
	if (Blueprint->GeneratedClass && NewParent == Blueprint->GeneratedClass)
	{
		return MCPError(TEXT("Cannot reparent a Blueprint to its own generated class"));
	}
	if (NewParent->IsChildOf(Blueprint->GeneratedClass))
	{
		return MCPError(TEXT("Cannot reparent to a subclass of this Blueprint (cycle)"));
	}

	UClass* OldParent = Blueprint->ParentClass;
	if (OldParent == NewParent)
	{
		auto NoOp = MCPSuccess();
		MCPSetExisted(NoOp);
		NoOp->SetStringField(TEXT("path"), AssetPath);
		NoOp->SetStringField(TEXT("parentClass"), NewParent->GetPathName());
		return MCPResult(NoOp);
	}

	// Prefer the canonical UBlueprintEditorLibrary path (matches the Python API
	// users have been falling back to in the workaround).
	UBlueprintEditorLibrary::ReparentBlueprint(Blueprint, NewParent);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("parentClass"), NewParent->GetPathName());
	if (OldParent)
	{
		Result->SetStringField(TEXT("previousParent"), OldParent->GetPathName());
	}
	return MCPResult(Result);
}

// ─── #116 set_actor_tick_settings ───────────────────────────────────
TSharedPtr<FJsonValue> FBlueprintHandlers::SetActorTickSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint || !Blueprint->GeneratedClass) return MCPError(TEXT("Blueprint not found or not compiled"));
	AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(true));
	if (!CDO) return MCPError(TEXT("Blueprint is not an Actor"));

	bool bCanEverTick = CDO->PrimaryActorTick.bCanEverTick;
	bool bStartWithTickEnabled = CDO->PrimaryActorTick.bStartWithTickEnabled;
	double TickInterval = CDO->PrimaryActorTick.TickInterval;

	Params->TryGetBoolField(TEXT("bCanEverTick"), bCanEverTick);
	Params->TryGetBoolField(TEXT("bStartWithTickEnabled"), bStartWithTickEnabled);
	Params->TryGetNumberField(TEXT("TickInterval"), TickInterval);

	CDO->PrimaryActorTick.bCanEverTick = bCanEverTick;
	CDO->PrimaryActorTick.bStartWithTickEnabled = bStartWithTickEnabled;
	CDO->PrimaryActorTick.TickInterval = (float)TickInterval;

	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("bCanEverTick"), bCanEverTick);
	Result->SetBoolField(TEXT("bStartWithTickEnabled"), bStartWithTickEnabled);
	Result->SetNumberField(TEXT("TickInterval"), TickInterval);
	return MCPResult(Result);
}

// ─── #128 get_component_property — inherited-aware single-prop read ──
// Params: assetPath, componentName, propertyName
// Returns the effective default for the given child BP: the ICH override
// if one exists, otherwise the parent template value. Supports dotted
// property paths ("RelativeLocation.X").
TSharedPtr<FJsonValue> FBlueprintHandlers::GetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;
	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

	bool bIsInherited = false;
	TArray<FString> Available;
	UActorComponent* Template = ResolveComponentTemplate(
		Blueprint, ComponentName, /*bForWrite=*/false, bIsInherited, Available);

	if (!Template)
	{
		return MCPError(FString::Printf(
			TEXT("Component '%s' not found. Available: [%s]"),
			*ComponentName, *FString::Join(Available, TEXT(", "))));
	}

	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = Template->GetClass();
	void* CurrentContainer = Template;
	FProperty* FinalProp = nullptr;

	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop)
		{
			return MCPError(FString::Printf(
				TEXT("Property '%s' not found on %s"), *PathParts[i], *CurrentStruct->GetName()));
		}
		if (i < PathParts.Num() - 1)
		{
			FStructProperty* StructProp = CastField<FStructProperty>(Prop);
			if (!StructProp)
			{
				return MCPError(FString::Printf(TEXT("Not a struct: %s"), *PathParts[i]));
			}
			CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProp->Struct;
		}
		else
		{
			FinalProp = Prop;
		}
	}
	if (!FinalProp) return MCPError(TEXT("Property path unresolved"));

	FString ValueStr;
	const void* ValPtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);
	FinalProp->ExportText_Direct(ValueStr, ValPtr, ValPtr, Template, PPF_None);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("componentName"), ComponentName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("type"), FinalProp->GetCPPType());
	Result->SetStringField(TEXT("value"), ValueStr);
	Result->SetBoolField(TEXT("inherited"), bIsInherited);
	Result->SetStringField(TEXT("templateClass"), Template->GetClass()->GetName());
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// v0.7.17 issue #130: bulk graph node import via T3D copy/paste.
// Mirrors the editor's Ctrl+C / Ctrl+V flow (FBlueprintEditor::CopySelectedNodes
// / PasteNodesHere) so callers can author whole subgraphs offline and import
// them atomically rather than building one node at a time.
// ---------------------------------------------------------------------------

TSharedPtr<FJsonValue> FBlueprintHandlers::ExportNodesT3D(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Collect nodes to export. If nodeIds is omitted/empty, export all nodes
	// in the graph (whole-subgraph round-trip).
	TArray<UEdGraphNode*> SelectedNodes;
	const TArray<TSharedPtr<FJsonValue>>* IdsArrayPtr = nullptr;
	if (Params.IsValid() && Params->TryGetArrayField(TEXT("nodeIds"), IdsArrayPtr) && IdsArrayPtr && IdsArrayPtr->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& Val : *IdsArrayPtr)
		{
			if (!Val.IsValid()) continue;
			const FString NodeId = Val->AsString();
			if (NodeId.IsEmpty()) continue;
			UEdGraphNode* Node = FindNodeByGuidOrName(TargetGraph, NodeId);
			if (!Node)
			{
				return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeId));
			}
			SelectedNodes.AddUnique(Node);
		}
	}
	else
	{
		for (UEdGraphNode* Node : TargetGraph->Nodes)
		{
			if (Node) SelectedNodes.Add(Node);
		}
	}

	if (SelectedNodes.Num() == 0)
	{
		return MCPError(TEXT("No nodes to export"));
	}

	// FEdGraphUtilities::ExportNodesToText only writes nodes that are flagged
	// CanDuplicateNode == true; mirrors how the editor's Copy filters root
	// entry/return nodes. Pre-filter so the count we report matches reality.
	TSet<UObject*> NodeSet;
	int32 SkippedCount = 0;
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (Node && Node->CanDuplicateNode())
		{
			Node->PrepareForCopying();
			NodeSet.Add(Node);
		}
		else
		{
			++SkippedCount;
		}
	}

	if (NodeSet.Num() == 0)
	{
		return MCPError(TEXT("No nodes are duplicatable (entry/return nodes cannot be exported)"));
	}

	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(NodeSet, ExportedText);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetStringField(TEXT("t3d"), ExportedText);
	Result->SetNumberField(TEXT("count"), NodeSet.Num());
	Result->SetNumberField(TEXT("skipped"), SkippedCount);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FBlueprintHandlers::ImportNodesT3D(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString GraphName = OptionalString(Params, TEXT("graphName"), TEXT("EventGraph"));

	FString T3D;
	if (auto Err = RequireStringAlt(Params, TEXT("t3d"), TEXT("text"), T3D)) return Err;
	if (T3D.IsEmpty())
	{
		return MCPError(TEXT("t3d text is empty"));
	}

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* TargetGraph = FindGraph(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return MCPError(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	if (!FEdGraphUtilities::CanImportNodesFromText(TargetGraph, T3D))
	{
		return MCPError(TEXT("T3D text is not importable into this graph (schema mismatch or malformed)"));
	}

	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(TargetGraph, T3D, /*out*/ PastedNodes);

	if (PastedNodes.Num() == 0)
	{
		return MCPError(TEXT("ImportNodesFromText produced no nodes"));
	}

	// Re-center pasted nodes around an explicit (posX, posY) anchor when given,
	// otherwise keep their exported positions. Mirrors PasteNodesHere.
	const bool bRecenter = Params.IsValid() && Params->HasField(TEXT("posX")) && Params->HasField(TEXT("posY"));
	double AnchorX = 0.0, AnchorY = 0.0;
	if (bRecenter)
	{
		Params->TryGetNumberField(TEXT("posX"), AnchorX);
		Params->TryGetNumberField(TEXT("posY"), AnchorY);

		double AvgX = 0.0, AvgY = 0.0;
		for (UEdGraphNode* Node : PastedNodes)
		{
			AvgX += Node->NodePosX;
			AvgY += Node->NodePosY;
		}
		AvgX /= PastedNodes.Num();
		AvgY /= PastedNodes.Num();

		for (UEdGraphNode* Node : PastedNodes)
		{
			Node->NodePosX = (Node->NodePosX - AvgX) + AnchorX;
			Node->NodePosY = (Node->NodePosY - AvgY) + AnchorY;
		}
	}

	// Fresh GUIDs so the pasted nodes don't collide with the originals when
	// pasting back into the same graph (e.g. round-trip duplicate).
	TArray<TSharedPtr<FJsonValue>> NodeIds;
	for (UEdGraphNode* Node : PastedNodes)
	{
		Node->CreateNewGuid();
		Node->PostPasteNode();
		if (UK2Node* K2 = Cast<UK2Node>(Node))
		{
			K2->ReconstructNode();
		}
		NodeIds.Add(MakeShared<FJsonValueString>(Node->NodeGuid.ToString()));
	}

	TargetGraph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("graphName"), GraphName);
	Result->SetArrayField(TEXT("nodeIds"), NodeIds);
	Result->SetNumberField(TEXT("count"), NodeIds.Num());
	// No rollback: delete_node only deletes one node at a time and the bulk
	// import has no natural key. Caller must clean up by node id if needed.
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_cdo_property -- Set a property on any C++ class CDO (not Blueprint CDO)
// Params: className (required), propertyName (required), value (required)
// Issues #182/#183
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::SetCdoProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// Accept value as any JSON type (string, number, bool, object, array).
	// Enables setting TArray<FStruct> with nested UObject refs via JSON (#196, #199).
	TSharedPtr<FJsonValue> ValueField = Params->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	// Resolve UClass: try full path first (e.g. "/Script/Engine.Actor"), then short name
	UClass* Class = LoadObject<UClass>(nullptr, *ClassName);
	if (!Class)
	{
		Class = FindClassByShortName(ClassName);
	}
	if (!Class)
	{
		return MCPError(FString::Printf(TEXT("Class not found: %s"), *ClassName));
	}

	UObject* CDO = Class->GetDefaultObject();
	if (!CDO)
	{
		return MCPError(FString::Printf(TEXT("Could not get CDO for class: %s"), *Class->GetName()));
	}

	// Navigate dotted property paths (e.g. "SomeStruct.Field")
	TArray<FString> PathParts;
	PropertyName.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = Class;
	void* CurrentContainer = CDO;
	FProperty* FinalProp = nullptr;

	for (int32 i = 0; i < PathParts.Num(); i++)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop) break;

		if (i < PathParts.Num() - 1)
		{
			FStructProperty* StructProp = CastField<FStructProperty>(Prop);
			if (!StructProp) break;
			CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProp->Struct;
		}
		else
		{
			FinalProp = Prop;
		}
	}

	if (!FinalProp)
	{
		TArray<FString> PropNames;
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			PropNames.Add(It->GetName());
		}
		return MCPError(FString::Printf(
			TEXT("Property '%s' not found on %s. Properties: [%s]"),
			*PropertyName, *Class->GetName(),
			*FString::Join(PropNames, TEXT(", "))));
	}

	void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);

	// Capture previous value for rollback / idempotency
	FString PrevValue;
	FinalProp->ExportText_Direct(PrevValue, ValuePtr, ValuePtr, nullptr, PPF_None);

	CDO->Modify();
	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(FinalProp, ValuePtr, ValueField, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	// Re-export for rollback payload and no-op detection
	FString NewValue;
	FinalProp->ExportText_Direct(NewValue, ValuePtr, ValuePtr, nullptr, PPF_None);
	if (NewValue == PrevValue)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("className"), ClassName);
		Noop->SetStringField(TEXT("propertyName"), PropertyName);
		Noop->SetStringField(TEXT("value"), NewValue);
		return MCPResult(Noop);
	}

	CDO->PostEditChange();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("className"), ClassName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("value"), NewValue);

	// Rollback: restore the previous value
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("className"), ClassName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetStringField(TEXT("value"), PrevValue);
	MCPSetRollback(Result, TEXT("set_cdo_property"), Payload);

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// get_cdo_properties -- Read properties from any C++ class CDO
// Params: className (required), propertyNames (optional array of strings)
// Issue #183
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::GetCdoProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;

	// Resolve UClass
	UClass* Class = LoadObject<UClass>(nullptr, *ClassName);
	if (!Class)
	{
		Class = FindClassByShortName(ClassName);
	}
	if (!Class)
	{
		return MCPError(FString::Printf(TEXT("Class not found: %s"), *ClassName));
	}

	UObject* CDO = Class->GetDefaultObject();
	if (!CDO)
	{
		return MCPError(FString::Printf(TEXT("Could not get CDO for class: %s"), *Class->GetName()));
	}

	// Optional filter: specific property names
	TSet<FString> Filter;
	const TArray<TSharedPtr<FJsonValue>>* PropNamesArr = nullptr;
	if (Params->TryGetArrayField(TEXT("propertyNames"), PropNamesArr) && PropNamesArr)
	{
		for (const auto& V : *PropNamesArr)
		{
			FString S;
			if (V->TryGetString(S))
			{
				Filter.Add(S);
			}
		}
	}

	TSharedPtr<FJsonObject> PropsObj = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;

		const FString PropName = Prop->GetName();
		if (Filter.Num() > 0 && !Filter.Contains(PropName))
		{
			continue;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);
		FString ExportedValue;
		Prop->ExportText_Direct(ExportedValue, ValuePtr, ValuePtr, nullptr, PPF_None);
		PropsObj->SetStringField(PropName, ExportedValue);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("className"), Class->GetPathName());
	Result->SetStringField(TEXT("classShortName"), Class->GetName());
	Result->SetObjectField(TEXT("properties"), PropsObj);
	Result->SetNumberField(TEXT("count"), PropsObj->Values.Num());

	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// run_construction_script -- Spawn a temp actor from a Blueprint, run its
// construction script, collect resulting component info, then destroy it.
// Params: assetPath (required), location (optional {x,y,z})
// Issue #195
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FBlueprintHandlers::RunConstructionScript(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UClass* SpawnClass = Blueprint->GeneratedClass;
	if (!SpawnClass)
	{
		return MCPError(TEXT("Blueprint has no GeneratedClass (needs compilation first?)"));
	}

	REQUIRE_EDITOR_WORLD(World);

	// Parse optional spawn location
	FVector SpawnLocation = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj) && LocationObj)
	{
		double X = 0.0, Y = 0.0, Z = 0.0;
		(*LocationObj)->TryGetNumberField(TEXT("x"), X);
		(*LocationObj)->TryGetNumberField(TEXT("y"), Y);
		(*LocationObj)->TryGetNumberField(TEXT("z"), Z);
		SpawnLocation = FVector(X, Y, Z);
	}

	// Spawn a temporary actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;
	SpawnParams.ObjectFlags |= RF_Transient; // Mark transient so it won't be saved

	FRotator SpawnRotation = FRotator::ZeroRotator;
	AActor* TempActor = World->SpawnActor<AActor>(SpawnClass, SpawnLocation, SpawnRotation, SpawnParams);
	if (!TempActor)
	{
		return MCPError(TEXT("Failed to spawn temporary actor from Blueprint"));
	}

	// Collect component info
	TArray<TSharedPtr<FJsonValue>> ComponentsArr;
	TArray<UActorComponent*> Components;
	TempActor->GetComponents(Components);

	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;

		TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
		CompObj->SetStringField(TEXT("name"), Comp->GetName());
		CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());

		// If it's a scene component, include transform info
		if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
		{
			FTransform RelTrans = SceneComp->GetRelativeTransform();
			FVector Loc = RelTrans.GetLocation();
			FRotator Rot = RelTrans.GetRotation().Rotator();
			FVector Scale = RelTrans.GetScale3D();

			TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
			LocObj->SetNumberField(TEXT("x"), Loc.X);
			LocObj->SetNumberField(TEXT("y"), Loc.Y);
			LocObj->SetNumberField(TEXT("z"), Loc.Z);
			TransObj->SetObjectField(TEXT("location"), LocObj);

			TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
			RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
			RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
			RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
			TransObj->SetObjectField(TEXT("rotation"), RotObj);

			TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
			ScaleObj->SetNumberField(TEXT("x"), Scale.X);
			ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
			ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
			TransObj->SetObjectField(TEXT("scale"), ScaleObj);

			CompObj->SetObjectField(TEXT("relativeTransform"), TransObj);

			// Is it the root?
			CompObj->SetBoolField(TEXT("isRoot"), SceneComp == TempActor->GetRootComponent());
		}

		ComponentsArr.Add(MakeShared<FJsonValueObject>(CompObj));
	}

	// Destroy the temporary actor
	World->DestroyActor(TempActor);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("className"), SpawnClass->GetName());
	Result->SetArrayField(TEXT("components"), ComponentsArr);
	Result->SetNumberField(TEXT("componentCount"), ComponentsArr.Num());

	return MCPResult(Result);
}
