// The response of every mutating Widget Blueprint handler, recorded before the
// family port and held to the contract after it (plans/domain-design.md 0 and
// 8). One scripted run covers each handler's success path, its no-op path where
// it has one, and the refusals a caller meets most. The fixture is
// tests/golden/cpp/widget-responses.json; a run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Widget/WidgetHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/MCPCharacterisation.h"
#include "Tests/MCPScopedTestMount.h"

/** Compiles and refusals below log on purpose; the responses carry the outcome. */
class FMCPWidgetCharacterisationTestBase : public FAutomationTestBase
{
public:
	FMCPWidgetCharacterisationTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogBlueprint"));
		Categories.Add(TEXT("LogK2Compiler"));
		Categories.Add(TEXT("LogUMG"));
		Categories.Add(TEXT("LogUObjectGlobals"));
		return Categories;
	}
};

namespace MCPWidgetCharacterisation
{
	/** Build a params object one field at a time. */
	struct FArgs
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		FArgs& S(const TCHAR* Key, const FString& Value) { Obj->SetStringField(Key, Value); return *this; }
		FArgs& N(const TCHAR* Key, double Value) { Obj->SetNumberField(Key, Value); return *this; }
		FArgs& O(const TCHAR* Key, const TSharedPtr<FJsonObject>& Value) { Obj->SetObjectField(Key, Value); return *this; }
		FArgs& A(const TCHAR* Key, const TArray<TSharedPtr<FJsonValue>>& Value) { Obj->SetArrayField(Key, Value); return *this; }
		FArgs& V(const TCHAR* Key, const TSharedPtr<FJsonValue>& Value) { Obj->SetField(Key, Value); return *this; }
	};

	TSharedPtr<FJsonValue> Entry(const FString& Widget, const FString& Property, const TSharedPtr<FJsonValue>& Value)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("widgetName"), Widget);
		Out->SetStringField(TEXT("propertyName"), Property);
		if (Value.IsValid()) Out->SetField(TEXT("value"), Value);
		return MakeShared<FJsonValueObject>(Out);
	}

	/** The rollback payload a result carries, or an empty object. */
	TSharedPtr<FJsonObject> RollbackPayload(const TSharedPtr<FJsonObject>& Result)
	{
		const TSharedPtr<FJsonObject>* Rollback = nullptr;
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		if (Result.IsValid() && Result->TryGetObjectField(TEXT("rollback"), Rollback)
			&& (*Rollback)->TryGetObjectField(TEXT("payload"), Payload))
		{
			return *Payload;
		}
		return MakeShared<FJsonObject>();
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPWidgetCharacterisationTest, FMCPWidgetCharacterisationTestBase,
	"UE.MCP.Widget.Characterisation.Responses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWidgetCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace MCPWidgetCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPWidgetChar/"), TEXT("UEMCPWidgetChar"));
	const FString Wbp = Mount.RootPath + TEXT("WBP_Char");

	FMCPHandlerRegistry Registry;
	FWidgetHandlers::RegisterHandlers(Registry);

	UEMCPCharacterisation::FRecording Run;
	Run.Normaliser.Replace(Mount.ContentPath, TEXT("<content>"));
	Run.Normaliser.Replace(FPaths::ConvertRelativePathToFull(Mount.ContentPath), TEXT("<content>"));
	Run.Normaliser.Replace(Mount.RootPath, TEXT("/<root>/"));

	auto Step = [&](const TCHAR* Name, const TCHAR* Method, const FArgs& Args)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Args.Obj);
		const TSharedPtr<FJsonObject> Result = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
		Run.Add(Name, MakeShared<FJsonValueObject>(Result));
		return Result;
	};
	auto At = [&]() { return FArgs().S(TEXT("assetPath"), Wbp); };
	auto Num = [](double Value) { return MakeShared<FJsonValueNumber>(Value); };

	// The fixture, through the bridge's own creator.
	Step(TEXT("00_create_fixture"), TEXT("create_widget_blueprint"), At());

	// add_widget.
	Step(TEXT("01_add_root_panel"), TEXT("add_widget"), At().S(TEXT("widgetClass"), TEXT("VerticalBox")).S(TEXT("widgetName"), TEXT("Root")));
	Step(TEXT("02_add_title"), TEXT("add_widget"), At().S(TEXT("widgetClass"), TEXT("TextBlock")).S(TEXT("widgetName"), TEXT("Title")).S(TEXT("parentWidgetName"), TEXT("Root")));
	Step(TEXT("03_add_button"), TEXT("add_widget"), At().S(TEXT("widgetClass"), TEXT("Button")).S(TEXT("widgetName"), TEXT("Btn")).S(TEXT("parentWidgetName"), TEXT("Root")));
	Step(TEXT("04_add_title_again"), TEXT("add_widget"), At().S(TEXT("widgetClass"), TEXT("TextBlock")).S(TEXT("widgetName"), TEXT("Title")).S(TEXT("parentWidgetName"), TEXT("Root")));
	Step(TEXT("05_add_title_other_class"), TEXT("add_widget"), At().S(TEXT("widgetClass"), TEXT("Image")).S(TEXT("widgetName"), TEXT("Title")));
	Step(TEXT("06_add_unknown_class"), TEXT("add_widget"), At().S(TEXT("widgetClass"), TEXT("NoSuchWidgetClass")).S(TEXT("widgetName"), TEXT("X")));
	Step(TEXT("07_add_missing_parent"), TEXT("add_widget"), At().S(TEXT("widgetClass"), TEXT("TextBlock")).S(TEXT("widgetName"), TEXT("Orphan")).S(TEXT("parentWidgetName"), TEXT("Nope")));
	Step(TEXT("08_add_under_non_panel"), TEXT("add_widget"), At().S(TEXT("widgetClass"), TEXT("TextBlock")).S(TEXT("widgetName"), TEXT("Inner")).S(TEXT("parentWidgetName"), TEXT("Title")));
	Step(TEXT("09_add_canvas"), TEXT("add_widget"), At().S(TEXT("widgetClass"), TEXT("CanvasPanel")).S(TEXT("widgetName"), TEXT("Canvas")).S(TEXT("parentWidgetName"), TEXT("Root")));
	Step(TEXT("10_add_label_in_canvas"), TEXT("add_widget"), At().S(TEXT("widgetClass"), TEXT("TextBlock")).S(TEXT("widgetName"), TEXT("Label")).S(TEXT("parentWidgetName"), TEXT("Canvas")));
	Step(TEXT("11_add_missing_asset"), TEXT("add_widget"), FArgs().S(TEXT("assetPath"), Mount.RootPath + TEXT("WBP_Nope")).S(TEXT("widgetClass"), TEXT("TextBlock")));

	// move_widget and reorder_child.
	Step(TEXT("12_move_label_to_root"), TEXT("move_widget"), At().S(TEXT("widgetName"), TEXT("Label")).S(TEXT("newParentWidgetName"), TEXT("Root")));
	Step(TEXT("13_move_label_again"), TEXT("move_widget"), At().S(TEXT("widgetName"), TEXT("Label")).S(TEXT("newParentWidgetName"), TEXT("Root")));
	Step(TEXT("14_move_cyclic"), TEXT("move_widget"), At().S(TEXT("widgetName"), TEXT("Root")).S(TEXT("newParentWidgetName"), TEXT("Canvas")));
	Step(TEXT("15_move_into_non_panel"), TEXT("move_widget"), At().S(TEXT("widgetName"), TEXT("Label")).S(TEXT("newParentWidgetName"), TEXT("Title")));
	Step(TEXT("16_move_missing_widget"), TEXT("move_widget"), At().S(TEXT("widgetName"), TEXT("Nope")).S(TEXT("newParentWidgetName"), TEXT("Root")));
	Step(TEXT("17_reorder_label_first"), TEXT("reorder_child"), At().S(TEXT("widgetName"), TEXT("Label")).N(TEXT("index"), 0));
	Step(TEXT("18_reorder_root"), TEXT("reorder_child"), At().S(TEXT("widgetName"), TEXT("Root")).N(TEXT("index"), 0));
	Step(TEXT("19_reorder_missing"), TEXT("reorder_child"), At().S(TEXT("widgetName"), TEXT("Nope")).N(TEXT("index"), 0));

	// set_widget_property: typed, slot and reflected routes, and refusals.
	Step(TEXT("20_set_text_typed"), TEXT("set_widget_property"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("text")).S(TEXT("value"), TEXT("Hello")));
	Step(TEXT("21_set_opacity_reflected"), TEXT("set_widget_property"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("RenderOpacity")).S(TEXT("value"), TEXT("0.5")));
	Step(TEXT("22_set_slot_padding"), TEXT("set_widget_property"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("slot.padding")).S(TEXT("value"), TEXT("4,4,4,4")));
	Step(TEXT("23_set_nested_reflected"), TEXT("set_widget_property"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("RenderTransform.Angle")).S(TEXT("value"), TEXT("15")));
	Step(TEXT("24_set_unknown_property"), TEXT("set_widget_property"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("NoSuchProperty")).S(TEXT("value"), TEXT("1")));
	Step(TEXT("25_set_bad_value"), TEXT("set_widget_property"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("Visibility")).S(TEXT("value"), TEXT("NotAVisibility")));
	Step(TEXT("26_set_missing_widget"), TEXT("set_widget_property"), At().S(TEXT("widgetName"), TEXT("Nope")).S(TEXT("propertyName"), TEXT("RenderOpacity")).S(TEXT("value"), TEXT("1")));

	// set_widget_style.
	Step(TEXT("27_style_scalar"), TEXT("set_widget_style"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("RenderOpacity")).V(TEXT("value"), Num(0.75)));
	{
		TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
		Transform->SetNumberField(TEXT("Angle"), 30.0);
		Step(TEXT("28_style_struct"), TEXT("set_widget_style"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("RenderTransform")).O(TEXT("value"), Transform));
	}
	Step(TEXT("29_style_unknown_property"), TEXT("set_widget_style"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("NoSuchProperty")).V(TEXT("value"), Num(1)));
	Step(TEXT("30_style_missing_widget"), TEXT("set_widget_style"), At().S(TEXT("widgetName"), TEXT("Nope")).S(TEXT("propertyName"), TEXT("RenderOpacity")).V(TEXT("value"), Num(1)));

	// bulk_set_widget_properties: a mixed batch and one where nothing lands.
	{
		TArray<TSharedPtr<FJsonValue>> Mixed;
		Mixed.Add(Entry(TEXT("Title"), TEXT("RenderOpacity"), Num(1.0)));
		Mixed.Add(Entry(TEXT("Nope"), TEXT("RenderOpacity"), Num(1.0)));
		Mixed.Add(Entry(TEXT("Title"), TEXT("NoSuchProperty"), Num(1.0)));
		Mixed.Add(MakeShared<FJsonValueString>(TEXT("not an object")));
		Mixed.Add(Entry(TEXT("Btn"), TEXT("RenderOpacity"), Num(0.5)));
		Step(TEXT("31_bulk_mixed"), TEXT("bulk_set_widget_properties"), At().A(TEXT("properties"), Mixed));
		TArray<TSharedPtr<FJsonValue>> None;
		None.Add(Entry(TEXT("Nope"), TEXT("RenderOpacity"), Num(1.0)));
		Step(TEXT("32_bulk_nothing_lands"), TEXT("bulk_set_widget_properties"), At().A(TEXT("properties"), None));
	}

	// clear_widget_binding: this blueprint has no designer bindings.
	Step(TEXT("33_clear_binding_none"), TEXT("clear_widget_binding"), At().S(TEXT("widgetName"), TEXT("Title")));
	Step(TEXT("34_clear_binding_property_none"), TEXT("clear_widget_binding"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("Text")));

	// Animations.
	auto Anim = [&]() { return At().S(TEXT("animationName"), TEXT("Fade")); };
	auto Track = [&]() { return Anim().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("RenderOpacity")); };
	Step(TEXT("40_create_animation"), TEXT("create_widget_animation"), Anim().N(TEXT("durationSeconds"), 2.0).N(TEXT("displayRate"), 30.0));
	Step(TEXT("41_create_animation_again"), TEXT("create_widget_animation"), Anim());
	Step(TEXT("42_create_animation_zero"), TEXT("create_widget_animation"), At().S(TEXT("animationName"), TEXT("Zero")).N(TEXT("durationSeconds"), 0.0));
	Step(TEXT("43_add_track"), TEXT("add_widget_animation_track"), Track());
	Step(TEXT("44_add_track_again"), TEXT("add_widget_animation_track"), Track());
	Step(TEXT("45_add_track_unknown_property"), TEXT("add_widget_animation_track"), Anim().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("NoSuchProperty")));
	Step(TEXT("46_add_track_missing_animation"), TEXT("add_widget_animation_track"), At().S(TEXT("animationName"), TEXT("Nope")).S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("RenderOpacity")));
	Step(TEXT("47_add_track_missing_widget"), TEXT("add_widget_animation_track"), Anim().S(TEXT("widgetName"), TEXT("Nope")).S(TEXT("propertyName"), TEXT("RenderOpacity")));
	Step(TEXT("48_add_key"), TEXT("add_widget_animation_key"), Track().N(TEXT("time"), 0.5).N(TEXT("value"), 0.25).S(TEXT("interpolation"), TEXT("linear")));
	Step(TEXT("49_add_key_replace"), TEXT("add_widget_animation_key"), Track().N(TEXT("time"), 0.5).N(TEXT("value"), 0.75));
	Step(TEXT("50_add_key_bad_channel"), TEXT("add_widget_animation_key"), Track().N(TEXT("time"), 1.0).N(TEXT("value"), 1.0).S(TEXT("channel"), TEXT("NoSuchChannel")));
	Step(TEXT("51_add_key_untracked_widget"), TEXT("add_widget_animation_key"), Anim().S(TEXT("widgetName"), TEXT("Btn")).S(TEXT("propertyName"), TEXT("RenderOpacity")).N(TEXT("time"), 0.5).N(TEXT("value"), 1.0));
	Step(TEXT("52_remove_key"), TEXT("remove_widget_animation_key"), Track().N(TEXT("time"), 0.5));
	Step(TEXT("53_remove_key_again"), TEXT("remove_widget_animation_key"), Track().N(TEXT("time"), 0.5));
	Step(TEXT("54_add_event_key"), TEXT("add_widget_animation_event_key"), Anim().S(TEXT("functionName"), TEXT("RemoveFromParent")).N(TEXT("time"), 1.0));
	Step(TEXT("55_add_event_key_replace"), TEXT("add_widget_animation_event_key"), Anim().S(TEXT("functionName"), TEXT("RemoveFromParent")).N(TEXT("time"), 1.0));
	Step(TEXT("56_add_event_key_with_params"), TEXT("add_widget_animation_event_key"), Anim().S(TEXT("functionName"), TEXT("SetVisibility")).N(TEXT("time"), 1.0));
	Step(TEXT("57_add_event_key_unknown_function"), TEXT("add_widget_animation_event_key"), Anim().S(TEXT("functionName"), TEXT("NoSuchFunction")).N(TEXT("time"), 1.0));
	Step(TEXT("58_remove_event_key"), TEXT("remove_widget_animation_event_key"), Anim().N(TEXT("time"), 1.0));
	Step(TEXT("59_remove_event_key_again"), TEXT("remove_widget_animation_event_key"), Anim().N(TEXT("time"), 1.0));
	Step(TEXT("60_remove_event_key_no_track"), TEXT("remove_widget_animation_event_key"), Anim().N(TEXT("time"), 1.0).S(TEXT("trackName"), TEXT("NoSuchTrack")));
	Step(TEXT("61_bind_event"), TEXT("bind_widget_animation_event"), Anim());
	Step(TEXT("62_bind_event_again"), TEXT("bind_widget_animation_event"), Anim());
	Step(TEXT("63_bind_started_tagged"), TEXT("bind_widget_animation_event"), Anim().S(TEXT("event"), TEXT("Started")).S(TEXT("userTag"), TEXT("Intro")));
	Step(TEXT("64_bind_bad_event"), TEXT("bind_widget_animation_event"), Anim().S(TEXT("event"), TEXT("Paused")));
	Step(TEXT("65_bind_missing_animation"), TEXT("bind_widget_animation_event"), At().S(TEXT("animationName"), TEXT("Nope")));
	Step(TEXT("66_unbind_event"), TEXT("unbind_widget_animation_event"), Anim());
	Step(TEXT("67_unbind_event_again"), TEXT("unbind_widget_animation_event"), Anim());
	Step(TEXT("68_unbind_started_tagged"), TEXT("unbind_widget_animation_event"), Anim().S(TEXT("event"), TEXT("Started")).S(TEXT("userTag"), TEXT("Intro")));
	Step(TEXT("69_remove_track"), TEXT("remove_widget_animation_track"), Track());
	Step(TEXT("70_remove_track_again"), TEXT("remove_widget_animation_track"), Track());
	Step(TEXT("71_remove_track_missing_animation"), TEXT("remove_widget_animation_track"), At().S(TEXT("animationName"), TEXT("Nope")).S(TEXT("widgetName"), TEXT("Title")).S(TEXT("propertyName"), TEXT("RenderOpacity")));
	Step(TEXT("72_delete_animation"), TEXT("delete_widget_animation"), Anim());
	Step(TEXT("73_delete_animation_again"), TEXT("delete_widget_animation"), Anim());

	// Navigation.
	auto Rule = [](const TCHAR* Widget, const TCHAR* Direction, const TCHAR* RuleName, const TCHAR* Focus)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("widgetName"), Widget);
		Out->SetStringField(TEXT("direction"), Direction);
		Out->SetStringField(TEXT("rule"), RuleName);
		if (Focus) Out->SetStringField(TEXT("widgetToFocus"), Focus);
		return MakeShared<FJsonValueObject>(Out);
	};
	Step(TEXT("80_set_navigation_single"), TEXT("set_widget_navigation"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("direction"), TEXT("Down"))
		.S(TEXT("rule"), TEXT("Explicit")).S(TEXT("widgetToFocus"), TEXT("Btn")));
	Step(TEXT("81_set_navigation_same"), TEXT("set_widget_navigation"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("direction"), TEXT("Down"))
		.S(TEXT("rule"), TEXT("Explicit")).S(TEXT("widgetToFocus"), TEXT("Btn")));
	{
		TArray<TSharedPtr<FJsonValue>> Rules;
		Rules.Add(Rule(TEXT("Btn"), TEXT("Up"), TEXT("Explicit"), TEXT("Title")));
		Rules.Add(Rule(TEXT("Btn"), TEXT("Next"), TEXT("Wrap"), nullptr));
		Step(TEXT("82_set_navigation_rules"), TEXT("set_widget_navigation"), At().A(TEXT("rules"), Rules));
		TArray<TSharedPtr<FJsonValue>> BadDirection;
		BadDirection.Add(Rule(TEXT("Btn"), TEXT("Sideways"), TEXT("Stop"), nullptr));
		Step(TEXT("83_set_navigation_bad_direction"), TEXT("set_widget_navigation"), At().A(TEXT("rules"), BadDirection));
		TArray<TSharedPtr<FJsonValue>> NoFocus;
		NoFocus.Add(Rule(TEXT("Btn"), TEXT("Left"), TEXT("Explicit"), nullptr));
		Step(TEXT("84_set_navigation_explicit_no_focus"), TEXT("set_widget_navigation"), At().A(TEXT("rules"), NoFocus));
		TArray<TSharedPtr<FJsonValue>> BadRule;
		BadRule.Add(Rule(TEXT("Btn"), TEXT("Left"), TEXT("Teleport"), nullptr));
		Step(TEXT("85_set_navigation_bad_rule"), TEXT("set_widget_navigation"), At().A(TEXT("rules"), BadRule));
	}
	const TSharedPtr<FJsonObject> Cleared = Step(TEXT("86_clear_navigation_direction"), TEXT("clear_widget_navigation"),
		At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("direction"), TEXT("Down")));
	Step(TEXT("87_clear_navigation_direction_again"), TEXT("clear_widget_navigation"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("direction"), TEXT("Down")));
	Step(TEXT("88_clear_navigation_none"), TEXT("clear_widget_navigation"), At().S(TEXT("widgetName"), TEXT("Label")));
	Step(TEXT("89_clear_navigation_bad_direction"), TEXT("clear_widget_navigation"), At().S(TEXT("widgetName"), TEXT("Title")).S(TEXT("direction"), TEXT("Sideways")));
	{
		const TSharedPtr<FJsonObject> Payload = RollbackPayload(Cleared);
		const TArray<TSharedPtr<FJsonValue>>* Previous = nullptr;
		TArray<TSharedPtr<FJsonValue>> PreviousCopy;
		if (Payload->TryGetArrayField(TEXT("previous"), Previous)) PreviousCopy = *Previous;
		Step(TEXT("90_restore_navigation"), TEXT("restore_widget_navigation"), At().A(TEXT("previous"), PreviousCopy));
		TArray<TSharedPtr<FJsonValue>> MissingWidget;
		TSharedPtr<FJsonObject> Ghost = MakeShared<FJsonObject>();
		Ghost->SetStringField(TEXT("widgetName"), TEXT("Nope"));
		MissingWidget.Add(MakeShared<FJsonValueObject>(Ghost));
		Step(TEXT("91_restore_navigation_missing_widget"), TEXT("restore_widget_navigation"), At().A(TEXT("previous"), MissingWidget));
	}
	Step(TEXT("92_clear_navigation_all"), TEXT("clear_widget_navigation"), At().S(TEXT("widgetName"), TEXT("Btn")));

	// Root operations.
	Step(TEXT("100_set_root_same"), TEXT("set_root_widget"), At().S(TEXT("widgetName"), TEXT("Root")));
	Step(TEXT("101_set_root_missing"), TEXT("set_root_widget"), At().S(TEXT("widgetName"), TEXT("Nope")));
	Step(TEXT("102_wrap_root"), TEXT("wrap_root_widget"), At().S(TEXT("wrapperClass"), TEXT("Overlay")).S(TEXT("wrapperName"), TEXT("Wrap")));
	Step(TEXT("103_wrap_root_not_panel"), TEXT("wrap_root_widget"), At().S(TEXT("wrapperClass"), TEXT("TextBlock")).S(TEXT("wrapperName"), TEXT("NotAPanel")));
	Step(TEXT("104_wrap_root_unknown_class"), TEXT("wrap_root_widget"), At().S(TEXT("wrapperClass"), TEXT("NoSuchWidgetClass")));
	Step(TEXT("105_set_root_back"), TEXT("set_root_widget"), At().S(TEXT("widgetName"), TEXT("Root")));

	// remove_widget.
	Step(TEXT("110_remove_label"), TEXT("remove_widget"), At().S(TEXT("widgetName"), TEXT("Label")));
	Step(TEXT("111_remove_label_again"), TEXT("remove_widget"), At().S(TEXT("widgetName"), TEXT("Label")));
	Step(TEXT("112_remove_canvas"), TEXT("remove_widget"), At().S(TEXT("widgetName"), TEXT("Canvas")));
	Step(TEXT("113_remove_root"), TEXT("remove_widget"), At().S(TEXT("widgetName"), TEXT("Root")));
	Step(TEXT("114_wrap_root_empty_tree"), TEXT("wrap_root_widget"), At().S(TEXT("wrapperClass"), TEXT("Overlay")));

	FString Note;
	const TArray<FString> Breaks = UEMCPCharacterisation::CompareWithFixture(Run, TEXT("widget-responses"), Note);
	if (!Note.IsEmpty()) AddInfo(Note);
	for (const FString& Break : Breaks) AddError(Break);
	return Breaks.Num() == 0;
}

#endif // WITH_DEV_AUTOMATION_TESTS
