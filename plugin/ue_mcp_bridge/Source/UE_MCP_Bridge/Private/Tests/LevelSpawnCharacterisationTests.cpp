// The response of the Level spawns and add_component_to_actor, recorded before
// the F6 port and held to the contract after it (plans/domain-design.md 0 and
// 8). Every actor is labelled UEMCPCharSpawn* in the editor world and removed
// again. The fixture is tests/golden/cpp/level-spawn-responses.json; a run
// without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Tests/MCPCharacterisationRun.h"

class FMCPLevelSpawnCharTestBase : public FAutomationTestBase
{
public:
	FMCPLevelSpawnCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogActor"));
		Categories.Add(TEXT("LogSpawn"));
		Categories.Add(TEXT("LogUObjectGlobals"));
		Categories.Add(TEXT("LogUE_MCP"));
		return Categories;
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPLevelSpawnCharacterisationTest, FMCPLevelSpawnCharTestBase,
	"UE.MCP.Level.Characterisation.Spawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPLevelSpawnCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPCharacterisation;

	UWorld* World = GetEditorWorld();
	if (!TestNotNull(TEXT("editor world"), World)) return false;

	FScriptedRun Run;
	FLevelHandlers::RegisterHandlers(Run.Registry);
	Run.Recording.Normaliser.Numbered({ TEXT("actorName"), TEXT("actorPath"), TEXT("path"), TEXT("objectPath") });
	Run.Recording.Normaliser.Replace(World->GetOutermost()->GetName(), TEXT("<map>"));

	auto RemoveSpawned = [World]()
	{
		TArray<AActor*> Doomed;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel().StartsWith(TEXT("UEMCPCharSpawn"))) Doomed.Add(*It);
		}
		for (AActor* Actor : Doomed) World->DestroyActor(Actor);
	};
	RemoveSpawned();

	auto Labelled = [](const TCHAR* Label) { return FArgs().S(TEXT("label"), Label); };

	// spawn_light: each type, the rerun and the refusals.
	for (const TCHAR* Type : { TEXT("point"), TEXT("spot"), TEXT("directional"), TEXT("rect"), TEXT("sky") })
	{
		const FString Label = FString(TEXT("UEMCPCharSpawnLight_")) + Type;
		Run.Step(*(FString(TEXT("spawn_light_")) + Type), TEXT("spawn_light"),
			Labelled(*Label).S(TEXT("lightType"), Type).V(TEXT("location"), 1, 2, 3).N(TEXT("intensity"), 100));
	}
	Run.Step(TEXT("spawn_light_existed"), TEXT("spawn_light"), Labelled(TEXT("UEMCPCharSpawnLight_point")).S(TEXT("lightType"), TEXT("point")));
	Run.Step(TEXT("spawn_light_conflict_error"), TEXT("spawn_light"), Labelled(TEXT("UEMCPCharSpawnLight_point")).S(TEXT("lightType"), TEXT("point")).S(TEXT("onConflict"), TEXT("error")));
	Run.Step(TEXT("spawn_light_bad_type"), TEXT("spawn_light"), Labelled(TEXT("UEMCPCharSpawnLightBad")).S(TEXT("lightType"), TEXT("laser")));

	// spawn_volume.
	for (const TCHAR* Type : { TEXT("trigger"), TEXT("blocking"), TEXT("postprocess"), TEXT("physics") })
	{
		const FString Label = FString(TEXT("UEMCPCharSpawnVolume_")) + Type;
		Run.Step(*(FString(TEXT("spawn_volume_")) + Type), TEXT("spawn_volume"),
			Labelled(*Label).S(TEXT("volumeType"), Type).V(TEXT("location"), 0, 0, 100).V(TEXT("extent"), 50, 50, 50));
	}
	Run.Step(TEXT("spawn_volume_existed"), TEXT("spawn_volume"), Labelled(TEXT("UEMCPCharSpawnVolume_trigger")).S(TEXT("volumeType"), TEXT("trigger")));
	Run.Step(TEXT("spawn_volume_bad_type"), TEXT("spawn_volume"), Labelled(TEXT("UEMCPCharSpawnVolumeBad")).S(TEXT("volumeType"), TEXT("NoSuchVolumeAnywhere")));
	Run.Step(TEXT("spawn_volume_missing_graph"), TEXT("spawn_volume"), Labelled(TEXT("UEMCPCharSpawnVolumePcg")).S(TEXT("volumeType"), TEXT("pcg")).S(TEXT("graphPath"), TEXT("/Game/UEMCPNoSuchGraph")));

	// place_actor and internal_spawn_actor.
	Run.Step(TEXT("place_actor_01_created"), TEXT("place_actor"), Labelled(TEXT("UEMCPCharSpawnPlaced")).S(TEXT("actorClass"), TEXT("StaticMeshActor"))
		.S(TEXT("staticMesh"), TEXT("/Engine/BasicShapes/Cube")).V(TEXT("location"), 100, 0, 0).V(TEXT("scale"), 2, 2, 2));
	Run.Step(TEXT("place_actor_02_existed"), TEXT("place_actor"), Labelled(TEXT("UEMCPCharSpawnPlaced")).S(TEXT("actorClass"), TEXT("StaticMeshActor")));
	Run.Step(TEXT("place_actor_03_conflict_error"), TEXT("place_actor"), Labelled(TEXT("UEMCPCharSpawnPlaced")).S(TEXT("actorClass"), TEXT("StaticMeshActor")).S(TEXT("onConflict"), TEXT("error")));
	Run.Step(TEXT("place_actor_04_bad_class"), TEXT("place_actor"), Labelled(TEXT("UEMCPCharSpawnPlacedBad")).S(TEXT("actorClass"), TEXT("NoSuchActorClassAnywhere")));
	Run.Step(TEXT("place_actor_05_volume"), TEXT("place_actor"), Labelled(TEXT("UEMCPCharSpawnPlacedVolume")).S(TEXT("actorClass"), TEXT("TriggerVolume")));
	Run.Step(TEXT("internal_spawn_actor_01_created"), TEXT("internal_spawn_actor"), Labelled(TEXT("UEMCPCharSpawnInternal")).S(TEXT("actorClass"), TEXT("Actor"))
		.S(TEXT("folderPath"), TEXT("UEMCPChar")).V(TEXT("location"), 0, 100, 0));
	Run.Step(TEXT("internal_spawn_actor_02_again"), TEXT("internal_spawn_actor"), Labelled(TEXT("UEMCPCharSpawnInternal")).S(TEXT("actorClass"), TEXT("PointLight")));
	Run.Step(TEXT("internal_spawn_actor_03_not_actor"), TEXT("internal_spawn_actor"), Labelled(TEXT("UEMCPCharSpawnInternalBad")).S(TEXT("actorClass"), TEXT("StaticMeshComponent")));

	// spawn_skeletal_mesh_actor.
	Run.Step(TEXT("spawn_skeletal_mesh_actor_01_created"), TEXT("spawn_skeletal_mesh_actor"), Labelled(TEXT("UEMCPCharSpawnSkel"))
		.S(TEXT("skeletalMesh"), TEXT("/Engine/EngineMeshes/SkeletalCube")).V(TEXT("location"), 0, 0, 50));
	Run.Step(TEXT("spawn_skeletal_mesh_actor_02_existed"), TEXT("spawn_skeletal_mesh_actor"), Labelled(TEXT("UEMCPCharSpawnSkel")).S(TEXT("skeletalMesh"), TEXT("/Engine/EngineMeshes/SkeletalCube")));
	Run.Step(TEXT("spawn_skeletal_mesh_actor_03_bad_mesh"), TEXT("spawn_skeletal_mesh_actor"), Labelled(TEXT("UEMCPCharSpawnSkelBad")).S(TEXT("skeletalMesh"), TEXT("/Game/UEMCPNoSuchMesh")));

	// add_component_to_actor, on the placed actor.
	auto OnPlaced = []() { return FArgs().S(TEXT("actorLabel"), TEXT("UEMCPCharSpawnPlaced")); };
	Run.Step(TEXT("add_component_to_actor_01_added"), TEXT("add_component_to_actor"), OnPlaced().S(TEXT("componentClass"), TEXT("PointLightComponent")).S(TEXT("componentName"), TEXT("CharLight")));
	Run.Step(TEXT("add_component_to_actor_02_existed"), TEXT("add_component_to_actor"), OnPlaced().S(TEXT("componentClass"), TEXT("PointLightComponent")).S(TEXT("componentName"), TEXT("CharLight")));
	Run.Step(TEXT("add_component_to_actor_03_conflict_error"), TEXT("add_component_to_actor"), OnPlaced().S(TEXT("componentClass"), TEXT("PointLightComponent")).S(TEXT("componentName"), TEXT("CharLight")).S(TEXT("onConflict"), TEXT("error")));
	Run.Step(TEXT("add_component_to_actor_04_bad_class"), TEXT("add_component_to_actor"), OnPlaced().S(TEXT("componentClass"), TEXT("NoSuchComponentAnywhere")).S(TEXT("componentName"), TEXT("Bad")));
	Run.Step(TEXT("add_component_to_actor_05_not_component"), TEXT("add_component_to_actor"), OnPlaced().S(TEXT("componentClass"), TEXT("Actor")).S(TEXT("componentName"), TEXT("Bad")));
	Run.Step(TEXT("add_component_to_actor_06_missing_actor"), TEXT("add_component_to_actor"), FArgs().S(TEXT("actorLabel"), TEXT("UEMCPCharSpawnNope")).S(TEXT("componentClass"), TEXT("PointLightComponent")).S(TEXT("componentName"), TEXT("X")));

	RemoveSpawned();
	return Run.Finish(*this, TEXT("level-spawn-responses"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
