// asset(read_graph): what it reports, and what it refuses to guess (#1059).
//
// Built on a Blueprint because that is a graph type the engine will create
// without an editor window. The reader itself is type-agnostic.

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* const MCPGraphTestRoot = TEXT("/UEMCPGraphTest/");

	/** A private content root for the duration of the test, so nothing here
	 *  can touch the project the bridge happens to be attached to. */
	struct FScopedGraphTestMount
	{
		FString RootPath;
		FString ContentPath;

		FScopedGraphTestMount()
			: RootPath(MCPGraphTestRoot)
			, ContentPath(FPaths::Combine(
				FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::UserTempDir())),
				FString(TEXT("UEMCPGraphTest")),
				FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
			IFileManager::Get().MakeDirectory(*ContentPath, /*Tree=*/true);
			FPackageName::RegisterMountPoint(RootPath, ContentPath);
		}

		~FScopedGraphTestMount()
		{
			FPackageName::UnRegisterMountPoint(RootPath, ContentPath);
			IFileManager::Get().DeleteDirectory(*ContentPath, /*RequireExists=*/false, /*Tree=*/true);
		}

		FScopedGraphTestMount(const FScopedGraphTestMount&) = delete;
		FScopedGraphTestMount& operator=(const FScopedGraphTestMount&) = delete;
	};

	/** Through the registry, which also proves the action is registered. */
	TSharedPtr<FJsonObject> ReadGraph(const FString& AssetPath, int32 MaxNodes = 0)
	{
		FMCPHandlerRegistry Registry;
		FAssetHandlers::RegisterHandlers(Registry);

		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), AssetPath);
		if (MaxNodes > 0) Params->SetNumberField(TEXT("maxNodes"), MaxNodes);

		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(TEXT("read_asset_graph"), Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}

	/** The named graph out of a read_graph response, or null. */
	TSharedPtr<FJsonObject> GraphNamed(const TSharedPtr<FJsonObject>& Result, const TCHAR* Name)
	{
		if (!Result.IsValid()) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (!Result->TryGetArrayField(TEXT("graphs"), Graphs) || !Graphs) return nullptr;
		for (const TSharedPtr<FJsonValue>& Entry : *Graphs)
		{
			const TSharedPtr<FJsonObject> Graph = Entry.IsValid() ? Entry->AsObject() : nullptr;
			FString GraphName;
			if (Graph.IsValid() && Graph->TryGetStringField(TEXT("name"), GraphName) && GraphName == Name)
			{
				return Graph;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetReadGraphTest,
	"UE.MCP.Asset.ReadGraph.ReportsNodesPinsAndWires",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetReadGraphTest::RunTest(const FString& Parameters)
{
	const FScopedGraphTestMount Mount;

	// Unique per run. CreateBlueprint asserts the name is free, and a blueprint
	// from an earlier run in this process is still in memory under the freshly
	// registered mount, so a fixed name takes the editor down on the second
	// pass rather than failing the test.
	const FString ProbeName = TEXT("BP_GraphProbe_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString PackageName = FString(MCPGraphTestRoot) + ProbeName;
	UPackage* Package = CreatePackage(*PackageName);
	TestNotNull(TEXT("probe package created"), Package);
	if (!Package) return false;

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), Package, FName(*ProbeName), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	TestNotNull(TEXT("probe blueprint created"), Blueprint);
	if (!Blueprint)
	{
		Package->SetDirtyFlag(false);
		return false;
	}

	// Its own graph, not the seeded event graph: CreateBlueprint puts default
	// nodes in that one, so the counts below would depend on how many.
	UEdGraph* EventGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, FName(TEXT("MCPProbeGraph")), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	TestNotNull(TEXT("a probe graph was created"), EventGraph);
	if (!EventGraph)
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	FBlueprintEditorUtils::AddUbergraphPage(Blueprint, EventGraph);
	TestEqual(TEXT("the probe graph starts empty"), EventGraph->Nodes.Num(), 0);

	// Two nodes and one wire between them.
	UK2Node_Event* Event = NewObject<UK2Node_Event>(EventGraph);
	Event->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
	Event->bOverrideFunction = true;
	Event->CreateNewGuid();
	Event->AllocateDefaultPins();
	EventGraph->AddNode(Event, /*bFromUI=*/false, /*bSelectNewNode=*/false);

	UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(EventGraph);
	Call->FunctionReference.SetExternalMember(TEXT("PrintString"), UKismetSystemLibrary::StaticClass());
	Call->CreateNewGuid();
	Call->AllocateDefaultPins();
	EventGraph->AddNode(Call, /*bFromUI=*/false, /*bSelectNewNode=*/false);

	UEdGraphPin* Then = Event->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	UEdGraphPin* Exec = Call->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	TestNotNull(TEXT("the event has a then pin"), Then);
	TestNotNull(TEXT("the call has an execute pin"), Exec);
	if (!Then || !Exec)
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	Then->MakeLinkTo(Exec);

	// The whole graph: two nodes, one wire, and the wire seen from both ends.
	{
		const TSharedPtr<FJsonObject> Graph = GraphNamed(ReadGraph(PackageName), *EventGraph->GetName());
		TestNotNull(TEXT("the event graph is reported"), Graph.Get());
		if (Graph.IsValid())
		{
			TestEqual(TEXT("both nodes are reported"), (int32)Graph->GetNumberField(TEXT("nodeCount")), 2);
			TestEqual(TEXT("the wire is counted once"),
				(int32)Graph->GetNumberField(TEXT("connectionCount")), 1);
		}
	}

	// THE TRUNCATION CASE: with one node emitted the wire has one retained end
	// and is still one wire. Halving the pin ends would report none.
	{
		const TSharedPtr<FJsonObject> Graph = GraphNamed(ReadGraph(PackageName, /*MaxNodes=*/1), *EventGraph->GetName());
		TestNotNull(TEXT("the event graph is reported when truncated"), Graph.Get());
		if (Graph.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			Graph->TryGetArrayField(TEXT("nodes"), Nodes);
			TestEqual(TEXT("one node was emitted"), Nodes ? Nodes->Num() : -1, 1);
			TestEqual(TEXT("nodeCount still reports the graph's own total"),
				(int32)Graph->GetNumberField(TEXT("nodeCount")), 2);
			TestEqual(TEXT("the wire survives truncation"),
				(int32)Graph->GetNumberField(TEXT("connectionCount")), 1);
		}
	}

	// A pinless node is named as such: that is how a graph damaged by an
	// append to Nodes is found again.
	{
		UK2Node_CallFunction* Pinless = NewObject<UK2Node_CallFunction>(EventGraph);
		Pinless->CreateNewGuid();
		EventGraph->AddNode(Pinless, /*bFromUI=*/false, /*bSelectNewNode=*/false);

		const TSharedPtr<FJsonObject> Graph = GraphNamed(ReadGraph(PackageName), *EventGraph->GetName());
		bool bFlagged = false;
		if (Graph.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (Graph->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes)
			{
				for (const TSharedPtr<FJsonValue>& Entry : *Nodes)
				{
					const TSharedPtr<FJsonObject> Node = Entry.IsValid() ? Entry->AsObject() : nullptr;
					if (Node.IsValid() && Node->HasField(TEXT("hasNoPins"))) bFlagged = true;
				}
			}
		}
		TestTrue(TEXT("a node with no pins is reported as such"), bFlagged);
	}

	// Tear the probe down. A blueprint left in memory with an uncompiled node
	// is offered to the editor's Play-in-Editor compile check, which then
	// raises a modal naming it and blocks every later call.
	EventGraph->Nodes.Empty();
	Blueprint->UbergraphPages.Empty();
	Blueprint->ClearFlags(RF_Public | RF_Standalone);
	Blueprint->MarkAsGarbage();
	Package->SetDirtyFlag(false);
	Package->ClearFlags(RF_Public | RF_Standalone);
	Blueprint = nullptr;
	EventGraph = nullptr;
	Package = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetReadGraphNoGraphTest,
	"UE.MCP.Asset.ReadGraph.AnAssetWithNoGraphSaysSoWithoutFailing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetReadGraphNoGraphTest::RunTest(const FString& Parameters)
{
	// A graph-less asset is not an error, and the answer must not assert more
	// than was actually searched.
	const TSharedPtr<FJsonObject> Result = ReadGraph(TEXT("/Engine/EngineMaterials/DefaultMaterial"));
	TestNotNull(TEXT("the call answered"), Result.Get());
	if (!Result.IsValid()) return false;

	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);
	TestTrue(TEXT("no graph is not a failure"), bSuccess);
	TestEqual(TEXT("no graphs are reported"), (int32)Result->GetNumberField(TEXT("graphCount")), 0);

	FString Note;
	Result->TryGetStringField(TEXT("note"), Note);
	TestTrue(TEXT("the note says what was searched"), Note.Contains(TEXT("Searched its subobjects")));
	TestTrue(TEXT("and names the types this reader cannot reach"),
		Note.Contains(TEXT("Material and PCGGraph")));

	return true;
}

#endif
