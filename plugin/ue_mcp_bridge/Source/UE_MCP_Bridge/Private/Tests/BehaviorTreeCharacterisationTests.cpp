// The response of every BehaviorTree mutating handler, recorded before the
// family port and held to the contract after it (plans/domain-design.md 0 and
// 8). One scripted run covers add_bt_node, move_bt_node, remove_bt_node,
// set_bt_node_property (and its set_bt_task_property alias) and
// set_behavior_tree_blackboard: success paths, no-op paths where they exist,
// and the refusals a caller meets most. The fixture is
// tests/golden/cpp/behaviortree-responses.json; a run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Gameplay/GameplayHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Internationalization/Regex.h"
#include "Misc/AutomationTest.h"
#include "Tests/MCPCharacterisation.h"
#include "Tests/MCPScopedTestMount.h"

/** Refusals and compiles here log on purpose; the responses carry the outcome. */
class FMCPBehaviorTreeCharTestBase : public FAutomationTestBase
{
public:
	FMCPBehaviorTreeCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogBehaviorTree"));
		Categories.Add(TEXT("LogBehaviorTreeEditor"));
		Categories.Add(TEXT("LogBlackboard"));
		Categories.Add(TEXT("LogAINavigation"));
		return Categories;
	}
};

namespace MCPBehaviorTreeCharacterisation
{
	struct FArgs
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		FArgs& S(const TCHAR* Key, const FString& Value) { Obj->SetStringField(Key, Value); return *this; }
		FArgs& N(const TCHAR* Key, double Value) { Obj->SetNumberField(Key, Value); return *this; }
		FArgs& O(const TCHAR* Key, const TSharedPtr<FJsonObject>& Value) { Obj->SetObjectField(Key, Value); return *this; }
	};

	TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	}

	bool Succeeded(const TSharedPtr<FJsonObject>& Result)
	{
		bool bSuccess = false;
		return Result.IsValid() && Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	FString Field(const TSharedPtr<FJsonObject>& Result, const TCHAR* Key)
	{
		FString Out;
		if (Result.IsValid()) Result->TryGetStringField(Key, Out);
		return Out;
	}

	TSharedPtr<FJsonObject> Map(const TCHAR* Key, const TSharedPtr<FJsonValue>& Value)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetField(Key, Value);
		return Out;
	}

	/**
	 * A UBTNode's object name carries a process-wide unique suffix
	 * (BTTask_Wait_7), which differs between runs. Drop the number so the
	 * recording does not depend on how many nodes the editor made before.
	 */
	TSharedPtr<FJsonValue> StripObjectNumbers(const TSharedPtr<FJsonValue>& In)
	{
		if (!In.IsValid()) return In;
		if (In->Type == EJson::Array)
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const TSharedPtr<FJsonValue>& Item : In->AsArray()) Items.Add(StripObjectNumbers(Item));
			return MakeShared<FJsonValueArray>(Items);
		}
		if (In->Type != EJson::Object) return In;
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		for (const auto& Pair : In->AsObject()->Values)
		{
			const FString Key(Pair.Key);
			if (Key == TEXT("objectName") && Pair.Value.IsValid() && Pair.Value->Type == EJson::String)
			{
				static const FRegexPattern Suffix(TEXT("^(.*)_[0-9]+$"));
				const FString Name = Pair.Value->AsString();
				FRegexMatcher Matcher(Suffix, Name);
				Out->SetStringField(Key, Matcher.FindNext() ? Matcher.GetCaptureGroup(1) + TEXT("_<n>") : Name);
				continue;
			}
			Out->SetField(Key, StripObjectNumbers(Pair.Value));
		}
		return MakeShared<FJsonValueObject>(Out);
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPBehaviorTreeCharacterisationTest, FMCPBehaviorTreeCharTestBase,
	"UE.MCP.BehaviorTree.Characterisation.Responses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBehaviorTreeCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace MCPBehaviorTreeCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPBehaviorTreeChar/"), TEXT("UEMCPBehaviorTreeChar"));
	const FString Folder = Mount.RootPath.LeftChop(1);
	const FString Tree = Mount.RootPath + TEXT("BT_Char");
	const FString Blackboard = Mount.RootPath + TEXT("BB_Char");
	const FString OtherBlackboard = Mount.RootPath + TEXT("BB_Other");

	FMCPHandlerRegistry Registry;
	FGameplayHandlers::RegisterHandlers(Registry);

	// Fixtures, made through the bridge's own creators and not recorded.
	{
		const bool bMade =
			Succeeded(Call(Registry, TEXT("create_blackboard"), FArgs().S(TEXT("name"), TEXT("BB_Char")).S(TEXT("packagePath"), Folder).Obj))
			&& Succeeded(Call(Registry, TEXT("create_blackboard"), FArgs().S(TEXT("name"), TEXT("BB_Other")).S(TEXT("packagePath"), Folder).Obj))
			&& Succeeded(Call(Registry, TEXT("add_blackboard_key"), FArgs().S(TEXT("blackboardPath"), Blackboard)
				.S(TEXT("keyName"), TEXT("Flag")).S(TEXT("keyType"), TEXT("Bool")).Obj))
			&& Succeeded(Call(Registry, TEXT("create_behavior_tree"), FArgs().S(TEXT("name"), TEXT("BT_Char")).S(TEXT("packagePath"), Folder).Obj));
		if (!TestTrue(TEXT("fixture blackboards and tree created"), bMade)) return false;
	}

	UEMCPCharacterisation::FRecording Run;
	Run.Normaliser.Replace(Mount.ContentPath, TEXT("<content>"));
	Run.Normaliser.Replace(FPaths::ConvertRelativePathToFull(Mount.ContentPath), TEXT("<content>"));
	Run.Normaliser.Replace(Mount.RootPath, TEXT("/<root>/"));

	auto Step = [&](const TCHAR* Name, const TCHAR* Method, const FArgs& Args)
	{
		const TSharedPtr<FJsonObject> Result = Call(Registry, Method, Args.Obj);
		Run.Add(Name, StripObjectNumbers(MakeShared<FJsonValueObject>(Result)));
		return Result;
	};
	auto At = [&]() { return FArgs().S(TEXT("assetPath"), Tree); };
	auto Num = [](double Value) { return MakeShared<FJsonValueNumber>(Value); };

	// A tree nobody has opened has no editor graph yet.
	Step(TEXT("01_move_without_graph"), TEXT("move_bt_node"), At().S(TEXT("node"), TEXT("root")).N(TEXT("index"), 0));
	Step(TEXT("02_remove_without_graph"), TEXT("remove_bt_node"), At().S(TEXT("node"), TEXT("root")));
	Step(TEXT("03_add_missing_asset"), TEXT("add_bt_node"), FArgs().S(TEXT("assetPath"), Mount.RootPath + TEXT("Nope")).S(TEXT("nodeClass"), TEXT("BTComposite_Selector")));

	// add_bt_node refusals that leave the graph unseeded.
	Step(TEXT("04_add_unknown_class"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("NoSuchBTNodeClass")));
	Step(TEXT("05_add_abstract_class"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("BTCompositeNode")));
	Step(TEXT("06_add_not_a_bt_node"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("Actor")));
	Step(TEXT("07_add_category_mismatch"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("BTComposite_Selector")).S(TEXT("nodeCategory"), TEXT("task")));
	Step(TEXT("08_add_missing_node_class"), TEXT("add_bt_node"), At());

	// Building a tree: root -> Selector -> two waits, a decorator and a service.
	Step(TEXT("10_add_task_under_root"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("BTTask_Wait")));
	const TSharedPtr<FJsonObject> Selector = Step(TEXT("11_add_selector"), TEXT("add_bt_node"),
		At().S(TEXT("nodeClass"), TEXT("BTComposite_Selector")).S(TEXT("parent"), TEXT("root")).S(TEXT("nodeName"), TEXT("Top")));
	const FString SelectorGuid = Field(Selector, TEXT("guid"));
	const TSharedPtr<FJsonObject> WaitA = Step(TEXT("12_add_wait_a"), TEXT("add_bt_node"),
		At().S(TEXT("nodeClass"), TEXT("BTTask_Wait")).S(TEXT("parent"), SelectorGuid).S(TEXT("nodeName"), TEXT("WaitA"))
			.O(TEXT("properties"), Map(TEXT("WaitTime"), Num(2.0))));
	const FString WaitAGuid = Field(WaitA, TEXT("guid"));
	const TSharedPtr<FJsonObject> WaitB = Step(TEXT("13_add_wait_b_first"), TEXT("add_bt_node"),
		At().S(TEXT("nodeClass"), TEXT("BTTask_Wait")).S(TEXT("parent"), SelectorGuid).S(TEXT("nodeName"), TEXT("WaitB")).N(TEXT("index"), 0));
	const FString WaitBGuid = Field(WaitB, TEXT("guid"));
	Step(TEXT("14_add_bad_property"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("BTTask_Wait")).S(TEXT("parent"), SelectorGuid)
		.O(TEXT("properties"), Map(TEXT("NoSuchProperty"), Num(1.0))));
	Step(TEXT("15_add_bad_parent"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("BTTask_Wait")).S(TEXT("parent"), TEXT("not-a-node")));
	Step(TEXT("16_add_key_without_blackboard"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("BTDecorator_Blackboard")).S(TEXT("parent"), WaitAGuid)
		.O(TEXT("blackboardKeys"), Map(TEXT("BlackboardKey"), MakeShared<FJsonValueString>(TEXT("Flag")))));

	// set_behavior_tree_blackboard: first bind, a rebind with an inverse, refusals.
	Step(TEXT("20_set_blackboard"), TEXT("set_behavior_tree_blackboard"), FArgs().S(TEXT("behaviorTreePath"), Tree).S(TEXT("blackboardPath"), OtherBlackboard));
	Step(TEXT("21_set_blackboard_rebind"), TEXT("set_behavior_tree_blackboard"), FArgs().S(TEXT("behaviorTreePath"), Tree).S(TEXT("blackboardPath"), Blackboard));
	Step(TEXT("22_set_blackboard_same"), TEXT("set_behavior_tree_blackboard"), FArgs().S(TEXT("behaviorTreePath"), Tree).S(TEXT("blackboardPath"), Blackboard));
	Step(TEXT("23_set_blackboard_missing_blackboard"), TEXT("set_behavior_tree_blackboard"), FArgs().S(TEXT("behaviorTreePath"), Tree).S(TEXT("blackboardPath"), Mount.RootPath + TEXT("BB_Nope")));
	Step(TEXT("24_set_blackboard_missing_tree"), TEXT("set_behavior_tree_blackboard"), FArgs().S(TEXT("behaviorTreePath"), Mount.RootPath + TEXT("BT_Nope")).S(TEXT("blackboardPath"), Blackboard));
	Step(TEXT("25_set_blackboard_no_blackboard_param"), TEXT("set_behavior_tree_blackboard"), FArgs().S(TEXT("behaviorTreePath"), Tree));

	// Subnodes.
	const TSharedPtr<FJsonObject> Decorator = Step(TEXT("30_add_decorator_with_key"), TEXT("add_bt_node"),
		At().S(TEXT("nodeClass"), TEXT("BTDecorator_Blackboard")).S(TEXT("parent"), WaitAGuid)
			.O(TEXT("blackboardKeys"), Map(TEXT("BlackboardKey"), MakeShared<FJsonValueString>(TEXT("Flag")))));
	const FString DecoratorGuid = Field(Decorator, TEXT("guid"));
	Step(TEXT("31_add_unknown_key"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("BTDecorator_Blackboard")).S(TEXT("parent"), WaitAGuid)
		.O(TEXT("blackboardKeys"), Map(TEXT("BlackboardKey"), MakeShared<FJsonValueString>(TEXT("NoSuchKey")))));
	Step(TEXT("32_add_decorator_on_decorator"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("BTDecorator_Blackboard")).S(TEXT("parent"), DecoratorGuid));
	Step(TEXT("33_add_service_on_root"), TEXT("add_bt_node"), At().S(TEXT("nodeClass"), TEXT("BTService_DefaultFocus")).S(TEXT("parent"), TEXT("root")));
	const TSharedPtr<FJsonObject> Service = Step(TEXT("34_add_service"), TEXT("add_bt_node"),
		At().S(TEXT("nodeClass"), TEXT("BTService_DefaultFocus")).S(TEXT("parent"), SelectorGuid));
	const FString ServiceGuid = Field(Service, TEXT("guid"));

	// move_bt_node.
	Step(TEXT("40_move_reorder"), TEXT("move_bt_node"), At().S(TEXT("node"), WaitAGuid).N(TEXT("index"), 0));
	Step(TEXT("41_move_same_place"), TEXT("move_bt_node"), At().S(TEXT("node"), WaitAGuid).N(TEXT("index"), 0));
	Step(TEXT("42_move_no_target"), TEXT("move_bt_node"), At().S(TEXT("node"), WaitAGuid));
	Step(TEXT("43_move_root"), TEXT("move_bt_node"), At().S(TEXT("node"), TEXT("root")).N(TEXT("index"), 0));
	Step(TEXT("44_move_under_descendant"), TEXT("move_bt_node"), At().S(TEXT("node"), SelectorGuid).S(TEXT("parent"), WaitAGuid));
	Step(TEXT("45_move_decorator"), TEXT("move_bt_node"), At().S(TEXT("node"), DecoratorGuid).S(TEXT("parent"), WaitBGuid));
	Step(TEXT("46_move_service_to_root"), TEXT("move_bt_node"), At().S(TEXT("node"), ServiceGuid).S(TEXT("parent"), TEXT("root")));
	Step(TEXT("47_move_by_name"), TEXT("move_bt_node"), At().S(TEXT("node"), TEXT("WaitB")).N(TEXT("index"), 0));
	Step(TEXT("48_move_unknown_node"), TEXT("move_bt_node"), At().S(TEXT("node"), TEXT("00000000000000000000000000000001")).N(TEXT("index"), 0));
	Step(TEXT("49_move_missing_node_param"), TEXT("move_bt_node"), At().N(TEXT("index"), 0));

	// set_bt_node_property and its alias.
	{
		TSharedPtr<FJsonObject> Args = At().S(TEXT("nodeName"), TEXT("WaitA")).S(TEXT("property"), TEXT("WaitTime")).Obj;
		Args->SetNumberField(TEXT("value"), 3.5);
		Run.Add(TEXT("51_set_single"), StripObjectNumbers(MakeShared<FJsonValueObject>(Call(Registry, TEXT("set_bt_node_property"), Args))));
	}
	{
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetNumberField(TEXT("WaitTime"), 1.0);
		Props->SetNumberField(TEXT("RandomDeviation"), 0.5);
		Step(TEXT("52_set_map"), TEXT("set_bt_node_property"), At().S(TEXT("nodeName"), TEXT("WaitA")).O(TEXT("properties"), Props));
	}
	Step(TEXT("53_set_no_selector"), TEXT("set_bt_node_property"), At().O(TEXT("properties"), Map(TEXT("WaitTime"), Num(1.0))));
	Step(TEXT("54_set_no_match"), TEXT("set_bt_node_property"), At().S(TEXT("nodeName"), TEXT("Nope")).O(TEXT("properties"), Map(TEXT("WaitTime"), Num(1.0))));
	Step(TEXT("55_set_ambiguous"), TEXT("set_bt_node_property"), At().S(TEXT("nodeClass"), TEXT("BTTask_Wait")).O(TEXT("properties"), Map(TEXT("WaitTime"), Num(1.0))));
	Step(TEXT("56_set_unknown_property"), TEXT("set_bt_node_property"), At().S(TEXT("nodeName"), TEXT("WaitA")).O(TEXT("properties"), Map(TEXT("NoSuchProperty"), Num(1.0))));
	{
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetNumberField(TEXT("RandomDeviation"), 0.25);
		Props->SetStringField(TEXT("WaitTime"), TEXT("banana"));
		Step(TEXT("57_set_bad_value_restores"), TEXT("set_bt_node_property"), At().S(TEXT("nodeName"), TEXT("WaitA")).O(TEXT("properties"), Props));
	}
	Step(TEXT("58_set_property_without_value"), TEXT("set_bt_node_property"), At().S(TEXT("nodeName"), TEXT("WaitA")).S(TEXT("property"), TEXT("WaitTime")));
	Step(TEXT("59_set_nothing"), TEXT("set_bt_node_property"), At().S(TEXT("nodeName"), TEXT("WaitA")));
	{
		TSharedPtr<FJsonObject> Args = At().S(TEXT("nodeName"), TEXT("WaitB")).S(TEXT("property"), TEXT("WaitTime")).Obj;
		Args->SetNumberField(TEXT("value"), 4.0);
		Run.Add(TEXT("60_set_task_alias"), StripObjectNumbers(MakeShared<FJsonValueObject>(Call(Registry, TEXT("set_bt_task_property"), Args))));
	}
	Step(TEXT("61_set_by_path"), TEXT("set_bt_node_property"), At().S(TEXT("nodePath"), TEXT("Root")).S(TEXT("kind"), TEXT("composite"))
		.O(TEXT("properties"), Map(TEXT("NodeName"), MakeShared<FJsonValueString>(TEXT("TopRenamed")))));
	Step(TEXT("62_set_missing_asset"), TEXT("set_bt_node_property"), FArgs().S(TEXT("assetPath"), Mount.RootPath + TEXT("BT_Nope")).S(TEXT("nodeName"), TEXT("WaitA"))
		.O(TEXT("properties"), Map(TEXT("WaitTime"), Num(1.0))));

	// remove_bt_node.
	Step(TEXT("70_remove_decorator"), TEXT("remove_bt_node"), At().S(TEXT("node"), DecoratorGuid));
	Step(TEXT("71_remove_decorator_again"), TEXT("remove_bt_node"), At().S(TEXT("node"), DecoratorGuid));
	Step(TEXT("72_remove_root"), TEXT("remove_bt_node"), At().S(TEXT("node"), TEXT("root")));
	Step(TEXT("73_remove_service"), TEXT("remove_bt_node"), At().S(TEXT("node"), ServiceGuid));
	Step(TEXT("74_remove_branch"), TEXT("remove_bt_node"), At().S(TEXT("node"), SelectorGuid));
	Step(TEXT("75_remove_missing_node_param"), TEXT("remove_bt_node"), At());
	Step(TEXT("76_remove_missing_asset"), TEXT("remove_bt_node"), FArgs().S(TEXT("assetPath"), Mount.RootPath + TEXT("BT_Nope")).S(TEXT("node"), TEXT("root")));

	FString Note;
	const TArray<FString> Breaks = UEMCPCharacterisation::CompareWithFixture(Run, TEXT("behaviortree-responses"), Note);
	if (!Note.IsEmpty()) AddInfo(Note);
	for (const FString& Break : Breaks) AddError(Break);
	return Breaks.Num() == 0;
}

#endif // WITH_DEV_AUTOMATION_TESTS
