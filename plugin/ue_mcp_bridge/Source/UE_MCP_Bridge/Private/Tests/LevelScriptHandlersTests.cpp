#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/LevelHandlers.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelScriptClearGraphNodesTest,
	"UE.MCP.Level.ClearLevelScript.GraphNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelScriptClearGraphNodesTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = NewObject<UBlueprint>(GetTransientPackage());
	UEdGraph* Graph = NewObject<UEdGraph>(Blueprint, TEXT("EventGraph"));
	Blueprint->UbergraphPages.Add(Graph);
	Graph->AddNode(NewObject<UEdGraphNode>(Graph, TEXT("FirstNode")));
	Graph->AddNode(NewObject<UEdGraphNode>(Graph, TEXT("SecondNode")));
	FEdGraphPinType VariableType;
	VariableType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("LegacyReference"), VariableType);

	TArray<TSharedPtr<FJsonValue>> Graphs;
	TestEqual(TEXT("dry run reports both nodes"),
		FLevelHandlers::ClearBlueprintGraphNodes(Blueprint, true, Graphs), 2);
	TestEqual(TEXT("dry run keeps both nodes"), Graph->Nodes.Num(), 2);
	TestEqual(TEXT("dry run reports the graph"), Graphs.Num(), 1);

	Graphs.Reset();
	TestEqual(TEXT("apply removes both nodes"),
		FLevelHandlers::ClearBlueprintGraphNodes(Blueprint, false, Graphs), 2);
	TestEqual(TEXT("apply leaves the graph empty"), Graph->Nodes.Num(), 0);
	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, TEXT("LegacyReference"));
	TestEqual(TEXT("member variable can be removed"), Blueprint->NewVariables.Num(), 0);
	return true;
}

#endif
