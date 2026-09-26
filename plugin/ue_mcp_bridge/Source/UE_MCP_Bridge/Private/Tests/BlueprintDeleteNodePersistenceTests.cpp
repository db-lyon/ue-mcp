// #1166: a node removed by delete_node must not reach the saved package. The
// deleted node is held alive across the save, the way an undo buffer would
// hold it, so the test covers the case the report describes. Assets live in a
// private temp mount.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPDeleteNodePersistenceTests
{
TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
	return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
}

bool Succeeded(const TSharedPtr<FJsonObject>& Result)
{
	bool bSuccess = false;
	return Result.IsValid() && Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
}

UEdGraphNode* FindByGuid(UEdGraph* Graph, const FGuid& Guid)
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == Guid) return Node;
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintDeleteNodeNotSerializedTest,
	"UE.MCP.Blueprint.DeleteNode.NotSerialized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintDeleteNodeNotSerializedTest::RunTest(const FString& Parameters)
{
	using namespace MCPDeleteNodePersistenceTests;
	// First member destroyed last, so it evicts the probe package.
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPDeleteNode_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPDeleteNode"));
	const FString PackageName = Mount.RootPath + TEXT("BP_DeleteProbe");
	const FString ObjectPath = PackageName + TEXT(".BP_DeleteProbe");

	UPackage* Package = CreatePackage(*PackageName);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), Package, FName(TEXT("BP_DeleteProbe")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("Blueprint fixture created"), Blueprint)) return false;
	if (!TestTrue(TEXT("fixture has an event graph"), Blueprint->UbergraphPages.Num() > 0)) return false;
	const FString GraphName = Blueprint->UbergraphPages[0]->GetName();

	FMCPHandlerRegistry Registry;
	FBlueprintHandlers::RegisterHandlers(Registry);

	auto Add = MakeShared<FJsonObject>();
	Add->SetStringField(TEXT("assetPath"), PackageName);
	Add->SetStringField(TEXT("graphName"), GraphName);
	Add->SetStringField(TEXT("nodeClass"), TEXT("CallFunction"));
	auto NodeParams = MakeShared<FJsonObject>();
	NodeParams->SetStringField(TEXT("functionName"), TEXT("PrintString"));
	NodeParams->SetStringField(TEXT("className"), TEXT("/Script/Engine.KismetSystemLibrary"));
	Add->SetObjectField(TEXT("nodeParams"), NodeParams);
	const TSharedPtr<FJsonObject> Added = Call(Registry, TEXT("add_node"), Add);
	if (!TestTrue(TEXT("add_node succeeds"), Succeeded(Added))) return false;

	const FString NodeId = Added->GetStringField(TEXT("nodeId"));
	FGuid NodeGuid;
	if (!TestTrue(TEXT("add_node returns a GUID"), FGuid::Parse(NodeId, NodeGuid))) return false;
	UEdGraph* Graph = Blueprint->UbergraphPages[0];
	UEdGraphNode* Node = FindByGuid(Graph, NodeGuid);
	if (!TestNotNull(TEXT("the added node is in the graph"), Node)) return false;
	const FName NodeName = Node->GetFName();

	// Hold the node the way the editor's undo buffer would, so the save runs
	// with the detached node still alive and still outered to the graph.
	TStrongObjectPtr<UEdGraphNode> Held(Node);
	const TWeakObjectPtr<UEdGraphNode> WeakNode(Node);

	auto Delete = MakeShared<FJsonObject>();
	Delete->SetStringField(TEXT("assetPath"), PackageName);
	Delete->SetStringField(TEXT("graphName"), GraphName);
	Delete->SetStringField(TEXT("nodeId"), NodeId);
	const TSharedPtr<FJsonObject> Deleted = Call(Registry, TEXT("delete_node"), Delete);
	if (!TestTrue(TEXT("delete_node succeeds"), Succeeded(Deleted))) return false;

	TestNull(TEXT("the node is gone from the graph's Nodes list"), FindByGuid(Graph, NodeGuid));
	TestFalse(TEXT("the graph no longer holds the node pointer"), Graph->Nodes.Contains(Node));
	AddInfo(FString::Printf(TEXT("after delete_node the held node's outer is %s"),
		Node->GetOuter() ? *Node->GetOuter()->GetPathName() : TEXT("(none)")));

	FString SaveReason;
	const bool bSaved = SaveAssetPackageChecked(Blueprint, SaveReason);
	if (!TestTrue(TEXT("Blueprint saves with the deleted node still alive: ") + SaveReason, bSaved)) return false;

	// Release everything, then read the package back from disk.
	Held.Reset();
	const TWeakObjectPtr<UBlueprint> Previous(Blueprint);
	Blueprint = nullptr;
	Graph = nullptr;
	Node = nullptr;
	ForEachObjectWithPackage(Package, [](UObject* Object)
	{
		Object->ClearFlags(RF_Standalone);
		return true;
	});
	Package->SetDirtyFlag(false);
	Package->ClearFlags(RF_Standalone);
	ResetLoaders(Package);
	Package = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	if (!TestFalse(TEXT("the original Blueprint was evicted"), Previous.IsValid())) return false;
	TestFalse(TEXT("nothing else kept the deleted node alive"), WeakNode.IsValid());
	if (!TestNull(TEXT("reload cannot reuse the original Blueprint"), FindObject<UBlueprint>(nullptr, *ObjectPath))) return false;

	UBlueprint* Reloaded = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	if (!TestNotNull(TEXT("Blueprint reloads from disk"), Reloaded)) return false;
	UPackage* ReloadedPackage = Reloaded->GetOutermost();

	// The export table is what was serialized, whether or not the loader
	// created an object for every entry.
	if (FLinkerLoad* Linker = FLinkerLoad::FindExistingLinkerForPackage(ReloadedPackage))
	{
		for (const FObjectExport& Export : Linker->ExportMap)
		{
			TestNotEqual(TEXT("no export carries the deleted node's name"), Export.ObjectName, NodeName);
		}
	}
	else
	{
		AddInfo(TEXT("no linker for the reloaded package; export table not checked"));
	}

	// And every graph node that came back is one its graph actually lists.
	int32 GraphNodes = 0;
	ForEachObjectWithPackage(ReloadedPackage, [this, &NodeGuid, &GraphNodes](UObject* Object)
	{
		UEdGraphNode* Loaded = Cast<UEdGraphNode>(Object);
		if (!Loaded) return true;
		++GraphNodes;
		TestNotEqual(TEXT("the deleted node's GUID did not come back"), Loaded->NodeGuid, NodeGuid);
		if (UEdGraph* Owner = Cast<UEdGraph>(Loaded->GetOuter()))
		{
			TestTrue(FString::Printf(TEXT("%s is listed in its graph's Nodes"), *Loaded->GetPathName()),
				Owner->Nodes.Contains(Loaded));
		}
		return true;
	});
	TestTrue(TEXT("the reloaded Blueprint still has graph nodes"), GraphNodes > 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
