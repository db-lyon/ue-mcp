// The response of every runtime handler the F7 port covers, recorded before it
// and held to the contract after it (plans/domain-design.md 0 and 8). No PIE
// runs here, so each call records what a caller meets without one: the
// refusal, or the editor-world answer a handler that defaults to auto gives.
// The fixture is tests/golden/cpp/runtime-responses.json; a run without it
// records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Handlers/Animation/AnimationHandlers.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "Handlers/Gameplay/GameplayHandlers.h"
#include "Handlers/Gas/GasHandlers.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Handlers/Physics/PhysicsHandlers.h"
#include "Handlers/StateTree/StateTreeHandlers.h"
#include "Handlers/Widget/WidgetHandlers.h"
#include "Tests/MCPCharacterisationRun.h"

class FMCPRuntimeCharTestBase : public FAutomationTestBase
{
public:
	FMCPRuntimeCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogUObjectGlobals"));
		Categories.Add(TEXT("LogAbilitySystem"));
		Categories.Add(TEXT("LogUE_MCP"));
		Categories.Add(TEXT("LogMCPBridge"));
		return Categories;
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPRuntimeCharacterisationTest, FMCPRuntimeCharTestBase,
	"UE.MCP.Families.Characterisation.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPRuntimeCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPCharacterisation;

	if (MCPIsPlayInEditorActive())
	{
		AddError(TEXT("This characterisation records the no-PIE answers; stop play first."));
		return false;
	}
	UWorld* World = GetEditorWorld();
	if (!TestNotNull(TEXT("editor world"), World)) return false;

	FScriptedRun Run;
	FGasHandlers::RegisterHandlers(Run.Registry);
	FEditorHandlers::RegisterHandlers(Run.Registry);
	FGameplayHandlers::RegisterHandlers(Run.Registry);
	FStateTreeHandlers::RegisterHandlers(Run.Registry);
	FAnimationHandlers::RegisterHandlers(Run.Registry);
	FWidgetHandlers::RegisterHandlers(Run.Registry);
	FPhysicsHandlers::RegisterHandlers(Run.Registry);
	FLevelHandlers::RegisterHandlers(Run.Registry);
	Run.Recording.Normaliser.Replace(World->GetOutermost()->GetName(), TEXT("<map>"));
	Run.Recording.Normaliser.Numbered({ TEXT("actorName"), TEXT("actorPath") });

	// One actor no world carries, and the params the handlers read before or after it.
	auto Common = []()
	{
		return FArgs()
			.S(TEXT("actorLabel"), TEXT("UEMCPCharNoSuchRuntimeActor"))
			.S(TEXT("attribute"), TEXT("UEMCPCharSet.Health"))
			.S(TEXT("effectClass"), TEXT("/Game/UEMCPCharNoSuchEffect"))
			.S(TEXT("abilityClass"), TEXT("/Game/UEMCPCharNoSuchAbility"))
			.S(TEXT("tag"), TEXT("UEMCP.Char"))
			.S(TEXT("key"), TEXT("Target"))
			.S(TEXT("eventTag"), TEXT("UEMCP.Char"))
			.S(TEXT("stateName"), TEXT("Root"))
			.S(TEXT("boneName"), TEXT("root"))
			.S(TEXT("widgetName"), TEXT("WBP_UEMCPCharNoSuchWidget"))
			.S(TEXT("assetPath"), TEXT("/Game/UEMCPCharNoSuchAsset"))
			.S(TEXT("functionName"), TEXT("UEMCPCharNoSuchFunction"))
			.S(TEXT("propertyPath"), TEXT("bHidden"))
			.S(TEXT("propertyName"), TEXT("bHidden"))
			.S(TEXT("componentName"), TEXT("UEMCPCharNoSuchComponent"))
			.S(TEXT("objectPath"), TEXT("/Game/UEMCPCharNoSuchObject.UEMCPCharNoSuchObject"))
			.S(TEXT("movementMode"), TEXT("Walking"))
			.S(TEXT("queryPath"), TEXT("/Game/UEMCPCharNoSuchQuery"))
			.S(TEXT("behaviorTreePath"), TEXT("/Game/UEMCPCharNoSuchTree"))
			.S(TEXT("targetLabel"), TEXT("UEMCPCharNoSuchRuntimeActor"))
			.N(TEXT("value"), 1)
			.N(TEXT("factor"), 1)
			.N(TEXT("inputId"), 1)
			.V(TEXT("location"), 0, 0, 0)
			.V(TEXT("start"), 0, 0, 1000)
			.V(TEXT("end"), 0, 0, -1000)
			.V(TEXT("impulse"), 0, 0, 100)
			.V(TEXT("delta"), 0, 0, 10);
	};

	const TCHAR* Methods[] = {
		// GAS
		TEXT("apply_effect"), TEXT("remove_effect"), TEXT("set_attribute"), TEXT("get_attribute"),
		TEXT("get_live_attribute_value"), TEXT("set_live_attribute_value"), TEXT("get_asc_state"), TEXT("init_asc"),
		TEXT("grant_ability"), TEXT("revoke_ability"), TEXT("get_active_effects"), TEXT("trace_ability_activation"),
		TEXT("add_loose_gameplay_tag"), TEXT("remove_loose_gameplay_tag"), TEXT("bind_ability_input"),
		TEXT("clear_ability_input"), TEXT("send_ability_input"), TEXT("audit_attributes"), TEXT("capture_gas_state"),
		// Editor
		TEXT("get_runtime_value"), TEXT("get_runtime_values"), TEXT("set_pie_time_scale"), TEXT("get_pie_pawn"),
		TEXT("pie_set_player_view"), TEXT("stage_game_input"), TEXT("teleport_runtime_actor"), TEXT("set_movement_mode"),
		TEXT("read_bone_transforms"), TEXT("invoke_object_function"), TEXT("get_object_properties"), TEXT("set_object_property"),
		// Gameplay
		TEXT("get_bt_runtime"), TEXT("get_live_blackboard"), TEXT("set_live_blackboard"), TEXT("run_behavior_tree"),
		TEXT("stop_behavior_tree"), TEXT("read_perception"), TEXT("get_perceived_actors"),
		TEXT("check_perception"), TEXT("run_eqs_query"), TEXT("get_state_tree_runtime"),
		// StateTree
		TEXT("read_state_tree_runtime"), TEXT("send_state_tree_event"), TEXT("request_state_tree_transition"),
		// Animation
		TEXT("get_bone_transform"), TEXT("list_bones"), TEXT("set_live_post_process_anim_blueprint"), TEXT("get_live_bone_transforms"),
		// Widget
		TEXT("list_runtime_widgets"), TEXT("get_runtime_widget"), TEXT("add_to_viewport"), TEXT("invoke_runtime_function"),
		TEXT("get_runtime_delegates"), TEXT("inspect_runtime_instances"), TEXT("get_runtime_focus_path"), TEXT("set_runtime_focus"),
		// Physics and Level
		TEXT("add_impulse"), TEXT("line_trace"), TEXT("snap_actor_to_floor"), TEXT("nudge_component"),
		TEXT("restore_component_relative_transform"),
	};

	int32 Index = 0;
	for (const TCHAR* Method : Methods)
	{
		const FString Prefix = FString::Printf(TEXT("%03d_%s"), ++Index, Method);
		Run.Step(*(Prefix + TEXT("_default")), Method, Common());
		Run.Step(*(Prefix + TEXT("_pie")), Method, Common().S(TEXT("world"), TEXT("pie")));
	}

	return Run.Finish(*this, TEXT("runtime-responses"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
