#include "BlueprintHandlers.h"
#include "BlueprintHandlers_Internal.h"
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
#include "AnimStateTransitionNode.h"
#include "AnimStateNodeBase.h"
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
#include "Components/ChildActorComponent.h"
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
	constexpr float ReadBlueprintGraphTimeoutSeconds = 180.0f;

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
	Registry.RegisterHandlerWithTimeout(TEXT("read_blueprint_graph"), &ReadBlueprintGraph, ReadBlueprintGraphTimeoutSeconds);
	Registry.RegisterHandler(TEXT("add_event_dispatcher"), &AddEventDispatcher);
	Registry.RegisterHandler(TEXT("rename_function"), &RenameFunction);
	Registry.RegisterHandler(TEXT("delete_function"), &DeleteFunction);
	Registry.RegisterHandler(TEXT("create_blueprint_interface"), &CreateBlueprintInterface);
	Registry.RegisterHandler(TEXT("connect_pins"), &ConnectPins);
	Registry.RegisterHandler(TEXT("delete_node"), &DeleteNode);
	Registry.RegisterHandler(TEXT("set_node_property"), &SetNodeProperty);
	Registry.RegisterHandler(TEXT("list_blueprint_graphs"), &ListGraphs);
	Registry.RegisterHandler(TEXT("set_blueprint_component_property"), &SetComponentProperty);
	// #442: dedicated OverrideMaterials writer that takes a materialPaths array
	// directly, avoiding any value coercion concerns on the generic path.
	Registry.RegisterHandler(TEXT("set_component_override_materials"), &SetComponentOverrideMaterials);
	// #457: timeline track authoring (float/vector/color/event) on a Blueprint.
	Registry.RegisterHandler(TEXT("add_timeline_track"), &AddTimelineTrack);
	Registry.RegisterHandler(TEXT("set_capsule_size"), &SetCapsuleSize);
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

	// v1.0.0-rc.15 — agent-friendly BP authoring
	Registry.RegisterHandler(TEXT("compile_blueprints"), &CompileBlueprints);
	Registry.RegisterHandler(TEXT("cleanup_graph"), &CleanupGraph);
	Registry.RegisterHandler(TEXT("connect_pins_batch"), &ConnectPinsBatch);
	Registry.RegisterHandler(TEXT("set_node_position"), &SetNodePosition);
	Registry.RegisterHandler(TEXT("auto_layout_graph"), &AutoLayoutGraph);
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
	// #298: identify graph type so callers can tell ubergraph / construction
	// script / function / macro apart without having to grep node titles.
	{
		FString GraphType = TEXT("Other");
		if (Blueprint->UbergraphPages.Contains(Graph)) GraphType = TEXT("Ubergraph");
		for (UEdGraph* G : Blueprint->FunctionGraphs)
		{
			if (G == Graph) { GraphType = (G->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript) ? TEXT("ConstructionScript") : TEXT("Function"); break; }
		}
		for (UEdGraph* G : Blueprint->MacroGraphs)        { if (G == Graph) { GraphType = TEXT("Macro"); break; } }
		for (UEdGraph* G : Blueprint->DelegateSignatureGraphs) { if (G == Graph) { GraphType = TEXT("DelegateSignature"); break; } }
		for (UEdGraph* G : Blueprint->IntermediateGeneratedGraphs) { if (G == Graph) { GraphType = TEXT("Intermediate"); break; } }
		if (Graph && Graph->Schema)
		{
			Result->SetStringField(TEXT("schemaClass"), Graph->Schema->GetName());
		}
		Result->SetStringField(TEXT("graphType"), GraphType);
	}
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
	return LoadAssetByPath<UBlueprint>(AssetPath);
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
	// (#428) Explicit enum reference: "enum:/Game/Path/E_Foo[.E_Foo]" or
	// "enum:/Script/Module.EEnumName". Used for user-defined enums where the
	// short-name resolver can't reach them.
	else if (TypeStr.StartsWith(TEXT("enum:")))
	{
		FString EnumPath = TypeStr.Mid(5);
		EnumPath.TrimStartAndEndInline();
		UEnum* Enum = LoadObject<UEnum>(nullptr, *EnumPath);
		if (!Enum && !EnumPath.Contains(TEXT(".")))
		{
			// Try object-path form ("/Game/Foo/Bar" -> "/Game/Foo/Bar.Bar")
			FString AssetName;
			EnumPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			Enum = LoadObject<UEnum>(nullptr, *(EnumPath + TEXT(".") + AssetName));
		}
		if (Enum)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			PinType.PinSubCategoryObject = Enum;
		}
	}
	// (#286) Resolve named enums by full path (/Script/Module.EEnumName) or
	// short name (EMyEnum / E_MyEnum). UE pin types for enums use PC_Byte with
	// PinSubCategoryObject = UEnum*.
	else if (TypeStr.StartsWith(TEXT("/Script/")) && TypeStr.Contains(TEXT(".")))
	{
		if (UEnum* Enum = LoadObject<UEnum>(nullptr, *TypeStr))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			PinType.PinSubCategoryObject = Enum;
		}
		// fall through to default-handling below if it's not actually an enum;
		// LoadObject returning nullptr leaves PinCategory NAME_None which the
		// next branch can still try as a struct or class.
		else if (TryResolveObjectPin(TypeStr))
		{
			// resolved as object/class
		}
	}
	// (#428) Bare /Game/... path - assume user-defined enum.
	else if (TypeStr.StartsWith(TEXT("/Game/")))
	{
		FString EnumPath = TypeStr;
		UEnum* Enum = LoadObject<UEnum>(nullptr, *EnumPath);
		if (!Enum && !EnumPath.Contains(TEXT(".")))
		{
			FString AssetName;
			EnumPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			Enum = LoadObject<UEnum>(nullptr, *(EnumPath + TEXT(".") + AssetName));
		}
		if (Enum)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			PinType.PinSubCategoryObject = Enum;
		}
	}
	else
	{
		// Try short-name enum lookup before the struct resolver — many engine
		// enums (EAttachmentRule, EMovementMode) match the convention E* but
		// would otherwise fall through and return an empty PinType. (#286)
		auto TryResolveEnumShort = [&](const FString& Name) -> UEnum*
		{
			if (Name.Len() < 2) return nullptr;
			if (Name[0] != 'E') return nullptr;
			for (TObjectIterator<UEnum> It; It; ++It)
			{
				if (It->GetName() == Name) return *It;
			}
			return nullptr;
		};
		if (UEnum* ShortEnum = TryResolveEnumShort(TypeStr))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			PinType.PinSubCategoryObject = ShortEnum;
			return PinType;
		}

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

	// #209: state-pair addressing "Idle to Resting" / "Idle->Resting" for
	// AnimBP transition condition graphs. The internal graph name is always
	// "Transition" so callers couldn't target a specific transition by name.
	auto SplitStatePair = [](const FString& In, FString& OutFrom, FString& OutTo) -> bool
	{
		const TCHAR* Seps[] = { TEXT(" to "), TEXT("->"), TEXT("→"), TEXT(" -> ") };
		for (const TCHAR* Sep : Seps)
		{
			int32 At = In.Find(Sep);
			if (At != INDEX_NONE)
			{
				OutFrom = In.Left(At).TrimStartAndEnd();
				OutTo = In.Mid(At + FCString::Strlen(Sep)).TrimStartAndEnd();
				return !OutFrom.IsEmpty() && !OutTo.IsEmpty();
			}
		}
		return false;
	};
	FString FromState, ToState;
	if (SplitStatePair(GraphName, FromState, ToState))
	{
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;
			if (UAnimStateTransitionNode* Trans = Cast<UAnimStateTransitionNode>(Graph->GetOuter()))
			{
				const FString PrevName = Trans->GetPreviousState() ? Trans->GetPreviousState()->GetStateName() : FString();
				const FString NextName = Trans->GetNextState() ? Trans->GetNextState()->GetStateName() : FString();
				if (PrevName.Equals(FromState, ESearchCase::IgnoreCase) && NextName.Equals(ToState, ESearchCase::IgnoreCase))
				{
					return Graph;
				}
			}
		}
	}

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

	// #353/#370: per-component property dump on demand. Off by default so the
	// common read stays small; flip on when the caller wants the full UPROPERTY
	// values from each component template (e.g. AIPerceptionStimuliSourceComponent's
	// bAutoRegisterAsSource for a read-then-modify flow).
	const bool bIncludeComponentProperties = OptionalBool(Params, TEXT("includeComponentProperties"));
	auto AppendComponentProperties = [&bIncludeComponentProperties](TSharedPtr<FJsonObject> CompObj, UActorComponent* Template)
	{
		if (!bIncludeComponentProperties || !Template) return;
		TArray<TSharedPtr<FJsonValue>> Props;
		for (TFieldIterator<FProperty> PIt(Template->GetClass()); PIt; ++PIt)
		{
			FProperty* Prop = *PIt;
			if (!Prop) continue;
			if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Prop->GetName());
			P->SetStringField(TEXT("type"), Prop->GetCPPType());
			FString ValueStr;
			const void* VP = Prop->ContainerPtrToValuePtr<void>(Template);
			Prop->ExportText_Direct(ValueStr, VP, VP, Template, PPF_None);
			P->SetStringField(TEXT("value"), ValueStr);
			Props.Add(MakeShared<FJsonValueObject>(P));
		}
		CompObj->SetArrayField(TEXT("properties"), Props);
	};

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

			AppendComponentProperties(CompObj, Template);
			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}

	// #353: inherited native components (e.g. CharacterMesh0, CharMoveComp on
	// ACharacter) live on the parent class' CDO, not in the BP's SCS. Walk the
	// generated class' default subobjects so the response covers the full
	// effective component list, not just the BP-authored slice.
	if (UClass* GenClass = Blueprint->GeneratedClass)
	{
		if (AActor* CDOActor = Cast<AActor>(GenClass->GetDefaultObject()))
		{
			TArray<UActorComponent*> AllComps;
			CDOActor->GetComponents(AllComps);
			for (UActorComponent* Comp : AllComps)
			{
				if (!Comp) continue;
				// Skip components that came from the SCS (already emitted above).
				if (Comp->CreationMethod == EComponentCreationMethod::SimpleConstructionScript) continue;
				TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
				CompObj->SetStringField(TEXT("name"), Comp->GetName());
				CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
				CompObj->SetStringField(TEXT("origin"), TEXT("native"));
				if (USceneComponent* SC = Cast<USceneComponent>(Comp))
				{
					TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
					Loc->SetNumberField(TEXT("x"), SC->GetRelativeLocation().X);
					Loc->SetNumberField(TEXT("y"), SC->GetRelativeLocation().Y);
					Loc->SetNumberField(TEXT("z"), SC->GetRelativeLocation().Z);
					CompObj->SetObjectField(TEXT("relativeLocation"), Loc);
				}
				AppendComponentProperties(CompObj, Comp);
				ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
			}
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

				// #526: when adding a ChildActorComponent, let callers set its
				// ChildActorClass in the same call. Accepts a Blueprint asset path
				// (with or without the _C generated-class suffix) or a C++ class.
				FString ChildActorClassPath;
				if (Params->TryGetStringField(TEXT("childActorClass"), ChildActorClassPath) && !ChildActorClassPath.IsEmpty())
				{
					if (const FSubobjectData* NewData = NewHandle.GetData())
					{
						if (UChildActorComponent* CAC = Cast<UChildActorComponent>(const_cast<UObject*>(NewData->GetObject())))
						{
							UClass* ChildCls = LoadClass<AActor>(nullptr, *ChildActorClassPath);
							if (!ChildCls && !ChildActorClassPath.EndsWith(TEXT("_C")))
							{
								ChildCls = LoadClass<AActor>(nullptr, *(ChildActorClassPath + TEXT("_C")));
							}
							if (!ChildCls)
							{
								if (UBlueprint* ChildBP = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ChildActorClassPath)))
								{
									ChildCls = ChildBP->GeneratedClass;
								}
							}
							if (ChildCls)
							{
								CAC->Modify();
								CAC->SetChildActorClass(ChildCls);
							}
						}
					}
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
TSharedPtr<FJsonValue> FBlueprintHandlers::DuplicateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString SourcePath;
	if (auto Err = RequireString(Params, TEXT("sourcePath"), SourcePath)) return Err;
	FString DestinationPath;
	if (auto Err = RequireString(Params, TEXT("destinationPath"), DestinationPath)) return Err;

	UObject* Dup = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestinationPath);
	if (!Dup)
	{
		// #441: DoesAssetExist can return false for valid Blueprint paths in
		// 5.7. Fall back to loading the source and driving AssetTools directly.
		UObject* SourceObj = UEditorAssetLibrary::LoadAsset(SourcePath);
		if (!SourceObj) SourceObj = LoadObject<UObject>(nullptr, *SourcePath);
		if (SourceObj)
		{
			FString DestPkg, DestName;
			if (DestinationPath.Split(TEXT("/"), &DestPkg, &DestName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
				Dup = AssetTools.DuplicateAsset(DestName, DestPkg, SourceObj);
			}
		}
	}
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
