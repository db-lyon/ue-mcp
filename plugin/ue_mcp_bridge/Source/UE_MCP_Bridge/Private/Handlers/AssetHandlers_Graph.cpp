// asset(read_graph) and the generic graph authoring actions for EdGraph-backed
// assets that have no category of their own (#1059, raised for Mutable's
// CustomizableObject).
//
// Blueprints, PCG graphs and Materials are NOT read here: blueprint(read_graph),
// blueprint(get_connections), pcg(read_graph) and material(read_graph) own those, address
// them in their own terms, and a second answer in a different shape is worse
// than no answer. Those types are refused with a pointer to the right action.
//
// Reflection cannot reach topology: UEdGraphNode::Pins has no UPROPERTY and
// UEdGraphPin is not a UObject. From C++ Pins is an ordinary member.
//
// Authoring (connect/disconnect/add/remove) goes through the graph's own
// schema, so the graph type's rules decide what is legal.

#include "AssetHandlers.h"
#include "HandlerUtils.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraphSchema.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/OutputDevice.h"
#include "ScopedTransaction.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectIterator.h"
#include "Materials/Material.h"
#include "UObject/UObjectHash.h"

namespace
{
	/** Every UEdGraph reachable from an asset.
	 *
	 *  Two routes, because a graph reaches its asset in two ways. Most hang off
	 *  it as inners, including graphs nested inside other graphs' nodes (a
	 *  state machine, a collapsed node, a macro). A Material or a PCG graph is
	 *  instead held by a UPROPERTY on the asset and outered elsewhere, so an
	 *  inner walk alone reports those assets as having no graph at all. */
	void CollectGraphs(UObject* Root, TArray<UEdGraph*>& Out)
	{
		if (!Root) return;

		TArray<UObject*> Inner;
		MCPGetNestedSubobjects(Root, Inner);
		for (UObject* Object : Inner)
		{
			if (UEdGraph* Graph = Cast<UEdGraph>(Object)) Out.AddUnique(Graph);
		}

		for (TFieldIterator<FObjectPropertyBase> It(Root->GetClass()); It; ++It)
		{
			if (!It->PropertyClass || !It->PropertyClass->IsChildOf(UEdGraph::StaticClass())) continue;
			if (UEdGraph* Graph = Cast<UEdGraph>(It->GetObjectPropertyValue_InContainer(Root)))
			{
				Out.AddUnique(Graph);
			}
		}
	}

	/** The readable type of a pin, in the shape the editor shows it. */
	FString PinTypeText(const UEdGraphPin& Pin)
	{
		FString Text = Pin.PinType.PinCategory.ToString();
		if (!Pin.PinType.PinSubCategory.IsNone())
		{
			Text += TEXT(".") + Pin.PinType.PinSubCategory.ToString();
		}
		if (const UObject* SubObject = Pin.PinType.PinSubCategoryObject.Get())
		{
			Text += FString::Printf(TEXT("(%s)"), *SubObject->GetName());
		}
		if (Pin.PinType.IsArray()) Text += TEXT("[]");
		return Text;
	}

	/** A wire, keyed by its two pin ids in a fixed order so both ends agree. */
	using FWireKey = TPair<FGuid, FGuid>;

	FWireKey WireKey(const UEdGraphPin& A, const UEdGraphPin& B)
	{
		return A.PinId < B.PinId ? FWireKey(A.PinId, B.PinId) : FWireKey(B.PinId, A.PinId);
	}

	/** One end of a connection, named so the other end can be found again. */
	TSharedPtr<FJsonObject> LinkJson(const UEdGraphPin* Other)
	{
		TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
		if (!Other) return Link;
		Link->SetStringField(TEXT("pinName"), Other->PinName.ToString());
		Link->SetStringField(TEXT("pinId"), Other->PinId.ToString());
		if (const UEdGraphNode* Owner = Other->GetOwningNodeUnchecked())
		{
			Link->SetStringField(TEXT("nodeGuid"), Owner->NodeGuid.ToString());
			Link->SetStringField(TEXT("nodeName"), Owner->GetName());
			Link->SetStringField(TEXT("nodePath"), Owner->GetPathName());
			Link->SetStringField(TEXT("nodeClass"), Owner->GetClass()->GetName());
		}
		return Link;
	}

	TSharedPtr<FJsonObject> PinJson(const UEdGraphPin& Pin)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Pin.PinName.ToString());
		Json->SetStringField(TEXT("pinId"), Pin.PinId.ToString());
		Json->SetStringField(TEXT("direction"), Pin.Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		Json->SetStringField(TEXT("type"), PinTypeText(Pin));
		// The literal a pin carries when nothing is connected to it. This is
		// what an unconnected input actually contributes, so a graph read that
		// omitted it would describe the wiring and not the values.
		if (!Pin.DefaultValue.IsEmpty()) Json->SetStringField(TEXT("defaultValue"), Pin.DefaultValue);
		if (Pin.DefaultObject) Json->SetStringField(TEXT("defaultObject"), Pin.DefaultObject->GetPathName());
		if (Pin.bHidden) Json->SetBoolField(TEXT("hidden"), true);
		if (Pin.bOrphanedPin) Json->SetBoolField(TEXT("orphaned"), true);

		TArray<TSharedPtr<FJsonValue>> Links;
		for (const UEdGraphPin* Other : Pin.LinkedTo)
		{
			Links.Add(MakeShared<FJsonValueObject>(LinkJson(Other)));
		}
		Json->SetArrayField(TEXT("linkedTo"), Links);
		Json->SetNumberField(TEXT("linkCount"), Pin.LinkedTo.Num());
		return Json;
	}

	TSharedPtr<FJsonObject> NodeJson(UEdGraphNode* Node, bool bIncludePins, int32& OutPinCount, int32& OutLinkCount, TSet<FWireKey>& OutWires)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Node->GetName());
		Json->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());
		Json->SetStringField(TEXT("path"), Node->GetPathName());
		Json->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		Json->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Json->SetNumberField(TEXT("posX"), Node->NodePosX);
		Json->SetNumberField(TEXT("posY"), Node->NodePosY);
		if (!Node->NodeComment.IsEmpty()) Json->SetStringField(TEXT("comment"), Node->NodeComment);

		// Node settings come from reflection(reflect_instance) on the path
		// above; this reports only what reflection cannot reach.
		OutPinCount += Node->Pins.Num();
		int32 NodeLinks = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			NodeLinks += Pin->LinkedTo.Num();
			// By wire, not by counting ends and halving: a wire whose far node
			// maxNodes cut away is only ever seen from this side.
			for (const UEdGraphPin* Other : Pin->LinkedTo)
			{
				if (Other) OutWires.Add(WireKey(*Pin, *Other));
			}
		}
		OutLinkCount += NodeLinks;
		Json->SetNumberField(TEXT("pinCount"), Node->Pins.Num());
		Json->SetNumberField(TEXT("linkCount"), NodeLinks);

		// No pins means AllocateDefaultPins never ran: a node built outside the
		// schema, which can never be wired (#1059).
		if (Node->Pins.Num() == 0)
		{
			Json->SetBoolField(TEXT("hasNoPins"), true);
			Json->SetStringField(TEXT("hasNoPinsNote"),
				TEXT("This node has no pins, which means AllocateDefaultPins never ran for it. A node created by ")
				TEXT("appending to the graph's Nodes array rather than through the graph's own schema looks like ")
				TEXT("this: it cannot be wired to anything and the editor cannot draw it. Delete it with ")
				TEXT("asset(action=\"remove_graph_node\") and create it again with asset(action=\"add_graph_node\")."));
		}

		if (bIncludePins)
		{
			TArray<TSharedPtr<FJsonValue>> Pins;
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin) Pins.Add(MakeShared<FJsonValueObject>(PinJson(*Pin)));
			}
			Json->SetArrayField(TEXT("pins"), Pins);
		}
		return Json;
	}
}

namespace
{
	/** What to do about an asset whose graph was never built, when this type has an answer. */
	FString MCPGraphMissingHint(const UObject* Asset)
	{
		for (const UClass* Class = Asset ? Asset->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == FName(TEXT("CustomizableObject")))
			{
				return TEXT(" A CustomizableObject gets its Source graph and Base Object node from Mutable's factory: ")
					TEXT("create it with asset(action=\"create_customizable_object\"). One made by create_asset_by_class ")
					TEXT("has no graph.");
			}
		}
		return FString();
	}

	/** The action that already owns this asset type's graph, or null. */
	const TCHAR* MCPGraphReaderFor(const UObject* Asset)
	{
		if (!Asset) return nullptr;
		if (Asset->IsA<UBlueprint>()) return TEXT("blueprint(read_graph) and blueprint(get_connections)");
		// A Material's UEdGraph exists only while its editor is open; the
		// stored expressions are read by material(read_graph).
		if (Asset->IsA<UMaterial>()) return TEXT("material(read_graph)");
		const UClass* Class = Asset->GetClass();
		for (; Class; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == FName(TEXT("PCGGraph"))) return TEXT("pcg(read_graph)");
		}
		return nullptr;
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::ReadAssetGraph(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;

	const FString GraphFilter = OptionalString(Params, TEXT("graphName"));
	const bool bIncludePins = OptionalBool(Params, TEXT("includePins"), true);
	const int32 MaxNodes = FMath::Clamp(OptionalInt(Params, TEXT("maxNodes"), 500), 1, 5000);

	// A type with a category that already reads its graph is sent there rather
	// than answered twice in two shapes. blueprint(get_connections) addresses
	// nodes by GUID and pcg(read_graph) speaks PCG's own node model; this
	// action exists for the EdGraph types that have no such owner.
	if (const TCHAR* Owner = MCPGraphReaderFor(Asset))
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is a %s, which %s reads. Use that: it addresses this type the way the type is addressed "
			     "everywhere else. asset(read_graph) covers the EdGraph types with no category of their own."),
			*AssetPath, *Asset->GetClass()->GetName(), Owner));
	}

	TArray<UEdGraph*> Graphs;
	CollectGraphs(Asset, Graphs);
	if (UEdGraph* AssetIsAGraph = Cast<UEdGraph>(Asset))
	{
		Graphs.AddUnique(AssetIsAGraph);
	}

	if (Graphs.Num() == 0)
	{
		// Not an error: a Texture has no graph and never will.
		auto Empty = MCPSuccess();
		Empty->SetStringField(TEXT("assetPath"), AssetPath);
		Empty->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
		Empty->SetArrayField(TEXT("graphs"), TArray<TSharedPtr<FJsonValue>>());
		Empty->SetNumberField(TEXT("graphCount"), 0);
		// States what was searched, not what the asset "has": a type whose editor
		// builds its graph holds none until opened.
		Empty->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No UEdGraph was reachable from '%s' (a %s). Searched its subobjects and its own graph-typed ")
			TEXT("properties. A type whose graph is built by its own editor holds none until that editor has ")
			TEXT("opened it.%s"),
			*AssetPath, *Asset->GetClass()->GetName(), *MCPGraphMissingHint(Asset)));
		return MCPResult(Empty);
	}

	TArray<TSharedPtr<FJsonValue>> GraphsJson;
	int32 TotalNodes = 0;
	int32 TotalPins = 0;
	TSet<FWireKey> AllWires;
	bool bTruncated = false;

	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph) continue;
		const FString GraphName = Graph->GetName();
		if (!GraphFilter.IsEmpty() && !GraphName.Contains(GraphFilter)) continue;

		TSharedPtr<FJsonObject> GraphJson = MakeShared<FJsonObject>();
		GraphJson->SetStringField(TEXT("name"), GraphName);
		GraphJson->SetStringField(TEXT("path"), Graph->GetPathName());
		GraphJson->SetStringField(TEXT("class"), Graph->GetClass()->GetName());
		GraphJson->SetStringField(TEXT("schema"), Graph->Schema ? Graph->Schema->GetName() : TEXT(""));
		GraphJson->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

		TArray<TSharedPtr<FJsonValue>> NodesJson;
		int32 GraphPins = 0;
		int32 GraphLinks = 0;
		TSet<FWireKey> GraphWires;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (NodesJson.Num() >= MaxNodes)
			{
				bTruncated = true;
				break;
			}
			NodesJson.Add(MakeShared<FJsonValueObject>(NodeJson(Node, bIncludePins, GraphPins, GraphLinks, GraphWires)));
		}
		GraphJson->SetArrayField(TEXT("nodes"), NodesJson);
		GraphJson->SetNumberField(TEXT("pinCount"), GraphPins);
		GraphJson->SetNumberField(TEXT("connectionCount"), GraphWires.Num());

		TotalNodes += NodesJson.Num();
		TotalPins += GraphPins;
		AllWires.Append(GraphWires);
		GraphsJson.Add(MakeShared<FJsonValueObject>(GraphJson));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
	Result->SetArrayField(TEXT("graphs"), GraphsJson);
	Result->SetNumberField(TEXT("graphCount"), GraphsJson.Num());
	Result->SetNumberField(TEXT("nodesReported"), TotalNodes);
	Result->SetNumberField(TEXT("pinCount"), TotalPins);
	Result->SetNumberField(TEXT("connectionCount"), AllWires.Num());
	if (bTruncated)
	{
		Result->SetBoolField(TEXT("truncated"), true);
		Result->SetStringField(TEXT("truncatedNote"), FString::Printf(
			TEXT("A graph held more than maxNodes (%d) nodes and was cut short. Raise maxNodes, or narrow the read ")
			TEXT("with graphName."),
			MaxNodes));
	}
	if (!GraphFilter.IsEmpty() && GraphsJson.Num() == 0)
	{
		TArray<FString> Available;
		for (const UEdGraph* Graph : Graphs)
		{
			if (Graph) Available.Add(Graph->GetName());
		}
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("No graph name contains '%s'. This asset has: %s."),
			*GraphFilter, *FString::Join(Available, TEXT(", "))));
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// Graph authoring (#1059): connect, disconnect, add and remove nodes through
// the graph's own schema, plus the Mutable compile.
// ---------------------------------------------------------------------------

// 5.5 replaced ActionGroup access with GetSchemaAction; 5.6 moved graph
// locations to FVector2f.
#define UE_MCP_EDGRAPH_HAS_GET_SCHEMA_ACTION (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5))
#define UE_MCP_EDGRAPH_FLOAT_LOCATION (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6))

namespace
{
	/** The actions that already author this asset type's graph, or null. */
	const TCHAR* MCPGraphAuthorOwnerFor(const UObject* Asset)
	{
		if (!Asset) return nullptr;
		if (Asset->IsA<UBlueprint>()) return TEXT("blueprint(add_node), blueprint(connect_pins), blueprint(disconnect_pins) and blueprint(delete_node)");
		if (Asset->IsA<UMaterial>()) return TEXT("material(add_expression), material(connect_expressions) and material(delete_expression)");
		for (const UClass* Class = Asset->GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == FName(TEXT("PCGGraph"))) return TEXT("pcg(add_node), pcg(connect_nodes), pcg(disconnect_nodes) and pcg(remove_node)");
		}
		return nullptr;
	}

	/** Shared prelude: the asset, the graphs graphName selects, and whether saving was asked for. */
	struct FMCPGraphAuthorContext
	{
		FString AssetPath;
		UObject* Asset = nullptr;
		TArray<UEdGraph*> Graphs;
		bool bSave = true;
	};

	TSharedPtr<FJsonValue> MCPGraphAuthorBegin(const TSharedPtr<FJsonObject>& Params, FMCPGraphAuthorContext& Ctx)
	{
		if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), Ctx.AssetPath)) return Err;

		TSharedPtr<FJsonValue> LoadError;
		Ctx.Asset = MCPRequireAssetObject(Ctx.AssetPath, LoadError);
		if (!Ctx.Asset) return LoadError;
		Ctx.bSave = OptionalBool(Params, TEXT("save"), true);

		if (const TCHAR* Owner = MCPGraphAuthorOwnerFor(Ctx.Asset))
		{
			return MCPError(FString::Printf(
				TEXT("'%s' is a %s, whose graph is authored by %s. Use those: they keep that type's own model in step. ")
				TEXT("The asset graph actions cover the EdGraph types with no category of their own."),
				*Ctx.AssetPath, *Ctx.Asset->GetClass()->GetName(), Owner));
		}

		TArray<UEdGraph*> All;
		CollectGraphs(Ctx.Asset, All);
		if (UEdGraph* AssetIsAGraph = Cast<UEdGraph>(Ctx.Asset)) All.AddUnique(AssetIsAGraph);
		if (All.Num() == 0)
		{
			return MCPError(FString::Printf(
				TEXT("No UEdGraph is reachable from '%s' (a %s), so there is nothing to author. A type whose graph ")
				TEXT("is built by its own editor holds none until that editor has opened it.%s"),
				*Ctx.AssetPath, *Ctx.Asset->GetClass()->GetName(), *MCPGraphMissingHint(Ctx.Asset)));
		}

		const FString GraphName = OptionalString(Params, TEXT("graphName"));
		if (GraphName.IsEmpty())
		{
			Ctx.Graphs = All;
			return nullptr;
		}
		for (UEdGraph* Graph : All)
		{
			if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) Ctx.Graphs.Add(Graph);
		}
		if (Ctx.Graphs.Num() == 0)
		{
			for (UEdGraph* Graph : All)
			{
				if (Graph && Graph->GetName().Contains(GraphName)) Ctx.Graphs.Add(Graph);
			}
		}
		if (Ctx.Graphs.Num() == 0)
		{
			TArray<FString> Names;
			for (const UEdGraph* Graph : All) if (Graph) Names.Add(Graph->GetName());
			return MCPError(FString::Printf(TEXT("No graph in '%s' is named or contains '%s'. It has: %s."),
				*Ctx.AssetPath, *GraphName, *FString::Join(Names, TEXT(", "))));
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> MCPGraphAuthorNodeJson(const UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		if (!Node) return Json;
		Json->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());
		Json->SetStringField(TEXT("nodeName"), Node->GetName());
		Json->SetStringField(TEXT("nodePath"), Node->GetPathName());
		Json->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
		Json->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		if (const UEdGraph* Graph = Node->GetGraph()) Json->SetStringField(TEXT("graphName"), Graph->GetName());
		return Json;
	}

	TSharedPtr<FJsonObject> MCPGraphAuthorPinRef(const UEdGraphPin* Pin)
	{
		TSharedPtr<FJsonObject> Json = LinkJson(Pin);
		if (Pin) Json->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		return Json;
	}

	FString MCPGraphAuthorNodeList(const TArray<UEdGraph*>& Graphs, int32 Cap = 30)
	{
		TArray<FString> Out;
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				if (Out.Num() >= Cap) return FString::Join(Out, TEXT("; ")) + TEXT("; ...");
				Out.Add(FString::Printf(TEXT("%s (%s, guid %s)"), *Node->GetName(), *Node->GetClass()->GetName(), *Node->NodeGuid.ToString()));
			}
		}
		return FString::Join(Out, TEXT("; "));
	}

	/** A node by GUID, object path, object name, or unique title. */
	UEdGraphNode* MCPGraphAuthorResolveNode(const TArray<UEdGraph*>& Graphs, const FString& Spec, const TCHAR* ParamName, FString& OutError)
	{
		TArray<UEdGraphNode*> All;
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes) if (Node) All.Add(Node);
		}

		auto Pick = [&](TFunctionRef<bool(const UEdGraphNode*)> Match, const TCHAR* How) -> UEdGraphNode*
		{
			TArray<UEdGraphNode*> Hits;
			for (UEdGraphNode* Node : All) if (Match(Node)) Hits.Add(Node);
			if (Hits.Num() == 1) return Hits[0];
			if (Hits.Num() > 1 && OutError.IsEmpty())
			{
				TArray<FString> Paths;
				for (const UEdGraphNode* Node : Hits) Paths.Add(Node->GetPathName());
				OutError = FString::Printf(TEXT("%s '%s' matches %d nodes by %s: %s. Pass the node path instead."),
					ParamName, *Spec, Hits.Num(), How, *FString::Join(Paths, TEXT(", ")));
			}
			return nullptr;
		};

		FGuid Guid;
		if (FGuid::Parse(Spec, Guid))
		{
			if (UEdGraphNode* Node = Pick([&](const UEdGraphNode* N) { return N->NodeGuid == Guid; }, TEXT("GUID"))) return Node;
			if (!OutError.IsEmpty()) return nullptr;
		}
		if (UEdGraphNode* Node = Pick([&](const UEdGraphNode* N) { return N->GetPathName().Equals(Spec, ESearchCase::IgnoreCase); }, TEXT("path"))) return Node;
		if (!OutError.IsEmpty()) return nullptr;
		if (UEdGraphNode* Node = Pick([&](const UEdGraphNode* N) { return N->GetName().Equals(Spec, ESearchCase::IgnoreCase); }, TEXT("name"))) return Node;
		if (!OutError.IsEmpty()) return nullptr;
		if (UEdGraphNode* Node = Pick([&](const UEdGraphNode* N) { return N->GetNodeTitle(ENodeTitleType::ListView).ToString().Equals(Spec, ESearchCase::IgnoreCase); }, TEXT("title"))) return Node;
		if (!OutError.IsEmpty()) return nullptr;

		OutError = FString::Printf(TEXT("No node matches %s '%s' by GUID, path, name or title. Nodes: %s."),
			ParamName, *Spec, *MCPGraphAuthorNodeList(Graphs));
		return nullptr;
	}

	FString MCPGraphAuthorPinList(const UEdGraphNode* Node)
	{
		TArray<FString> Out;
		if (!Node) return FString();
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			Out.Add(FString::Printf(TEXT("%s [%s, pinId %s]"), *Pin->PinName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"), *Pin->PinId.ToString()));
		}
		return FString::Join(Out, TEXT("; "));
	}

	/** One pin, addressed by <Role>Node plus <Role>PinId (preferred) or <Role>Pin and <Role>PinDirection. */
	UEdGraphPin* MCPGraphAuthorResolvePin(
		const TSharedPtr<FJsonObject>& Params,
		const TArray<UEdGraph*>& Graphs,
		const FString& Role,
		EEdGraphPinDirection PreferredDirection,
		FString& OutError)
	{
		const FString NodeKey = Role + TEXT("Node");
		const FString PinKey = Role + TEXT("Pin");
		const FString PinIdKey = Role + TEXT("PinId");
		const FString DirectionKey = Role + TEXT("PinDirection");
		const FString NodeSpec = OptionalString(Params, *NodeKey);
		const FString PinName = OptionalString(Params, *PinKey);
		const FString PinIdText = OptionalString(Params, *PinIdKey);
		const FString DirectionText = OptionalString(Params, *DirectionKey);

		if (PinName.IsEmpty() && PinIdText.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Pass %s (from read_graph) or %s to name the %s pin."), *PinIdKey, *PinKey, *Role);
			return nullptr;
		}

		UEdGraphNode* Node = nullptr;
		if (!NodeSpec.IsEmpty())
		{
			Node = MCPGraphAuthorResolveNode(Graphs, NodeSpec, *NodeKey, OutError);
			if (!Node) return nullptr;
		}
		else if (PinIdText.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s names a pin by name, which needs %s to say which node it is on. %s alone needs no node."),
				*PinKey, *NodeKey, *PinIdKey);
			return nullptr;
		}

		TArray<UEdGraphPin*> Candidates;
		if (Node)
		{
			for (UEdGraphPin* Pin : Node->Pins) if (Pin) Candidates.Add(Pin);
		}
		else
		{
			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph) continue;
				for (UEdGraphNode* Each : Graph->Nodes)
				{
					if (!Each) continue;
					for (UEdGraphPin* Pin : Each->Pins) if (Pin) Candidates.Add(Pin);
				}
			}
		}

		if (!PinIdText.IsEmpty())
		{
			FGuid PinId;
			if (!FGuid::Parse(PinIdText, PinId))
			{
				OutError = FString::Printf(TEXT("%s '%s' is not a GUID. read_graph reports each pin's pinId."), *PinIdKey, *PinIdText);
				return nullptr;
			}
			TArray<UEdGraphPin*> Hits;
			for (UEdGraphPin* Pin : Candidates) if (Pin->PinId == PinId) Hits.Add(Pin);
			if (Hits.Num() == 1) return Hits[0];
			OutError = Hits.Num() > 1
				? FString::Printf(TEXT("%s '%s' is carried by %d pins. Pass %s as well."), *PinIdKey, *PinIdText, Hits.Num(), *NodeKey)
				: Node
					? FString::Printf(TEXT("%s has no pin with %s '%s'. Its pins: %s."), *Node->GetName(), *PinIdKey, *PinIdText, *MCPGraphAuthorPinList(Node))
					: FString::Printf(TEXT("No pin in the selected graphs has %s '%s'."), *PinIdKey, *PinIdText);
			return nullptr;
		}

		bool bHasDirection = false;
		EEdGraphPinDirection Direction = EGPD_Input;
		if (!DirectionText.IsEmpty())
		{
			if (DirectionText.Equals(TEXT("input"), ESearchCase::IgnoreCase)) Direction = EGPD_Input;
			else if (DirectionText.Equals(TEXT("output"), ESearchCase::IgnoreCase)) Direction = EGPD_Output;
			else
			{
				OutError = FString::Printf(TEXT("%s must be 'input' or 'output', not '%s'."), *DirectionKey, *DirectionText);
				return nullptr;
			}
			bHasDirection = true;
		}

		TArray<UEdGraphPin*> Hits;
		for (UEdGraphPin* Pin : Candidates)
		{
			if (bHasDirection && Pin->Direction != Direction) continue;
			if (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase)) Hits.Add(Pin);
		}
		if (Hits.Num() == 0)
		{
			for (UEdGraphPin* Pin : Candidates)
			{
				if (bHasDirection && Pin->Direction != Direction) continue;
				if (Pin->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase)) Hits.Add(Pin);
			}
		}
		if (Hits.Num() > 1 && !bHasDirection)
		{
			TArray<UEdGraphPin*> Preferred = Hits.FilterByPredicate([&](const UEdGraphPin* Pin) { return Pin->Direction == PreferredDirection; });
			if (Preferred.Num() > 0) Hits = Preferred;
		}
		if (Hits.Num() == 1) return Hits[0];
		if (Hits.Num() > 1)
		{
			TArray<FString> Ids;
			for (const UEdGraphPin* Pin : Hits) Ids.Add(Pin->PinId.ToString());
			OutError = FString::Printf(TEXT("%s '%s' matches %d pins on %s (pin names repeat on some node types). Pass %s: %s."),
				*PinKey, *PinName, Hits.Num(), *Node->GetName(), *PinIdKey, *FString::Join(Ids, TEXT(", ")));
			return nullptr;
		}
		OutError = FString::Printf(TEXT("%s has no pin named '%s'%s. Its pins: %s."), *Node->GetName(), *PinName,
			bHasDirection ? *FString::Printf(TEXT(" with direction %s"), *DirectionText) : TEXT(""), *MCPGraphAuthorPinList(Node));
		return nullptr;
	}

	const TCHAR* MCPGraphAuthorResponseName(ECanCreateConnectionResponse Response)
	{
		switch (Response)
		{
		case CONNECT_RESPONSE_MAKE: return TEXT("make");
		case CONNECT_RESPONSE_DISALLOW: return TEXT("disallow");
		case CONNECT_RESPONSE_BREAK_OTHERS_A: return TEXT("break_others_a");
		case CONNECT_RESPONSE_BREAK_OTHERS_B: return TEXT("break_others_b");
		case CONNECT_RESPONSE_BREAK_OTHERS_AB: return TEXT("break_others_ab");
		case CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE: return TEXT("make_with_conversion_node");
		default: return TEXT("other");
		}
	}

	/** Mark dirty, then save when asked, reporting the outcome on Result. */
	void MCPGraphAuthorFinish(const FMCPGraphAuthorContext& Ctx, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Result)
	{
		if (Graph) Graph->NotifyGraphChanged();
		Ctx.Asset->MarkPackageDirty();
		Result->SetStringField(TEXT("assetPath"), Ctx.AssetPath);
		if (!Ctx.bSave)
		{
			Result->SetBoolField(TEXT("saved"), false);
			Result->SetStringField(TEXT("saveNote"), TEXT("save=false: the change is in memory and the package is dirty."));
			return;
		}
		FString Reason;
		const bool bSaved = SaveAssetPackageChecked(Ctx.Asset, Reason);
		MCPNoteSaveOutcome(Result, Ctx.AssetPath, bSaved, Reason);
	}

	TSharedPtr<FJsonObject> MCPGraphAuthorConnectPayload(const FString& AssetPath, const UEdGraphPin* A, const UEdGraphPin* B)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("sourceNode"), A->GetOwningNode()->GetPathName());
		Payload->SetStringField(TEXT("sourcePinId"), A->PinId.ToString());
		Payload->SetStringField(TEXT("targetNode"), B->GetOwningNode()->GetPathName());
		Payload->SetStringField(TEXT("targetPinId"), B->PinId.ToString());
		return Payload;
	}

	/** Every schema action the graph's context menu offers. */
	void MCPGraphAuthorCollectActions(UEdGraph* Graph, TArray<TSharedPtr<FEdGraphSchemaAction>>& Out)
	{
		const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
		if (!Schema) return;
		FGraphContextMenuBuilder Builder(Graph);
		Schema->GetGraphContextActions(Builder);
		for (int32 Index = 0; Index < Builder.GetNumActions(); ++Index)
		{
#if UE_MCP_EDGRAPH_HAS_GET_SCHEMA_ACTION
			const TSharedPtr<FEdGraphSchemaAction>& Action = Builder.GetSchemaAction(Index);
			if (Action.IsValid()) Out.Add(Action);
#else
			auto&& Group = Builder.GetAction(Index);
			for (const TSharedPtr<FEdGraphSchemaAction>& Action : Group.Actions)
			{
				if (Action.IsValid()) Out.Add(Action);
			}
#endif
		}
	}

	/** The USTRUCT an action instance is, found by its vtable since actions carry no reliable type id. */
	const UScriptStruct* MCPGraphAuthorActionStruct(const FEdGraphSchemaAction& Action)
	{
		static TMap<const void*, TWeakObjectPtr<const UScriptStruct>> Cache;
		const void* VTable = *reinterpret_cast<const void* const*>(&Action);
		if (const TWeakObjectPtr<const UScriptStruct>* Hit = Cache.Find(VTable)) return Hit->Get();

		static const UScriptStruct* Base = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Engine.EdGraphSchemaAction"));
		const UScriptStruct* Found = nullptr;
		if (Base)
		{
			for (TObjectIterator<UScriptStruct> It; It && !Found; ++It)
			{
				const UScriptStruct* Struct = *It;
				if (!Struct->IsChildOf(Base)) continue;
				const UScriptStruct::ICppStructOps* Ops = Struct->GetCppStructOps();
				if (!Ops || Ops->HasZeroConstructor()) continue;
				void* Memory = FMemory::Malloc(Struct->GetStructureSize(), Struct->GetMinAlignment());
				Struct->InitializeStruct(Memory);
				if (*reinterpret_cast<const void* const*>(Memory) == VTable) Found = Struct;
				Struct->DestroyStruct(Memory);
				FMemory::Free(Memory);
			}
		}
		Cache.Add(VTable, Found);
		return Found;
	}

	/** The class an action spawns: a NodeTemplate's class, or a class-valued property (a sound node class). */
	UClass* MCPGraphAuthorActionSpawnClass(const TSharedPtr<FEdGraphSchemaAction>& Action)
	{
		if (Action->GetTypeId() == FEdGraphSchemaAction_NewNode::StaticGetTypeId())
		{
			const FEdGraphSchemaAction_NewNode* NewNode = static_cast<const FEdGraphSchemaAction_NewNode*>(Action.Get());
			return NewNode->NodeTemplate ? NewNode->NodeTemplate->GetClass() : nullptr;
		}
		const UScriptStruct* Struct = MCPGraphAuthorActionStruct(*Action);
		if (!Struct) return nullptr;
		for (TFieldIterator<FObjectPropertyBase> It(Struct); It; ++It)
		{
			UObject* Value = It->GetObjectPropertyValue_InContainer(Action.Get());
			if (!Value) continue;
			if (UClass* AsClass = Cast<UClass>(Value)) return AsClass;
			if (Value->IsA<UEdGraphNode>()) return Value->GetClass();
		}
		return nullptr;
	}

	struct FMCPGraphAuthorActionInfo
	{
		TSharedPtr<FEdGraphSchemaAction> Action;
		FString Menu;
		FString Category;
		UClass* SpawnClass = nullptr;
	};

	TSharedPtr<FJsonObject> MCPGraphAuthorActionJson(const FMCPGraphAuthorActionInfo& Info)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("actionName"), Info.Menu);
		Json->SetStringField(TEXT("category"), Info.Category);
		if (Info.SpawnClass) Json->SetStringField(TEXT("nodeClass"), Info.SpawnClass->GetName());
		return Json;
	}

	/** An error that lists the actions the schema offers, narrowed by what the caller asked for. */
	TSharedPtr<FJsonValue> MCPGraphAuthorActionError(const FString& Message, const TArray<FMCPGraphAuthorActionInfo>& Infos, const FString& Filter)
	{
		TArray<TSharedPtr<FJsonValue>> Listed;
		auto Matches = [&](const FMCPGraphAuthorActionInfo& Info)
		{
			return Filter.IsEmpty() || Info.Menu.Contains(Filter) || Info.Category.Contains(Filter)
				|| (Info.SpawnClass && Info.SpawnClass->GetName().Contains(Filter));
		};
		for (const FMCPGraphAuthorActionInfo& Info : Infos)
		{
			if (Listed.Num() >= 60) break;
			if (Matches(Info)) Listed.Add(MakeShared<FJsonValueObject>(MCPGraphAuthorActionJson(Info)));
		}
		if (Listed.Num() == 0)
		{
			for (const FMCPGraphAuthorActionInfo& Info : Infos)
			{
				if (Listed.Num() >= 60) break;
				Listed.Add(MakeShared<FJsonValueObject>(MCPGraphAuthorActionJson(Info)));
			}
		}
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), Message);
		Obj->SetNumberField(TEXT("actionCount"), Infos.Num());
		Obj->SetArrayField(TEXT("availableActions"), Listed);
		return MakeShared<FJsonValueObject>(Obj);
	}

	/** Collects LogMutable warnings and errors while a compile runs. */
	class FMCPGraphCompileLog : public FOutputDevice
	{
	public:
		FMCPGraphCompileLog() { if (GLog) GLog->AddOutputDevice(this); }
		virtual ~FMCPGraphCompileLog() override { if (GLog) GLog->RemoveOutputDevice(this); }
		FMCPGraphCompileLog(const FMCPGraphCompileLog&) = delete;
		FMCPGraphCompileLog& operator=(const FMCPGraphCompileLog&) = delete;

		virtual bool CanBeUsedOnAnyThread() const override { return true; }

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
		{
			const FString CategoryText = Category.ToString();
			if (!CategoryText.Contains(TEXT("Mutable")) && !CategoryText.Contains(TEXT("CustomizableObject"))) return;
			const ELogVerbosity::Type Level = (ELogVerbosity::Type)(Verbosity & ELogVerbosity::VerbosityMask);
			FScopeLock Lock(&Mutex);
			if (Level == ELogVerbosity::Error || Level == ELogVerbosity::Fatal)
			{
				if (Errors.Num() < 200) Errors.Add(V);
			}
			else if (Level == ELogVerbosity::Warning)
			{
				if (Warnings.Num() < 200) Warnings.Add(V);
			}
		}

		TArray<FString> Errors;
		TArray<FString> Warnings;
		FCriticalSection Mutex;
	};

	/** Set an enum-typed property from a value name ("Fast" or "ECustomizableObjectTextureCompression::Fast"). */
	bool MCPGraphSetEnumText(FProperty* Prop, void* ValuePtr, const FString& Text)
	{
		UEnum* Enum = nullptr;
		FNumericProperty* Underlying = nullptr;
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			Enum = EnumProp->GetEnum();
			Underlying = EnumProp->GetUnderlyingProperty();
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			Enum = ByteProp->Enum;
			Underlying = ByteProp;
		}
		if (!Enum || !Underlying) return false;
		int64 Value = Enum->GetValueByNameString(Text);
		if (Value == INDEX_NONE) Value = Enum->GetValueByNameString(Enum->GenerateFullEnumName(*Text));
		if (Value == INDEX_NONE) return false;
		Underlying->SetIntPropertyValue(ValuePtr, Value);
		return true;
	}

	FString MCPGraphEnumText(const FProperty* Prop, const void* ValuePtr)
	{
		const UEnum* Enum = nullptr;
		const FNumericProperty* Underlying = nullptr;
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			Enum = EnumProp->GetEnum();
			Underlying = EnumProp->GetUnderlyingProperty();
		}
		else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			Enum = ByteProp->Enum;
			Underlying = ByteProp;
		}
		if (!Enum || !Underlying) return FString();
		return Enum->GetNameStringByValue(Underlying->GetSignedIntPropertyValue(ValuePtr));
	}

	/** The caller's option for a reflected compile parameter, by what its name says it is. */
	FString MCPGraphCompileOption(const TSharedPtr<FJsonObject>& Params, const FString& PropName)
	{
		if (PropName.Contains(TEXT("Optimization"))) return OptionalString(Params, TEXT("optimizationLevel"));
		if (PropName.Contains(TEXT("Compression"))) return OptionalString(Params, TEXT("textureCompression"));
		return FString();
	}

	bool MCPGraphApplyText(FProperty* Prop, void* ValuePtr, const FString& Text)
	{
		if (MCPGraphSetEnumText(Prop, ValuePtr, Text)) return true;
		if (CastField<FEnumProperty>(Prop)) return false;
		return Prop->ImportText_Direct(*Text, ValuePtr, nullptr, PPF_None) != nullptr;
	}
}

TSharedPtr<FJsonValue> FAssetHandlers::ConnectGraphPins(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FMCPGraphAuthorContext Ctx;
	if (auto Err = MCPGraphAuthorBegin(Params, Ctx)) return Err;

	FString Error;
	UEdGraphPin* A = MCPGraphAuthorResolvePin(Params, Ctx.Graphs, TEXT("source"), EGPD_Output, Error);
	if (!A) return MCPError(Error);
	UEdGraphPin* B = MCPGraphAuthorResolvePin(Params, Ctx.Graphs, TEXT("target"), EGPD_Input, Error);
	if (!B) return MCPError(Error);
	if (A == B) return MCPError(TEXT("The source and target resolve to the same pin."));

	UEdGraphNode* NodeA = A->GetOwningNode();
	UEdGraphNode* NodeB = B->GetOwningNode();
	UEdGraph* Graph = NodeA ? NodeA->GetGraph() : nullptr;
	if (!Graph || !NodeB || NodeB->GetGraph() != Graph)
	{
		return MCPError(TEXT("The two pins are in different graphs. A connection joins pins of one graph."));
	}
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema) return MCPError(FString::Printf(TEXT("Graph '%s' has no schema, so nothing can decide whether the link is legal."), *Graph->GetName()));

	auto Describe = [&](const TSharedPtr<FJsonObject>& Result)
	{
		Result->SetStringField(TEXT("graphName"), Graph->GetName());
		Result->SetObjectField(TEXT("source"), MCPGraphAuthorPinRef(A));
		Result->SetObjectField(TEXT("target"), MCPGraphAuthorPinRef(B));
	};

	if (A->LinkedTo.Contains(B))
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("assetPath"), Ctx.AssetPath);
		Existed->SetBoolField(TEXT("alreadyConnected"), true);
		Describe(Existed);
		return MCPResult(Existed);
	}

	const FPinConnectionResponse Response = Schema->CanCreateConnection(A, B);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		TSharedPtr<FJsonObject> Refused = MakeShared<FJsonObject>();
		Refused->SetBoolField(TEXT("success"), false);
		Refused->SetStringField(TEXT("error"), FString::Printf(TEXT("The %s refused the connection: %s"),
			*Schema->GetClass()->GetName(),
			Response.Message.IsEmpty() ? TEXT("no reason given") : *Response.Message.ToString()));
		Refused->SetStringField(TEXT("reason"), TEXT("schema_disallowed"));
		Refused->SetStringField(TEXT("schemaResponse"), MCPGraphAuthorResponseName(Response.Response));
		Refused->SetStringField(TEXT("schemaMessage"), Response.Message.ToString());
		Refused->SetStringField(TEXT("assetPath"), Ctx.AssetPath);
		Describe(Refused);
		return MakeShared<FJsonValueObject>(Refused);
	}

	if (Ctx.bSave)
	{
		if (auto Blocked = MCPAssetWriteBlockedError(Ctx.Asset, Ctx.AssetPath, TEXT("connect these pins"))) return Blocked;
	}

	const TArray<UEdGraphPin*> BeforeA = A->LinkedTo;
	const TArray<UEdGraphPin*> BeforeB = B->LinkedTo;

	FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "ConnectGraphPins", "Connect graph pins"));
	Graph->Modify();
	NodeA->Modify();
	NodeB->Modify();
	const bool bConnected = Schema->TryCreateConnection(A, B);
	if (!bConnected)
	{
		Transaction.Cancel();
		return MCPError(FString::Printf(TEXT("The %s did not make the connection: %s"),
			*Schema->GetClass()->GetName(),
			Response.Message.IsEmpty() ? TEXT("no reason given") : *Response.Message.ToString()));
	}

	TArray<TSharedPtr<FJsonValue>> Broken;
	for (const UEdGraphPin* Other : BeforeA) if (Other && !A->LinkedTo.Contains(Other)) Broken.Add(MakeShared<FJsonValueObject>(MCPGraphAuthorPinRef(Other)));
	for (const UEdGraphPin* Other : BeforeB) if (Other && !B->LinkedTo.Contains(Other)) Broken.Add(MakeShared<FJsonValueObject>(MCPGraphAuthorPinRef(Other)));

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Describe(Result);
	Result->SetStringField(TEXT("schemaResponse"), MCPGraphAuthorResponseName(Response.Response));
	if (!Response.Message.IsEmpty()) Result->SetStringField(TEXT("schemaMessage"), Response.Message.ToString());
	Result->SetArrayField(TEXT("brokenLinks"), Broken);

	const bool bDirect = A->LinkedTo.Contains(B);
	Result->SetBoolField(TEXT("directLink"), bDirect);
	if (!bDirect)
	{
		MCPSetNoRollback(Result, TEXT("The schema joined the pins through an intermediate node, so there is no single wire to break. ")
			TEXT("Read the graph and remove that node with remove_graph_node to undo it."));
	}
	else
	{
		MCPSetRollback(Result, TEXT("disconnect_graph_pins"), MCPGraphAuthorConnectPayload(Ctx.AssetPath, A, B));
		if (Broken.Num() > 0)
		{
			Result->SetBoolField(TEXT("rollbackLossy"), true);
			Result->SetStringField(TEXT("rollbackNote"), TEXT("The schema replaced the links listed in brokenLinks. ")
				TEXT("The rollback removes the new wire only; reconnect those with connect_graph_pins."));
		}
	}
	MCPGraphAuthorFinish(Ctx, Graph, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::DisconnectGraphPins(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FMCPGraphAuthorContext Ctx;
	if (auto Err = MCPGraphAuthorBegin(Params, Ctx)) return Err;

	FString Error;
	UEdGraphPin* A = MCPGraphAuthorResolvePin(Params, Ctx.Graphs, TEXT("source"), EGPD_Output, Error);
	if (!A) return MCPError(Error);

	const bool bHasTarget = !OptionalString(Params, TEXT("targetPinId")).IsEmpty() || !OptionalString(Params, TEXT("targetPin")).IsEmpty();
	UEdGraphPin* B = nullptr;
	if (bHasTarget)
	{
		B = MCPGraphAuthorResolvePin(Params, Ctx.Graphs, TEXT("target"), EGPD_Input, Error);
		if (!B) return MCPError(Error);
	}

	UEdGraphNode* NodeA = A->GetOwningNode();
	UEdGraph* Graph = NodeA ? NodeA->GetGraph() : nullptr;
	const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
	if (!Schema) return MCPError(TEXT("The pin's graph has no schema, so no link can be broken through it."));

	const bool bNothingToBreak = B ? !A->LinkedTo.Contains(B) : A->LinkedTo.Num() == 0;
	if (bNothingToBreak)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("assetPath"), Ctx.AssetPath);
		Noop->SetStringField(TEXT("graphName"), Graph->GetName());
		Noop->SetBoolField(TEXT("wasConnected"), false);
		Noop->SetObjectField(TEXT("source"), MCPGraphAuthorPinRef(A));
		if (B) Noop->SetObjectField(TEXT("target"), MCPGraphAuthorPinRef(B));
		Noop->SetArrayField(TEXT("brokenLinks"), TArray<TSharedPtr<FJsonValue>>());
		return MCPResult(Noop);
	}

	if (Ctx.bSave)
	{
		if (auto Blocked = MCPAssetWriteBlockedError(Ctx.Asset, Ctx.AssetPath, TEXT("disconnect these pins"))) return Blocked;
	}

	const TArray<UEdGraphPin*> Targets = B ? TArray<UEdGraphPin*>{ B } : A->LinkedTo;
	TArray<TSharedPtr<FJsonValue>> Broken;
	for (const UEdGraphPin* Other : Targets) if (Other) Broken.Add(MakeShared<FJsonValueObject>(MCPGraphAuthorPinRef(Other)));
	TSharedPtr<FJsonObject> Payload;
	if (Targets.Num() == 1 && Targets[0]) Payload = MCPGraphAuthorConnectPayload(Ctx.AssetPath, A, Targets[0]);

	{
		FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "DisconnectGraphPins", "Disconnect graph pins"));
		Graph->Modify();
		NodeA->Modify();
		if (B)
		{
			if (UEdGraphNode* NodeB = B->GetOwningNode()) NodeB->Modify();
			Schema->BreakSinglePinLink(A, B);
		}
		else
		{
			Schema->BreakPinLinks(*A, true);
		}
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("graphName"), Graph->GetName());
	Result->SetBoolField(TEXT("wasConnected"), true);
	Result->SetObjectField(TEXT("source"), MCPGraphAuthorPinRef(A));
	if (B) Result->SetObjectField(TEXT("target"), MCPGraphAuthorPinRef(B));
	Result->SetArrayField(TEXT("brokenLinks"), Broken);
	Result->SetNumberField(TEXT("brokenCount"), Broken.Num());
	if (Payload.IsValid())
	{
		MCPSetRollback(Result, TEXT("connect_graph_pins"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, FString::Printf(TEXT("%d links were broken and one call re-makes one. ")
			TEXT("Reconnect each entry of brokenLinks with connect_graph_pins."), Broken.Num()));
	}
	MCPGraphAuthorFinish(Ctx, Graph, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::AddGraphNode(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FMCPGraphAuthorContext Ctx;
	if (auto Err = MCPGraphAuthorBegin(Params, Ctx)) return Err;

	if (Ctx.Graphs.Num() != 1)
	{
		TArray<FString> Names;
		for (const UEdGraph* Each : Ctx.Graphs) if (Each) Names.Add(Each->GetName());
		return MCPError(FString::Printf(TEXT("'%s' has %d graphs. Pass graphName to say which one the node goes in: %s."),
			*Ctx.AssetPath, Ctx.Graphs.Num(), *FString::Join(Names, TEXT(", "))));
	}
	UEdGraph* Graph = Ctx.Graphs[0];
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema) return MCPError(FString::Printf(TEXT("Graph '%s' has no schema, so no node can be created in it."), *Graph->GetName()));

	const FString NodeClassSpec = OptionalString(Params, TEXT("nodeClass"));
	const FString ActionName = OptionalString(Params, TEXT("actionName"));
	const FString SpawnMode = OptionalString(Params, TEXT("spawnMode"), TEXT("auto")).ToLower();
	if (SpawnMode != TEXT("auto") && SpawnMode != TEXT("action") && SpawnMode != TEXT("direct"))
	{
		return MCPError(FString::Printf(TEXT("spawnMode must be auto, action or direct, not '%s'."), *SpawnMode));
	}
	if (NodeClassSpec.IsEmpty() && ActionName.IsEmpty())
	{
		return MCPError(TEXT("Pass nodeClass (the class the node spawns, as read_graph reports it) or actionName (a schema menu entry)."));
	}
	if (SpawnMode == TEXT("direct") && NodeClassSpec.IsEmpty())
	{
		return MCPError(TEXT("spawnMode=direct constructs the node class itself, so it needs nodeClass."));
	}

	UClass* NodeClass = nullptr;
	if (!NodeClassSpec.IsEmpty())
	{
		NodeClass = MCPResolveClass(NodeClassSpec);
		if (!NodeClass) return MCPClassNotFoundError(NodeClassSpec, TEXT("nodeClass"));
	}
	const double PosX = OptionalNumber(Params, TEXT("posX"), 0.0);
	const double PosY = OptionalNumber(Params, TEXT("posY"), 0.0);

	TSharedPtr<FEdGraphSchemaAction> Chosen;
	TArray<FMCPGraphAuthorActionInfo> Infos;
	bool bUseDirect = SpawnMode == TEXT("direct");
	if (!bUseDirect)
	{
		TArray<TSharedPtr<FEdGraphSchemaAction>> Actions;
		MCPGraphAuthorCollectActions(Graph, Actions);
		bool bAnySpawner = false;
		TArray<int32> Matches;
		for (const TSharedPtr<FEdGraphSchemaAction>& Action : Actions)
		{
			FMCPGraphAuthorActionInfo Info;
			Info.Action = Action;
			Info.Menu = Action->GetMenuDescription().ToString();
			Info.Category = Action->GetCategory().ToString();
			Info.SpawnClass = MCPGraphAuthorActionSpawnClass(Action);
			bAnySpawner |= Info.SpawnClass != nullptr;
			const bool bClassOk = !NodeClass || Info.SpawnClass == NodeClass;
			const bool bNameOk = ActionName.IsEmpty()
				|| Info.Menu.Equals(ActionName, ESearchCase::IgnoreCase)
				|| (Info.Category + TEXT("|") + Info.Menu).Equals(ActionName, ESearchCase::IgnoreCase);
			if (bClassOk && bNameOk) Matches.Add(Infos.Num());
			Infos.Add(MoveTemp(Info));
		}

		const FString Filter = !NodeClassSpec.IsEmpty() ? NodeClassSpec : ActionName;
		if (Matches.Num() > 1)
		{
			TArray<FMCPGraphAuthorActionInfo> Ambiguous;
			for (int32 Index : Matches) Ambiguous.Add(Infos[Index]);
			return MCPGraphAuthorActionError(FString::Printf(
				TEXT("%d schema actions match. Pass actionName (optionally 'category|actionName') to pick one."), Matches.Num()),
				Ambiguous, FString());
		}
		if (Matches.Num() == 1)
		{
			Chosen = Infos[Matches[0]].Action;
		}
		else if (SpawnMode == TEXT("action") || !ActionName.IsEmpty() || bAnySpawner
			|| !NodeClass || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
		{
			// A schema that publishes spawn actions builds its nodes through them; a
			// node constructed around them would miss what the action sets up.
			return MCPGraphAuthorActionError(FString::Printf(
				TEXT("No action of %s spawns '%s' in graph '%s'. availableActions lists what it offers.%s"),
				*Schema->GetClass()->GetName(), *Filter, *Graph->GetName(),
				bAnySpawner ? TEXT("") : TEXT(" This schema offers no node-creating actions outside its own editor, so nodes cannot be added here.")),
				Infos, Filter);
		}
		else
		{
			bUseDirect = true;
		}
	}

	if (bUseDirect)
	{
		if (auto Err = MCPCheckClassUsable(NodeClassSpec, NodeClass, UEdGraphNode::StaticClass())) return Err;
		const UEdGraphNode* Default = NodeClass->GetDefaultObject<UEdGraphNode>();
		if (Default && !Default->CanCreateUnderSpecifiedSchema(Schema))
		{
			return MCPError(FString::Printf(TEXT("%s cannot be created under %s."), *NodeClass->GetName(), *Schema->GetClass()->GetName()));
		}
	}

	if (Ctx.bSave)
	{
		if (auto Blocked = MCPAssetWriteBlockedError(Ctx.Asset, Ctx.AssetPath, TEXT("add a node to this graph"))) return Blocked;
	}

	TSet<UEdGraphNode*> Before;
	for (UEdGraphNode* Each : Graph->Nodes) Before.Add(Each);
	UEdGraphNode* Node = nullptr;
	{
		FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "AddGraphNode", "Add graph node"));
		Graph->Modify();
		if (Chosen.IsValid())
		{
#if UE_MCP_EDGRAPH_FLOAT_LOCATION
			const FVector2f Location((float)PosX, (float)PosY);
#else
			const FVector2D Location(PosX, PosY);
#endif
			Node = Chosen->PerformAction(Graph, static_cast<UEdGraphPin*>(nullptr), Location, false);
		}
		else
		{
			Node = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
			Graph->AddNode(Node, true, false);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			if (Node->Pins.Num() == 0) Node->AllocateDefaultPins();
			Node->NodePosX = FMath::RoundToInt(PosX);
			Node->NodePosY = FMath::RoundToInt(PosY);
		}
	}

	TArray<UEdGraphNode*> Added;
	for (UEdGraphNode* Each : Graph->Nodes) if (Each && !Before.Contains(Each)) Added.Add(Each);
	if (!Node && Added.Num() == 1) Node = Added[0];
	if (!Node)
	{
		return MCPError(FString::Printf(TEXT("The schema action ran and returned no node (%d nodes were added). Read the graph to see what changed."),
			Added.Num()));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("graphName"), Graph->GetName());
	Result->SetObjectField(TEXT("node"), MCPGraphAuthorNodeJson(Node));
	Result->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());
	Result->SetStringField(TEXT("nodePath"), Node->GetPathName());
	Result->SetStringField(TEXT("createdVia"), Chosen.IsValid() ? TEXT("schema_action") : TEXT("direct"));
	if (Chosen.IsValid())
	{
		Result->SetStringField(TEXT("actionName"), Chosen->GetMenuDescription().ToString());
		Result->SetStringField(TEXT("actionCategory"), Chosen->GetCategory().ToString());
	}
	TArray<TSharedPtr<FJsonValue>> Pins;
	for (const UEdGraphPin* Pin : Node->Pins) if (Pin) Pins.Add(MakeShared<FJsonValueObject>(PinJson(*Pin)));
	Result->SetArrayField(TEXT("pins"), Pins);
	if (Pins.Num() == 0) Result->SetBoolField(TEXT("hasNoPins"), true);
	if (Added.Num() > 1) Result->SetNumberField(TEXT("nodesAdded"), Added.Num());

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), Ctx.AssetPath);
	Payload->SetStringField(TEXT("node"), Node->GetPathName());
	MCPSetRollback(Result, TEXT("remove_graph_node"), Payload);
	MCPGraphAuthorFinish(Ctx, Graph, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::RemoveGraphNode(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FMCPGraphAuthorContext Ctx;
	if (auto Err = MCPGraphAuthorBegin(Params, Ctx)) return Err;

	FString NodeSpec;
	if (auto Err = RequireString(Params, TEXT("node"), NodeSpec)) return Err;
	FString Error;
	UEdGraphNode* Node = MCPGraphAuthorResolveNode(Ctx.Graphs, NodeSpec, TEXT("node"), Error);
	if (!Node) return MCPError(Error);
	UEdGraph* Graph = Node->GetGraph();
	if (!Graph) return MCPError(TEXT("The node is not in a graph."));
	if (!Node->CanUserDeleteNode())
	{
		return MCPError(FString::Printf(TEXT("%s (%s) refuses deletion: the graph type requires it."), *Node->GetName(), *Node->GetClass()->GetName()));
	}

	if (Ctx.bSave)
	{
		if (auto Blocked = MCPAssetWriteBlockedError(Ctx.Asset, Ctx.AssetPath, TEXT("remove this node"))) return Blocked;
	}

	const TSharedPtr<FJsonObject> Removed = MCPGraphAuthorNodeJson(Node);
	// Re-creatable when one of the schema's actions spawns this node's class.
	bool bRecreatable = false;
	{
		TArray<TSharedPtr<FEdGraphSchemaAction>> Actions;
		MCPGraphAuthorCollectActions(Graph, Actions);
		int32 Spawners = 0;
		for (const TSharedPtr<FEdGraphSchemaAction>& Action : Actions)
		{
			if (MCPGraphAuthorActionSpawnClass(Action) == Node->GetClass()) ++Spawners;
		}
		bRecreatable = Spawners == 1;
	}
	const FString RemovedClass = Node->GetClass()->GetName();
	const int32 RemovedX = Node->NodePosX;
	const int32 RemovedY = Node->NodePosY;
	TArray<TSharedPtr<FJsonValue>> Broken;
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin) continue;
		for (const UEdGraphPin* Other : Pin->LinkedTo)
		{
			if (!Other) continue;
			TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
			Link->SetStringField(TEXT("pinName"), Pin->PinName.ToString());
			Link->SetStringField(TEXT("pinId"), Pin->PinId.ToString());
			Link->SetObjectField(TEXT("linkedTo"), MCPGraphAuthorPinRef(Other));
			Broken.Add(MakeShared<FJsonValueObject>(Link));
		}
	}

	{
		FScopedTransaction Transaction(NSLOCTEXT("UEMCPBridge", "RemoveGraphNode", "Remove graph node"));
		Graph->Modify();
		Node->Modify();
		if (const UEdGraphSchema* Schema = Graph->GetSchema()) Schema->BreakNodeLinks(*Node);
		Node->DestroyNode();
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("removed"), true);
	Result->SetStringField(TEXT("graphName"), Graph->GetName());
	Result->SetObjectField(TEXT("node"), Removed);
	Result->SetArrayField(TEXT("brokenLinks"), Broken);
	if (bRecreatable)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), Ctx.AssetPath);
		Payload->SetStringField(TEXT("graphName"), Graph->GetName());
		Payload->SetStringField(TEXT("nodeClass"), RemovedClass);
		Payload->SetNumberField(TEXT("posX"), RemovedX);
		Payload->SetNumberField(TEXT("posY"), RemovedY);
		MCPSetRollback(Result, TEXT("add_graph_node"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("The rollback re-creates a default node of the same class with a new GUID. ")
			TEXT("Its settings and the links in brokenLinks are not restored."));
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("No schema action re-creates this node class by itself. Re-create it with add_graph_node ")
			TEXT("and reconnect the entries of brokenLinks with connect_graph_pins."));
	}
	MCPGraphAuthorFinish(Ctx, Graph, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::CompileCustomizableObject(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UClass* ObjectClass = FindObject<UClass>(nullptr, TEXT("/Script/CustomizableObject.CustomizableObject"));
	if (!ObjectClass)
	{
		return MCPError(TEXT("Mutable plugin not available: the CustomizableObject module is not loaded. Enable the Mutable plugin and restart the editor."));
	}

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;
	if (!Asset->IsA(ObjectClass))
	{
		return MCPError(FString::Printf(TEXT("'%s' is a %s, not a CustomizableObject."), *AssetPath, *Asset->GetClass()->GetName()));
	}

	UClass* LibraryClass = FindObject<UClass>(nullptr, TEXT("/Script/CustomizableObjectEditor.CustomizableObjectEditorFunctionLibrary"));
	UFunction* SyncCompile = LibraryClass ? LibraryClass->FindFunctionByName(TEXT("CompileCustomizableObjectSynchronously")) : nullptr;
	UFunction* Compile = ObjectClass->FindFunctionByName(TEXT("Compile"));
	if (!SyncCompile && !Compile)
	{
		return MCPError(TEXT("Mutable plugin not available: neither CompileCustomizableObjectSynchronously nor CustomizableObject::Compile is registered. Is the CustomizableObjectEditor module loaded?"));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	MCPSetNoRollback(Result, TEXT("A compile rebuilds the object's compiled data from its graph. There is no earlier ")
		TEXT("compiled state to restore; edit the graph and compile again."));
	FString State;
	TArray<FString> Errors;
	TArray<FString> Warnings;
	TArray<FString> IgnoredOptions;
	{
		FMCPGraphCompileLog Log;
		if (SyncCompile)
		{
			FStructOnScope Frame(SyncCompile);
			uint8* Memory = Frame.GetStructMemory();
			FProperty* ReturnProp = nullptr;
			for (TFieldIterator<FProperty> It(SyncCompile); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				FProperty* Prop = *It;
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Memory);
				if (Prop->HasAnyPropertyFlags(CPF_ReturnParm)) { ReturnProp = Prop; continue; }
				if (FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Prop))
				{
					if (ObjectProp->PropertyClass && Asset->IsA(ObjectProp->PropertyClass)) ObjectProp->SetObjectPropertyValue(ValuePtr, Asset);
					continue;
				}
				const FString Option = MCPGraphCompileOption(Params, Prop->GetName());
				if (!Option.IsEmpty())
				{
					if (!MCPGraphApplyText(Prop, ValuePtr, Option)) IgnoredOptions.Add(FString::Printf(TEXT("%s=%s"), *Prop->GetName(), *Option));
					continue;
				}
				const FString Default = SyncCompile->GetMetaData(*(FString(TEXT("CPP_Default_")) + Prop->GetName()));
				if (!Default.IsEmpty()) MCPGraphApplyText(Prop, ValuePtr, Default);
			}
			LibraryClass->GetDefaultObject()->ProcessEvent(SyncCompile, Memory);
			if (ReturnProp) State = MCPGraphEnumText(ReturnProp, ReturnProp->ContainerPtrToValuePtr<void>(Memory));
			Result->SetStringField(TEXT("compiledWith"), TEXT("CompileCustomizableObjectSynchronously"));
		}
		else
		{
			FStructOnScope Frame(Compile);
			uint8* Memory = Frame.GetStructMemory();
			for (TFieldIterator<FProperty> It(Compile); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				FStructProperty* StructProp = CastField<FStructProperty>(*It);
				if (!StructProp) continue;
				void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Memory);
				for (TFieldIterator<FProperty> Field(StructProp->Struct); Field; ++Field)
				{
					void* FieldPtr = Field->ContainerPtrToValuePtr<void>(StructPtr);
					const FString Name = Field->GetName();
					if (FBoolProperty* Bool = CastField<FBoolProperty>(*Field))
					{
						if (Name == TEXT("bAsync") || Name == TEXT("bSkipIfCompiled") || Name == TEXT("bSkipIfNotOutOfDate"))
						{
							Bool->SetPropertyValue(FieldPtr, false);
							continue;
						}
					}
					const FString Option = MCPGraphCompileOption(Params, Name);
					if (!Option.IsEmpty() && !MCPGraphApplyText(*Field, FieldPtr, Option))
					{
						IgnoredOptions.Add(FString::Printf(TEXT("%s=%s"), *Name, *Option));
					}
				}
			}
			Asset->ProcessEvent(Compile, Memory);
			Result->SetStringField(TEXT("compiledWith"), TEXT("CustomizableObject.Compile"));
		}
		if (GLog) GLog->Flush();
		FScopeLock Lock(&Log.Mutex);
		Errors = Log.Errors;
		Warnings = Log.Warnings;
	}

	bool bCompiled = false;
	bool bKnown = false;
	if (UFunction* IsCompiled = ObjectClass->FindFunctionByName(TEXT("IsCompiled")))
	{
		FStructOnScope Frame(IsCompiled);
		Asset->ProcessEvent(IsCompiled, Frame.GetStructMemory());
		if (FBoolProperty* Ret = CastField<FBoolProperty>(IsCompiled->GetReturnProperty()))
		{
			bCompiled = Ret->GetPropertyValue_InContainer(Frame.GetStructMemory());
			bKnown = true;
		}
	}

	if (!State.IsEmpty()) Result->SetStringField(TEXT("state"), State);
	if (bKnown) Result->SetBoolField(TEXT("compiled"), bCompiled);
	Result->SetArrayField(TEXT("errors"), MCPStringListToJson(Errors));
	Result->SetArrayField(TEXT("warnings"), MCPStringListToJson(Warnings));
	Result->SetNumberField(TEXT("errorCount"), Errors.Num());
	Result->SetNumberField(TEXT("warningCount"), Warnings.Num());
	if (IgnoredOptions.Num() > 0) Result->SetArrayField(TEXT("ignoredOptions"), MCPStringListToJson(IgnoredOptions));

	const bool bFailed = State == TEXT("Failed") || (bKnown && !bCompiled) || Errors.Num() > 0;
	if (bFailed)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), Errors.Num() > 0
			? FString::Printf(TEXT("Compile of '%s' failed with %d error(s); the first: %s"), *AssetPath, Errors.Num(), *Errors[0])
			: FString::Printf(TEXT("Compile of '%s' did not produce a compiled object. See warnings and the Mutable message log."), *AssetPath));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAssetHandlers::CreateCustomizableObject(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));
	while (PackagePath.Len() > 1 && PackagePath.EndsWith(TEXT("/"))) PackagePath.LeftChopInline(1);
	FText InvalidReason;
	if (!FPackageName::IsValidLongPackageName(PackagePath + TEXT("/") + Name, true, &InvalidReason))
	{
		return MCPError(FString::Printf(TEXT("Invalid destination '%s/%s': %s"), *PackagePath, *Name, *InvalidReason.ToString()));
	}
	if (MCPIsProtectedAssetPath(PackagePath)) return MCPProtectedPathError(PackagePath);

	FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	OnConflict.ToLowerInline();
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("error"))
	{
		return MCPError(TEXT("onConflict must be 'skip' or 'error'"));
	}
	const bool bSave = OptionalBool(Params, TEXT("save"), true);

	UClass* ObjectClass = FindObject<UClass>(nullptr, TEXT("/Script/CustomizableObject.CustomizableObject"));
	if (!ObjectClass)
	{
		return MCPError(TEXT("Mutable plugin not available: the CustomizableObject module is not loaded. Enable the Mutable plugin and restart the editor."));
	}

	const FString AssetPath = PackagePath + TEXT("/") + Name;
	const FString ObjectPath = AssetPath + TEXT(".") + Name;

	// The graph, its schema and every node with pins, so the caller can wire straight away.
	auto Describe = [](UObject* Object, const TSharedPtr<FJsonObject>& Result) -> int32
	{
		TArray<UEdGraph*> Graphs;
		CollectGraphs(Object, Graphs);
		TArray<TSharedPtr<FJsonValue>> GraphsJson;
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			TSharedPtr<FJsonObject> GraphJson = MakeShared<FJsonObject>();
			GraphJson->SetStringField(TEXT("graphName"), Graph->GetName());
			GraphJson->SetStringField(TEXT("schema"), Graph->Schema ? Graph->Schema->GetName() : TEXT(""));
			TArray<TSharedPtr<FJsonValue>> Nodes;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				TSharedPtr<FJsonObject> NodeJsonObj = MCPGraphAuthorNodeJson(Node);
				TArray<TSharedPtr<FJsonValue>> Pins;
				for (const UEdGraphPin* Pin : Node->Pins) if (Pin) Pins.Add(MakeShared<FJsonValueObject>(PinJson(*Pin)));
				NodeJsonObj->SetArrayField(TEXT("pins"), Pins);
				Nodes.Add(MakeShared<FJsonValueObject>(NodeJsonObj));
				if (!Result->HasField(TEXT("rootNode"))) Result->SetObjectField(TEXT("rootNode"), NodeJsonObj);
			}
			GraphJson->SetArrayField(TEXT("nodes"), Nodes);
			GraphsJson.Add(MakeShared<FJsonValueObject>(GraphJson));
			if (!Result->HasField(TEXT("graphName"))) Result->SetStringField(TEXT("graphName"), Graph->GetName());
		}
		Result->SetArrayField(TEXT("graphs"), GraphsJson);
		return GraphsJson.Num();
	};

	if (UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath))
	{
		if (!Existing->IsA(ObjectClass))
		{
			return MCPError(FString::Printf(TEXT("'%s' is already taken by a %s."), *ObjectPath, *Existing->GetClass()->GetName()));
		}
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("CustomizableObject '%s' already exists"), *ObjectPath));
		}
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("assetPath"), AssetPath);
		Existed->SetStringField(TEXT("objectPath"), Existing->GetPathName());
		if (Describe(Existing, Existed) == 0)
		{
			Existed->SetStringField(TEXT("note"), TEXT("The existing asset has no graph; it was not made by Mutable's factory. Delete it and create it again here."));
		}
		return MCPResult(Existed);
	}

	// Mutable's own factory builds the Source graph and adds the Base Object
	// node (UCustomizableObjectGraph::AddEssentialGraphNodes).
	UObject* Created = nullptr;
	FString CreatedVia;
	if (UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/CustomizableObjectEditor.CustomizableObjectFactory")))
	{
		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		Created = AssetTools.CreateAsset(Name, PackagePath, ObjectClass, Factory);
		CreatedVia = TEXT("CustomizableObjectFactory");
	}
	// The editor library's NewCustomizableObject builds the same graph.
	if (!Created)
	{
		UClass* LibraryClass = FindObject<UClass>(nullptr, TEXT("/Script/CustomizableObjectEditor.CustomizableObjectEditorFunctionLibrary"));
		UFunction* NewObjectFunc = LibraryClass ? LibraryClass->FindFunctionByName(TEXT("NewCustomizableObject")) : nullptr;
		if (NewObjectFunc)
		{
			FStructOnScope Frame(NewObjectFunc);
			uint8* Memory = Frame.GetStructMemory();
			for (TFieldIterator<FProperty> It(NewObjectFunc); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				FStructProperty* StructProp = CastField<FStructProperty>(*It);
				if (!StructProp) continue;
				void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Memory);
				if (FStrProperty* PathProp = CastField<FStrProperty>(StructProp->Struct->FindPropertyByName(TEXT("PackagePath"))))
				{
					PathProp->SetPropertyValue_InContainer(StructPtr, PackagePath);
				}
				if (FStrProperty* NameProp = CastField<FStrProperty>(StructProp->Struct->FindPropertyByName(TEXT("AssetName"))))
				{
					NameProp->SetPropertyValue_InContainer(StructPtr, Name);
				}
			}
			LibraryClass->GetDefaultObject()->ProcessEvent(NewObjectFunc, Memory);
			if (FObjectPropertyBase* Ret = CastField<FObjectPropertyBase>(NewObjectFunc->GetReturnProperty()))
			{
				Created = Ret->GetObjectPropertyValue_InContainer(Memory);
			}
			CreatedVia = TEXT("CustomizableObjectEditorFunctionLibrary.NewCustomizableObject");
		}
	}
	if (!Created)
	{
		return MCPError(TEXT("Mutable plugin not available: neither CustomizableObjectFactory nor NewCustomizableObject could create the asset. Is the CustomizableObjectEditor module loaded?"));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("objectPath"), Created->GetPathName());
	Result->SetStringField(TEXT("createdVia"), CreatedVia);
	MCPSetDeleteAssetRollback(Result, AssetPath);
	if (Describe(Created, Result) == 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%s created '%s' but it holds no graph, so nothing can be authored in it. Delete it with the rollback."),
			*CreatedVia, *ObjectPath));
		return MCPResult(Result);
	}

	if (bSave)
	{
		FString Reason;
		const bool bSaved = SaveAssetPackageChecked(Created, Reason);
		MCPNoteSaveOutcome(Result, AssetPath, bSaved, Reason);
	}
	else
	{
		Created->MarkPackageDirty();
		Result->SetBoolField(TEXT("saved"), false);
	}
	return MCPResult(Result);
}
