// asset(read_graph) - node, pin and connection topology for any EdGraph-backed
// asset (#1059).
//
// Reflection cannot reach this: UEdGraphNode::Pins has no UPROPERTY and
// UEdGraphPin is not a UObject. From C++ Pins is an ordinary member.
//
// Generic rather than per-system, because UEdGraph is the engine's one graph
// type. Read only: making a connection means running the owning schema's
// TryCreateConnection, which is separate work.

#include "AssetHandlers.h"
#include "HandlerUtils.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UObjectHash.h"

namespace
{
	/** Every UEdGraph reachable from an asset, including graphs nested inside
	 *  other graphs' nodes (a state machine, a collapsed node, a macro). */
	void CollectGraphs(UObject* Root, TArray<UEdGraph*>& Out)
	{
		if (!Root) return;
		TArray<UObject*> Inner;
		GetObjectsWithOuter(Root, Inner, /*bIncludeNestedObjects=*/true);
		for (UObject* Object : Inner)
		{
			if (UEdGraph* Graph = Cast<UEdGraph>(Object))
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

	/** One end of a connection, named so the other end can be found again. */
	TSharedPtr<FJsonObject> LinkJson(const UEdGraphPin* Other)
	{
		TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
		if (!Other) return Link;
		Link->SetStringField(TEXT("pinName"), Other->PinName.ToString());
		Link->SetStringField(TEXT("pinId"), Other->PinId.ToString());
		if (const UEdGraphNode* Owner = Other->GetOwningNodeUnchecked())
		{
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

	TSharedPtr<FJsonObject> NodeJson(UEdGraphNode* Node, bool bIncludePins, int32& OutPinCount, int32& OutLinkCount)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Node->GetName());
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
			if (Pin) NodeLinks += Pin->LinkedTo.Num();
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
				TEXT("this: it cannot be wired to anything and the editor cannot draw it. Delete it and create the ")
				TEXT("node through the editor or an action that knows the schema."));
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
		Empty->SetStringField(TEXT("note"), FString::Printf(
			TEXT("'%s' is a %s and holds no UEdGraph, so it has no node graph to read. This action reads assets ")
			TEXT("built on the engine's EdGraph (CustomizableObject, Material, AnimBP, Niagara and the rest)."),
			*AssetPath, *Asset->GetClass()->GetName()));
		return MCPResult(Empty);
	}

	TArray<TSharedPtr<FJsonValue>> GraphsJson;
	int32 TotalNodes = 0;
	int32 TotalPins = 0;
	int32 TotalLinks = 0;
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
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (NodesJson.Num() >= MaxNodes)
			{
				bTruncated = true;
				break;
			}
			NodesJson.Add(MakeShared<FJsonValueObject>(NodeJson(Node, bIncludePins, GraphPins, GraphLinks)));
		}
		GraphJson->SetArrayField(TEXT("nodes"), NodesJson);
		GraphJson->SetNumberField(TEXT("pinCount"), GraphPins);
		// Each wire is seen from both ends, so the raw sum double-counts.
		GraphJson->SetNumberField(TEXT("connectionCount"), GraphLinks / 2);

		TotalNodes += NodesJson.Num();
		TotalPins += GraphPins;
		TotalLinks += GraphLinks;
		GraphsJson.Add(MakeShared<FJsonValueObject>(GraphJson));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
	Result->SetArrayField(TEXT("graphs"), GraphsJson);
	Result->SetNumberField(TEXT("graphCount"), GraphsJson.Num());
	Result->SetNumberField(TEXT("nodeCount"), TotalNodes);
	Result->SetNumberField(TEXT("pinCount"), TotalPins);
	Result->SetNumberField(TEXT("connectionCount"), TotalLinks / 2);
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
