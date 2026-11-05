// PCG handlers through the registry: graph folder, spawner selector ownership,
// volume transform and the run record. Graphs live on a private mount and
// actors in a disposable world, never the open project or map.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/PCG/PCGHandlers.h"
#include "Handlers/PCG/PCGHandlers_Internal.h"
#include "Tests/MCPScopedTestMount.h"
#include "Editor.h"
#include "Elements/PCGAddTag.h"
#include "PCGComponent.h"
#include "PCGPin.h"
#include "PCGSubgraph.h"
#include "HandlerPackageSave.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "MeshSelectors/PCGMeshSelectorByAttribute.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGVolume.h"
#include "UObject/Package.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "StructUtils/PropertyBag.h"

namespace PCGHandlerTests
{
	const TCHAR* const Root = TEXT("/UEMCPPCGTest/");

	TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	}

	bool Succeeded(const TSharedPtr<FJsonObject>& Result)
	{
		bool bSuccess = false;
		return Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	/** A fresh editor world made current for the test, restored on the way out. */
	struct FWorldScope
	{
		UWorld* OriginalWorld = nullptr;
		UWorld* World = nullptr;

		FWorldScope()
		{
			if (!GEditor) return;
			OriginalWorld = GEditor->GetEditorWorldContext().World();
			const UWorld::InitializationValues Initialization = UWorld::InitializationValues()
				.InitializeScenes(false).AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
				.ShouldSimulatePhysics(false).EnableTraceCollision(false).SetTransactional(false)
				.CreateFXSystem(false).CreateWorldPartition(false);
			World = UWorld::CreateWorld(EWorldType::Editor, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("UEMCP_PCGTest")),
				GetTransientPackage(), true, ERHIFeatureLevel::Num, &Initialization);
			if (World) GEditor->GetEditorWorldContext().SetCurrentWorld(World);
		}

		~FWorldScope()
		{
			if (!World) return;
			GEditor->GetEditorWorldContext().SetCurrentWorld(OriginalWorld);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGSpawnerSelectorTest,
	"UE.MCP.PCG.Graph.PathFolderAndSpawnerSelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGSpawnerSelectorTest::RunTest(const FString& Parameters)
{
	using namespace PCGHandlerTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGTest") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);

	// create_pcg_graph: 'path' names the folder, as packagePath does.
	const FString Folder = FString(Root) + TEXT("Sub");
	auto CreateParams = MakeShared<FJsonObject>();
	CreateParams->SetStringField(TEXT("name"), TEXT("PCG_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	CreateParams->SetStringField(TEXT("path"), Folder);
	const TSharedPtr<FJsonObject> Created = Call(Registry, TEXT("create_pcg_graph"), CreateParams);
	if (!TestTrue(TEXT("create_pcg_graph succeeds"), Succeeded(Created))) return false;
	const FString GraphPath = Created->GetStringField(TEXT("path"));
	TestTrue(TEXT("the graph is created in the folder 'path' names"), GraphPath.StartsWith(Folder + TEXT("/")));

	auto AddParams = MakeShared<FJsonObject>();
	AddParams->SetStringField(TEXT("assetPath"), GraphPath);
	AddParams->SetStringField(TEXT("nodeType"), TEXT("PCGStaticMeshSpawnerSettings"));
	const TSharedPtr<FJsonObject> Added = Call(Registry, TEXT("add_pcg_node"), AddParams);
	if (!TestTrue(TEXT("add_pcg_node adds a spawner"), Succeeded(Added))) return false;
	const FString NodeName = Added->GetStringField(TEXT("nodeName"));

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!TestNotNull(TEXT("the graph loads"), Graph)) return false;
	UPCGNode* Node = nullptr;
	for (UPCGNode* Candidate : Graph->GetNodes())
	{
		if (Candidate && Candidate->GetName() == NodeName) Node = Candidate;
	}
	UPCGStaticMeshSpawnerSettings* Spawner = Node ? Cast<UPCGStaticMeshSpawnerSettings>(const_cast<UPCGSettings*>(Node->GetSettings())) : nullptr;
	if (!TestNotNull(TEXT("the spawner settings are reachable"), Spawner)) return false;

	auto WriteSettings = [&](const TCHAR* Key, const TSharedPtr<FJsonValue>& Value)
	{
		auto Settings = MakeShared<FJsonObject>();
		Settings->SetField(Key, Value);
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), GraphPath);
		Params->SetStringField(TEXT("nodeName"), NodeName);
		Params->SetObjectField(TEXT("settings"), Settings);
		return Call(Registry, TEXT("set_pcg_node_settings"), Params);
	};

	// A type change goes through SetMeshSelectorType, so the selector follows it.
	const TSharedPtr<FJsonObject> ByAttribute = WriteSettings(TEXT("MeshSelectorType"), MakeShared<FJsonValueString>(TEXT("/Script/PCG.PCGMeshSelectorByAttribute")));
	TestTrue(TEXT("writing MeshSelectorType succeeds"), Succeeded(ByAttribute));
	TestTrue(TEXT("the selector is recreated as the new type"),
		Spawner->MeshSelectorParameters && Spawner->MeshSelectorParameters->GetClass()->GetName() == TEXT("PCGMeshSelectorByAttribute"));

	// Type already weighted, selector stale and owned elsewhere: rewriting the
	// unchanged type still recreates an owned selector.
	Spawner->MeshSelectorType = UPCGMeshSelectorWeighted::StaticClass();
	UPCGMeshSelectorWeighted* Foreign = NewObject<UPCGMeshSelectorWeighted>(GetTransientPackage());
	Spawner->MeshSelectorParameters = Foreign;
	const TSharedPtr<FJsonObject> Repaired = WriteSettings(TEXT("MeshSelectorType"), MakeShared<FJsonValueString>(TEXT("/Script/PCG.PCGMeshSelectorWeighted")));
	TestTrue(TEXT("rewriting the unchanged type succeeds"), Succeeded(Repaired));
	TestTrue(TEXT("the stale selector is replaced"), Spawner->MeshSelectorParameters && Spawner->MeshSelectorParameters != Foreign);
	TestTrue(TEXT("the new selector is weighted and owned by the settings"),
		Cast<UPCGMeshSelectorWeighted>(Spawner->MeshSelectorParameters) && Spawner->MeshSelectorParameters->GetOuter() == Spawner);
	TestTrue(TEXT("the replacement is reported"), Repaired->HasField(TEXT("recreatedInstancedParameters")));

	// A path for the instanced selector is refused with nothing changed.
	UObject* OwnSelector = Spawner->MeshSelectorParameters;
	const TSharedPtr<FJsonObject> PathWrite = WriteSettings(TEXT("MeshSelectorParameters"), MakeShared<FJsonValueString>(Foreign->GetPathName()));
	TestFalse(TEXT("a path for the selector is refused"), Succeeded(PathWrite));
	TestEqual(TEXT("the refusal names its reason"), PathWrite->GetStringField(TEXT("reason")), FString(TEXT("instanced_reference_path")));
	TestTrue(TEXT("the selector is untouched"), Spawner->MeshSelectorParameters == OwnSelector);

	// set_static_mesh_spawner_meshes writes into the owned selector.
	auto Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("mesh"), TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto MeshParams = MakeShared<FJsonObject>();
	MeshParams->SetStringField(TEXT("assetPath"), GraphPath);
	MeshParams->SetStringField(TEXT("nodeName"), NodeName);
	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Add(MakeShared<FJsonValueObject>(Entry));
	MeshParams->SetArrayField(TEXT("entries"), Entries);
	const TSharedPtr<FJsonObject> Meshes = Call(Registry, TEXT("set_static_mesh_spawner_meshes"), MeshParams);
	TestTrue(TEXT("set_static_mesh_spawner_meshes succeeds and saves"), Succeeded(Meshes));
	bool bOwned = false;
	TestTrue(TEXT("it reports the selector is the settings' own"), Meshes->TryGetBoolField(TEXT("selectorOuterIsSettings"), bOwned) && bOwned);

	// Export leaves the instanced selector out; import refuses a path for it.
	auto ExportParams = MakeShared<FJsonObject>();
	ExportParams->SetStringField(TEXT("assetPath"), GraphPath);
	const TSharedPtr<FJsonObject> Exported = Call(Registry, TEXT("export_pcg_graph"), ExportParams);
	bool bOmitted = false;
	for (const TSharedPtr<FJsonValue>& NodeValue : Exported->GetArrayField(TEXT("nodes")))
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
		if (NodeObj->GetStringField(TEXT("name")) != NodeName) continue;
		TestFalse(TEXT("export omits the selector object"), NodeObj->GetObjectField(TEXT("settings"))->HasField(TEXT("MeshSelectorParameters")));
		const TArray<TSharedPtr<FJsonValue>>* Omitted = nullptr;
		bOmitted = NodeObj->TryGetArrayField(TEXT("omittedInstancedProperties"), Omitted) && Omitted
			&& Omitted->ContainsByPredicate([](const TSharedPtr<FJsonValue>& V) { return V->AsString() == TEXT("MeshSelectorParameters"); });
	}
	TestTrue(TEXT("export lists the omitted selector"), bOmitted);

	auto ImportSettings = MakeShared<FJsonObject>();
	ImportSettings->SetStringField(TEXT("MeshSelectorParameters"), OwnSelector->GetPathName());
	auto ImportNode = MakeShared<FJsonObject>();
	ImportNode->SetStringField(TEXT("name"), TEXT("Copy"));
	ImportNode->SetStringField(TEXT("class"), TEXT("PCGStaticMeshSpawnerSettings"));
	ImportNode->SetObjectField(TEXT("settings"), ImportSettings);
	auto ImportParams = MakeShared<FJsonObject>();
	ImportParams->SetStringField(TEXT("assetPath"), GraphPath);
	TArray<TSharedPtr<FJsonValue>> ImportNodes;
	ImportNodes.Add(MakeShared<FJsonValueObject>(ImportNode));
	ImportParams->SetArrayField(TEXT("nodes"), ImportNodes);
	const int32 NodesBefore = Graph->GetNodes().Num();
	const TSharedPtr<FJsonObject> Imported = Call(Registry, TEXT("import_pcg_graph"), ImportParams);
	TestFalse(TEXT("import refuses a path for the selector"), Succeeded(Imported));
	TestEqual(TEXT("and adds no node"), Graph->GetNodes().Num(), NodesBefore);

	// A refused mesh path must not replace a different selector or dirty the package.
	WriteSettings(TEXT("MeshSelectorType"), MakeShared<FJsonValueString>(TEXT("/Script/PCG.PCGMeshSelectorByAttribute")));
	UObject* BeforeBadMesh = Spawner->MeshSelectorParameters;
	Graph->GetOutermost()->SetDirtyFlag(false);
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);
	Entry->SetStringField(TEXT("mesh"), TEXT("/Engine/BasicShapes/MissingMesh.MissingMesh"));
	TestFalse(TEXT("invalid mesh is refused"), Succeeded(Call(Registry, TEXT("set_static_mesh_spawner_meshes"), MeshParams)));
	TestTrue(TEXT("invalid mesh preserves selector and type"), Spawner->MeshSelectorParameters == BeforeBadMesh
		&& Spawner->MeshSelectorType == BeforeBadMesh->GetClass());
	TestFalse(TEXT("invalid mesh does not dirty the graph"), Graph->GetOutermost()->IsDirty());

	// Type notifications must finish before nested writes, independent of JSON order.
	auto WeightedEntry = MakeShared<FJsonObject>();
	WeightedEntry->SetNumberField(TEXT("Weight"), 7);
	TArray<TSharedPtr<FJsonValue>> WeightedEntries{MakeShared<FJsonValueObject>(WeightedEntry)};
	for (bool bTypeFirst : {true, false})
	{
		WriteSettings(TEXT("MeshSelectorType"), MakeShared<FJsonValueString>(TEXT("/Script/PCG.PCGMeshSelectorByAttribute")));
		auto Batch = MakeShared<FJsonObject>();
		if (bTypeFirst) Batch->SetStringField(TEXT("MeshSelectorType"), TEXT("/Script/PCG.PCGMeshSelectorWeighted"));
		Batch->SetArrayField(TEXT("MeshSelectorParameters.MeshEntries"), WeightedEntries);
		if (!bTypeFirst) Batch->SetStringField(TEXT("MeshSelectorType"), TEXT("/Script/PCG.PCGMeshSelectorWeighted"));
		auto BatchParams = MakeShared<FJsonObject>();
		BatchParams->SetStringField(TEXT("assetPath"), GraphPath);
		BatchParams->SetStringField(TEXT("nodeName"), NodeName);
		BatchParams->SetObjectField(TEXT("settings"), Batch);
		for (int32 Repeat = 0; Repeat < 2; ++Repeat)
		{
			const auto BatchResult = Call(Registry, TEXT("set_pcg_node_settings"), BatchParams);
			TestTrue(TEXT("type plus entries succeeds, including same-type rewrite"), Succeeded(BatchResult));
			TestFalse(TEXT("rollback excludes entries owned by a replaced selector"), BatchResult->GetObjectField(TEXT("previousProperties"))->HasField(TEXT("MeshSelectorParameters.MeshEntries")));
			TestTrue(TEXT("the selector-content loss is disclosed"), BatchResult->GetBoolField(TEXT("rollbackLossy")));
			const auto* Weighted = Cast<UPCGMeshSelectorWeighted>(Spawner->MeshSelectorParameters);
			TestTrue(TEXT("entries survive all settings notifications"), Weighted && Weighted->MeshEntries.Num() == 1 && Weighted->MeshEntries[0].Weight == 7);
			if (Repeat == 0)
			{
				const auto Inverse = BatchResult->GetObjectField(TEXT("rollback"))->GetObjectField(TEXT("payload"));
				TestTrue(TEXT("type-changing batch rollback succeeds"), Succeeded(Call(Registry, TEXT("set_pcg_node_settings"), Inverse)));
				TestEqual(TEXT("rollback restores the original type"), Spawner->MeshSelectorParameters->GetClass()->GetName(), FString(TEXT("PCGMeshSelectorByAttribute")));
				Call(Registry, TEXT("set_pcg_node_settings"), BatchParams);
			}
		}

		ImportNode->SetObjectField(TEXT("settings"), Batch);
		const auto BatchImported = Call(Registry, TEXT("import_pcg_graph"), ImportParams);
		TestTrue(TEXT("import accepts type plus entries in either order"), Succeeded(BatchImported));
		const auto* ImportedSpawner = Cast<UPCGStaticMeshSpawnerSettings>(Graph->GetNodes().Last()->GetSettings());
		const auto* ImportedWeighted = ImportedSpawner ? Cast<UPCGMeshSelectorWeighted>(ImportedSpawner->MeshSelectorParameters) : nullptr;
		TestTrue(TEXT("imported entries survive notifications"), ImportedWeighted && ImportedWeighted->MeshEntries.Num() == 1 && ImportedWeighted->MeshEntries[0].Weight == 7);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGImportWarningsTest,
	"UE.MCP.PCG.Graph.ImportWarningsAndFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGImportWarningsTest::RunTest(const FString& Parameters)
{
	using namespace PCGHandlerTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGTest") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString PackageName = FString(Root) + TEXT("PG_Import_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*PackageName);
	UPCGGraph* Graph = NewObject<UPCGGraph>(Package, TEXT("PG_Import"), RF_Public | RF_Standalone | RF_Transactional);
	const FString ChildPackageName = PackageName + TEXT("_Child");
	UPCGGraph* Child = NewObject<UPCGGraph>(CreatePackage(*ChildPackageName), TEXT("Child"), RF_Public | RF_Standalone | RF_Transactional);
	Child->AddUserParameters({FPropertyBagPropertyDesc(TEXT("Preserved"), EPropertyBagPropertyType::Bool)});
	FString SaveError;
	if (!TestTrue(TEXT("child graph saves"), SaveAssetPackageChecked(Child, SaveError))) return false;
	UPCGSubgraphSettings* FromSettings = nullptr;
	UPCGAddTagSettings* ToSettings = nullptr;
	UPCGNode* From = Graph->AddNodeOfType(FromSettings);
	UPCGNode* To = Graph->AddNodeOfType(ToSettings);
	if (!TestTrue(TEXT("two nodes added"), From && To)) return false;
	FromSettings->SetSubgraph(Child);
	From->UpdateAfterSettingsChangeDuringCreation();
	auto OverrideParams = MakeShared<FJsonObject>();
	OverrideParams->SetStringField(TEXT("assetPath"), Graph->GetPathName());
	OverrideParams->SetStringField(TEXT("nodeName"), From->GetName());
	auto OverrideValues = MakeShared<FJsonObject>();
	OverrideValues->SetBoolField(TEXT("Preserved"), true);
	OverrideParams->SetObjectField(TEXT("parameters"), OverrideValues);
	if (!TestTrue(TEXT("subgraph override is set"), Succeeded(Call(Registry, TEXT("set_pcg_subgraph_parameters"), OverrideParams)))) return false;
	auto CheckSubgraph = [&]()
	{
		const UPCGSubgraphSettings* Restored = nullptr;
		for (const UPCGNode* Candidate : Graph->GetNodes())
		{
			if (const auto* Sub = Cast<UPCGSubgraphSettings>(Candidate->GetSettings())) Restored = Sub;
		}
		if (!TestTrue(TEXT("subgraph asset and owned instance survive"), Restored && Restored->SubgraphInstance
			&& Restored->SubgraphInstance->Graph == Child && Restored->SubgraphInstance->GetOuter() == Restored)) return;
		const auto* Bag = Restored->SubgraphInstance->GetUserParametersStruct();
		const auto Value = Bag->GetValueBool(TEXT("Preserved"));
		TestTrue(TEXT("subgraph parameter value survives"), Value.HasValue() && Value.GetValue());
		const auto* Desc = Bag->GetPropertyBagStruct()->FindPropertyDescByName(TEXT("Preserved"));
		TestTrue(TEXT("subgraph override flag survives"), Desc && Restored->SubgraphInstance->IsPropertyOverridden(Desc->CachedProperty));
	};
	Graph->AddEdge(From, PCGPinConstants::DefaultOutputLabel, To, PCGPinConstants::DefaultInputLabel);
	auto Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), Graph->GetPathName());
	const auto Exported = Call(Registry, TEXT("export_pcg_graph"), Params);
	Params->SetBoolField(TEXT("replace"), true);
	Params->SetArrayField(TEXT("nodes"), Exported->GetArrayField(TEXT("nodes")));
	Params->SetArrayField(TEXT("connections"), Exported->GetArrayField(TEXT("connections")));
	const auto Imported = Call(Registry, TEXT("import_pcg_graph"), Params);
	TestTrue(TEXT("replace round trip succeeds before removed names are collected"), Succeeded(Imported));
	// The engine may free a removed node's name before the re-add, so a collision
	// warning is possible but not guaranteed. Any warning must be only that.
	const TArray<TSharedPtr<FJsonValue>>* ImportWarnings = nullptr;
	if (Imported->TryGetArrayField(TEXT("warnings"), ImportWarnings))
	{
		for (const TSharedPtr<FJsonValue>& Warning : *ImportWarnings)
		{
			TestTrue(TEXT("only name collisions are warned about"), Warning->AsString().Contains(TEXT("name collision")));
		}
	}
	TestEqual(TEXT("both nodes landed"), Imported->GetNumberField(TEXT("nodesCreated")), 2.0);
	TestEqual(TEXT("the edge landed"), Imported->GetNumberField(TEXT("connectionsMade")), 1.0);
	CheckSubgraph();
	const auto Rollback = Imported->GetObjectField(TEXT("rollback"))->GetObjectField(TEXT("payload"));
	const auto RolledBack = Call(Registry, TEXT("import_pcg_graph"), Rollback);
	TestTrue(TEXT("the import's rollback also succeeds with name collisions"), Succeeded(RolledBack));
	TestEqual(TEXT("rollback restores the subgraph edge"), RolledBack->GetNumberField(TEXT("connectionsMade")), 1.0);
	CheckSubgraph();

	// Actual losses still fail, whether a node, a setting, or a connection.
	auto Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("name"), TEXT("FailureProbe"));
	Node->SetStringField(TEXT("class"), TEXT("ClassThatDoesNotExist"));
	Params->SetArrayField(TEXT("nodes"), {MakeShared<FJsonValueObject>(Node)});
	Params->RemoveField(TEXT("connections"));
	TestFalse(TEXT("missing node class fails"), Succeeded(Call(Registry, TEXT("import_pcg_graph"), Params)));
	Node->SetStringField(TEXT("class"), TEXT("PCGAddTagSettings"));
	auto BadSettings = MakeShared<FJsonObject>();
	BadSettings->SetBoolField(TEXT("PropertyThatDoesNotExist"), true);
	Node->SetObjectField(TEXT("settings"), BadSettings);
	TestFalse(TEXT("unapplied setting fails"), Succeeded(Call(Registry, TEXT("import_pcg_graph"), Params)));
	Node->RemoveField(TEXT("settings"));
	auto Edge = MakeShared<FJsonObject>();
	Edge->SetStringField(TEXT("from"), TEXT("FailureProbe"));
	Edge->SetStringField(TEXT("to"), TEXT("MissingTarget"));
	Params->SetArrayField(TEXT("connections"), {MakeShared<FJsonValueObject>(Edge)});
	TestFalse(TEXT("unapplied edge fails"), Succeeded(Call(Registry, TEXT("import_pcg_graph"), Params)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGOwnedSelectorFieldsTest,
	"UE.MCP.PCG.Graph.OwnedSelectorFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGOwnedSelectorFieldsTest::RunTest(const FString& Parameters)
{
	using namespace PCGHandlerTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGTest") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);

	auto CreateParams = MakeShared<FJsonObject>();
	CreateParams->SetStringField(TEXT("name"), TEXT("PCG_Fields_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	CreateParams->SetStringField(TEXT("packagePath"), FString(Root) + TEXT("Fields"));
	const TSharedPtr<FJsonObject> Created = Call(Registry, TEXT("create_pcg_graph"), CreateParams);
	if (!TestTrue(TEXT("create_pcg_graph succeeds"), Succeeded(Created))) return false;
	const FString GraphPath = Created->GetStringField(TEXT("path"));
	auto AddParams = MakeShared<FJsonObject>();
	AddParams->SetStringField(TEXT("assetPath"), GraphPath);
	AddParams->SetStringField(TEXT("nodeType"), TEXT("PCGStaticMeshSpawnerSettings"));
	const TSharedPtr<FJsonObject> Added = Call(Registry, TEXT("add_pcg_node"), AddParams);
	if (!TestTrue(TEXT("add_pcg_node adds a spawner"), Succeeded(Added))) return false;
	const FString NodeName = Added->GetStringField(TEXT("nodeName"));
	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	UPCGNode* Node = Graph ? MCPPCG::FindNode(Graph, NodeName) : nullptr;
	UPCGStaticMeshSpawnerSettings* Spawner = Node ? Cast<UPCGStaticMeshSpawnerSettings>(const_cast<UPCGSettings*>(Node->GetSettings())) : nullptr;
	if (!TestNotNull(TEXT("the spawner settings are reachable"), Spawner)) return false;

	auto Write = [&](const TSharedPtr<FJsonObject>& Settings)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), GraphPath);
		Params->SetStringField(TEXT("nodeName"), NodeName);
		Params->SetObjectField(TEXT("settings"), Settings);
		return Call(Registry, TEXT("set_pcg_node_settings"), Params);
	};

	// A field object for the selector, with the type that selects its class, in one call.
	auto Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("AttributeName"), TEXT("Mesh"));
	auto Batch = MakeShared<FJsonObject>();
	Batch->SetObjectField(TEXT("MeshSelectorParameters"), Fields);
	Batch->SetStringField(TEXT("MeshSelectorType"), TEXT("/Script/PCG.PCGMeshSelectorByAttribute"));
	TestTrue(TEXT("a field object for the owned selector is written"), Succeeded(Write(Batch)));
	const UPCGMeshSelectorByAttribute* ByAttribute = Cast<UPCGMeshSelectorByAttribute>(Spawner->MeshSelectorParameters);
	TestTrue(TEXT("the selector is by-attribute, owned, and holds the field"), ByAttribute && ByAttribute->GetOuter() == Spawner
		&& ByAttribute->AttributeName == FName(TEXT("Mesh")));

	// A dotted key into the same owned selector.
	auto Dotted = MakeShared<FJsonObject>();
	Dotted->SetStringField(TEXT("MeshSelectorParameters.AttributeName"), TEXT("Other"));
	TestTrue(TEXT("a dotted key into the owned selector is written"), Succeeded(Write(Dotted)));
	TestTrue(TEXT("the dotted write landed"), ByAttribute && ByAttribute->AttributeName == FName(TEXT("Other")));

	// A field the current class lacks names the property that selects the class.
	Spawner->SetMeshSelectorType(UPCGMeshSelectorWeighted::StaticClass());
	const TSharedPtr<FJsonObject> Missing = Write(Dotted);
	TestFalse(TEXT("a field the weighted selector lacks is refused"), Succeeded(Missing));
	TestTrue(TEXT("the refusal names MeshSelectorType"), Missing->GetStringField(TEXT("error")).Contains(TEXT("MeshSelectorType")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGPinLabelsTest,
	"UE.MCP.PCG.Graph.PinLabelsAndPinErrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGPinLabelsTest::RunTest(const FString& Parameters)
{
	using namespace PCGHandlerTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGTest") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);

	auto CreateParams = MakeShared<FJsonObject>();
	CreateParams->SetStringField(TEXT("name"), TEXT("PCG_Pins_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	CreateParams->SetStringField(TEXT("packagePath"), FString(Root) + TEXT("Pins"));
	const TSharedPtr<FJsonObject> Created = Call(Registry, TEXT("create_pcg_graph"), CreateParams);
	if (!TestTrue(TEXT("create_pcg_graph succeeds"), Succeeded(Created))) return false;
	const FString GraphPath = Created->GetStringField(TEXT("path"));

	auto AddParams = MakeShared<FJsonObject>();
	AddParams->SetStringField(TEXT("assetPath"), GraphPath);
	AddParams->SetStringField(TEXT("nodeType"), TEXT("PCGAddTagSettings"));
	const TSharedPtr<FJsonObject> Added = Call(Registry, TEXT("add_pcg_node"), AddParams);
	if (!TestTrue(TEXT("add_pcg_node adds a node"), Succeeded(Added))) return false;
	const FString NodeName = Added->GetStringField(TEXT("nodeName"));

	auto GraphParams = MakeShared<FJsonObject>();
	GraphParams->SetStringField(TEXT("assetPath"), GraphPath);
	const TSharedPtr<FJsonObject> Read = Call(Registry, TEXT("read_pcg_graph"), GraphParams);
	TArray<FString> InputLabels;
	for (const TSharedPtr<FJsonValue>& NodeValue : Read->GetArrayField(TEXT("nodes")))
	{
		const TSharedPtr<FJsonObject> NodeObj = NodeValue->AsObject();
		if (NodeObj->GetStringField(TEXT("name")) != NodeName) continue;
		TestTrue(TEXT("read_pcg_graph lists output pins"), NodeObj->GetArrayField(TEXT("outputPins")).Num() > 0);
		for (const TSharedPtr<FJsonValue>& Label : NodeObj->GetArrayField(TEXT("inputPins"))) InputLabels.Add(Label->AsString());
	}
	if (!TestTrue(TEXT("read_pcg_graph lists the node's input pins"), InputLabels.Num() > 0)) return false;

	auto Connect = MakeShared<FJsonObject>();
	Connect->SetStringField(TEXT("assetPath"), GraphPath);
	Connect->SetStringField(TEXT("sourceNode"), TEXT("DefaultInputNode"));
	Connect->SetStringField(TEXT("targetNode"), NodeName);
	Connect->SetStringField(TEXT("targetPin"), TEXT("NoSuchPin"));
	const TSharedPtr<FJsonObject> Refused = Call(Registry, TEXT("connect_pcg_nodes"), Connect);
	TestFalse(TEXT("an unknown target pin is refused"), Succeeded(Refused));
	TestEqual(TEXT("the refusal names its reason"), Refused->GetStringField(TEXT("reason")), FString(TEXT("pin_not_found")));
	TArray<FString> Valid;
	for (const TSharedPtr<FJsonValue>& Label : Refused->GetArrayField(TEXT("validPins"))) Valid.Add(Label->AsString());
	TestEqual(TEXT("validPins are the node's input labels"), Valid, InputLabels);

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	TestTrue(TEXT("nothing was wired"), Graph && Graph->GetInputNode() && Graph->GetInputNode()->GetOutputPins().Num() > 0
		&& Graph->GetInputNode()->GetOutputPins()[0]->Edges.Num() == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGSpawnedOutputTest,
	"UE.MCP.PCG.Volume.SpawnedComponentsInOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGSpawnedOutputTest::RunTest(const FString& Parameters)
{
	using namespace PCGHandlerTests;
	FWorldScope Scope;
	if (!TestNotNull(TEXT("disposable editor world exists"), Scope.World)) return false;
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);

	auto Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("label"), TEXT("UEMCP_PCGSpawned"));
	if (!TestTrue(TEXT("add_pcg_volume succeeds"), Succeeded(Call(Registry, TEXT("add_pcg_volume"), Params)))) return false;
	TSharedPtr<FJsonValue> LookupError;
	APCGVolume* Volume = Cast<APCGVolume>(MCPResolveActorToken(Scope.World, TEXT("UEMCP_PCGSpawned"), LookupError));
	if (!TestNotNull(TEXT("the volume is in the disposable world"), Volume)) return false;

	// Two managed ISMs, as a spawner with two meshes leaves them: one with three instances, one with two.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	TArray<UActorComponent*> Spawned;
	for (int32 Count : { 3, 2 })
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Volume);
		ISM->ComponentTags.Add(FName(TEXT("PCG Generated Component")));
		ISM->SetStaticMesh(Cube);
		for (int32 i = 0; i < Count; ++i) ISM->AddInstance(FTransform(FVector(i * 100.0, 0.0, 0.0)));
		Volume->AddInstanceComponent(ISM);
		Spawned.Add(ISM);
	}
	Volume->PCGComponent->AddComponentsToManagedResources(Spawned);

	auto ReadParams = MakeShared<FJsonObject>();
	ReadParams->SetStringField(TEXT("actorLabel"), TEXT("UEMCP_PCGSpawned"));
	const TSharedPtr<FJsonObject> Out = Call(Registry, TEXT("read_pcg_generated_output"), ReadParams);
	if (!TestTrue(TEXT("read_pcg_generated_output succeeds"), Succeeded(Out))) return false;
	const TSharedPtr<FJsonObject> Row = Out->GetArrayField(TEXT("components"))[0]->AsObject();
	TestEqual(TEXT("both spawned components are listed"), Row->GetArrayField(TEXT("spawnedComponents")).Num(), 2);
	TestEqual(TEXT("the instance counts add up"), Row->GetNumberField(TEXT("spawnedInstanceCount")), 5.0);
	TestEqual(TEXT("the total is reported at the top"), Out->GetNumberField(TEXT("spawnedInstanceCount")), 5.0);
	bool bMesh = false;
	for (const TSharedPtr<FJsonValue>& Entry : Row->GetArrayField(TEXT("spawnedComponents")))
	{
		bMesh |= Entry->AsObject()->GetStringField(TEXT("mesh")) == Cube->GetPathName();
	}
	TestTrue(TEXT("each spawned ISM names its mesh"), bMesh);
	TestTrue(TEXT("an empty data list beside spawned content carries a note"), Row->HasField(TEXT("note")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGComponentParametersTest,
	"UE.MCP.PCG.Component.GraphAndParameterOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGComponentParametersTest::RunTest(const FString& Parameters)
{
	using namespace PCGHandlerTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGTest") };
	FWorldScope Scope;
	if (!TestNotNull(TEXT("disposable editor world exists"), Scope.World)) return false;
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);

	// A graph declaring one parameter.
	auto CreateParams = MakeShared<FJsonObject>();
	CreateParams->SetStringField(TEXT("name"), TEXT("PCG_CompParams_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	CreateParams->SetStringField(TEXT("packagePath"), FString(Root) + TEXT("Component"));
	const TSharedPtr<FJsonObject> Created = Call(Registry, TEXT("create_pcg_graph"), CreateParams);
	if (!TestTrue(TEXT("create_pcg_graph succeeds"), Succeeded(Created))) return false;
	const FString GraphPath = Created->GetStringField(TEXT("path"));
	auto Spacing = MakeShared<FJsonObject>();
	Spacing->SetStringField(TEXT("name"), TEXT("Spacing"));
	Spacing->SetStringField(TEXT("type"), TEXT("double"));
	Spacing->SetNumberField(TEXT("default"), 100.0);
	auto GraphParams = MakeShared<FJsonObject>();
	GraphParams->SetStringField(TEXT("assetPath"), GraphPath);
	GraphParams->SetArrayField(TEXT("graphParameters"), TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueObject>(Spacing) });
	if (!TestTrue(TEXT("set_pcg_graph_parameters succeeds"), Succeeded(Call(Registry, TEXT("set_pcg_graph_parameters"), GraphParams)))) return false;
	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!TestNotNull(TEXT("the graph loads"), Graph)) return false;

	auto SpacingOf = [](const TSharedPtr<FJsonObject>& Result, double& OutValue, bool& bOutOverridden)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Result->TryGetArrayField(TEXT("parameters"), Entries) || !Entries) return false;
		for (const TSharedPtr<FJsonValue>& Entry : *Entries)
		{
			const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
			if (Obj->GetStringField(TEXT("name")) != TEXT("Spacing")) continue;
			OutValue = Obj->GetNumberField(TEXT("value"));
			bOutOverridden = Obj->GetBoolField(TEXT("overridden"));
			return true;
		}
		return false;
	};

	// A placed volume: its native component is not rebuilt by a construction script.
	auto VolumeParams = MakeShared<FJsonObject>();
	VolumeParams->SetStringField(TEXT("label"), TEXT("UEMCP_PCGCompParams"));
	if (!TestTrue(TEXT("add_pcg_volume succeeds"), Succeeded(Call(Registry, TEXT("add_pcg_volume"), VolumeParams)))) return false;
	TSharedPtr<FJsonValue> LookupError;
	APCGVolume* Volume = Cast<APCGVolume>(MCPResolveActorToken(Scope.World, TEXT("UEMCP_PCGCompParams"), LookupError));
	if (!TestNotNull(TEXT("the volume is in the disposable world"), Volume)) return false;

	auto AssignOnActor = MakeShared<FJsonObject>();
	AssignOnActor->SetStringField(TEXT("actorLabel"), TEXT("UEMCP_PCGCompParams"));
	AssignOnActor->SetStringField(TEXT("graphPath"), GraphPath);
	const TSharedPtr<FJsonObject> Assigned = Call(Registry, TEXT("set_pcg_component_graph"), AssignOnActor);
	TestTrue(TEXT("set_pcg_component_graph succeeds on a placed volume"), Succeeded(Assigned));
	TestTrue(TEXT("SetGraph assigned the graph"), Volume->PCGComponent->GetGraph() == Graph);
	TestFalse(TEXT("a native component is not reported as Blueprint-owned"), Assigned->GetBoolField(TEXT("blueprintOwned")));

	auto Overrides = MakeShared<FJsonObject>();
	Overrides->SetNumberField(TEXT("Spacing"), 3.0);
	auto OverrideOnActor = MakeShared<FJsonObject>();
	OverrideOnActor->SetStringField(TEXT("actorLabel"), TEXT("UEMCP_PCGCompParams"));
	OverrideOnActor->SetObjectField(TEXT("overrides"), Overrides);
	const TSharedPtr<FJsonObject> Overridden = Call(Registry, TEXT("set_pcg_component_parameters"), OverrideOnActor);
	TestTrue(TEXT("set_pcg_component_parameters succeeds on a placed volume"), Succeeded(Overridden));
	double Value = 0.0;
	bool bOverridden = false;
	TestTrue(TEXT("the override reads back"), SpacingOf(Overridden, Value, bOverridden) && bOverridden && FMath::IsNearlyEqual(Value, 3.0));

	auto ReadOnActor = MakeShared<FJsonObject>();
	ReadOnActor->SetStringField(TEXT("actorLabel"), TEXT("UEMCP_PCGCompParams"));
	const TSharedPtr<FJsonObject> ReadBack = Call(Registry, TEXT("get_pcg_component_parameters"), ReadOnActor);
	TestTrue(TEXT("get_pcg_component_parameters reports the override"), SpacingOf(ReadBack, Value, bOverridden) && bOverridden && FMath::IsNearlyEqual(Value, 3.0));

	// A Blueprint with a PCG component in its construction script: the write lands on the template.
	const FString BlueprintName = TEXT("BP_PCGHolder_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*(FString(Root) + TEXT("Component/") + BlueprintName));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, FName(*BlueprintName), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint is created"), Blueprint)) return false;
	USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(UPCGComponent::StaticClass(), TEXT("PCG"));
	Blueprint->SimpleConstructionScript->AddNode(Node);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	auto AssignOnBlueprint = MakeShared<FJsonObject>();
	AssignOnBlueprint->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
	AssignOnBlueprint->SetStringField(TEXT("graphPath"), GraphPath);
	TestTrue(TEXT("set_pcg_component_graph succeeds on a Blueprint"), Succeeded(Call(Registry, TEXT("set_pcg_component_graph"), AssignOnBlueprint)));
	const UPCGComponent* Template = Cast<UPCGComponent>(Node->ComponentTemplate);
	TestTrue(TEXT("the template holds the graph"), Template && Template->GetGraph() == Graph);

	auto OverrideOnBlueprint = MakeShared<FJsonObject>();
	OverrideOnBlueprint->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
	Overrides->SetNumberField(TEXT("Spacing"), 42.0);
	OverrideOnBlueprint->SetObjectField(TEXT("overrides"), Overrides);
	TestTrue(TEXT("set_pcg_component_parameters succeeds on a Blueprint"), Succeeded(Call(Registry, TEXT("set_pcg_component_parameters"), OverrideOnBlueprint)));
	Template = Cast<UPCGComponent>(Node->ComponentTemplate);
	const UPCGGraphInstance* TemplateInstance = Template ? Template->GetGraphInstance() : nullptr;
	const FInstancedPropertyBag* Bag = TemplateInstance ? TemplateInstance->GetUserParametersStruct() : nullptr;
	const FPropertyBagPropertyDesc* Desc = Bag && Bag->GetPropertyBagStruct() ? Bag->GetPropertyBagStruct()->FindPropertyDescByName(TEXT("Spacing")) : nullptr;
	TestTrue(TEXT("the template's graph instance overrides Spacing"), Desc && TemplateInstance->IsPropertyOverridden(Desc->CachedProperty));
	TestTrue(TEXT("with the written value"), Bag && FMath::IsNearlyEqual(Bag->GetValueDouble(TEXT("Spacing")).GetValue(), 42.0));

	// A placed instance of the Blueprint: its component is reported as built by the construction script.
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags = RF_Transient;
	AActor* Placed = Scope.World->SpawnActor<AActor>(Blueprint->GeneratedClass, FTransform::Identity, Spawn);
	if (TestNotNull(TEXT("the Blueprint spawns"), Placed))
	{
		auto ReadPlaced = MakeShared<FJsonObject>();
		ReadPlaced->SetStringField(TEXT("actorPath"), Placed->GetPathName());
		const TSharedPtr<FJsonObject> PlacedRead = Call(Registry, TEXT("get_pcg_component_parameters"), ReadPlaced);
		TestTrue(TEXT("the placed component is Blueprint-owned"), Succeeded(PlacedRead) && PlacedRead->GetBoolField(TEXT("blueprintOwned")));
		TestTrue(TEXT("and carries the reconstruction warning"), PlacedRead->HasField(TEXT("warnings")));
		TestTrue(TEXT("and spawned with the template's override"), SpacingOf(PlacedRead, Value, bOverridden) && bOverridden && FMath::IsNearlyEqual(Value, 42.0));
		Placed->Destroy();
	}
	Blueprint->ClearFlags(RF_Public | RF_Standalone);
	Blueprint->MarkAsGarbage();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGVolumeAndRunTest,
	"UE.MCP.PCG.Volume.TransformAndRunRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGVolumeAndRunTest::RunTest(const FString& Parameters)
{
	using namespace PCGHandlerTests;
	FWorldScope Scope;
	if (!TestNotNull(TEXT("disposable editor world exists"), Scope.World)) return false;
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);

	const FTransform Requested(FRotator(10.0, 20.0, 30.0), FVector(100.0, -200.0, 300.0), FVector(2.0, 3.0, 0.5));
	auto Transform = MakeShared<FJsonObject>();
	Transform->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Requested.GetLocation()));
	Transform->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Requested.Rotator()));
	Transform->SetObjectField(TEXT("scale3D"), MCPVec3ToJsonObject(Requested.GetScale3D()));
	auto Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("label"), TEXT("UEMCP_PCGVolume"));
	Params->SetObjectField(TEXT("transform"), Transform);
	Params->SetObjectField(TEXT("extent"), MCPVec3ToJsonObject(FVector(50.0)));
	const TSharedPtr<FJsonObject> Placed = Call(Registry, TEXT("add_pcg_volume"), Params);
	if (!TestTrue(TEXT("add_pcg_volume succeeds"), Succeeded(Placed))) return false;

	TSharedPtr<FJsonValue> LookupError;
	AActor* Volume = MCPResolveActorToken(Scope.World, TEXT("UEMCP_PCGVolume"), LookupError);
	if (!TestNotNull(TEXT("the volume is in the disposable world"), Volume)) return false;
	TestTrue(TEXT("location is honoured"), Volume->GetActorLocation().Equals(Requested.GetLocation(), 0.01));
	TestTrue(TEXT("rotation is honoured"), Volume->GetActorQuat().Equals(Requested.GetRotation(), 0.0001));
	TestTrue(TEXT("scale3D is honoured"), Volume->GetActorScale3D().Equals(Requested.GetScale3D(), 0.0001));
	FVector ScaledExtent;
	TestTrue(TEXT("the scaled extent is reported"), ReadVec3Fields(Placed->GetObjectField(TEXT("scaledExtent")), ScaledExtent)
		&& ScaledExtent.Equals(FVector(100.0, 150.0, 25.0), 0.01));

	// A volume with no graph: Generate schedules nothing, so the run settles at once.
	auto RunParams = MakeShared<FJsonObject>();
	RunParams->SetStringField(TEXT("actorLabel"), TEXT("UEMCP_PCGVolume"));
	const TSharedPtr<FJsonObject> Run = Call(Registry, TEXT("execute_pcg_graph"), RunParams);
	if (!TestTrue(TEXT("execute_pcg_graph succeeds"), Succeeded(Run))) return false;
	const double RunId = Run->GetNumberField(TEXT("runId"));
	TestTrue(TEXT("a run id is returned"), RunId > 0);
	TestEqual(TEXT("nothing to generate settles as not_started"), Run->GetObjectField(TEXT("lastRun"))->GetStringField(TEXT("state")), FString(TEXT("not_started")));

	const TSharedPtr<FJsonObject> Details = Call(Registry, TEXT("get_pcg_component_details"), RunParams);
	if (!TestTrue(TEXT("get_pcg_component_details succeeds"), Succeeded(Details))) return false;
	const TSharedPtr<FJsonObject> Comp = Details->GetArrayField(TEXT("components"))[0]->AsObject();
	TestFalse(TEXT("the component is not generating"), Comp->GetBoolField(TEXT("isGenerating")));
	TestTrue(TEXT("completion fields are present"), Comp->HasField(TEXT("isCleaningUp")) && Comp->HasField(TEXT("generated")) && Comp->HasField(TEXT("partitioned")));
	TestEqual(TEXT("details carry the same run"), Comp->GetObjectField(TEXT("lastRun"))->GetNumberField(TEXT("id")), RunId);
	TestTrue(TEXT("a not_started run says whether the component already holds output"),
		Run->GetObjectField(TEXT("lastRun"))->HasField(TEXT("componentGenerated")));

	// Generate deferred its start: a generation finishing shortly after the run settled is that run.
	UPCGComponent* DeferredComponent = CastChecked<APCGVolume>(Volume)->PCGComponent;
	DeferredComponent->OnPCGGraphStartGeneratingDelegate.Broadcast(DeferredComponent);
	const TSharedPtr<FJsonObject> Started = Call(Registry, TEXT("get_pcg_generation_state"), RunParams);
	TestEqual(TEXT("a deferred start moves the run back to generating"),
		Started->GetObjectField(TEXT("lastRun"))->GetStringField(TEXT("state")), FString(TEXT("generating")));
	DeferredComponent->OnPCGGraphGeneratedDelegate.Broadcast(DeferredComponent);
	const TSharedPtr<FJsonObject> Finished = Call(Registry, TEXT("get_pcg_generation_state"), RunParams);
	const TSharedPtr<FJsonObject> FinishedRun = Finished->GetObjectField(TEXT("lastRun"));
	TestEqual(TEXT("the deferred generation is reported as the run's outcome"), FinishedRun->GetStringField(TEXT("state")), FString(TEXT("generated")));
	TestEqual(TEXT("it is the same run"), FinishedRun->GetNumberField(TEXT("id")), RunId);
	bool bDeferred = false;
	TestTrue(TEXT("the run is marked deferredStart"), FinishedRun->TryGetBoolField(TEXT("deferredStart"), bDeferred) && bDeferred);

	// Schedule work without ticking the world: normal execute refuses a second run,
	// while force_regenerate remains a recovery path and supplies a fresh run id.
	UPCGComponent* PCGComponent = CastChecked<APCGVolume>(Volume)->PCGComponent;
	PCGComponent->SetGraph(NewObject<UPCGGraph>(GetTransientPackage()));
	const auto Pending = Call(Registry, TEXT("execute_pcg_graph"), RunParams);
	if (!TestTrue(TEXT("generation is pending before the world ticks"), PCGComponent->IsGenerating())) return false;
	TestFalse(TEXT("ordinary execute refuses while generating"), Succeeded(Call(Registry, TEXT("execute_pcg_graph"), RunParams)));
	const auto Forced = Call(Registry, TEXT("force_regenerate_pcg"), RunParams);
	TestTrue(TEXT("force regeneration accepts a busy component"), Succeeded(Forced));
	TestTrue(TEXT("force regeneration starts a newer run"), Forced->GetNumberField(TEXT("runId")) > Pending->GetNumberField(TEXT("runId")));
	PCGComponent->CancelGeneration();

	return true;
}

#endif
