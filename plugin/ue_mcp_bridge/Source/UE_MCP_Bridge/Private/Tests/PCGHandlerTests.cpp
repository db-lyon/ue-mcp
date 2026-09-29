// PCG handlers through the registry: graph folder, spawner selector ownership,
// volume transform and the run record. Graphs live on a private mount and
// actors in a disposable world, never the open project or map.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/PCG/PCGHandlers.h"
#include "Tests/MCPScopedTestMount.h"
#include "Editor.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Engine/World.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGVolume.h"
#include "UObject/Package.h"

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
	return true;
}

#endif
