#include "PCGHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerAssetCreate.h"
#include "HandlerJsonProperty.h"
#include "VolumeHelpers_Internal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/TopLevelAssetPath.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PCGGraph.h"
// PCGGraphInterface.h may not be directly includable in 5.7
#include "PCGComponent.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGVolume.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "UObject/UObjectIterator.h"
#include "Engine/StaticMesh.h"
#include "Engine/Brush.h"
#include "Components/BrushComponent.h"
#include "Builders/CubeBuilder.h"
#include "BSPOps.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "GameFramework/Actor.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/SoftObjectPtr.h"
#include "ScopedTransaction.h"
#include "Runtime/Launch/Resources/Version.h"
#include <functional>

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
#define UE_MCP_HAS_PCG_EDITOR_GRAPH_NODE_LAYOUT 1
#else
#define UE_MCP_HAS_PCG_EDITOR_GRAPH_NODE_LAYOUT 0
#endif

#define UE_MCP_HAS_STATIC_FIND_OBJECT_FLAGS (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7))

namespace
{
	// #213: shared class lookup. Mirrors AddPCGNode's tolerant resolver - accepts
	// short name, "/Script/PCG.X" path, or "U"-prefixed short name.
	static UClass* FindPCGSettingsClass(const FString& ClassName)
	{
		if (ClassName.IsEmpty()) return nullptr;
		UClass* Cls = FindObject<UClass>(nullptr, *ClassName);
		if (!Cls) Cls = FindObject<UClass>(nullptr, *(TEXT("/Script/PCG.") + ClassName));
		if (!Cls && !ClassName.StartsWith(TEXT("U")))
		{
			Cls = FindObject<UClass>(nullptr, *(TEXT("/Script/PCG.U") + ClassName));
		}
		if (Cls && !Cls->IsChildOf(UPCGSettings::StaticClass())) return nullptr;
		return Cls;
	}

	// #213: locate a node by name within a graph, including Input/Output specials.
	static UPCGNode* FindPCGNodeByName(UPCGGraph* Graph, const FString& Name)
	{
		if (!Graph || Name.IsEmpty()) return nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Node->GetName() == Name) return Node;
		}
		if (UPCGNode* In = Graph->GetInputNode(); In && In->GetName() == Name) return In;
		if (UPCGNode* Out = Graph->GetOutputNode(); Out && Out->GetName() == Name) return Out;
		return nullptr;
	}

	// #213: structured property serializer used by export_pcg_graph. Emits every
	// editable property recursively as JSON so the result round-trips through
	// MCPJsonProperty::SetDottedPropertyFromJson on import. Mirrors the lambda in
	// ReadPCGNodeSettings (#214/#215) but kept as a standalone function so both
	// callers can share it without rebuilding the closure each call.
	static TSharedPtr<FJsonValue> SerializePropForExport(FProperty* Prop, const void* Addr)
	{
		if (!Prop || !Addr) return MakeShared<FJsonValueNull>();

		if (FStructProperty* SP = CastField<FStructProperty>(Prop))
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(SP->Struct); It; ++It)
			{
				FProperty* Inner = *It;
				if (!Inner) continue;
				const void* InnerAddr = Inner->ContainerPtrToValuePtr<void>(Addr);
				Obj->SetField(Inner->GetName(), SerializePropForExport(Inner, InnerAddr));
			}
			return MakeShared<FJsonValueObject>(Obj);
		}
		if (FArrayProperty* AP = CastField<FArrayProperty>(Prop))
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			FScriptArrayHelper H(AP, Addr);
			for (int32 i = 0; i < H.Num(); ++i)
			{
				Items.Add(SerializePropForExport(AP->Inner, H.GetRawPtr(i)));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			return MakeShared<FJsonValueBoolean>(BP->GetPropertyValue(Addr));
		}
		if (FNumericProperty* NP = CastField<FNumericProperty>(Prop))
		{
			if (NP->IsFloatingPoint())
			{
				return MakeShared<FJsonValueNumber>(NP->GetFloatingPointPropertyValue(Addr));
			}
			return MakeShared<FJsonValueNumber>((double)NP->GetSignedIntPropertyValue(Addr));
		}
		if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
		{
			UObject* Ref = OP->GetObjectPropertyValue(Addr);
			return MakeShared<FJsonValueString>(Ref ? Ref->GetPathName() : TEXT(""));
		}
		FString Out;
		Prop->ExportTextItem_Direct(Out, Addr, nullptr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Out);
	}
}

void FPCGHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_pcg_graphs"), &ListPCGGraphs);
	Registry.RegisterHandler(TEXT("get_pcg_components"), &GetPCGComponents);
	Registry.RegisterHandler(TEXT("create_pcg_graph"), &CreatePCGGraph);
	Registry.RegisterHandler(TEXT("read_pcg_graph"), &ReadPCGGraph);
	Registry.RegisterHandler(TEXT("add_pcg_node"), &AddPCGNode);
	Registry.RegisterHandler(TEXT("connect_pcg_nodes"), &ConnectPCGNodes);
	Registry.RegisterHandler(TEXT("disconnect_pcg_nodes"), &DisconnectPCGNodes);
	Registry.RegisterHandler(TEXT("remove_pcg_node"), &RemovePCGNode);
	Registry.RegisterHandler(TEXT("set_pcg_node_settings"), &SetPCGNodeSettings);
	Registry.RegisterHandler(TEXT("execute_pcg_graph"), &ExecutePCGGraph);
	Registry.RegisterHandler(TEXT("add_pcg_volume"), &SpawnPCGVolume);
	Registry.RegisterHandler(TEXT("read_pcg_node_settings"), &ReadPCGNodeSettings);
	Registry.RegisterHandler(TEXT("get_pcg_component_details"), &GetPCGComponentDetails);
	Registry.RegisterHandler(TEXT("set_static_mesh_spawner_meshes"), &SetStaticMeshSpawnerMeshes);
	// #146: force_regenerate / cleanup / toggle_graph on PCG components
	Registry.RegisterHandler(TEXT("force_regenerate_pcg"), &ForceRegeneratePCG);
	Registry.RegisterHandler(TEXT("cleanup_pcg"), &CleanupPCG);
	Registry.RegisterHandler(TEXT("toggle_pcg_graph"), &ToggleGraphPCG);

	// #213: bulk JSON-driven graph authoring (mirrors material.import_graph).
	Registry.RegisterHandler(TEXT("import_pcg_graph"), &ImportGraph);
	Registry.RegisterHandler(TEXT("export_pcg_graph"), &ExportGraph);
}

TSharedPtr<FJsonValue> FPCGHandlers::ListPCGGraphs(const TSharedPtr<FJsonObject>& Params)
{
	// T3: paged.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params, TEXT("list_pcg_graphs"), /*DefaultLimit*/ 200, /*MaxLimit*/ 2000, Page))
	{
		return Err;
	}

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/PCG"), TEXT("PCGGraph")), Assets, true);

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(Assets.Num());
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		// The asset's object path is the page anchor.
		Rows.Add({ Asset.GetObjectPathString(), MakeShared<FJsonValueObject>(AssetObj) });
	}
	// The registry returns assets in scan order, which is not a contract.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	MCPPagination::EmitPage(Page, Rows, TEXT("graphs"), Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::GetPCGComponents(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<TSharedPtr<FJsonValue>> CompArray;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!Actor) continue;

		TArray<UPCGComponent*> PCGComps;
		Actor->GetComponents<UPCGComponent>(PCGComps);
		for (UPCGComponent* PCGComp : PCGComps)
		{
			if (!PCGComp) continue;
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
			CompObj->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
			CompObj->SetStringField(TEXT("componentName"), PCGComp->GetName());
			if (PCGComp->GetGraph())
			{
				CompObj->SetStringField(TEXT("graphName"), PCGComp->GetGraph()->GetName());
			}
			CompArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("components"), CompArray);
	Result->SetNumberField(TEXT("count"), CompArray.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::CreatePCGGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/PCG"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Created = MCPCreateAssetIdempotent<UPCGGraph>(Name, PackagePath, OnConflict, TEXT("PCGGraph"), nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveLoadedAsset(Created.Asset, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ReadPCGGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), Graph->GetName());
	Result->SetStringField(TEXT("path"), AssetPath);

	const auto& Nodes = Graph->GetNodes();
	TArray<TSharedPtr<FJsonValue>> NodeArray;
	for (const UPCGNode* Node : Nodes)
	{
		if (!Node) continue;
		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("name"), Node->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
		NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	Result->SetArrayField(TEXT("nodes"), NodeArray);
	Result->SetNumberField(TEXT("nodeCount"), NodeArray.Num());

	// #239: surface the implicit DefaultInputNode / DefaultOutputNode so
	// callers can wire to them via connect_pcg_nodes. They aren't part of
	// GetNodes() but their names are required to thread the PCG component's
	// bounds into samplers (e.g. SurfaceSampler.BoundingShape <- graph input).
	auto SerializeImplicitNode = [](const UPCGNode* Node) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Node) return Obj;
		Obj->SetStringField(TEXT("name"), Node->GetName());
		Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
		TArray<TSharedPtr<FJsonValue>> InPins, OutPins;
		for (const TObjectPtr<UPCGPin>& P : Node->GetInputPins())
		{
			if (P) InPins.Add(MakeShared<FJsonValueString>(P->Properties.Label.ToString()));
		}
		for (const TObjectPtr<UPCGPin>& P : Node->GetOutputPins())
		{
			if (P) OutPins.Add(MakeShared<FJsonValueString>(P->Properties.Label.ToString()));
		}
		Obj->SetArrayField(TEXT("inputPins"), InPins);
		Obj->SetArrayField(TEXT("outputPins"), OutPins);
		return Obj;
	};
	if (const UPCGNode* InputNode = Graph->GetInputNode())
	{
		Result->SetObjectField(TEXT("inputNode"), SerializeImplicitNode(InputNode));
	}
	if (const UPCGNode* OutputNode = Graph->GetOutputNode())
	{
		Result->SetObjectField(TEXT("outputNode"), SerializeImplicitNode(OutputNode));
	}

	// #217: include edges so callers can verify wiring without dropping into
	// Python. Walk every node's output pins and serialise each edge as
	// {from, fromPin, to, toPin}. Input/Output graph nodes participate too.
	auto EmitEdgesFromNode = [](const UPCGNode* From, TArray<TSharedPtr<FJsonValue>>& OutEdges)
	{
		if (!From) return;
		for (const TObjectPtr<UPCGPin>& OutPin : From->GetOutputPins())
		{
			if (!OutPin) continue;
			for (const TObjectPtr<UPCGEdge>& Edge : OutPin->Edges)
			{
				if (!Edge) continue;
				const UPCGPin* OtherPin = Edge->InputPin == OutPin ? Edge->OutputPin.Get() : Edge->InputPin.Get();
				const UPCGNode* ToNode = OtherPin ? OtherPin->Node.Get() : nullptr;
				if (!OtherPin || !ToNode) continue;
				TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
				EdgeObj->SetStringField(TEXT("from"), From->GetName());
				EdgeObj->SetStringField(TEXT("fromPin"), OutPin->Properties.Label.ToString());
				EdgeObj->SetStringField(TEXT("to"), ToNode->GetName());
				EdgeObj->SetStringField(TEXT("toPin"), OtherPin->Properties.Label.ToString());
				OutEdges.Add(MakeShared<FJsonValueObject>(EdgeObj));
			}
		}
	};

	TArray<TSharedPtr<FJsonValue>> EdgeArray;
	if (const UPCGNode* InputNode = Graph->GetInputNode())
	{
		EmitEdgesFromNode(InputNode, EdgeArray);
	}
	for (const UPCGNode* Node : Nodes)
	{
		EmitEdgesFromNode(Node, EdgeArray);
	}
	Result->SetArrayField(TEXT("edges"), EdgeArray);
	Result->SetNumberField(TEXT("edgeCount"), EdgeArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::AddPCGNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NodeType;
	if (auto Err = RequireString(Params, TEXT("nodeType"), NodeType)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	// Find the settings class by name
	UClass* SettingsClass = FindObject<UClass>(nullptr, *NodeType);
	if (!SettingsClass)
	{
		// Try with /Script/PCG prefix
		SettingsClass = FindObject<UClass>(nullptr, *(TEXT("/Script/PCG.") + NodeType));
	}
	if (!SettingsClass)
	{
		// Try with U prefix stripped
		FString CleanName = NodeType;
		if (!CleanName.StartsWith(TEXT("U")))
		{
			SettingsClass = FindObject<UClass>(nullptr, *(TEXT("/Script/PCG.U") + CleanName));
		}
	}
	if (!SettingsClass || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("PCG settings class not found or invalid: %s"), *NodeType));
	}

	// #157: wrap mutation in a scoped transaction, outer settings to the graph
	// (so they serialize with the package), and force SaveLoadedAsset on the
	// exact Graph instance. The prior AddNode + SaveAsset(path) combination
	// could silently drop mutations when save-by-path resolved to a different
	// loaded instance than the one we mutated.
	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "AddPCGNode", "Add PCG Node"));
	Graph->Modify();

	UPCGSettings* DefaultSettings = NewObject<UPCGSettings>(Graph, SettingsClass, NAME_None, RF_Transactional);
	if (!DefaultSettings)
	{
		return MCPError(TEXT("Failed to create PCG settings instance"));
	}

	// AddNodeInstance matches the proven Python path: wraps settings in a
	// UPCGSettingsInstance parented to the new node. AddNode(UPCGSettings*)
	// still persists but diverges from how the PCG editor authors nodes.
	UPCGNode* NewNode = Graph->AddNodeInstance(DefaultSettings);
	if (!NewNode)
	{
		return MCPError(TEXT("Failed to add node to PCG graph"));
	}

	// Keep settings parented to the node so duplicate/rename of the graph
	// carries settings with it. AddNodeInstance already does this for the
	// SettingsInstance wrapper; we also reparent the underlying settings.
	if (DefaultSettings->GetOuter() != NewNode && !DefaultSettings->GetOuter()->IsA<UPackage>())
	{
		DefaultSettings->Rename(nullptr, NewNode, REN_DontCreateRedirectors | REN_DoNotDirty);
	}
	DefaultSettings->PostEditChange();

	double PosX = 0, PosY = 0;
	if (Params->TryGetNumberField(TEXT("posX"), PosX) || Params->TryGetNumberField(TEXT("posY"), PosY))
	{
		NewNode->PositionX = (int32)PosX;
		NewNode->PositionY = (int32)PosY;
	}

	NewNode->PostEditChange();
	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }

	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	const FString NewNodeName = NewNode->GetName();
	Result->SetStringField(TEXT("nodeName"), NewNodeName);
	Result->SetStringField(TEXT("nodeType"), NodeType);
	Result->SetStringField(TEXT("nodeTitle"), NewNode->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());

	// Rollback: remove_pcg_node
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("nodeName"), NewNodeName);
	MCPSetRollback(Result, TEXT("remove_pcg_node"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ConnectPCGNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SourceNodeName;
	if (auto Err = RequireStringAlt(Params, TEXT("sourceNodeName"), TEXT("sourceNode"), SourceNodeName)) return Err;

	FString TargetNodeName;
	if (auto Err = RequireStringAlt(Params, TEXT("targetNodeName"), TEXT("targetNode"), TargetNodeName)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	// Find source and target nodes
	UPCGNode* SourceNode = nullptr;
	UPCGNode* TargetNode = nullptr;
	const auto& Nodes = Graph->GetNodes();
	for (UPCGNode* Node : Nodes)
	{
		if (!Node) continue;
		if (Node->GetName() == SourceNodeName)
		{
			SourceNode = Node;
		}
		if (Node->GetName() == TargetNodeName)
		{
			TargetNode = Node;
		}
	}

	// Also check the input and output nodes
	if (!SourceNode && Graph->GetInputNode() && Graph->GetInputNode()->GetName() == SourceNodeName)
	{
		SourceNode = Graph->GetInputNode();
	}
	if (!TargetNode && Graph->GetOutputNode() && Graph->GetOutputNode()->GetName() == TargetNodeName)
	{
		TargetNode = Graph->GetOutputNode();
	}

	// #239: callers commonly try "Input"/"Output" - point them at the actual
	// implicit node names (DefaultInputNode/DefaultOutputNode) which are now
	// surfaced by read_pcg_graph.
	auto ImplicitHint = [&](const FString& Name) -> FString
	{
		if (Name.Equals(TEXT("Input"), ESearchCase::IgnoreCase) ||
			Name.Equals(TEXT("InputNode"), ESearchCase::IgnoreCase) ||
			Name.Equals(TEXT("GraphInput"), ESearchCase::IgnoreCase))
		{
			return Graph->GetInputNode() ? FString::Printf(TEXT(" - did you mean '%s'?"), *Graph->GetInputNode()->GetName()) : FString();
		}
		if (Name.Equals(TEXT("Output"), ESearchCase::IgnoreCase) ||
			Name.Equals(TEXT("OutputNode"), ESearchCase::IgnoreCase) ||
			Name.Equals(TEXT("GraphOutput"), ESearchCase::IgnoreCase))
		{
			return Graph->GetOutputNode() ? FString::Printf(TEXT(" - did you mean '%s'?"), *Graph->GetOutputNode()->GetName()) : FString();
		}
		return FString();
	};
	if (!SourceNode)
	{
		return MCPError(FString::Printf(TEXT("Source node not found: %s%s"), *SourceNodeName, *ImplicitHint(SourceNodeName)));
	}
	if (!TargetNode)
	{
		return MCPError(FString::Printf(TEXT("Target node not found: %s%s"), *TargetNodeName, *ImplicitHint(TargetNodeName)));
	}

	// Get pin labels if specified, otherwise use the first available pins
	FString SourcePinLabel;
	if (!Params->TryGetStringField(TEXT("sourcePinLabel"), SourcePinLabel))
	{
		Params->TryGetStringField(TEXT("sourcePin"), SourcePinLabel);
	}
	FString TargetPinLabel;
	if (!Params->TryGetStringField(TEXT("targetPinLabel"), TargetPinLabel))
	{
		Params->TryGetStringField(TEXT("targetPin"), TargetPinLabel);
	}

	// UE 5.7: Pin and edge APIs refactored; use Graph->AddEdge() with node+label
	// Resolve the pin labels to use for the connection
	FName ResolvedSourcePinLabel = NAME_None;
	FName ResolvedTargetPinLabel = NAME_None;

	if (SourcePinLabel.IsEmpty())
	{
		// Use the first output pin's label
		const TArray<TObjectPtr<UPCGPin>>& OutPins = SourceNode->GetOutputPins();
		if (OutPins.Num() > 0 && OutPins[0])
		{
			ResolvedSourcePinLabel = OutPins[0]->Properties.Label;
		}
	}
	else
	{
		ResolvedSourcePinLabel = FName(*SourcePinLabel);
	}

	if (TargetPinLabel.IsEmpty())
	{
		// Use the first input pin's label
		const TArray<TObjectPtr<UPCGPin>>& InPins = TargetNode->GetInputPins();
		if (InPins.Num() > 0 && InPins[0])
		{
			ResolvedTargetPinLabel = InPins[0]->Properties.Label;
		}
	}
	else
	{
		ResolvedTargetPinLabel = FName(*TargetPinLabel);
	}

	if (ResolvedSourcePinLabel == NAME_None)
	{
		return MCPError(TEXT("No suitable output pin found on source node"));
	}
	if (ResolvedTargetPinLabel == NAME_None)
	{
		return MCPError(TEXT("No suitable input pin found on target node"));
	}

	// Idempotency, and a correctness guard on the rollback: if this edge is
	// already here, this call creates nothing, so it must not hand back a
	// disconnect that would cut a wire it did not make.
	for (const TObjectPtr<UPCGPin>& OutPin : SourceNode->GetOutputPins())
	{
		if (!OutPin || OutPin->Properties.Label != ResolvedSourcePinLabel) continue;
		for (const TObjectPtr<UPCGEdge>& Edge : OutPin->Edges)
		{
			if (!Edge || !Edge->OutputPin || Edge->OutputPin->Node != TargetNode) continue;
			if (Edge->OutputPin->Properties.Label != ResolvedTargetPinLabel) continue;

			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("assetPath"), AssetPath);
			Existed->SetStringField(TEXT("sourceNodeName"), SourceNodeName);
			Existed->SetStringField(TEXT("targetNodeName"), TargetNodeName);
			Existed->SetStringField(TEXT("sourcePinLabel"), ResolvedSourcePinLabel.ToString());
			Existed->SetStringField(TEXT("targetPinLabel"), ResolvedTargetPinLabel.ToString());
			Existed->SetBoolField(TEXT("edgeVerified"), true);
			Existed->SetBoolField(TEXT("rollbackPossible"), false);
			Existed->SetStringField(TEXT("rollbackNote"),
				TEXT("The edge was already here, so this call created nothing. No disconnect is offered: running one would cut a wire this call did not make."));
			return MCPResult(Existed);
		}
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "ConnectPCGNodes", "Connect PCG Nodes"));
	Graph->Modify();
	SourceNode->Modify();
	TargetNode->Modify();

	UPCGNode* ResultNode = Graph->AddEdge(SourceNode, ResolvedSourcePinLabel, TargetNode, ResolvedTargetPinLabel);
	if (!ResultNode)
	{
		return MCPError(TEXT("Failed to connect pins - connection may already exist or be incompatible"));
	}

	// #304: AddEdge has shipped variants that report success while failing to
	// instantiate the UPCGEdge object on the pin. Verify by walking the source
	// pin's Edges array post-call. If the edge is missing, surface the failure
	// instead of returning a green response that maps to a phantom connection.
	auto FindPinByLabel = [](const TArray<TObjectPtr<UPCGPin>>& Pins, FName Label) -> UPCGPin*
	{
		for (const TObjectPtr<UPCGPin>& P : Pins)
		{
			if (P && P->Properties.Label == Label) return P;
		}
		return nullptr;
	};
	UPCGPin* SrcPin = FindPinByLabel(SourceNode->GetOutputPins(), ResolvedSourcePinLabel);
	UPCGPin* DstPin = FindPinByLabel(TargetNode->GetInputPins(), ResolvedTargetPinLabel);
	bool bEdgeVerified = false;
	if (SrcPin && DstPin)
	{
		for (const TObjectPtr<UPCGEdge>& Edge : SrcPin->Edges)
		{
			if (Edge && Edge->InputPin == SrcPin && Edge->OutputPin == DstPin)
			{
				bEdgeVerified = true;
				break;
			}
		}
	}
	if (!bEdgeVerified)
	{
		return MCPError(FString::Printf(
			TEXT("AddEdge returned success but no UPCGEdge was found on '%s.%s' -> '%s.%s'. ")
			TEXT("Pin labels likely mismatch the node's declared pins."),
			*SourceNodeName, *ResolvedSourcePinLabel.ToString(),
			*TargetNodeName, *ResolvedTargetPinLabel.ToString()));
	}

	SourceNode->PostEditChange();
	TargetNode->PostEditChange();
	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }

	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	// The edge did not exist a moment ago - the pre-check above returned early
	// if it had - so this call is the one that made it.
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("sourceNodeName"), SourceNodeName);
	Result->SetStringField(TEXT("targetNodeName"), TargetNodeName);
	Result->SetStringField(TEXT("sourcePinLabel"), ResolvedSourcePinLabel.ToString());
	Result->SetStringField(TEXT("targetPinLabel"), ResolvedTargetPinLabel.ToString());
	Result->SetBoolField(TEXT("edgeVerified"), true);

	// Rollback: disconnect the freshly-added edge.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("assetPath"), AssetPath);
	RollbackPayload->SetStringField(TEXT("sourceNodeName"), SourceNodeName);
	RollbackPayload->SetStringField(TEXT("targetNodeName"), TargetNodeName);
	RollbackPayload->SetStringField(TEXT("sourcePinLabel"), ResolvedSourcePinLabel.ToString());
	RollbackPayload->SetStringField(TEXT("targetPinLabel"), ResolvedTargetPinLabel.ToString());
	MCPSetRollback(Result, TEXT("disconnect_pcg_nodes"), RollbackPayload);

	return MCPResult(Result);
}

// #346: per-edge removal. UPCGGraph has no public RemoveEdge; do it manually by
// finding the matching UPCGEdge on the source pin and clearing it from both
// pins' Edges arrays, then PostEditChange + save.
TSharedPtr<FJsonValue> FPCGHandlers::DisconnectPCGNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString SourceNodeName;
	if (auto Err = RequireStringAlt(Params, TEXT("sourceNodeName"), TEXT("sourceNode"), SourceNodeName)) return Err;

	FString TargetNodeName;
	if (auto Err = RequireStringAlt(Params, TEXT("targetNodeName"), TEXT("targetNode"), TargetNodeName)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	UPCGNode* SourceNode = nullptr;
	UPCGNode* TargetNode = nullptr;
	for (UPCGNode* Node : Graph->GetNodes())
	{
		if (!Node) continue;
		if (Node->GetName() == SourceNodeName) SourceNode = Node;
		if (Node->GetName() == TargetNodeName) TargetNode = Node;
	}
	if (!SourceNode && Graph->GetInputNode() && Graph->GetInputNode()->GetName() == SourceNodeName)
	{
		SourceNode = Graph->GetInputNode();
	}
	if (!TargetNode && Graph->GetOutputNode() && Graph->GetOutputNode()->GetName() == TargetNodeName)
	{
		TargetNode = Graph->GetOutputNode();
	}
	if (!SourceNode) return MCPError(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeName));
	if (!TargetNode) return MCPError(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeName));

	FString SourcePinLabel = OptionalString(Params, TEXT("sourcePinLabel"));
	if (SourcePinLabel.IsEmpty()) SourcePinLabel = OptionalString(Params, TEXT("sourcePin"));
	FString TargetPinLabel = OptionalString(Params, TEXT("targetPinLabel"));
	if (TargetPinLabel.IsEmpty()) TargetPinLabel = OptionalString(Params, TEXT("targetPin"));

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "DisconnectPCGNodes", "Disconnect PCG Nodes"));
	Graph->Modify();
	SourceNode->Modify();
	TargetNode->Modify();

	int32 RemovedCount = 0;
	// Which edges actually went. With both pin labels omitted this call can cut
	// several at once, and a rollback record carries one connect - so the list is
	// what makes the difference visible instead of implied.
	TArray<TSharedPtr<FJsonValue>> RemovedEdgeList;
	FString FirstRemovedSourcePin, FirstRemovedTargetPin;
	for (const TObjectPtr<UPCGPin>& OutPin : SourceNode->GetOutputPins())
	{
		if (!OutPin) continue;
		if (!SourcePinLabel.IsEmpty() && OutPin->Properties.Label != FName(*SourcePinLabel)) continue;

		// Mutating the Edges array during iteration is unsafe; collect first.
		TArray<UPCGEdge*> ToRemove;
		for (const TObjectPtr<UPCGEdge>& Edge : OutPin->Edges)
		{
			if (!Edge || !Edge->OutputPin) continue;
			UPCGNode* EdgeDstNode = Edge->OutputPin->Node;
			if (EdgeDstNode != TargetNode) continue;
			if (!TargetPinLabel.IsEmpty() && Edge->OutputPin->Properties.Label != FName(*TargetPinLabel)) continue;
			ToRemove.Add(Edge);
		}
		for (UPCGEdge* Edge : ToRemove)
		{
			if (!Edge) continue;
			const FString CutSourcePin = OutPin->Properties.Label.ToString();
			const FString CutTargetPin = Edge->OutputPin ? Edge->OutputPin->Properties.Label.ToString() : FString();
			TSharedPtr<FJsonObject> Cut = MakeShared<FJsonObject>();
			Cut->SetStringField(TEXT("sourcePinLabel"), CutSourcePin);
			Cut->SetStringField(TEXT("targetPinLabel"), CutTargetPin);
			RemovedEdgeList.Add(MakeShared<FJsonValueObject>(Cut));
			if (RemovedCount == 0)
			{
				FirstRemovedSourcePin = CutSourcePin;
				FirstRemovedTargetPin = CutTargetPin;
			}
			if (Edge->OutputPin)
			{
				Edge->OutputPin->Edges.Remove(Edge);
			}
			OutPin->Edges.Remove(Edge);
			RemovedCount++;
		}
	}

	if (RemovedCount == 0)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("sourceNodeName"), SourceNodeName);
		Noop->SetStringField(TEXT("targetNodeName"), TargetNodeName);
		Noop->SetNumberField(TEXT("removedEdges"), 0);
		Noop->SetBoolField(TEXT("alreadyDisconnected"), true);
		// No connect is offered here. Nothing was cut, so a replayed
		// connect_pcg_nodes would CREATE an edge this call never removed, on
		// whichever pins the resolver picks by default.
		Noop->SetBoolField(TEXT("rollbackPossible"), false);
		Noop->SetStringField(TEXT("rollbackNote"),
			TEXT("No edge matched, so nothing was removed and there is nothing to undo. connect_pcg_nodes is deliberately NOT offered as the inverse: it would add a wire this call never cut."));
		return MCPResult(Noop);
	}

	SourceNode->PostEditChange();
	TargetNode->PostEditChange();
	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }
	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("sourceNodeName"), SourceNodeName);
	Result->SetStringField(TEXT("targetNodeName"), TargetNodeName);
	Result->SetNumberField(TEXT("removedEdges"), RemovedCount);
	Result->SetArrayField(TEXT("removedEdgeList"), RemovedEdgeList);

	// Rollback: re-add the edge, naming both pins explicitly so the replay lands
	// on the same slots rather than on whichever pins connect_pcg_nodes would
	// pick by default.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("sourceNodeName"), SourceNodeName);
	Payload->SetStringField(TEXT("targetNodeName"), TargetNodeName);
	Payload->SetStringField(TEXT("sourcePinLabel"), FirstRemovedSourcePin);
	Payload->SetStringField(TEXT("targetPinLabel"), FirstRemovedTargetPin);
	MCPSetRollback(Result, TEXT("connect_pcg_nodes"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), RemovedCount > 1);
	if (RemovedCount > 1)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("%d edges were cut because the pin labels were left open, and the rollback restores only the first one (%s -> %s). Replay the rest from removedEdgeList with connect_pcg_nodes."),
			RemovedCount, *FirstRemovedSourcePin, *FirstRemovedTargetPin));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::RemovePCGNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NodeName;
	if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	// Find the node by name
	UPCGNode* FoundNode = nullptr;
	const auto& Nodes = Graph->GetNodes();
	for (UPCGNode* Node : Nodes)
	{
		if (Node && Node->GetName() == NodeName)
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode)
	{
		// Idempotent: node already absent
		auto Noop = MCPSuccess();
		Noop->SetStringField(TEXT("assetPath"), AssetPath);
		Noop->SetStringField(TEXT("nodeName"), NodeName);
		Noop->SetBoolField(TEXT("alreadyDeleted"), true);
		return MCPResult(Noop);
	}

	// Read what is about to be lost: the settings class is what a replacement
	// node can be built from, and the edges are what it cannot.
	const UPCGSettings* RemovedSettings = FoundNode->GetSettings();
	const FString RemovedSettingsClassPath = RemovedSettings ? RemovedSettings->GetClass()->GetPathName() : FString();
	const int32 RemovedPosX = FoundNode->PositionX;
	const int32 RemovedPosY = FoundNode->PositionY;

	TArray<TSharedPtr<FJsonValue>> SeveredEdges;
	for (const TObjectPtr<UPCGPin>& OutPin : FoundNode->GetOutputPins())
	{
		if (!OutPin) continue;
		for (const TObjectPtr<UPCGEdge>& Edge : OutPin->Edges)
		{
			if (!Edge || !Edge->OutputPin || !Edge->OutputPin->Node) continue;
			TSharedPtr<FJsonObject> Cut = MakeShared<FJsonObject>();
			Cut->SetStringField(TEXT("from"), NodeName);
			Cut->SetStringField(TEXT("fromPin"), OutPin->Properties.Label.ToString());
			Cut->SetStringField(TEXT("to"), Edge->OutputPin->Node->GetName());
			Cut->SetStringField(TEXT("toPin"), Edge->OutputPin->Properties.Label.ToString());
			SeveredEdges.Add(MakeShared<FJsonValueObject>(Cut));
		}
	}
	for (const TObjectPtr<UPCGPin>& InPin : FoundNode->GetInputPins())
	{
		if (!InPin) continue;
		for (const TObjectPtr<UPCGEdge>& Edge : InPin->Edges)
		{
			if (!Edge || !Edge->InputPin || !Edge->InputPin->Node) continue;
			TSharedPtr<FJsonObject> Cut = MakeShared<FJsonObject>();
			Cut->SetStringField(TEXT("from"), Edge->InputPin->Node->GetName());
			Cut->SetStringField(TEXT("fromPin"), Edge->InputPin->Properties.Label.ToString());
			Cut->SetStringField(TEXT("to"), NodeName);
			Cut->SetStringField(TEXT("toPin"), InPin->Properties.Label.ToString());
			SeveredEdges.Add(MakeShared<FJsonValueObject>(Cut));
		}
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "RemovePCGNode", "Remove PCG Node"));
	Graph->Modify();

	Graph->RemoveNode(FoundNode);

	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }

	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("removedNodeName"), NodeName);
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetStringField(TEXT("removedNodeType"), RemovedSettingsClassPath);
	Result->SetArrayField(TEXT("severedEdges"), SeveredEdges);

	if (!RemovedSettingsClassPath.IsEmpty())
	{
		// Rollback: put a node of the same settings class back at the same spot.
		// add_pcg_node resolves a full class path through FindObject<UClass>, so
		// the class this node really had is the one recreated. What it cannot do
		// is restore the settings values or the edges, both of which are listed
		// above for a caller that has to rebuild them.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("nodeType"), RemovedSettingsClassPath);
		Payload->SetNumberField(TEXT("posX"), RemovedPosX);
		Payload->SetNumberField(TEXT("posY"), RemovedPosY);
		MCPSetRollback(Result, TEXT("add_pcg_node"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The rollback adds a fresh node of the same class at the same position, with DEFAULT settings and no wiring. This node's settings values and its %d edge(s) do not come back - the edges are listed in severedEdges, replay them with connect_pcg_nodes. The replacement also gets a new engine-assigned name, so '%s' will not exist again and anything holding that name has to be re-pointed. To undo a node removal exactly, snapshot with export_pcg_graph first and restore with import_pcg_graph replace=true."),
			SeveredEdges.Num(), *NodeName));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The removed node carried no settings object, so there is no class for add_pcg_node to recreate it from. Restore the graph from an export_pcg_graph snapshot with import_pcg_graph replace=true."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::SetPCGNodeSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NodeName;
	if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;

	// Accept either a 'settings' object (key-value pairs) or individual 'propertyName'/'propertyValue'
	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	FString PropertyName;
	FString PropertyValue;
	bool bUseSettingsObject = Params->TryGetObjectField(TEXT("settings"), SettingsObj) && SettingsObj && (*SettingsObj).IsValid();
	bool bUseSingleProperty = !bUseSettingsObject && Params->TryGetStringField(TEXT("propertyName"), PropertyName);

	if (!bUseSettingsObject && !bUseSingleProperty)
	{
		return MCPError(TEXT("Missing 'settings' object or 'propertyName'/'propertyValue' parameters"));
	}

	if (bUseSingleProperty && !Params->TryGetStringField(TEXT("propertyValue"), PropertyValue))
	{
		return MCPError(TEXT("Missing 'propertyValue' parameter"));
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	// Find the node by name
	UPCGNode* FoundNode = nullptr;
	const auto& Nodes = Graph->GetNodes();
	for (UPCGNode* Node : Nodes)
	{
		if (Node && Node->GetName() == NodeName)
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode)
	{
		return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeName));
	}

	// Get the settings object from the node
	UPCGSettings* Settings = const_cast<UPCGSettings*>(FoundNode->GetSettings());
	if (!Settings)
	{
		return MCPError(TEXT("Node has no settings object"));
	}

	// #149: JSON values drive a recursive property setter that handles
	// TSet/TArray, nested struct objects, UObject/class references by path,
	// and dotted property names (e.g. "SplineMeshDescriptor.StaticMesh").
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> PropertiesToSet;
	if (bUseSettingsObject)
	{
		for (const auto& Pair : (*SettingsObj)->Values)
		{
			PropertiesToSet.Add(TPair<FString, TSharedPtr<FJsonValue>>(FString(*Pair.Key), Pair.Value));
		}
	}
	else
	{
		// Wrap the string value in a JsonValueString so we run through the same path.
		PropertiesToSet.Add(TPair<FString, TSharedPtr<FJsonValue>>(PropertyName, MakeShared<FJsonValueString>(PropertyValue)));
	}

	TSharedPtr<FJsonObject> SetResults = MakeShared<FJsonObject>();
	// The previous value of every property this call lands, as UE export text.
	// SetJsonOnProperty's last resort imports export text from a JSON string, so
	// what is read out here goes back in through the very same setter.
	TSharedPtr<FJsonObject> PreviousSettings = MakeShared<FJsonObject>();
	int32 UncapturedCount = 0;
	TArray<FString> Errors;

	Settings->Modify();

	for (const auto& Prop : PropertiesToSet)
	{
		// Resolve and export BEFORE the write, through the same dotted-path
		// walker the setter uses, so nested and indexed keys capture the leaf
		// the write is about to land on.
		FProperty* LeafProp = nullptr;
		void* LeafAddr = nullptr;
		UObject* LeafOwner = nullptr;
		FString ResolveErr;
		FString PreviousText;
		bool bCaptured = MCPJsonProperty::ResolveDottedPath(Settings, Prop.Key, LeafProp, LeafAddr, LeafOwner, ResolveErr)
			&& LeafProp != nullptr && LeafAddr != nullptr;
		if (bCaptured)
		{
			LeafProp->ExportText_Direct(PreviousText, LeafAddr, LeafAddr, LeafOwner, PPF_None);
			// An empty export is not a value that reads back: the JSON setter's
			// last resort imports the string as export text, and an empty one
			// fails for most property types rather than clearing the value. Drop
			// it from the rollback and count it as uncaptured instead.
			if (PreviousText.IsEmpty()) bCaptured = false;
		}

		FString SubErr;
		if (MCPJsonProperty::SetDottedPropertyFromJson(Settings, Prop.Key, Prop.Value, SubErr))
		{
			SetResults->SetField(Prop.Key, Prop.Value);
			if (bCaptured) PreviousSettings->SetStringField(Prop.Key, PreviousText);
			else ++UncapturedCount;
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("Property '%s': %s"), *Prop.Key, *SubErr));
		}
	}

	Settings->PostEditChange();

	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }

	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodeName"), NodeName);
	Result->SetObjectField(TEXT("setProperties"), SetResults);
	Result->SetObjectField(TEXT("previousProperties"), PreviousSettings);
	if (SetResults->Values.Num() > 0) MCPSetUpdated(Result);
	else Result->SetBoolField(TEXT("unchanged"), true);
	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		for (const FString& Err : Errors)
		{
			ErrorArray.Add(MakeShared<FJsonValueString>(Err));
		}
		Result->SetArrayField(TEXT("errors"), ErrorArray);
	}
	Result->SetBoolField(TEXT("success"), Errors.Num() == 0);

	if (PreviousSettings->Values.Num() > 0)
	{
		// Rollback: the same call, carrying the values read off the node a moment
		// ago. Only the properties that were actually written appear, so a
		// rollback never touches a key this call left alone.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("nodeName"), NodeName);
		Payload->SetObjectField(TEXT("settings"), PreviousSettings);
		MCPSetRollback(Result, TEXT("set_pcg_node_settings"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), UncapturedCount > 0);
		if (UncapturedCount > 0)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("%d written propert(ies) could not be read back before the write (the dotted path did not resolve to a leaf) and are not in the rollback. Everything in previousProperties is restored exactly."),
				UncapturedCount));
		}
	}
	else if (SetResults->Values.Num() > 0)
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("None of the written properties could be read back before the write, so no inverse call is offered rather than one that would write something else. Snapshot with export_pcg_graph and restore with import_pcg_graph replace=true if this has to be undone."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ExecutePCGGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* FoundActor = MCPResolveActor(World, Params, ActorErr);
	if (!FoundActor) return ActorErr;
	ActorLabel = FoundActor->GetActorLabel();

	// Get PCG component from the actor
	UPCGComponent* PCGComp = FoundActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp)
	{
		return MCPError(FString::Printf(TEXT("No PCGComponent found on actor: %s"), *ActorLabel));
	}

	// What the component looked like before: whether it already held generated
	// content, and the seed that content was built from. Both decide which
	// inverse is honest.
	//
	// bGenerated is only meaningful on a NON-partitioned component: the engine
	// documents it as always false for partitionable actors, where the real
	// generation lives on the local components the partition subsystem owns. So
	// a partitioned component is a third state - "cannot tell" - and must not be
	// read as "nothing was generated", which would hand back a cleanup_pcg
	// rollback that destroys content while calling itself lossless.
	const bool bPartitioned = PCGComp->IsPartitioned();
	const bool bWasGenerated = PCGComp->bGenerated;
	const int32 PreviousSeed = PCGComp->Seed;

	// Set seed if provided
	double Seed = 0;
	if (Params->TryGetNumberField(TEXT("seed"), Seed))
	{
		PCGComp->Seed = (int32)Seed;
	}

	// Trigger generation
	PCGComp->Generate();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), FoundActor->GetPathName());
	Result->SetStringField(TEXT("componentName"), PCGComp->GetName());
	if (PCGComp->GetGraph())
	{
		Result->SetStringField(TEXT("graphName"), PCGComp->GetGraph()->GetName());
	}
	Result->SetNumberField(TEXT("seed"), PCGComp->Seed);
	Result->SetNumberField(TEXT("previousSeed"), PreviousSeed);
	Result->SetBoolField(TEXT("wasGenerated"), bWasGenerated);
	Result->SetBoolField(TEXT("partitioned"), bPartitioned);

	if (bPartitioned)
	{
		// Generation state is not readable here, so neither inverse can be
		// offered honestly: cleanup_pcg would destroy content that may have
		// predated this call, and regenerating repeats the destruction.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This component is partitioned, so bGenerated is always false on it and whether it already held generated content cannot be read: the real generation lives on the local components the PCG partition subsystem owns. cleanup_pcg is not offered as the inverse because on a partitioned component it would destroy content this call may not have created. Undo it by regenerating with the seed reported in previousSeed, once you have confirmed what the partition held."));
	}
	else if (!bWasGenerated)
	{
		// Nothing was generated before, so clearing the output is an exact
		// return to the previous state.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), FoundActor->GetPathName());
		Payload->SetBoolField(TEXT("removeComponents"), true);
		MCPSetRollback(Result, TEXT("cleanup_pcg"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), PCGComp->Seed != PreviousSeed);
		if (PCGComp->Seed != PreviousSeed)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("cleanup_pcg removes the content this call generated, which is the state the component was in. It does not put the seed back: this call changed it from %d to %d, and cleanup does not write seeds."),
				PreviousSeed, PCGComp->Seed));
		}
	}
	else if (PCGComp->Seed != PreviousSeed)
	{
		// Content existed and this call rebuilt it under a DIFFERENT seed, so
		// the seed is real state that can be put back. Re-running with the old
		// seed restores it and rebuilds equivalent content from the same graph.
		// The instances that were destroyed, and any hand edits to them, are not
		// recoverable, which is what makes it lossy rather than exact.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), FoundActor->GetPathName());
		Payload->SetNumberField(TEXT("seed"), PreviousSeed);
		MCPSetRollback(Result, TEXT("execute_pcg_graph"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The component already held generated content, which this call destroyed and rebuilt under seed %d. The rollback puts seed %d back and regenerates from it, which reproduces equivalent content from the same graph rather than restoring what was there: the destroyed actors and components are gone, and any hand edits made to them do not come back."),
			PCGComp->Seed, PreviousSeed));
	}
	else
	{
		// Content existed and the seed did not change, so a replay would only
		// destroy and rebuild the same content a second time. That is not an
		// inverse, and force_regenerate_pcg says the same thing about the same
		// situation - the two must not disagree.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The component already held generated content and the seed did not change, so this call destroyed and rebuilt it from the same graph and seed. There is no inverse: running this action again repeats the destruction, and cleanup_pcg would leave the component empty rather than as it was. The content is equivalent to what was there, so this only matters where the generated actors had been edited by hand."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::SpawnPCGVolume(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FString Label = OptionalString(Params, TEXT("label"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckActorLabelExists(World, Label, OnConflict, TEXT("PCGVolume")))
	{
		return Existing;
	}

	// #218: location/extent ship as nested {x,y,z} objects per the TS schema
	// (Vec3). Older calls may pass flat x/y/z and extentX/Y/Z; accept both.
	auto ReadVec3 = [&](const TCHAR* ObjKey, const TCHAR* FlatX, const TCHAR* FlatY, const TCHAR* FlatZ, FVector& Out) -> bool
	{
		bool bAny = false;
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Params->TryGetObjectField(ObjKey, Obj) && Obj && Obj->IsValid())
		{
			double V = 0;
			if ((*Obj)->TryGetNumberField(TEXT("x"), V)) { Out.X = V; bAny = true; }
			if ((*Obj)->TryGetNumberField(TEXT("y"), V)) { Out.Y = V; bAny = true; }
			if ((*Obj)->TryGetNumberField(TEXT("z"), V)) { Out.Z = V; bAny = true; }
		}
		double V = 0;
		if (Params->TryGetNumberField(FlatX, V)) { Out.X = V; bAny = true; }
		if (Params->TryGetNumberField(FlatY, V)) { Out.Y = V; bAny = true; }
		if (Params->TryGetNumberField(FlatZ, V)) { Out.Z = V; bAny = true; }
		return bAny;
	};

	FVector Location = FVector::ZeroVector;
	ReadVec3(TEXT("location"), TEXT("x"), TEXT("y"), TEXT("z"), Location);

	FVector Extent(500.0, 500.0, 500.0);
	ReadVec3(TEXT("extent"), TEXT("extentX"), TEXT("extentY"), TEXT("extentZ"), Extent);

	// Spawn PCG Volume actor
	FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	APCGVolume* PCGVolumeActor = World->SpawnActor<APCGVolume>(APCGVolume::StaticClass(), SpawnTransform);
	if (!PCGVolumeActor)
	{
		return MCPError(TEXT("Failed to spawn PCGVolume actor"));
	}

	UEMCP::BuildVolumeAsCube(World, PCGVolumeActor, Extent);

	if (!Label.IsEmpty())
	{
		PCGVolumeActor->SetActorLabel(Label);
	}

	FString GraphPath = OptionalString(Params, TEXT("graphPath"));
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	TSharedPtr<FJsonObject> RBPayload = MakeShared<FJsonObject>();
	RBPayload->SetStringField(TEXT("actorLabel"), PCGVolumeActor->GetActorLabel());
	MCPSetRollback(Result, TEXT("delete_actor"), RBPayload);

	if (!GraphPath.IsEmpty())
	{
		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
		if (Graph)
		{
			UPCGComponent* PCGComp = PCGVolumeActor->FindComponentByClass<UPCGComponent>();
			if (PCGComp)
			{
				PCGComp->SetGraph(Graph);
				Result->SetStringField(TEXT("graphPath"), GraphPath);
				Result->SetStringField(TEXT("graphName"), Graph->GetName());
			}
		}
		else
		{
			Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("PCGGraph not found: %s - volume spawned without graph"), *GraphPath));
		}
	}

	Result->SetStringField(TEXT("actorName"), PCGVolumeActor->GetActorLabel());
	Result->SetStringField(TEXT("actorClass"), PCGVolumeActor->GetClass()->GetName());

	TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);
	Result->SetObjectField(TEXT("location"), LocationObj);

	TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
	ExtentObj->SetNumberField(TEXT("x"), Extent.X);
	ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
	ExtentObj->SetNumberField(TEXT("z"), Extent.Z);
	Result->SetObjectField(TEXT("extent"), ExtentObj);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ReadPCGNodeSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString NodeName;
	if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	// Find the node by name
	UPCGNode* FoundNode = nullptr;
	const auto& Nodes = Graph->GetNodes();
	for (UPCGNode* Node : Nodes)
	{
		if (Node && Node->GetName() == NodeName)
		{
			FoundNode = Node;
			break;
		}
	}

	// Also check input/output nodes
	if (!FoundNode && Graph->GetInputNode() && Graph->GetInputNode()->GetName() == NodeName)
	{
		FoundNode = Graph->GetInputNode();
	}
	if (!FoundNode && Graph->GetOutputNode() && Graph->GetOutputNode()->GetName() == NodeName)
	{
		FoundNode = Graph->GetOutputNode();
	}

	if (!FoundNode)
	{
		return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeName));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("nodeName"), FoundNode->GetName());
	Result->SetStringField(TEXT("nodeTitle"), FoundNode->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());

	// Read the settings object properties
	const UPCGSettings* Settings = FoundNode->GetSettings();
	if (!Settings)
	{
		Result->SetStringField(TEXT("note"), TEXT("Node has no settings object"));
		return MCPResult(Result);
	}

	Result->SetStringField(TEXT("settingsClass"), Settings->GetClass()->GetName());

	// #214 / #215: also emit a fully-keyed structured form alongside the
	// ExportText round-trip. UScriptStruct::ExportText skips fields equal to
	// the struct's CDO, so callers that diffed write/read couldn't see whether
	// a key was actually applied (e.g. ActorSelection=ByTag, bMustOverlapSelf=false).
	// The structured form recurses into struct fields and emits every key.
	std::function<TSharedPtr<FJsonValue>(FProperty*, const void*)> SerializeProp;
	SerializeProp = [&SerializeProp](FProperty* Prop, const void* Addr) -> TSharedPtr<FJsonValue>
	{
		if (!Prop || !Addr) return MakeShared<FJsonValueNull>();

		if (FStructProperty* SP = CastField<FStructProperty>(Prop))
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(SP->Struct); It; ++It)
			{
				FProperty* Inner = *It;
				if (!Inner) continue;
				const void* InnerAddr = Inner->ContainerPtrToValuePtr<void>(Addr);
				Obj->SetField(Inner->GetName(), SerializeProp(Inner, InnerAddr));
			}
			return MakeShared<FJsonValueObject>(Obj);
		}
		if (FArrayProperty* AP = CastField<FArrayProperty>(Prop))
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			FScriptArrayHelper H(AP, Addr);
			for (int32 i = 0; i < H.Num(); ++i)
			{
				Items.Add(SerializeProp(AP->Inner, H.GetRawPtr(i)));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			return MakeShared<FJsonValueBoolean>(BP->GetPropertyValue(Addr));
		}
		if (FNumericProperty* NP = CastField<FNumericProperty>(Prop))
		{
			if (NP->IsFloatingPoint())
			{
				return MakeShared<FJsonValueNumber>(NP->GetFloatingPointPropertyValue(Addr));
			}
			return MakeShared<FJsonValueNumber>((double)NP->GetSignedIntPropertyValue(Addr));
		}
		if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
		{
			UObject* Ref = OP->GetObjectPropertyValue(Addr);
			return MakeShared<FJsonValueString>(Ref ? Ref->GetPathName() : TEXT(""));
		}
		// Enums and remaining scalars: ExportTextItem with no Defaults emits the literal.
		FString Out;
		Prop->ExportTextItem_Direct(Out, Addr, nullptr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Out);
	};

	// Enumerate all editable properties on the settings
	TSharedPtr<FJsonObject> PropertiesObj = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> PropIt(Settings->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property) continue;

		// Only include properties that are editable and visible
		if (!Property->HasAnyPropertyFlags(CPF_Edit)) continue;

		FString PropertyName = Property->GetName();
		FString PropertyValue;
		const void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(Settings);
		Property->ExportTextItem_Direct(PropertyValue, PropertyAddr, nullptr, nullptr, PPF_None);

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("value"), PropertyValue);
		PropObj->SetStringField(TEXT("type"), Property->GetCPPType());
		PropObj->SetField(TEXT("structured"), SerializeProp(Property, PropertyAddr));
		PropertiesObj->SetObjectField(PropertyName, PropObj);
	}

	Result->SetObjectField(TEXT("settings"), PropertiesObj);

	// List input/output pins
	TArray<TSharedPtr<FJsonValue>> InputPinsArray;
	const TArray<TObjectPtr<UPCGPin>>& InPins = FoundNode->GetInputPins();
	for (const TObjectPtr<UPCGPin>& Pin : InPins)
	{
		if (!Pin) continue;
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
		InputPinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	Result->SetArrayField(TEXT("inputPins"), InputPinsArray);

	TArray<TSharedPtr<FJsonValue>> OutputPinsArray;
	const TArray<TObjectPtr<UPCGPin>>& OutPins = FoundNode->GetOutputPins();
	for (const TObjectPtr<UPCGPin>& Pin : OutPins)
	{
		if (!Pin) continue;
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
		OutputPinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	Result->SetArrayField(TEXT("outputPins"), OutputPinsArray);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::GetPCGComponentDetails(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* FoundActor = MCPResolveActor(World, Params, ActorErr);
	if (!FoundActor) return ActorErr;
	ActorLabel = FoundActor->GetActorLabel();

	// Get all PCG components on the actor
	TArray<UPCGComponent*> PCGComps;
	FoundActor->GetComponents<UPCGComponent>(PCGComps);

	if (PCGComps.Num() == 0)
	{
		return MCPError(FString::Printf(TEXT("No PCGComponent found on actor: %s"), *ActorLabel));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), FoundActor->GetPathName());
	Result->SetStringField(TEXT("actorClass"), FoundActor->GetClass()->GetName());

	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	for (UPCGComponent* PCGComp : PCGComps)
	{
		if (!PCGComp) continue;

		TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
		CompObj->SetStringField(TEXT("componentName"), PCGComp->GetName());
		CompObj->SetNumberField(TEXT("seed"), PCGComp->Seed);
		CompObj->SetBoolField(TEXT("activated"), PCGComp->bActivated);

		// Generation trigger
		FString GenTriggerStr;
		switch (PCGComp->GenerationTrigger)
		{
		case EPCGComponentGenerationTrigger::GenerateOnLoad:
			GenTriggerStr = TEXT("GenerateOnLoad");
			break;
		case EPCGComponentGenerationTrigger::GenerateOnDemand:
			GenTriggerStr = TEXT("GenerateOnDemand");
			break;
		default:
			GenTriggerStr = TEXT("Unknown");
			break;
		}
		CompObj->SetStringField(TEXT("generationTrigger"), GenTriggerStr);

		// Graph details
		UPCGGraph* Graph = const_cast<UPCGGraph*>(PCGComp->GetGraph());
		if (Graph)
		{
			TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
			GraphObj->SetStringField(TEXT("name"), Graph->GetName());
			GraphObj->SetStringField(TEXT("path"), Graph->GetPathName());
			GraphObj->SetNumberField(TEXT("nodeCount"), Graph->GetNodes().Num());
			CompObj->SetObjectField(TEXT("graph"), GraphObj);
		}

		// Location info
		FVector CompLocation = PCGComp->GetOwner() ? PCGComp->GetOwner()->GetActorLocation() : FVector::ZeroVector;
		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), CompLocation.X);
		LocObj->SetNumberField(TEXT("y"), CompLocation.Y);
		LocObj->SetNumberField(TEXT("z"), CompLocation.Z);
		CompObj->SetObjectField(TEXT("location"), LocObj);

		ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
	}

	Result->SetArrayField(TEXT("components"), ComponentsArray);
	Result->SetNumberField(TEXT("componentCount"), ComponentsArray.Num());
	return MCPResult(Result);
}

// ─── #145 set_static_mesh_spawner_meshes ────────────────────────────
// Populates the MeshEntries array on a PCGStaticMeshSpawner node's
// weighted mesh selector. set_pcg_node_settings can't reach into
// instanced subobject properties (MeshSelectorParameters.MeshEntries),
// so this dedicated helper handles the common scatter-graph authoring case.
TSharedPtr<FJsonValue> FPCGHandlers::SetStaticMeshSpawnerMeshes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString NodeName;
	if (auto Err = RequireString(Params, TEXT("nodeName"), NodeName)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* EntriesArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("entries"), EntriesArr) || !EntriesArr)
	{
		return MCPError(TEXT("Missing 'entries' array - each item should be {mesh: <path>, weight?: <int>}"));
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph) return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));

	UPCGNode* FoundNode = nullptr;
	for (UPCGNode* Node : Graph->GetNodes())
	{
		if (Node && Node->GetName() == NodeName)
		{
			FoundNode = Node;
			break;
		}
	}
	if (!FoundNode) return MCPError(FString::Printf(TEXT("Node not found: %s"), *NodeName));

	UPCGStaticMeshSpawnerSettings* SpawnerSettings = Cast<UPCGStaticMeshSpawnerSettings>(const_cast<UPCGSettings*>(FoundNode->GetSettings()));
	if (!SpawnerSettings)
	{
		return MCPError(FString::Printf(TEXT("Node '%s' is not a PCGStaticMeshSpawner"), *NodeName));
	}

	// Ensure the selector is UPCGMeshSelectorWeighted; instantiate if missing/mismatched.
	// Swapping the selector destroys the previous one outright, which is a change
	// this action cannot undo - record what it was so the note can name it.
	UPCGMeshSelectorWeighted* WeightedSelector = Cast<UPCGMeshSelectorWeighted>(SpawnerSettings->MeshSelectorParameters);
	FString ReplacedSelectorClass;
	if (!WeightedSelector)
	{
		if (SpawnerSettings->MeshSelectorParameters)
		{
			ReplacedSelectorClass = SpawnerSettings->MeshSelectorParameters->GetClass()->GetName();
		}
		SpawnerSettings->SetMeshSelectorType(UPCGMeshSelectorWeighted::StaticClass());
		WeightedSelector = Cast<UPCGMeshSelectorWeighted>(SpawnerSettings->MeshSelectorParameters);
	}
	if (!WeightedSelector)
	{
		return MCPError(TEXT("Failed to configure UPCGMeshSelectorWeighted on spawner"));
	}

	const bool bReplace = OptionalBool(Params, TEXT("replace"), true);

	// The entry list as found, in the only shape this action can be handed back:
	// {mesh, weight}. That is NOT the whole entry. Each one carries a full
	// FPCGSoftISMComponentDescriptor - material overrides, collision profile,
	// cull distances, component tags - and the parser below rebuilds an entry
	// from the mesh and weight alone, defaulting the rest. Since the rollback
	// replays with replace=true, restoring the list DISCARDS every descriptor,
	// so a non-empty previous list always makes the rollback lossy.
	TArray<TSharedPtr<FJsonValue>> PreviousEntries;
	bool bPreviousEntryHadNoMesh = false;
	for (const FPCGMeshSelectorWeightedEntry& Existing : WeightedSelector->MeshEntries)
	{
		TSharedPtr<FJsonObject> EntryJson = MakeShared<FJsonObject>();
		EntryJson->SetStringField(TEXT("mesh"), Existing.Descriptor.StaticMesh.ToString());
		EntryJson->SetNumberField(TEXT("weight"), Existing.Weight);
		PreviousEntries.Add(MakeShared<FJsonValueObject>(EntryJson));
		if (Existing.Descriptor.StaticMesh.IsNull()) bPreviousEntryHadNoMesh = true;
	}
	const bool bDescriptorsWillBeLost = PreviousEntries.Num() > 0;

	TArray<FPCGMeshSelectorWeightedEntry> Rebuilt;
	if (!bReplace)
	{
		Rebuilt = WeightedSelector->MeshEntries;
	}

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& V : *EntriesArr)
	{
		const TSharedPtr<FJsonObject>* EObj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(EObj) || !EObj) continue;
		FString MeshPath;
		if (!(*EObj)->TryGetStringField(TEXT("mesh"), MeshPath) || MeshPath.IsEmpty()) continue;

		int32 Weight = 1;
		double WeightD = 1.0;
		if ((*EObj)->TryGetNumberField(TEXT("weight"), WeightD)) Weight = FMath::Max(0, (int32)WeightD);

		TSoftObjectPtr<UStaticMesh> MeshRef;
		MeshRef = FSoftObjectPath(MeshPath);
		FPCGMeshSelectorWeightedEntry Entry(MeshRef, Weight);
		Rebuilt.Add(MoveTemp(Entry));
		Added++;
	}

	WeightedSelector->Modify();
	WeightedSelector->MeshEntries = MoveTemp(Rebuilt);
#if WITH_EDITOR
	WeightedSelector->RefreshDisplayNames();
#endif

	SpawnerSettings->Modify();

	// Persist the asset
	SaveAssetPackage(Graph);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodeName"), NodeName);
	Result->SetNumberField(TEXT("entriesAdded"), Added);
	Result->SetNumberField(TEXT("totalEntries"), WeightedSelector->MeshEntries.Num());
	Result->SetBoolField(TEXT("replaced"), bReplace);
	Result->SetArrayField(TEXT("previousEntries"), PreviousEntries);

	// Rollback: write the entry list back exactly as it was found, with
	// replace=true so the entries this call appended or overwrote are gone
	// rather than merged with the restored ones.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("nodeName"), NodeName);
	Payload->SetArrayField(TEXT("entries"), PreviousEntries);
	Payload->SetBoolField(TEXT("replace"), true);
	MCPSetRollback(Result, TEXT("set_static_mesh_spawner_meshes"), Payload);
	const bool bLossy = bDescriptorsWillBeLost || bPreviousEntryHadNoMesh || !ReplacedSelectorClass.IsEmpty();
	Result->SetBoolField(TEXT("rollbackLossy"), bLossy);
	if (!ReplacedSelectorClass.IsEmpty())
	{
		Result->SetStringField(TEXT("replacedMeshSelectorClass"), ReplacedSelectorClass);
	}
	if (bLossy)
	{
		FString Note;
		if (bDescriptorsWillBeLost)
		{
			Note += FString::Printf(
				TEXT("The rollback restores the mesh and weight of the %d entr(ies) that were here, and replays with replace=true, which rebuilds each entry from {mesh, weight} alone. Every entry's ISM descriptor is therefore DISCARDED: material overrides, collision profile, cull distances and component tags all return to their defaults. "),
				PreviousEntries.Num());
		}
		if (bPreviousEntryHadNoMesh)
		{
			Note += TEXT("One or more entries had no static mesh set; an entry with an empty mesh is skipped on the way back in and its slot in the list disappears. ");
		}
		if (!ReplacedSelectorClass.IsEmpty())
		{
			Note += FString::Printf(
				TEXT("This node also used a %s mesh selector, which was replaced outright by a weighted one. The previous selector and its configuration do not come back - that object is gone and this action only writes weighted entries. Set the selector type in the PCG editor to undo it."),
				*ReplacedSelectorClass);
		}
		Result->SetStringField(TEXT("rollbackNote"), Note.TrimEnd());
	}
	return MCPResult(Result);
}

namespace
{
	// Locate a UPCGComponent by actor label. Returns error JsonValue on failure.
	// #983: takes the parameter bag so it can read actorPath, and refuses a
	// label that names more than one actor rather than regenerating whichever
	// PCG volume the iterator reached first.
	TSharedPtr<FJsonValue> FindPCGComponentByLabel(const TSharedPtr<FJsonObject>& Params, UPCGComponent*& OutComp, AActor*& OutActor)
	{
		OutComp = nullptr;
		OutActor = nullptr;

		UWorld* World = nullptr;
		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			World = GEditor->GetEditorWorldContext().World();
		}
		if (!World) return MCPError(TEXT("Editor world not available"));

		TSharedPtr<FJsonValue> ActorErr;
		OutActor = MCPResolveActor(World, Params, ActorErr);
		if (!OutActor) return ActorErr;
		const FString ActorLabel = OutActor->GetActorLabel();
		OutComp = OutActor->FindComponentByClass<UPCGComponent>();
		if (!OutComp) return MCPError(FString::Printf(TEXT("No PCGComponent on actor: %s"), *ActorLabel));
		return nullptr;
	}
}

// ─── #146 force_regenerate / cleanup / toggle_graph ──────────────────
// The set-graph-to-null + re-set workaround is the only reliable way to
// unstick a PCG component whose graph state desynced from the executor
// (common after editor restart or graph edits). The feedback log showed
// this pattern worked consistently; raw Generate(true) alone did not.
TSharedPtr<FJsonValue> FPCGHandlers::ForceRegeneratePCG(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	UPCGComponent* PCGComp = nullptr; AActor* Actor = nullptr;
	if (auto Err = FindPCGComponentByLabel(Params, PCGComp, Actor)) return Err;
	ActorLabel = Actor->GetActorLabel();

	UPCGGraph* OriginalGraph = PCGComp->GetGraph();
	if (!OriginalGraph)
	{
		return MCPError(FString::Printf(TEXT("PCGComponent on '%s' has no graph assigned"), *ActorLabel));
	}

	// bGenerated is always false on a partitioned component (the engine says so
	// on the flag itself), so it cannot be read as "nothing was generated" here.
	const bool bPartitioned = PCGComp->IsPartitioned();
	const bool bWasGenerated = PCGComp->bGenerated;

	// Full reset: clear → re-assign → cleanup → generate. UE 5.7's
	// UPCGComponent::Cleanup takes a single bRemoveComponents arg.
	PCGComp->SetGraph(nullptr);
	PCGComp->SetGraph(OriginalGraph);
	PCGComp->Cleanup(/*bRemoveComponents*/ true);
	PCGComp->Generate(/*bForce*/ true);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), PCGComp->GetName());
	Result->SetStringField(TEXT("graphName"), OriginalGraph->GetName());
	Result->SetStringField(TEXT("graphPath"), OriginalGraph->GetPathName());
	Result->SetBoolField(TEXT("regenerated"), true);
	Result->SetBoolField(TEXT("wasGenerated"), bWasGenerated);
	Result->SetBoolField(TEXT("partitioned"), bPartitioned);

	if (bPartitioned)
	{
		// Generation state is unreadable on a partitioned component, so
		// cleanup_pcg cannot be offered: it would destroy content this call may
		// not have created, while reporting the undo as exact.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This component is partitioned, so bGenerated is always false on it and whether it already held generated content cannot be read: the real generation lives on the local components the PCG partition subsystem owns. cleanup_pcg is not offered as the inverse because on a partitioned component it would destroy content this call may not have created."));
	}
	else if (!bWasGenerated)
	{
		// Nothing had been generated, so clearing the output puts the component
		// back exactly where it was.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Payload->SetBoolField(TEXT("removeComponents"), true);
		MCPSetRollback(Result, TEXT("cleanup_pcg"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		// No inverse. The graph and seed did not change, so the content is
		// equivalent to what was destroyed - but the destroyed instances are
		// gone, and running this action again would only destroy and rebuild
		// them a second time rather than restore anything. cleanup_pcg would
		// leave the component empty, which is not the state it was in.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The component already held generated content, which this call destroyed and rebuilt from the same graph and seed. There is no inverse: regenerating again repeats the destruction, and cleanup_pcg would leave the component empty rather than as it was. The content is equivalent to what was there, so this only matters where the generated actors had been edited by hand."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::CleanupPCG(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	UPCGComponent* PCGComp = nullptr; AActor* Actor = nullptr;
	if (auto Err = FindPCGComponentByLabel(Params, PCGComp, Actor)) return Err;
	ActorLabel = Actor->GetActorLabel();

	const bool bRemoveComponents = OptionalBool(Params, TEXT("removeComponents"), true);
	// bGenerated is always false on a partitioned component, so it must not be
	// reported as "nothing was removed" there: the content it cleaned lives on
	// the local components the partition subsystem owns.
	const bool bPartitioned = PCGComp->IsPartitioned();
	const bool bWasGenerated = PCGComp->bGenerated;
	const bool bHasGraph = PCGComp->GetGraph() != nullptr;
	PCGComp->Cleanup(bRemoveComponents);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), PCGComp->GetName());
	Result->SetBoolField(TEXT("removeComponents"), bRemoveComponents);
	Result->SetBoolField(TEXT("cleaned"), true);
	Result->SetBoolField(TEXT("wasGenerated"), bWasGenerated);
	Result->SetBoolField(TEXT("partitioned"), bPartitioned);

	if (bPartitioned && bHasGraph)
	{
		// Whether anything was removed is unreadable here, so this must not
		// claim "unchanged". Regenerating is still the intended inverse of a
		// cleanup on a partitioned component, with the caveat spelled out.
		MCPSetUpdated(Result);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		MCPSetRollback(Result, TEXT("force_regenerate_pcg"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This component is partitioned, so bGenerated is always false on it and whether this call removed anything cannot be read: the real generation lives on the local components the PCG partition subsystem owns. The rollback regenerates from the component's graph and seed, which are unchanged. It reproduces equivalent content rather than restoring the exact instances, and if the partition happened to hold nothing it CREATES content where there was none."));
	}
	else if (bPartitioned)
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("This component is partitioned, so whether this call removed anything cannot be read, and it has no graph assigned, so nothing can regenerate what it may have removed."));
	}
	else if (!bWasGenerated)
	{
		// Nothing had been generated, so nothing was removed.
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The component held no generated content, so this call removed nothing and there is nothing to undo."));
	}
	else if (bHasGraph)
	{
		MCPSetUpdated(Result);
		// PCG output is a function of the graph and the seed, both untouched by a
		// cleanup, so regenerating reproduces equivalent content.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		MCPSetRollback(Result, TEXT("force_regenerate_pcg"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The rollback regenerates from the component's graph and seed, which are unchanged, so the content that comes back is equivalent to what was removed. It is not the same content: the actors and components that were destroyed are gone, so any edit made to them by hand after they were generated does not come back."));
	}
	else
	{
		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The component has no graph assigned, so nothing can regenerate the content this call removed. Assign a graph with toggle_pcg_graph before regenerating."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPCGHandlers::ToggleGraphPCG(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	UPCGComponent* PCGComp = nullptr; AActor* Actor = nullptr;
	if (auto Err = FindPCGComponentByLabel(Params, PCGComp, Actor)) return Err;
	ActorLabel = Actor->GetActorLabel();

	// If the caller supplies graphPath, load and use that; otherwise re-apply the current graph.
	FString GraphPath;
	UPCGGraph* TargetGraph = nullptr;
	if (Params->TryGetStringField(TEXT("graphPath"), GraphPath) && !GraphPath.IsEmpty())
	{
		TargetGraph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
		if (!TargetGraph) return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *GraphPath));
	}
	else
	{
		TargetGraph = PCGComp->GetGraph();
		if (!TargetGraph) return MCPError(FString::Printf(TEXT("No graph on '%s' and no graphPath provided"), *ActorLabel));
	}

	// The graph the component had. When the caller named a different one this is
	// the only thing that undoes the swap; when they named none, this call is a
	// deliberate re-apply of the same graph and changes no state at all.
	UPCGGraph* PreviousGraph = PCGComp->GetGraph();

	PCGComp->SetGraph(nullptr);
	PCGComp->SetGraph(TargetGraph);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), PCGComp->GetName());
	Result->SetStringField(TEXT("graphName"), TargetGraph->GetName());
	Result->SetStringField(TEXT("graphPath"), TargetGraph->GetPathName());
	Result->SetBoolField(TEXT("toggled"), true);
	if (PreviousGraph)
	{
		Result->SetStringField(TEXT("previousGraphPath"), PreviousGraph->GetPathName());
	}

	if (PreviousGraph == TargetGraph)
	{
		// Cleared and re-set the same graph: a reinit kick, not a state change.
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The component's graph assignment is the one it already had; clearing and re-setting it forces a reinit and leaves no state change to undo."));
	}
	else if (PreviousGraph)
	{
		MCPSetUpdated(Result);
		// Rollback: assign the graph it had, through the same action.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Payload->SetStringField(TEXT("graphPath"), PreviousGraph->GetPathName());
		MCPSetRollback(Result, TEXT("toggle_pcg_graph"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The component had no graph assigned before this call, and toggle_pcg_graph cannot clear an assignment: it requires a graph to set, and with no graphPath it re-applies the current one. Clear it with editor(set_property) on the component if the change has to be undone."));
	}
	return MCPResult(Result);
}

// ─── #213 import_pcg_graph / export_pcg_graph ────────────────────────
// Bulk JSON-driven graph authoring. Mirrors material.import_graph: one tool
// call replaces N add_node + M connect_nodes + K set_node_settings round-trips.
// Operates directly on the runtime UPCGGraph (no editor-graph required).
TSharedPtr<FJsonValue> FPCGHandlers::ImportGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr)
	{
		return MCPError(TEXT("Missing 'nodes' array"));
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	const bool bReplace = OptionalBool(Params, TEXT("replace"), false);

	// Snapshot the graph before touching it. export_pcg_graph and this action
	// speak the same vocabulary by design (#213), so the export IS the inverse
	// payload: replayed with replace=true it wipes whatever this import leaves
	// and rebuilds what was here. That works for an additive import too, since
	// replacing with the previous contents removes the nodes it added.
	//
	// Cost, stated rather than hidden: ExportGraph walks a process-wide
	// TObjectIterator<UEdGraphNode> to recover hand-laid-out editor positions,
	// so every import now pays one object-iterator pass. That is small beside
	// what an import already does - instantiate N settings objects, rebuild the
	// edges, PostEditChange and save the package - and it buys the only exact
	// inverse this action can have, so it is taken unconditionally rather than
	// behind a flag the TS surface would have to grow.
	TSharedPtr<FJsonObject> Snapshot;
	{
		TSharedPtr<FJsonValue> Exported = ExportGraph(Params);
		const TSharedPtr<FJsonObject>* ExportedObj = nullptr;
		if (Exported.IsValid() && Exported->TryGetObject(ExportedObj) && ExportedObj && (*ExportedObj).IsValid()
			&& (*ExportedObj)->HasField(TEXT("nodes")))
		{
			Snapshot = *ExportedObj;
		}
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "ImportPCGGraph", "Import PCG Graph"));
	Graph->Modify();

	int32 Removed = 0;
	if (bReplace)
	{
		// Snapshot first; RemoveNode mutates the graph's node list.
		TArray<UPCGNode*> Existing;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node) Existing.Add(Node);
		}
		for (UPCGNode* Node : Existing)
		{
			Graph->RemoveNode(Node);
			++Removed;
		}
	}

	// User-supplied JSON `name` is a local identifier only; the engine assigns
	// the actual UPCGNode name on creation. Edges reference these local names.
	TMap<FString, UPCGNode*> ByLocalName;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	int32 NodesCreated = 0;
	int32 SettingsApplied = 0;

	for (const TSharedPtr<FJsonValue>& V : *NodesArr)
	{
		const TSharedPtr<FJsonObject>* NodeObj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(NodeObj) || !NodeObj || !(*NodeObj).IsValid()) continue;

		FString LocalName;
		(*NodeObj)->TryGetStringField(TEXT("name"), LocalName);

		FString ClassName;
		if (!(*NodeObj)->TryGetStringField(TEXT("class"), ClassName) || ClassName.IsEmpty())
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s': missing 'class'"), *LocalName)));
			continue;
		}

		UClass* SettingsClass = FindPCGSettingsClass(ClassName);
		if (!SettingsClass)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s': class not found or invalid: %s"), *LocalName, *ClassName)));
			continue;
		}

		UPCGSettings* DefaultSettings = NewObject<UPCGSettings>(Graph, SettingsClass, NAME_None, RF_Transactional);
		if (!DefaultSettings)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s': failed to instantiate settings"), *LocalName)));
			continue;
		}

		UPCGNode* NewNode = Graph->AddNodeInstance(DefaultSettings);
		if (!NewNode)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s': AddNodeInstance failed"), *LocalName)));
			continue;
		}

		if (DefaultSettings->GetOuter() != NewNode && !DefaultSettings->GetOuter()->IsA<UPackage>())
		{
			DefaultSettings->Rename(nullptr, NewNode, REN_DontCreateRedirectors | REN_DoNotDirty);
		}

		// #236: preserve the user-supplied node name when there's no collision.
		// AddNodeInstance assigns engine-derived names (e.g. SurfaceSampler_0),
		// which makes export -> import not name-stable. Rename the new node
		// to the local id when nothing else in the graph already uses it.
		if (!LocalName.IsEmpty() && NewNode->GetName() != LocalName)
		{
#if UE_MCP_HAS_STATIC_FIND_OBJECT_FLAGS
			const bool bClashes = (StaticFindObject(nullptr, NewNode->GetOuter(), *LocalName, EFindObjectFlags::ExactClass) != nullptr);
#else
			const bool bClashes = (StaticFindObject(nullptr, NewNode->GetOuter(), *LocalName, true) != nullptr);
#endif
			if (!bClashes)
			{
				NewNode->Rename(*LocalName, NewNode->GetOuter(), REN_DontCreateRedirectors | REN_DoNotDirty);
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s': name collision, kept engine-assigned '%s'"), *LocalName, *NewNode->GetName())));
			}
		}

		double PosX = 0, PosY = 0;
		if ((*NodeObj)->TryGetNumberField(TEXT("posX"), PosX)) NewNode->PositionX = (int32)PosX;
		if ((*NodeObj)->TryGetNumberField(TEXT("posY"), PosY)) NewNode->PositionY = (int32)PosY;

		// Apply settings via the same dotted-path JSON setter used by
		// set_pcg_node_settings (#149).
		const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
		if ((*NodeObj)->TryGetObjectField(TEXT("settings"), SettingsObj) && SettingsObj && (*SettingsObj).IsValid())
		{
			DefaultSettings->Modify();
			for (const auto& Pair : (*SettingsObj)->Values)
			{
				const FString SettingName(*Pair.Key);
				FString SubErr;
				if (MCPJsonProperty::SetDottedPropertyFromJson(DefaultSettings, SettingName, Pair.Value, SubErr))
				{
					++SettingsApplied;
				}
				else
				{
					Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("node '%s' setting '%s': %s"), *LocalName, *SettingName, *SubErr)));
				}
			}
			DefaultSettings->PostEditChange();
		}

		NewNode->PostEditChange();

		if (!LocalName.IsEmpty())
		{
			ByLocalName.Add(LocalName, NewNode);
		}
		++NodesCreated;
	}

	// Connections. Resolve names via the local-name map first, then fall back
	// to the graph's own nodes (Input/Output/already-existing) by engine name.
	const TArray<TSharedPtr<FJsonValue>>* ConnsArr = nullptr;
	int32 ConnectionsMade = 0;
	if (Params->TryGetArrayField(TEXT("connections"), ConnsArr) && ConnsArr)
	{
		auto Resolve = [&](const FString& Name) -> UPCGNode*
		{
			if (UPCGNode** Found = ByLocalName.Find(Name); Found && *Found) return *Found;
			return FindPCGNodeByName(Graph, Name);
		};

		auto FirstOutputPin = [](UPCGNode* N) -> FName
		{
			if (!N) return NAME_None;
			const auto& Pins = N->GetOutputPins();
			return (Pins.Num() > 0 && Pins[0]) ? Pins[0]->Properties.Label : NAME_None;
		};

		auto FirstInputPin = [](UPCGNode* N) -> FName
		{
			if (!N) return NAME_None;
			const auto& Pins = N->GetInputPins();
			return (Pins.Num() > 0 && Pins[0]) ? Pins[0]->Properties.Label : NAME_None;
		};

		for (const TSharedPtr<FJsonValue>& V : *ConnsArr)
		{
			const TSharedPtr<FJsonObject>* CObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(CObj) || !CObj || !(*CObj).IsValid()) continue;

			FString From, To, FromPin, ToPin;
			(*CObj)->TryGetStringField(TEXT("from"), From);
			(*CObj)->TryGetStringField(TEXT("to"), To);
			(*CObj)->TryGetStringField(TEXT("fromPin"), FromPin);
			(*CObj)->TryGetStringField(TEXT("toPin"), ToPin);

			UPCGNode* SrcNode = Resolve(From);
			UPCGNode* DstNode = Resolve(To);
			if (!SrcNode)
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("connection: source not found: %s"), *From)));
				continue;
			}
			if (!DstNode)
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("connection: target not found: %s"), *To)));
				continue;
			}

			const FName SrcPinName = FromPin.IsEmpty() ? FirstOutputPin(SrcNode) : FName(*FromPin);
			const FName DstPinName = ToPin.IsEmpty() ? FirstInputPin(DstNode) : FName(*ToPin);

			if (SrcPinName == NAME_None || DstPinName == NAME_None)
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("connection %s.%s -> %s.%s: pin not resolvable"), *From, *FromPin, *To, *ToPin)));
				continue;
			}

			SrcNode->Modify();
			DstNode->Modify();
			if (!Graph->AddEdge(SrcNode, SrcPinName, DstNode, DstPinName))
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("connection %s.%s -> %s.%s: AddEdge failed (incompatible or duplicate?)"), *From, *SrcPinName.ToString(), *To, *DstPinName.ToString())));
				continue;
			}
			// #304: verify the UPCGEdge object was actually instantiated. Some
			// pin/label combinations return success from AddEdge while leaving
			// no edge in either pin's Edges array; those phantom connections
			// were previously counted toward connectionsMade and made the
			// import look healthier than the on-disk asset.
			UPCGPin* VerifySrcPin = nullptr;
			UPCGPin* VerifyDstPin = nullptr;
			for (const TObjectPtr<UPCGPin>& P : SrcNode->GetOutputPins())
			{
				if (P && P->Properties.Label == SrcPinName) { VerifySrcPin = P; break; }
			}
			for (const TObjectPtr<UPCGPin>& P : DstNode->GetInputPins())
			{
				if (P && P->Properties.Label == DstPinName) { VerifyDstPin = P; break; }
			}
			bool bVerified = false;
			if (VerifySrcPin && VerifyDstPin)
			{
				for (const TObjectPtr<UPCGEdge>& Edge : VerifySrcPin->Edges)
				{
					if (Edge && Edge->InputPin == VerifySrcPin && Edge->OutputPin == VerifyDstPin)
					{
						bVerified = true;
						break;
					}
				}
			}
			if (bVerified)
			{
				++ConnectionsMade;
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
					TEXT("connection %s.%s -> %s.%s: AddEdge returned success but no UPCGEdge persisted (pin label mismatch?)"),
					*From, *SrcPinName.ToString(), *To, *DstPinName.ToString())));
			}
		}
	}

	Graph->PostEditChange();
	if (UPackage* Pkg = Graph->GetOutermost()) { Pkg->MarkPackageDirty(); }
	UEditorAssetLibrary::SaveLoadedAsset(Graph, /*bOnlyIfIsDirty=*/false);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("replace"), bReplace);
	Result->SetNumberField(TEXT("nodesRemoved"), Removed);
	Result->SetNumberField(TEXT("nodesCreated"), NodesCreated);
	Result->SetNumberField(TEXT("connectionsMade"), ConnectionsMade);
	Result->SetNumberField(TEXT("settingsApplied"), SettingsApplied);
	if (Warnings.Num() > 0)
	{
		Result->SetArrayField(TEXT("warnings"), Warnings);
	}

	if (Snapshot.IsValid())
	{
		// Rollback: import the snapshot taken before the write, with replace=true
		// so what this call built is torn out rather than merged with it. The
		// snapshot came from export_pcg_graph, whose nodes/connections shape is
		// the shape this action reads.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetBoolField(TEXT("replace"), true);
		Payload->SetField(TEXT("nodes"), Snapshot->TryGetField(TEXT("nodes")));
		if (Snapshot->HasField(TEXT("connections")))
		{
			Payload->SetField(TEXT("connections"), Snapshot->TryGetField(TEXT("connections")));
		}
		MCPSetRollback(Result, TEXT("import_pcg_graph"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The rollback rebuilds the graph from a snapshot taken before this call: each node's class, position, every edge, and the settings the export captured. Node names are attempted but NOT guaranteed. The import renames each rebuilt node to the name in the snapshot only when nothing else is found under the graph's outer holding it, and the check is a StaticFindObject on the outer rather than a look at the node list - a node the same replace pass has just removed still occupies its name until garbage collection, so a rollback can land on the engine-assigned name instead. Every such case is listed in that call's warnings, so read them rather than assuming the names came back. What does NOT come back at all is any settings property the export could not see: only properties flagged editable are captured, so anything outside that set returns to its class default."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The graph could not be exported before the import, so there is no snapshot to restore from and no other call undoes a bulk import."));
	}
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FPCGHandlers::ExportGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
	if (!Graph)
	{
		return MCPError(FString::Printf(TEXT("PCGGraph not found: %s"), *AssetPath));
	}

	const bool bIncludeSettings = OptionalBool(Params, TEXT("includeSettings"), true);

	// #235: editor layout (NodePosX/NodePosY) lives on UPCGEditorGraphNodeBase
	// in the asset's UPCGEditorGraph. The runtime UPCGNode::PositionX/Y is
	// only populated when the node was authored through this bridge - the PCG
	// editor never writes back to it. Build a lookup so editor-authored
	// graphs round-trip their hand-laid-out positions.
	// Use reflection instead of including private PCG editor graph-node headers.
#if UE_MCP_HAS_PCG_EDITOR_GRAPH_NODE_LAYOUT
	TMap<const UPCGNode*, const UEdGraphNode*> EditorNodeByPCGNode;
	for (TObjectIterator<UEdGraphNode> It; It; ++It)
	{
		const UEdGraphNode* EdNode = *It;
		if (!EdNode || EdNode->IsTemplate()) continue;
		const FObjectPropertyBase* PCGNodeProp = FindFProperty<FObjectPropertyBase>(EdNode->GetClass(), TEXT("PCGNode"));
		if (!PCGNodeProp) continue;
		const UPCGNode* PCGN = Cast<UPCGNode>(PCGNodeProp->GetObjectPropertyValue_InContainer(EdNode));
		if (!PCGN) continue;
		if (PCGN->GetTypedOuter<UPCGGraph>() == Graph)
		{
			EditorNodeByPCGNode.Add(PCGN, EdNode);
		}
	}
#endif

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (const UPCGNode* Node : Graph->GetNodes())
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("name"), Node->GetName());

		double PosX = Node->PositionX;
		double PosY = Node->PositionY;
#if UE_MCP_HAS_PCG_EDITOR_GRAPH_NODE_LAYOUT
		if (const UEdGraphNode* const* EdNodePtr = EditorNodeByPCGNode.Find(Node); EdNodePtr && *EdNodePtr)
		{
			PosX = (*EdNodePtr)->NodePosX;
			PosY = (*EdNodePtr)->NodePosY;
		}
#endif
		NodeObj->SetNumberField(TEXT("posX"), PosX);
		NodeObj->SetNumberField(TEXT("posY"), PosY);

		const UPCGSettings* Settings = Node->GetSettings();
		if (Settings)
		{
			NodeObj->SetStringField(TEXT("class"), Settings->GetClass()->GetName());

			if (bIncludeSettings)
			{
				TSharedPtr<FJsonObject> SettingsOut = MakeShared<FJsonObject>();
				for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
				{
					FProperty* Prop = *It;
					if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
					const void* Addr = Prop->ContainerPtrToValuePtr<void>(Settings);
					SettingsOut->SetField(Prop->GetName(), SerializePropForExport(Prop, Addr));
				}
				NodeObj->SetObjectField(TEXT("settings"), SettingsOut);
			}
		}

		NodesArr.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	// Edges - same shape as read_pcg_graph (#217), reused so import/export speak
	// the same vocabulary. Walk every output pin, including Input/Output specials.
	auto EmitEdgesFromNode = [](const UPCGNode* From, TArray<TSharedPtr<FJsonValue>>& OutEdges)
	{
		if (!From) return;
		for (const TObjectPtr<UPCGPin>& OutPin : From->GetOutputPins())
		{
			if (!OutPin) continue;
			for (const TObjectPtr<UPCGEdge>& Edge : OutPin->Edges)
			{
				if (!Edge) continue;
				const UPCGPin* OtherPin = Edge->InputPin == OutPin ? Edge->OutputPin.Get() : Edge->InputPin.Get();
				const UPCGNode* ToNode = OtherPin ? OtherPin->Node.Get() : nullptr;
				if (!OtherPin || !ToNode) continue;
				TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
				EdgeObj->SetStringField(TEXT("from"), From->GetName());
				EdgeObj->SetStringField(TEXT("fromPin"), OutPin->Properties.Label.ToString());
				EdgeObj->SetStringField(TEXT("to"), ToNode->GetName());
				EdgeObj->SetStringField(TEXT("toPin"), OtherPin->Properties.Label.ToString());
				OutEdges.Add(MakeShared<FJsonValueObject>(EdgeObj));
			}
		}
	};

	TArray<TSharedPtr<FJsonValue>> ConnsArr;
	if (const UPCGNode* InputNode = Graph->GetInputNode())
	{
		EmitEdgesFromNode(InputNode, ConnsArr);
	}
	for (const UPCGNode* Node : Graph->GetNodes())
	{
		EmitEdgesFromNode(Node, ConnsArr);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), Graph->GetName());
	Result->SetArrayField(TEXT("nodes"), NodesArr);
	Result->SetArrayField(TEXT("connections"), ConnsArr);
	Result->SetNumberField(TEXT("nodeCount"), NodesArr.Num());
	Result->SetNumberField(TEXT("connectionCount"), ConnsArr.Num());
	return MCPResult(Result);
}
