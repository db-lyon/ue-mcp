// The response of every Gameplay asset creator and of spawn_nav_modifier_volume,
// recorded before the F4 and F6 port and held to the contract after it
// (plans/domain-design.md 0 and 8): created, rerun (existed), onConflict=error
// and the refusals a caller meets most. The fixture is
// tests/golden/cpp/gameplay-create-responses.json; a run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Handlers/Gameplay/GameplayHandlers.h"
#include "Tests/MCPCharacterisationRun.h"
#include "Tests/MCPScopedTestMount.h"

class FMCPGameplayCreateCharTestBase : public FAutomationTestBase
{
public:
	FMCPGameplayCreateCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogBlueprint"));
		Categories.Add(TEXT("LogK2Compiler"));
		Categories.Add(TEXT("LogStateTree"));
		Categories.Add(TEXT("LogUObjectGlobals"));
		Categories.Add(TEXT("LogLinker"));
		Categories.Add(TEXT("LogUE_MCP"));
		return Categories;
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPGameplayCreateCharacterisationTest, FMCPGameplayCreateCharTestBase,
	"UE.MCP.Gameplay.Characterisation.Create",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPGameplayCreateCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPGameplayCreateChar/"), TEXT("UEMCPGameplayCreateChar"));
	const FString Folder = Mount.RootPath.LeftChop(1);

	FScriptedRun Run;
	FGameplayHandlers::RegisterHandlers(Run.Registry);
	Run.Recording.Normaliser.Replace(Mount.RootPath, TEXT("/<root>/"));
	Run.Recording.Normaliser.Numbered({ TEXT("actorName"), TEXT("actorPath") });

	auto At = [&](const TCHAR* Name) { return FArgs().S(TEXT("name"), Name).S(TEXT("packagePath"), Folder); };

	for (const TCHAR* Method : { TEXT("create_blackboard"), TEXT("create_eqs_query"), TEXT("create_state_tree") })
	{
		const FString Name = FString(TEXT("A_")) + Method;
		Run.Step(*(FString(Method) + TEXT("_01_created")), Method, At(*Name));
		Run.Step(*(FString(Method) + TEXT("_02_existed")), Method, At(*Name));
		Run.Step(*(FString(Method) + TEXT("_03_conflict_error")), Method, At(*Name).S(TEXT("onConflict"), TEXT("error")));
		Run.Step(*(FString(Method) + TEXT("_04_missing_name")), Method, FArgs().S(TEXT("packagePath"), Folder));
	}
	Run.Step(TEXT("create_state_tree_05_bad_schema"), TEXT("create_state_tree"), At(TEXT("ST_BadSchema")).S(TEXT("schema"), TEXT("/Script/NoSuch.Schema")));

	Run.Step(TEXT("create_behavior_tree_01_created"), TEXT("create_behavior_tree"), At(TEXT("BT_Char")));
	Run.Step(TEXT("create_behavior_tree_02_existed"), TEXT("create_behavior_tree"), At(TEXT("BT_Char")));
	Run.Step(TEXT("create_behavior_tree_03_with_blackboard"), TEXT("create_behavior_tree"),
		At(TEXT("BT_CharBB")).S(TEXT("blackboardPath"), Mount.RootPath + TEXT("A_create_blackboard")));
	Run.Step(TEXT("create_behavior_tree_04_bad_blackboard"), TEXT("create_behavior_tree"),
		At(TEXT("BT_CharBadBB")).S(TEXT("blackboardPath"), Mount.RootPath + TEXT("BB_Nope")));

	Run.Step(TEXT("create_smart_object_definition_01_created"), TEXT("create_smart_object_definition"), At(TEXT("SO_Char")));
	Run.Step(TEXT("create_smart_object_definition_02_existed"), TEXT("create_smart_object_definition"), At(TEXT("SO_Char")));
	Run.Step(TEXT("create_smart_object_definition_03_bad_behavior"), TEXT("create_smart_object_definition"),
		At(TEXT("SO_CharBad")).S(TEXT("defaultBehaviorClass"), TEXT("/Script/NoSuch.Behavior")));

	for (const TCHAR* Method : { TEXT("create_game_mode"), TEXT("create_game_state"), TEXT("create_player_controller"),
		TEXT("create_player_state"), TEXT("create_hud") })
	{
		const FString Name = FString(TEXT("BP_")) + Method;
		Run.Step(*(FString(Method) + TEXT("_01_created")), Method, At(*Name));
		Run.Step(*(FString(Method) + TEXT("_02_existed")), Method, At(*Name));
		Run.Step(*(FString(Method) + TEXT("_03_bad_parent")), Method, At(*(Name + TEXT("_Bad"))).S(TEXT("parentClass"), TEXT("NoSuchParentClassAnywhere")));
		Run.Step(*(FString(Method) + TEXT("_04_unrelated_parent")), Method, At(*(Name + TEXT("_Unrelated"))).S(TEXT("parentClass"), TEXT("StaticMeshActor")));
	}
	Run.Step(TEXT("create_game_mode_05_subclass_parent"), TEXT("create_game_mode"), At(TEXT("BP_GameModeSub")).S(TEXT("parentClass"), TEXT("GameMode")));

	// A nav modifier volume in the editor world, then removed again.
	const FString Label = TEXT("UEMCPCharNavModifier");
	auto RemoveSpawned = [&Label]()
	{
		UWorld* World = GetEditorWorld();
		if (!World) return;
		TArray<AActor*> Spawned;
		MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Spawned);
		for (AActor* Actor : Spawned) World->DestroyActor(Actor);
	};
	RemoveSpawned();
	Run.Step(TEXT("spawn_nav_modifier_volume_01_created"), TEXT("spawn_nav_modifier_volume"),
		FArgs().S(TEXT("label"), Label).V(TEXT("location"), 10, 20, 30).V(TEXT("extent"), 50, 60, 70));
	Run.Step(TEXT("spawn_nav_modifier_volume_02_existed"), TEXT("spawn_nav_modifier_volume"), FArgs().S(TEXT("label"), Label));
	Run.Step(TEXT("spawn_nav_modifier_volume_03_conflict_error"), TEXT("spawn_nav_modifier_volume"), FArgs().S(TEXT("label"), Label).S(TEXT("onConflict"), TEXT("error")));
	Run.Step(TEXT("spawn_nav_modifier_volume_04_bad_area"), TEXT("spawn_nav_modifier_volume"), FArgs().S(TEXT("label"), TEXT("UEMCPCharNavBad")).S(TEXT("areaClass"), TEXT("NoSuchArea")));
	Run.Step(TEXT("spawn_nav_modifier_volume_05_bad_extent"), TEXT("spawn_nav_modifier_volume"), FArgs().S(TEXT("label"), TEXT("UEMCPCharNavBad")).V(TEXT("extent"), 0, 10, 10));

	RemoveSpawned();

	return Run.Finish(*this, TEXT("gameplay-create-responses"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
