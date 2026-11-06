// blueprint(focus_node) (#1487): open a Blueprint on one node, the way a
// find-results hyperlink does, and read back what the editor then shows.

#include "BlueprintHandlers.h"
#include "BlueprintHandlers_Internal.h"
#include "HandlerUtils.h"
#include "BlueprintEditorModule.h"
#include "GraphEditor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"

TSharedPtr<FJsonValue> FBlueprintHandlers::FocusNode(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;
	FString NodeId;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("nodeId"), NodeId)) return Err;
	const FString GraphFilter = ReadGraphNameOrSelector(Params, FString());

	UBlueprint* Blueprint = LoadBlueprint(AssetPath);
	if (!Blueprint) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	if (!GEditor || !FSlateApplication::IsInitialized())
	{
		return MCPError(TEXT("focus_node needs the editor UI; this editor has no Slate application."));
	}

	TArray<UEdGraph*> Graphs;
	if (GraphFilter.IsEmpty())
	{
		Blueprint->GetAllGraphs(Graphs);
	}
	else
	{
		UEdGraph* Graph = FindGraph(Blueprint, GraphFilter);
		if (!Graph) return MCPError(FString::Printf(TEXT("Graph '%s' not found in %s"), *GraphFilter, *AssetPath));
		Graphs.Add(Graph);
	}

	// A GUID names one node in the whole Blueprint. A name or title can repeat
	// across graphs, so it has to be unique to be focused.
	TArray<UEdGraphNode*> Matches;
	FGuid Guid;
	if (FGuid::Parse(NodeId, Guid))
	{
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->NodeGuid == Guid) Matches.AddUnique(Node);
			}
		}
	}
	if (Matches.Num() == 0)
	{
		for (UEdGraph* Graph : Graphs)
		{
			if (UEdGraphNode* Node = FindNodeByGuidOrName(Graph, NodeId)) Matches.AddUnique(Node);
		}
	}
	if (Matches.Num() == 0)
	{
		return MCPError(FString::Printf(TEXT("No node '%s' in %s%s"), *NodeId, *AssetPath,
			GraphFilter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" graph '%s'"), *GraphFilter)));
	}
	if (Matches.Num() > 1)
	{
		TArray<FString> Where;
		for (UEdGraphNode* Node : Matches) Where.Add(Node->GetGraph() ? Node->GetGraph()->GetName() : TEXT("?"));
		return MCPError(FString::Printf(TEXT("'%s' names %d nodes (in %s). Pass the node GUID, or graphName to narrow it."),
			*NodeId, Matches.Num(), *FString::Join(Where, TEXT(", "))));
	}
	UEdGraphNode* Node = Matches[0];
	UEdGraph* Graph = Node->GetGraph();

	// The hyperlink path: open the editor, bring the node's graph to the front,
	// select the node and frame it.
	const TSharedPtr<IBlueprintEditor> Editor = FKismetEditorUtilities::GetIBlueprintEditorForObject(Node, /*bOpenEditor*/ true);
	if (!Editor.IsValid())
	{
		return MCPError(FString::Printf(TEXT("The Blueprint editor did not open for %s"), *AssetPath));
	}
	Editor->JumpToHyperlink(Node, /*bRequestRename*/ false);
	Editor->FocusWindow(Node);

	UEdGraph* Focused = Editor->GetFocusedGraph();
	const TSharedPtr<SGraphEditor> GraphEditor = Editor->OpenGraphAndBringToFront(Graph, /*bSetFocus*/ false);
	const bool bSelected = GraphEditor.IsValid() && GraphEditor->GetSelectedNodes().Contains(Node);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
	Result->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
	Result->SetStringField(TEXT("graphName"), Graph ? Graph->GetName() : FString());
	Result->SetStringField(TEXT("focusedGraph"), Focused ? Focused->GetName() : FString());
	Result->SetBoolField(TEXT("graphFocused"), Focused != nullptr && Focused == Graph);
	Result->SetBoolField(TEXT("selected"), bSelected);
	Result->SetNumberField(TEXT("selectedCount"), GraphEditor.IsValid() ? GraphEditor->GetSelectedNodes().Num() : 0);
	return MCPResult(Result);
}
