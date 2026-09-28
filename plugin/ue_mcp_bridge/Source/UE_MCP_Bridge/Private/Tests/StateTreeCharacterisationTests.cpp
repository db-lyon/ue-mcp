// The response of every statetree handler, recorded before the family port and
// held to the contract after it (plans/domain-design.md 0 and 8 step 4). One
// scripted run covers each handler's success path, its no-op path where it
// has one, and the refusals a caller meets most. The fixture is
// tests/golden/cpp/statetree-responses.json; a run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/StateTree/StateTreeHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "StateTree.h"
#include "Tests/MCPCharacterisation.h"
#include "Tests/MCPScopedTestMount.h"
#include "Tests/MCPStateTreeTestBase.h"

namespace MCPStateTreeCharacterisation
{
	/** Build a params object from alternating key/value pairs. */
	struct FArgs
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		FArgs& S(const TCHAR* Key, const FString& Value) { Obj->SetStringField(Key, Value); return *this; }
		FArgs& N(const TCHAR* Key, double Value) { Obj->SetNumberField(Key, Value); return *this; }
		FArgs& B(const TCHAR* Key, bool Value) { Obj->SetBoolField(Key, Value); return *this; }
		FArgs& O(const TCHAR* Key, const TSharedPtr<FJsonObject>& Value) { Obj->SetObjectField(Key, Value); return *this; }
		FArgs& A(const TCHAR* Key, const TArray<TSharedPtr<FJsonValue>>& Value) { Obj->SetArrayField(Key, Value); return *this; }
	};

	FString Field(const TSharedPtr<FJsonObject>& Result, const TCHAR* Key)
	{
		FString Out;
		if (Result.IsValid()) Result->TryGetStringField(Key, Out);
		return Out;
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPStateTreeCharacterisationTest, FMCPStateTreeTestBase,
	"UE.MCP.StateTree.Characterisation.Responses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MCPStateTreeCharacterisation
{
	/**
	 * The response with its run-to-run noise removed: a paging cursor encodes
	 * a state GUID the normaliser cannot see inside the base64, and bindable
	 * sources come out in GUID hash order.
	 */
	TSharedPtr<FJsonObject> Stable(const TSharedPtr<FJsonObject>& Result)
	{
		if (!Result.IsValid()) return Result;
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>(*Result);
		FString Cursor;
		if (Out->TryGetStringField(TEXT("nextCursor"), Cursor) && !Cursor.IsEmpty())
		{
			Out->SetStringField(TEXT("nextCursor"), TEXT("<cursor>"));
		}
		const TArray<TSharedPtr<FJsonValue>>* Sources = nullptr;
		if (Out->TryGetArrayField(TEXT("sources"), Sources))
		{
			TArray<TSharedPtr<FJsonValue>> Sorted = *Sources;
			auto SortKey = [](const TSharedPtr<FJsonValue>& V)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O)) return FString();
				return (*O)->GetStringField(TEXT("structPath")) + TEXT("|") + (*O)->GetStringField(TEXT("structType"));
			};
			Sorted.StableSort([&](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B) { return SortKey(A) < SortKey(B); });
			Out->SetArrayField(TEXT("sources"), Sorted);
		}
		return Out;
	}
}

bool FMCPStateTreeCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace MCPStateTreeCharacterisation;
	using UEMCPStateTreeTests::Call;

	// 07_missing_asset loads a path that does not exist, which the asset subsystem logs as an error.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	FMCPScopedTestMount Mount(TEXT("/UEMCPStateTreeChar/"), TEXT("UEMCPStateTreeChar"));
	const FString Tree = Mount.RootPath + TEXT("ST_Char");
	const FString Linked = Mount.RootPath + TEXT("ST_CharLinked");
	UEMCPStateTreeTests::MakeBareTree(Tree);
	UEMCPStateTreeTests::MakeBareTree(Linked);

	FMCPHandlerRegistry Registry;
	FStateTreeHandlers::RegisterHandlers(Registry);

	UEMCPCharacterisation::FRecording Run;
	Run.Normaliser.Replace(Mount.ContentPath, TEXT("<content>"));
	Run.Normaliser.Replace(FPaths::ConvertRelativePathToFull(Mount.ContentPath), TEXT("<content>"));
	Run.Normaliser.Replace(Mount.RootPath, TEXT("/<root>/"));

	auto Step = [&](const TCHAR* Name, const TCHAR* Method, const FArgs& Args)
	{
		const TSharedPtr<FJsonObject> Result = Call(Registry, Method, Args.Obj);
		Run.Add(Name, MakeShared<FJsonValueObject>(Stable(Result)));
		return Result;
	};
	auto At = [&]() { return FArgs().S(TEXT("assetPath"), Tree); };

	// Before a schema: the editor-data refusals.
	Step(TEXT("01_read_without_editor_data"), TEXT("read_state_tree"), At());
	Step(TEXT("02_compile_without_editor_data"), TEXT("compile_state_tree"), At());
	Step(TEXT("03_add_state_without_editor_data"), TEXT("add_state_tree_state"), At().S(TEXT("name"), TEXT("X")));
	Step(TEXT("04_set_schema"), TEXT("set_state_tree_schema"), At());
	Step(TEXT("05_set_schema_again"), TEXT("set_state_tree_schema"), At());
	Step(TEXT("06_set_schema_linked"), TEXT("set_state_tree_schema"), FArgs().S(TEXT("assetPath"), Linked));
	Step(TEXT("07_missing_asset"), TEXT("read_state_tree"), FArgs().S(TEXT("assetPath"), Mount.RootPath + TEXT("Nope")));

	// States.
	Step(TEXT("10_add_child"), TEXT("add_state_tree_state"), At().S(TEXT("statePath"), TEXT("Root")).S(TEXT("name"), TEXT("Child")));
	Step(TEXT("11_add_group_first"), TEXT("add_state_tree_state"), At().S(TEXT("statePath"), TEXT("Root")).S(TEXT("name"), TEXT("Second"))
		.S(TEXT("stateType"), TEXT("Group")).S(TEXT("selectionBehavior"), TEXT("TrySelectChildrenAtRandom")).N(TEXT("insertIndex"), 0));
	Step(TEXT("12_add_subtree_root"), TEXT("add_state_tree_state"), At().S(TEXT("name"), TEXT("Sub")).S(TEXT("stateType"), TEXT("Subtree")));
	Step(TEXT("13_add_linked_asset"), TEXT("add_state_tree_state"), At().S(TEXT("statePath"), TEXT("Root")).S(TEXT("name"), TEXT("Link"))
		.S(TEXT("stateType"), TEXT("LinkedAsset")).S(TEXT("linkedSubtree"), Linked));
	Step(TEXT("14_list_states"), TEXT("list_state_tree_states"), At());
	Step(TEXT("15_list_states_paged"), TEXT("list_state_tree_states"), At().N(TEXT("limit"), 2));
	Step(TEXT("16_set_weight"), TEXT("set_state_tree_state_property"), At().S(TEXT("statePath"), TEXT("Root.Child"))
		.S(TEXT("propertyName"), TEXT("weight")).S(TEXT("value"), TEXT("2.5")));
	Step(TEXT("17_set_description"), TEXT("set_state_tree_state_property"), At().S(TEXT("statePath"), TEXT("Root.Child"))
		.S(TEXT("propertyName"), TEXT("description")).S(TEXT("value"), TEXT("hello")));
	Step(TEXT("18_set_enabled"), TEXT("set_state_tree_state_property"), At().S(TEXT("statePath"), TEXT("Root.Child"))
		.S(TEXT("propertyName"), TEXT("bEnabled")).S(TEXT("value"), TEXT("false")));
	Step(TEXT("19_set_selection"), TEXT("set_state_tree_state_property"), At().S(TEXT("statePath"), TEXT("Root.Child"))
		.S(TEXT("propertyName"), TEXT("selectionBehavior")).S(TEXT("value"), TEXT("TryEnterState")));
	Step(TEXT("20_set_tag_clear"), TEXT("set_state_tree_state_property"), At().S(TEXT("statePath"), TEXT("Root.Child"))
		.S(TEXT("propertyName"), TEXT("tag")).S(TEXT("value"), TEXT("")));
	Step(TEXT("21_set_tag_unknown"), TEXT("set_state_tree_state_property"), At().S(TEXT("statePath"), TEXT("Root.Child"))
		.S(TEXT("propertyName"), TEXT("tag")).S(TEXT("value"), TEXT("No.Such.Tag.Anywhere")));
	Step(TEXT("22_set_unknown_property"), TEXT("set_state_tree_state_property"), At().S(TEXT("statePath"), TEXT("Root.Child"))
		.S(TEXT("propertyName"), TEXT("nope")).S(TEXT("value"), TEXT("1")));
	Step(TEXT("23_set_linked_asset_unlinked"), TEXT("set_state_tree_state_property"), At().S(TEXT("statePath"), TEXT("Root.Second"))
		.S(TEXT("propertyName"), TEXT("linkedAsset")).S(TEXT("value"), Linked));
	Step(TEXT("24_set_state_missing"), TEXT("set_state_tree_state_property"), At().S(TEXT("statePath"), TEXT("Root.Nope"))
		.S(TEXT("propertyName"), TEXT("weight")).S(TEXT("value"), TEXT("1")));
	Step(TEXT("25_set_type"), TEXT("set_state_tree_state_property"), At().S(TEXT("statePath"), TEXT("Root.Second"))
		.S(TEXT("propertyName"), TEXT("type")).S(TEXT("value"), TEXT("State")));

	// Tasks.
	auto Duration = [](double Value) { auto O = MakeShared<FJsonObject>(); O->SetNumberField(TEXT("Duration"), Value); return O; };
	auto ChildArgs = [&]() { return At().S(TEXT("statePath"), TEXT("Root.Child")); };
	Step(TEXT("30_add_task"), TEXT("add_state_tree_task"), ChildArgs().S(TEXT("structType"), TEXT("StateTreeDelayTask")).O(TEXT("instanceProperties"), Duration(2.5)));
	{
		auto Bad = MakeShared<FJsonObject>();
		Bad->SetNumberField(TEXT("NoSuchProperty"), 1.0);
		Step(TEXT("31_add_task_unknown_key"), TEXT("add_state_tree_task"), ChildArgs().S(TEXT("structType"), TEXT("StateTreeDelayTask")).O(TEXT("instanceProperties"), Bad));
	}
	Step(TEXT("32_add_task_unknown_struct"), TEXT("add_state_tree_task"), ChildArgs().S(TEXT("structType"), TEXT("NoSuchStruct")));
	Step(TEXT("33_set_task_instance"), TEXT("set_state_tree_task_instance_property"), ChildArgs().N(TEXT("taskIndex"), 0)
		.S(TEXT("propertyName"), TEXT("Duration")).S(TEXT("value"), TEXT("3")));
	Step(TEXT("34_set_task_instance_bad_value"), TEXT("set_state_tree_task_instance_property"), ChildArgs().N(TEXT("taskIndex"), 0)
		.S(TEXT("propertyName"), TEXT("Duration")).S(TEXT("value"), TEXT("banana")));
	Step(TEXT("35_set_task_instance_bad_index"), TEXT("set_state_tree_task_instance_property"), ChildArgs().N(TEXT("taskIndex"), 7)
		.S(TEXT("propertyName"), TEXT("Duration")).S(TEXT("value"), TEXT("1")));
	Step(TEXT("36_set_task_instance_unknown"), TEXT("set_state_tree_task_instance_property"), ChildArgs().N(TEXT("taskIndex"), 0)
		.S(TEXT("propertyName"), TEXT("Nope")).S(TEXT("value"), TEXT("1")));
	Step(TEXT("37_set_task_node"), TEXT("set_state_tree_task_property"), ChildArgs().N(TEXT("taskIndex"), 0)
		.S(TEXT("propertyName"), TEXT("bTaskEnabled")).S(TEXT("value"), TEXT("false")));
	Step(TEXT("38_set_task_node_unknown"), TEXT("set_state_tree_task_property"), ChildArgs().N(TEXT("taskIndex"), 0)
		.S(TEXT("propertyName"), TEXT("Nope")).S(TEXT("value"), TEXT("1")));
	Step(TEXT("39_set_task_node_bad_index"), TEXT("set_state_tree_task_property"), ChildArgs().N(TEXT("taskIndex"), 4)
		.S(TEXT("propertyName"), TEXT("bTaskEnabled")).S(TEXT("value"), TEXT("true")));

	// Enter conditions, transitions, transition conditions, considerations.
	Step(TEXT("40_add_enter_condition"), TEXT("add_state_tree_enter_condition"), ChildArgs().S(TEXT("structType"), TEXT("StateTreeCompareIntCondition")).S(TEXT("operand"), TEXT("Or")));
	Step(TEXT("41_remove_enter_condition"), TEXT("remove_state_tree_enter_condition"), ChildArgs().N(TEXT("conditionIndex"), 0));
	Step(TEXT("42_remove_enter_condition_bad"), TEXT("remove_state_tree_enter_condition"), ChildArgs().N(TEXT("conditionIndex"), 0));
	Step(TEXT("43_add_transition"), TEXT("add_state_tree_transition"), ChildArgs().S(TEXT("trigger"), TEXT("OnStateSucceeded|OnStateFailed"))
		.S(TEXT("transitionType"), TEXT("GotoState")).S(TEXT("targetStatePath"), TEXT("Root.Second")).S(TEXT("priority"), TEXT("High"))
		.N(TEXT("delayDuration"), 1.5).B(TEXT("bDelayTransition"), true));
	Step(TEXT("44_add_transition_condition"), TEXT("add_state_tree_transition_condition"), ChildArgs().N(TEXT("transitionIndex"), 0)
		.S(TEXT("structType"), TEXT("StateTreeCompareIntCondition")).S(TEXT("operand"), TEXT("And")));
	Step(TEXT("45_add_transition_condition_bad"), TEXT("add_state_tree_transition_condition"), ChildArgs().N(TEXT("transitionIndex"), 3)
		.S(TEXT("structType"), TEXT("StateTreeCompareIntCondition")));
	Step(TEXT("46_remove_transition_condition"), TEXT("remove_state_tree_transition_condition"), ChildArgs().N(TEXT("transitionIndex"), 0).N(TEXT("conditionIndex"), 0));
	Step(TEXT("47_remove_transition_condition_bad"), TEXT("remove_state_tree_transition_condition"), ChildArgs().N(TEXT("transitionIndex"), 0).N(TEXT("conditionIndex"), 0));
	Step(TEXT("48_add_transition_condition_again"), TEXT("add_state_tree_transition_condition"), ChildArgs().N(TEXT("transitionIndex"), 0)
		.S(TEXT("structType"), TEXT("StateTreeCompareIntCondition")));
	Step(TEXT("49_add_consideration"), TEXT("add_state_tree_consideration"), ChildArgs().S(TEXT("structType"), TEXT("StateTreeFloatInputConsideration")).S(TEXT("operand"), TEXT("or")));
	Step(TEXT("50_add_consideration_wrong_base"), TEXT("add_state_tree_consideration"), ChildArgs().S(TEXT("structType"), TEXT("StateTreeDelayTask")));
	Step(TEXT("51_remove_consideration"), TEXT("remove_state_tree_consideration"), ChildArgs().N(TEXT("considerationIndex"), 0));
	Step(TEXT("52_remove_consideration_bad"), TEXT("remove_state_tree_consideration"), ChildArgs().N(TEXT("considerationIndex"), 0));
	Step(TEXT("53_read_state"), TEXT("read_state_tree_state"), ChildArgs());
	Step(TEXT("54_remove_transition"), TEXT("remove_state_tree_transition"), ChildArgs().N(TEXT("transitionIndex"), 0));
	Step(TEXT("55_remove_transition_bad"), TEXT("remove_state_tree_transition"), ChildArgs().N(TEXT("transitionIndex"), 0));
	Step(TEXT("56_remove_task"), TEXT("remove_state_tree_task"), ChildArgs().N(TEXT("taskIndex"), 0));
	Step(TEXT("57_remove_task_bad"), TEXT("remove_state_tree_task"), ChildArgs().N(TEXT("taskIndex"), 0));
	Step(TEXT("58_add_task_for_clear"), TEXT("add_state_tree_task"), ChildArgs().S(TEXT("structType"), TEXT("StateTreeDelayTask")));
	Step(TEXT("59_clear_nodes"), TEXT("clear_state_tree_state_nodes"), ChildArgs());
	Step(TEXT("60_clear_nodes_again"), TEXT("clear_state_tree_state_nodes"), ChildArgs());

	// Evaluators and global tasks, addressed by node id.
	const TSharedPtr<FJsonObject> Eval = Step(TEXT("61_add_evaluator"), TEXT("add_state_tree_evaluator"), At().S(TEXT("structType"), TEXT("StateTreeBlueprintEvaluatorWrapper")));
	const FString EvalId = Field(Eval, TEXT("nodeId"));
	Step(TEXT("62_add_evaluator_wrong_base"), TEXT("add_state_tree_evaluator"), At().S(TEXT("structType"), TEXT("StateTreeDelayTask")));
	Step(TEXT("63_set_evaluator_instance_none"), TEXT("set_state_tree_evaluator_instance_property"), At().S(TEXT("nodeId"), EvalId)
		.S(TEXT("propertyName"), TEXT("X")).S(TEXT("value"), TEXT("1")));
	Step(TEXT("64_set_evaluator_node"), TEXT("set_state_tree_evaluator_property"), At().S(TEXT("nodeId"), EvalId)
		.S(TEXT("propertyName"), TEXT("EvaluatorClass")).S(TEXT("value"), TEXT("None")));
	Step(TEXT("65_set_evaluator_bad_id"), TEXT("set_state_tree_evaluator_property"), At().S(TEXT("nodeId"), TEXT("not-a-guid"))
		.S(TEXT("propertyName"), TEXT("EvaluatorClass")).S(TEXT("value"), TEXT("None")));
	Step(TEXT("66_set_node_class_native"), TEXT("set_state_tree_node_class"), At().S(TEXT("nodeId"), EvalId).S(TEXT("nodeClass"), TEXT("NoSuchClass")));
	Step(TEXT("67_remove_evaluator"), TEXT("remove_state_tree_evaluator"), At().S(TEXT("nodeId"), EvalId));
	Step(TEXT("68_remove_evaluator_gone"), TEXT("remove_state_tree_evaluator"), At().S(TEXT("nodeId"), EvalId));
	const TSharedPtr<FJsonObject> Global = Step(TEXT("69_add_global_task"), TEXT("add_state_tree_global_task"), At().S(TEXT("structType"), TEXT("StateTreeDelayTask")).O(TEXT("instanceProperties"), Duration(1.0)));
	const FString GlobalId = Field(Global, TEXT("nodeId"));
	Step(TEXT("70_set_global_instance"), TEXT("set_state_tree_global_task_instance_property"), At().S(TEXT("nodeId"), GlobalId)
		.S(TEXT("propertyName"), TEXT("Duration")).S(TEXT("value"), TEXT("4")));
	Step(TEXT("71_set_global_instance_bad"), TEXT("set_state_tree_global_task_instance_property"), At().S(TEXT("nodeId"), GlobalId)
		.S(TEXT("propertyName"), TEXT("Duration")).S(TEXT("value"), TEXT("banana")));
	Step(TEXT("72_set_global_node"), TEXT("set_state_tree_global_task_property"), At().S(TEXT("nodeId"), GlobalId)
		.S(TEXT("propertyName"), TEXT("bTaskEnabled")).S(TEXT("value"), TEXT("false")));
	Step(TEXT("73_set_node_class_not_wrapper"), TEXT("set_state_tree_node_class"), At().S(TEXT("nodeId"), GlobalId).S(TEXT("nodeClass"), TEXT("Actor")));

	// Bindings.
	Step(TEXT("74_add_binding"), TEXT("add_state_tree_binding"), At().S(TEXT("sourceStructId"), GlobalId).S(TEXT("sourcePath"), TEXT("Duration"))
		.S(TEXT("targetStructId"), GlobalId).S(TEXT("targetPath"), TEXT("RandomDeviation")));
	Step(TEXT("75_add_binding_replace"), TEXT("add_state_tree_binding"), At().S(TEXT("sourceStructId"), GlobalId).S(TEXT("sourcePath"), TEXT("RandomDeviation"))
		.S(TEXT("targetStructId"), GlobalId).S(TEXT("targetPath"), TEXT("RandomDeviation")));
	Step(TEXT("76_list_bindings"), TEXT("list_state_tree_bindings"), At());
	Step(TEXT("77_list_bindings_filtered"), TEXT("list_state_tree_bindings"), At().S(TEXT("structId"), GlobalId));
	Step(TEXT("78_remove_binding"), TEXT("remove_state_tree_binding"), At().S(TEXT("targetStructId"), GlobalId).S(TEXT("targetPath"), TEXT("RandomDeviation")));
	Step(TEXT("79_remove_binding_again"), TEXT("remove_state_tree_binding"), At().S(TEXT("targetStructId"), GlobalId).S(TEXT("targetPath"), TEXT("RandomDeviation")));
	Step(TEXT("80_list_bindable_sources"), TEXT("list_state_tree_bindable_sources"), At());
	Step(TEXT("81_remove_global_task"), TEXT("remove_state_tree_global_task"), At().S(TEXT("nodeId"), GlobalId));
	Step(TEXT("82_remove_global_task_gone"), TEXT("remove_state_tree_global_task"), At().S(TEXT("nodeId"), GlobalId));

	// Colours and parameters.
	Step(TEXT("83_list_colors"), TEXT("list_state_tree_colors"), At());
	Step(TEXT("84_add_color"), TEXT("add_state_tree_color"), At().S(TEXT("displayName"), TEXT("Red")).S(TEXT("color"), TEXT("(R=1.0,G=0.0,B=0.0,A=1.0)")));
	Step(TEXT("85_add_color_duplicate"), TEXT("add_state_tree_color"), At().S(TEXT("displayName"), TEXT("Red")));
	Step(TEXT("86_set_color"), TEXT("set_state_tree_state_property"), ChildArgs().S(TEXT("propertyName"), TEXT("color")).S(TEXT("value"), TEXT("Red")));
	Step(TEXT("87_set_color_unknown"), TEXT("set_state_tree_state_property"), ChildArgs().S(TEXT("propertyName"), TEXT("color")).S(TEXT("value"), TEXT("Blue")));
	Step(TEXT("88_add_state_parameter"), TEXT("add_state_tree_state_parameter"), ChildArgs().S(TEXT("paramName"), TEXT("P")).S(TEXT("paramType"), TEXT("Float")));
	Step(TEXT("89_add_state_parameter_bad_type"), TEXT("add_state_tree_state_parameter"), ChildArgs().S(TEXT("paramName"), TEXT("Q")).S(TEXT("paramType"), TEXT("Vector")));
	Step(TEXT("90_list_state_parameters"), TEXT("list_state_tree_state_parameters"), ChildArgs());
	Step(TEXT("91_set_state_parameter"), TEXT("set_state_tree_state_parameter"), ChildArgs().S(TEXT("paramName"), TEXT("P")).S(TEXT("value"), TEXT("1.5")));
	Step(TEXT("92_set_state_parameter_bad"), TEXT("set_state_tree_state_parameter"), ChildArgs().S(TEXT("paramName"), TEXT("P")).S(TEXT("value"), TEXT("banana")));
	Step(TEXT("93_set_state_parameter_missing"), TEXT("set_state_tree_state_parameter"), ChildArgs().S(TEXT("paramName"), TEXT("Nope")).S(TEXT("value"), TEXT("1")));
	Step(TEXT("94_remove_state_parameter"), TEXT("remove_state_tree_state_parameter"), ChildArgs().S(TEXT("paramName"), TEXT("P")));
	Step(TEXT("95_remove_state_parameter_again"), TEXT("remove_state_tree_state_parameter"), ChildArgs().S(TEXT("paramName"), TEXT("P")));
	{
		TArray<TSharedPtr<FJsonValue>> Params;
		auto Speed = MakeShared<FJsonObject>();
		Speed->SetStringField(TEXT("name"), TEXT("Speed"));
		Speed->SetStringField(TEXT("type"), TEXT("float"));
		Params.Add(MakeShared<FJsonValueObject>(Speed));
		Step(TEXT("96_set_root_parameters"), TEXT("set_state_tree_root_parameters"), At().A(TEXT("parameters"), Params));
	}

	// Links and moves.
	Step(TEXT("100_link_subtree"), TEXT("set_state_tree_state_link"), At().S(TEXT("statePath"), TEXT("Root.Second")).S(TEXT("linkType"), TEXT("subtree")).S(TEXT("targetStatePath"), TEXT("Sub")));
	Step(TEXT("101_link_subtree_again"), TEXT("set_state_tree_state_link"), At().S(TEXT("statePath"), TEXT("Root.Second")).S(TEXT("linkType"), TEXT("subtree")).S(TEXT("targetStatePath"), TEXT("Sub")));
	Step(TEXT("102_link_asset"), TEXT("set_state_tree_state_link"), At().S(TEXT("statePath"), TEXT("Root.Second")).S(TEXT("linkType"), TEXT("asset")).S(TEXT("linkedAsset"), Linked));
	Step(TEXT("103_link_none"), TEXT("set_state_tree_state_link"), At().S(TEXT("statePath"), TEXT("Root.Second")).S(TEXT("linkType"), TEXT("none")));
	Step(TEXT("104_link_bad_type"), TEXT("set_state_tree_state_link"), At().S(TEXT("statePath"), TEXT("Root.Second")).S(TEXT("linkType"), TEXT("sideways")));
	Step(TEXT("105_move_reorder"), TEXT("move_state_tree_state"), At().S(TEXT("statePath"), TEXT("Root.Second")).N(TEXT("insertIndex"), 1));
	Step(TEXT("106_move_unchanged"), TEXT("move_state_tree_state"), At().S(TEXT("statePath"), TEXT("Root.Second")).N(TEXT("insertIndex"), 1));
	Step(TEXT("107_move_to_root"), TEXT("move_state_tree_state"), At().S(TEXT("statePath"), TEXT("Root.Second")).B(TEXT("toRoot"), true));
	Step(TEXT("108_move_under_descendant"), TEXT("move_state_tree_state"), At().S(TEXT("statePath"), TEXT("Root")).S(TEXT("newParentStatePath"), TEXT("Root.Child")));
	Step(TEXT("109_move_back"), TEXT("move_state_tree_state"), At().S(TEXT("statePath"), TEXT("Second")).S(TEXT("newParentStatePath"), TEXT("Root")).N(TEXT("insertIndex"), 0));

	// Reads, lifecycle, removal.
	Step(TEXT("110_read_tree"), TEXT("read_state_tree"), At());
	Step(TEXT("111_list_node_types"), TEXT("list_state_tree_node_types"), At().S(TEXT("nodeType"), TEXT("task")).S(TEXT("filter"), TEXT("StateTreeDelay")));
	Step(TEXT("112_list_node_types_bad"), TEXT("list_state_tree_node_types"), At().S(TEXT("nodeType"), TEXT("gizmo")));
	Step(TEXT("113_validate"), TEXT("validate_state_tree"), At());
	Step(TEXT("114_compile"), TEXT("compile_state_tree"), At());
	Step(TEXT("115_remove_state"), TEXT("remove_state_tree_state"), At().S(TEXT("statePath"), TEXT("Root.Link")));
	Step(TEXT("116_remove_state_missing"), TEXT("remove_state_tree_state"), At().S(TEXT("statePath"), TEXT("Root.Link")));
	Step(TEXT("117_read_state_missing"), TEXT("read_state_tree_state"), At().S(TEXT("statePath"), TEXT("Root.Nope")));

	// Runtime handlers only answer without a running tree here.
	Step(TEXT("120_read_runtime_no_actor"), TEXT("read_state_tree_runtime"), FArgs().S(TEXT("actorLabel"), TEXT("UEMCPNoSuchActor")).S(TEXT("world"), TEXT("editor")));
	Step(TEXT("121_send_event_no_actor"), TEXT("send_state_tree_event"), FArgs().S(TEXT("actorLabel"), TEXT("UEMCPNoSuchActor")).S(TEXT("eventTag"), TEXT("A.B")).S(TEXT("world"), TEXT("editor")));
	Step(TEXT("122_request_transition_no_actor"), TEXT("request_state_tree_transition"), FArgs().S(TEXT("actorLabel"), TEXT("UEMCPNoSuchActor")).S(TEXT("world"), TEXT("editor")));

	FString Note;
	const TArray<FString> Breaks = UEMCPCharacterisation::CompareWithFixture(Run, TEXT("statetree-responses"), Note);
	if (!Note.IsEmpty()) AddInfo(Note);
	for (const FString& Break : Breaks) AddError(Break);
	return Breaks.Num() == 0;
}

#endif
