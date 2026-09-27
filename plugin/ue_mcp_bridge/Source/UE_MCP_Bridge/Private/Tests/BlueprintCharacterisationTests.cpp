// The response of every mutating Blueprint handler in BlueprintHandlers.cpp,
// BlueprintHandlers_Properties.cpp and BlueprintHandlers_Functions.cpp, and of
// the Blueprint component adders in the gameplay category, recorded before the
// F1 family port and held to the contract after it (plans/domain-design.md 0
// and 8). One scripted run covers each handler's success path, its no-op path
// where it has one, and the refusals a caller meets most. The fixture is
// tests/golden/cpp/blueprint-responses.json; a run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Handlers/Gameplay/GameplayHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Tests/MCPCharacterisation.h"
#include "Tests/MCPScopedTestMount.h"

/** Compiles, missing assets and refused loads log on purpose here; the responses carry the outcome. */
class FMCPBlueprintCharacterisationTestBase : public FAutomationTestBase
{
public:
	FMCPBlueprintCharacterisationTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogBlueprint"));
		Categories.Add(TEXT("LogK2Compiler"));
		Categories.Add(TEXT("LogBlueprintUserMessages"));
		Categories.Add(TEXT("LogUObjectGlobals"));
		Categories.Add(TEXT("LogLinker"));
		Categories.Add(TEXT("LogStreaming"));
		Categories.Add(TEXT("LogPackageName"));
		Categories.Add(TEXT("LogUE_MCP"));
		return Categories;
	}
};

namespace MCPBlueprintCharacterisation
{
	/** Build a params object from chained key/value pairs. */
	struct FArgs
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		FArgs& S(const TCHAR* Key, const FString& Value) { Obj->SetStringField(Key, Value); return *this; }
		FArgs& N(const TCHAR* Key, double Value) { Obj->SetNumberField(Key, Value); return *this; }
		FArgs& B(const TCHAR* Key, bool Value) { Obj->SetBoolField(Key, Value); return *this; }
		FArgs& O(const TCHAR* Key, const TSharedPtr<FJsonObject>& Value) { Obj->SetObjectField(Key, Value); return *this; }
		FArgs& A(const TCHAR* Key, const TArray<TSharedPtr<FJsonValue>>& Value) { Obj->SetArrayField(Key, Value); return *this; }
	};

	TArray<TSharedPtr<FJsonValue>> Strings(std::initializer_list<const TCHAR*> Items)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TCHAR* Item : Items) Out.Add(MakeShared<FJsonValueString>(Item));
		return Out;
	}

	TSharedPtr<FJsonObject> ParamSpec(const TCHAR* Name, const TCHAR* Type)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("name"), Name);
		Out->SetStringField(TEXT("type"), Type);
		return Out;
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPBlueprintCharacterisationTest, FMCPBlueprintCharacterisationTestBase,
	"UE.MCP.Blueprint.Characterisation.Responses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace MCPBlueprintCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPBlueprintChar/"), TEXT("UEMCPBlueprintChar"));
	const FString BP = Mount.RootPath + TEXT("BP_Char");
	const FString Reparent = Mount.RootPath + TEXT("BP_CharReparent");
	const FString Interface = Mount.RootPath + TEXT("BPI_Char");
	const FString InterfaceClass = Interface + TEXT(".BPI_Char_C");
	const FString Missing = Mount.RootPath + TEXT("BP_Nope");

	FMCPHandlerRegistry Registry;
	FBlueprintHandlers::RegisterHandlers(Registry);
	FGameplayHandlers::RegisterHandlers(Registry);

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
	auto At = [&]() { return FArgs().S(TEXT("assetPath"), BP); };
	auto Owner = [&]() { return FArgs().S(TEXT("blueprintPath"), BP); };

	// Creation, and the fixtures everything else edits.
	Step(TEXT("001_create_blueprint"), TEXT("create_blueprint"), FArgs().S(TEXT("assetPath"), BP).S(TEXT("parentClass"), TEXT("Actor")));
	Step(TEXT("002_create_blueprint_existing"), TEXT("create_blueprint"), FArgs().S(TEXT("assetPath"), BP));
	Step(TEXT("003_create_blueprint_existing_error"), TEXT("create_blueprint"), FArgs().S(TEXT("assetPath"), BP).S(TEXT("onConflict"), TEXT("error")));
	Step(TEXT("004_create_blueprint_bad_parent"), TEXT("create_blueprint"), FArgs().S(TEXT("assetPath"), Mount.RootPath + TEXT("BP_BadParent")).S(TEXT("parentClass"), TEXT("NoSuchParentClassAnywhere")));
	Step(TEXT("005_create_blueprint_reparent_fixture"), TEXT("create_blueprint"), FArgs().S(TEXT("assetPath"), Reparent).S(TEXT("parentClass"), TEXT("Actor")));
	Step(TEXT("006_create_blueprint_interface"), TEXT("create_blueprint_interface"), FArgs().S(TEXT("assetPath"), Interface));
	Step(TEXT("007_create_blueprint_interface_existing"), TEXT("create_blueprint_interface"), FArgs().S(TEXT("assetPath"), Interface));
	Step(TEXT("008_compile_blueprint"), TEXT("compile_blueprint"), At());
	Step(TEXT("009_compile_blueprint_missing"), TEXT("compile_blueprint"), FArgs().S(TEXT("assetPath"), Missing));

	// Variables.
	Step(TEXT("010_add_variable"), TEXT("add_variable"), At().S(TEXT("name"), TEXT("Health")).S(TEXT("varType"), TEXT("Float")));
	Step(TEXT("011_add_variable_existing"), TEXT("add_variable"), At().S(TEXT("name"), TEXT("Health")).S(TEXT("varType"), TEXT("Float")));
	Step(TEXT("012_add_variable_existing_error"), TEXT("add_variable"), At().S(TEXT("name"), TEXT("Health")).S(TEXT("onConflict"), TEXT("error")));
	Step(TEXT("013_add_variable_bad_type"), TEXT("add_variable"), At().S(TEXT("name"), TEXT("Bad")).S(TEXT("varType"), TEXT("NoSuchTypeAnywhere")));
	Step(TEXT("014_add_variable_container"), TEXT("add_variable"), At().S(TEXT("name"), TEXT("Scores")).S(TEXT("varType"), TEXT("int[]")));
	Step(TEXT("015_add_variable_missing_bp"), TEXT("add_variable"), FArgs().S(TEXT("assetPath"), Missing).S(TEXT("name"), TEXT("X")));
	Step(TEXT("016_set_variable_properties"), TEXT("set_variable_properties"), At().S(TEXT("name"), TEXT("Health"))
		.S(TEXT("editFlag"), TEXT("EditAnywhere")).S(TEXT("category"), TEXT("Stats")).S(TEXT("tooltip"), TEXT("Hit points")).B(TEXT("exposeOnSpawn"), true));
	Step(TEXT("017_set_variable_properties_again"), TEXT("set_variable_properties"), At().S(TEXT("name"), TEXT("Health"))
		.S(TEXT("editFlag"), TEXT("EditAnywhere")).S(TEXT("category"), TEXT("Stats")).S(TEXT("tooltip"), TEXT("Hit points")).B(TEXT("exposeOnSpawn"), true));
	Step(TEXT("018_set_variable_properties_bad_flag"), TEXT("set_variable_properties"), At().S(TEXT("name"), TEXT("Health")).S(TEXT("editFlag"), TEXT("Sideways")));
	Step(TEXT("019_set_variable_properties_both_flags"), TEXT("set_variable_properties"), At().S(TEXT("name"), TEXT("Health"))
		.S(TEXT("editFlag"), TEXT("none")).B(TEXT("instanceEditable"), true));
	Step(TEXT("020_set_variable_properties_missing_var"), TEXT("set_variable_properties"), At().S(TEXT("name"), TEXT("Nope")).B(TEXT("private"), true));
	Step(TEXT("021_set_variable_default"), TEXT("set_variable_default"), At().S(TEXT("name"), TEXT("Health")).S(TEXT("value"), TEXT("100")));
	Step(TEXT("022_set_variable_default_again"), TEXT("set_variable_default"), At().S(TEXT("name"), TEXT("Health")).S(TEXT("value"), TEXT("100")));
	Step(TEXT("023_set_variable_default_bad_value"), TEXT("set_variable_default"), At().S(TEXT("name"), TEXT("Health")).S(TEXT("value"), TEXT("banana")));
	Step(TEXT("024_set_variable_default_missing_var"), TEXT("set_variable_default"), At().S(TEXT("name"), TEXT("Nope")).S(TEXT("value"), TEXT("1")));

	// Components.
	Step(TEXT("030_add_component_scene"), TEXT("add_component"), At().S(TEXT("componentClass"), TEXT("SceneComponent")).S(TEXT("componentName"), TEXT("Pivot")));
	Step(TEXT("031_add_component_mesh"), TEXT("add_component"), At().S(TEXT("componentClass"), TEXT("StaticMeshComponent")).S(TEXT("componentName"), TEXT("Mesh")));
	Step(TEXT("032_add_component_capsule"), TEXT("add_component"), At().S(TEXT("componentClass"), TEXT("CapsuleComponent")).S(TEXT("componentName"), TEXT("Capsule")));
	Step(TEXT("033_add_component_existing"), TEXT("add_component"), At().S(TEXT("componentClass"), TEXT("StaticMeshComponent")).S(TEXT("componentName"), TEXT("Mesh")));
	Step(TEXT("034_add_component_existing_error"), TEXT("add_component"), At().S(TEXT("componentClass"), TEXT("StaticMeshComponent"))
		.S(TEXT("componentName"), TEXT("Mesh")).S(TEXT("onConflict"), TEXT("error")));
	Step(TEXT("035_add_component_bad_class"), TEXT("add_component"), At().S(TEXT("componentClass"), TEXT("NoSuchComponentClassAnywhere")).S(TEXT("componentName"), TEXT("Bad")));
	Step(TEXT("036_add_component_child"), TEXT("add_component"), At().S(TEXT("componentClass"), TEXT("SceneComponent"))
		.S(TEXT("componentName"), TEXT("Child")).S(TEXT("parentComponent"), TEXT("Pivot")));
	Step(TEXT("037_set_component_property"), TEXT("set_blueprint_component_property"), At().S(TEXT("componentName"), TEXT("Mesh"))
		.S(TEXT("propertyName"), TEXT("CastShadow")).B(TEXT("value"), false));
	Step(TEXT("038_set_component_property_unknown"), TEXT("set_blueprint_component_property"), At().S(TEXT("componentName"), TEXT("Mesh"))
		.S(TEXT("propertyName"), TEXT("NoSuchProperty")).B(TEXT("value"), false));
	Step(TEXT("039_set_component_property_missing_component"), TEXT("set_blueprint_component_property"), At().S(TEXT("componentName"), TEXT("Nope"))
		.S(TEXT("propertyName"), TEXT("CastShadow")).B(TEXT("value"), false));
	Step(TEXT("040_set_component_override_materials"), TEXT("set_component_override_materials"), At().S(TEXT("componentName"), TEXT("Mesh"))
		.A(TEXT("materialPaths"), Strings({ TEXT("/Engine/EngineMaterials/WorldGridMaterial") })));
	Step(TEXT("041_set_component_override_materials_missing_material"), TEXT("set_component_override_materials"), At().S(TEXT("componentName"), TEXT("Mesh"))
		.A(TEXT("materialPaths"), Strings({ TEXT("/Engine/NoSuchMaterialAnywhere") })));
	Step(TEXT("042_set_component_override_materials_not_mesh"), TEXT("set_component_override_materials"), At().S(TEXT("componentName"), TEXT("Pivot"))
		.A(TEXT("materialPaths"), Strings({ TEXT("/Engine/EngineMaterials/WorldGridMaterial") })));
	Step(TEXT("043_set_capsule_size"), TEXT("set_capsule_size"), At().S(TEXT("componentName"), TEXT("Capsule")).N(TEXT("halfHeight"), 60.0).N(TEXT("radius"), 25.0));
	Step(TEXT("044_set_capsule_size_again"), TEXT("set_capsule_size"), At().S(TEXT("componentName"), TEXT("Capsule")).N(TEXT("halfHeight"), 60.0).N(TEXT("radius"), 25.0));
	Step(TEXT("045_set_capsule_size_not_capsule"), TEXT("set_capsule_size"), At().S(TEXT("componentName"), TEXT("Mesh")).N(TEXT("radius"), 25.0));
	Step(TEXT("046_reparent_component"), TEXT("reparent_component"), At().S(TEXT("componentName"), TEXT("Mesh")).S(TEXT("newParent"), TEXT("Pivot")));
	Step(TEXT("047_reparent_component_again"), TEXT("reparent_component"), At().S(TEXT("componentName"), TEXT("Mesh")).S(TEXT("newParent"), TEXT("Pivot")));
	Step(TEXT("048_reparent_component_missing_parent"), TEXT("reparent_component"), At().S(TEXT("componentName"), TEXT("Mesh")).S(TEXT("newParent"), TEXT("Nope")));
	Step(TEXT("049_reparent_component_missing_child"), TEXT("reparent_component"), At().S(TEXT("componentName"), TEXT("Nope")).S(TEXT("newParent"), TEXT("Pivot")));

	// Class defaults and tick settings.
	Step(TEXT("050_set_class_default"), TEXT("set_class_default"), At().S(TEXT("propertyName"), TEXT("InitialLifeSpan")).N(TEXT("value"), 5.0));
	Step(TEXT("051_set_class_default_again"), TEXT("set_class_default"), At().S(TEXT("propertyName"), TEXT("InitialLifeSpan")).N(TEXT("value"), 5.0));
	Step(TEXT("052_set_class_default_unknown"), TEXT("set_class_default"), At().S(TEXT("propertyName"), TEXT("NoSuchProperty")).N(TEXT("value"), 1.0));
	Step(TEXT("053_set_class_default_bad_value"), TEXT("set_class_default"), At().S(TEXT("propertyName"), TEXT("InitialLifeSpan")).S(TEXT("value"), TEXT("banana")));
	Step(TEXT("054_set_actor_tick_settings"), TEXT("set_actor_tick_settings"), At().B(TEXT("bCanEverTick"), true).B(TEXT("bStartWithTickEnabled"), false).N(TEXT("TickInterval"), 0.25));
	Step(TEXT("055_set_actor_tick_settings_again"), TEXT("set_actor_tick_settings"), At().B(TEXT("bCanEverTick"), true).B(TEXT("bStartWithTickEnabled"), false).N(TEXT("TickInterval"), 0.25));
	Step(TEXT("056_set_actor_tick_settings_missing_bp"), TEXT("set_actor_tick_settings"), FArgs().S(TEXT("assetPath"), Missing).B(TEXT("bCanEverTick"), true));

	// Timelines.
	{
		TArray<TSharedPtr<FJsonValue>> Keys;
		for (const double Time : { 0.0, 1.0 })
		{
			TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
			Key->SetNumberField(TEXT("time"), Time);
			Key->SetNumberField(TEXT("value"), Time * 2.0);
			Keys.Add(MakeShared<FJsonValueObject>(Key));
		}
		Step(TEXT("060_add_timeline_track"), TEXT("add_timeline_track"), At().S(TEXT("timelineName"), TEXT("Fade")).S(TEXT("trackName"), TEXT("Alpha"))
			.S(TEXT("trackType"), TEXT("float")).A(TEXT("keyframes"), Keys));
	}
	Step(TEXT("061_add_timeline_track_existing"), TEXT("add_timeline_track"), At().S(TEXT("timelineName"), TEXT("Fade")).S(TEXT("trackName"), TEXT("Alpha")));
	Step(TEXT("062_add_timeline_track_event"), TEXT("add_timeline_track"), At().S(TEXT("timelineName"), TEXT("Fade")).S(TEXT("trackName"), TEXT("Ping")).S(TEXT("trackType"), TEXT("event")));
	Step(TEXT("063_add_timeline_track_bad_type"), TEXT("add_timeline_track"), At().S(TEXT("timelineName"), TEXT("Fade")).S(TEXT("trackName"), TEXT("Bad")).S(TEXT("trackType"), TEXT("sideways")));

	// Functions, parameters and local variables.
	Step(TEXT("070_create_function"), TEXT("create_function"), At().S(TEXT("functionName"), TEXT("DoThing")));
	Step(TEXT("071_create_function_existing"), TEXT("create_function"), At().S(TEXT("functionName"), TEXT("DoThing")));
	Step(TEXT("072_create_function_existing_error"), TEXT("create_function"), At().S(TEXT("functionName"), TEXT("DoThing")).S(TEXT("onConflict"), TEXT("error")));
	Step(TEXT("073_add_function_parameter"), TEXT("add_function_parameter"), At().S(TEXT("functionName"), TEXT("DoThing"))
		.S(TEXT("parameterName"), TEXT("Amount")).S(TEXT("parameterType"), TEXT("Float")));
	Step(TEXT("074_add_function_parameter_output"), TEXT("add_function_parameter"), At().S(TEXT("functionName"), TEXT("DoThing"))
		.S(TEXT("parameterName"), TEXT("Ok")).S(TEXT("parameterType"), TEXT("bool")).B(TEXT("isOutput"), true));
	Step(TEXT("075_add_function_parameter_again"), TEXT("add_function_parameter"), At().S(TEXT("functionName"), TEXT("DoThing"))
		.S(TEXT("parameterName"), TEXT("Amount")).S(TEXT("parameterType"), TEXT("Float")));
	Step(TEXT("076_add_function_parameter_bad_type"), TEXT("add_function_parameter"), At().S(TEXT("functionName"), TEXT("DoThing"))
		.S(TEXT("parameterName"), TEXT("Bad")).S(TEXT("parameterType"), TEXT("NoSuchTypeAnywhere")));
	Step(TEXT("077_add_function_parameter_missing_function"), TEXT("add_function_parameter"), At().S(TEXT("functionName"), TEXT("Nope"))
		.S(TEXT("parameterName"), TEXT("X")));
	Step(TEXT("078_add_local_variable"), TEXT("add_local_variable"), At().S(TEXT("functionName"), TEXT("DoThing")).S(TEXT("name"), TEXT("Counter")).S(TEXT("varType"), TEXT("int")));
	Step(TEXT("079_add_local_variable_existing"), TEXT("add_local_variable"), At().S(TEXT("functionName"), TEXT("DoThing")).S(TEXT("name"), TEXT("Counter")).S(TEXT("varType"), TEXT("int")));
	Step(TEXT("080_add_local_variable_missing_function"), TEXT("add_local_variable"), At().S(TEXT("functionName"), TEXT("Nope")).S(TEXT("name"), TEXT("X")));
	Step(TEXT("081_add_local_variable_bad_type"), TEXT("add_local_variable"), At().S(TEXT("functionName"), TEXT("DoThing")).S(TEXT("name"), TEXT("Y")).S(TEXT("varType"), TEXT("NoSuchTypeAnywhere")));
	Step(TEXT("082_rename_function"), TEXT("rename_function"), At().S(TEXT("oldName"), TEXT("DoThing")).S(TEXT("newName"), TEXT("DoOtherThing")));
	Step(TEXT("083_rename_function_missing"), TEXT("rename_function"), At().S(TEXT("oldName"), TEXT("DoThing")).S(TEXT("newName"), TEXT("DoOtherThing")));
	Step(TEXT("084_create_function_to_delete"), TEXT("create_function"), At().S(TEXT("functionName"), TEXT("Disposable")));
	Step(TEXT("085_delete_function"), TEXT("delete_function"), At().S(TEXT("functionName"), TEXT("Disposable")));
	Step(TEXT("086_delete_function_again"), TEXT("delete_function"), At().S(TEXT("functionName"), TEXT("Disposable")));
	Step(TEXT("087_override_function"), TEXT("override_function"), At().S(TEXT("functionName"), TEXT("ReceiveBeginPlay")));
	Step(TEXT("088_override_function_again"), TEXT("override_function"), At().S(TEXT("functionName"), TEXT("ReceiveBeginPlay")));
	Step(TEXT("089_override_function_as_function"), TEXT("override_function"), At().S(TEXT("functionName"), TEXT("ReceiveTick")).B(TEXT("preferFunction"), true));

	// Interfaces and dispatchers.
	Step(TEXT("090_add_blueprint_interface"), TEXT("add_blueprint_interface"), Owner().S(TEXT("interfacePath"), InterfaceClass));
	Step(TEXT("091_add_blueprint_interface_existing"), TEXT("add_blueprint_interface"), Owner().S(TEXT("interfacePath"), InterfaceClass));
	Step(TEXT("092_add_blueprint_interface_missing"), TEXT("add_blueprint_interface"), Owner().S(TEXT("interfacePath"), Mount.RootPath + TEXT("BPI_Nope.BPI_Nope_C")));
	Step(TEXT("093_add_blueprint_interface_missing_bp"), TEXT("add_blueprint_interface"), FArgs().S(TEXT("blueprintPath"), Missing).S(TEXT("interfacePath"), InterfaceClass));
	{
		TArray<TSharedPtr<FJsonValue>> Signature;
		Signature.Add(MakeShared<FJsonValueObject>(ParamSpec(TEXT("Damage"), TEXT("float"))));
		Signature.Add(MakeShared<FJsonValueObject>(ParamSpec(TEXT("Source"), TEXT("Actor"))));
		Step(TEXT("094_add_event_dispatcher"), TEXT("add_event_dispatcher"), Owner().S(TEXT("name"), TEXT("OnHit")).A(TEXT("parameters"), Signature));
	}
	Step(TEXT("095_add_event_dispatcher_existing"), TEXT("add_event_dispatcher"), Owner().S(TEXT("name"), TEXT("OnHit")));
	{
		TArray<TSharedPtr<FJsonValue>> Bad;
		Bad.Add(MakeShared<FJsonValueObject>(ParamSpec(TEXT("X"), TEXT("NoSuchTypeAnywhere"))));
		Step(TEXT("096_add_event_dispatcher_bad_type"), TEXT("add_event_dispatcher"), Owner().S(TEXT("name"), TEXT("OnBad")).A(TEXT("parameters"), Bad));
	}
	Step(TEXT("097_add_event_dispatcher_plain"), TEXT("add_event_dispatcher"), Owner().S(TEXT("name"), TEXT("OnPing")));

	// Gameplay components on a Blueprint.
	Step(TEXT("100_add_perception_component"), TEXT("add_perception_component"), Owner().A(TEXT("senses"), Strings({ TEXT("Sight"), TEXT("Hearing") })));
	Step(TEXT("101_add_perception_component_again"), TEXT("add_perception_component"), Owner().A(TEXT("senses"), Strings({ TEXT("Sight"), TEXT("Hearing") })));
	Step(TEXT("102_add_perception_component_bad_sense"), TEXT("add_perception_component"), Owner().A(TEXT("senses"), Strings({ TEXT("NoSuchSense") })));
	{
		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetNumberField(TEXT("SightRadius"), 1500.0);
		Step(TEXT("103_configure_ai_perception_sense"), TEXT("configure_ai_perception_sense"), Owner().S(TEXT("senseType"), TEXT("Sight")).O(TEXT("settings"), Settings));
	}
	{
		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetNumberField(TEXT("NoSuchSetting"), 1.0);
		Step(TEXT("104_configure_ai_perception_sense_bad_setting"), TEXT("configure_ai_perception_sense"), Owner().S(TEXT("senseType"), TEXT("Sight")).O(TEXT("settings"), Settings));
	}
	Step(TEXT("105_configure_ai_perception_sense_bad_type"), TEXT("configure_ai_perception_sense"), Owner().S(TEXT("senseType"), TEXT("Smell")));
	Step(TEXT("106_remove_sense"), TEXT("remove_sense"), Owner().S(TEXT("senseType"), TEXT("Hearing")));
	Step(TEXT("107_remove_sense_again"), TEXT("remove_sense"), Owner().S(TEXT("senseType"), TEXT("Hearing")));
	Step(TEXT("108_remove_sense_bad_index"), TEXT("remove_sense"), Owner().N(TEXT("index"), 9));
	Step(TEXT("109_add_state_tree_component"), TEXT("add_state_tree_component"), Owner());
	Step(TEXT("110_add_state_tree_component_again"), TEXT("add_state_tree_component"), Owner());
	Step(TEXT("111_add_smart_object_component"), TEXT("add_smart_object_component"), Owner());
	Step(TEXT("112_add_smart_object_component_again"), TEXT("add_smart_object_component"), Owner());
	Step(TEXT("113_add_state_tree_component_missing_bp"), TEXT("add_state_tree_component"), FArgs().S(TEXT("blueprintPath"), Missing));

	// Reparenting a whole Blueprint, and the component-record flushes.
	Step(TEXT("120_reparent_blueprint"), TEXT("reparent_blueprint"), FArgs().S(TEXT("assetPath"), Reparent).S(TEXT("parentClass"), TEXT("Pawn")));
	Step(TEXT("121_reparent_blueprint_again"), TEXT("reparent_blueprint"), FArgs().S(TEXT("assetPath"), Reparent).S(TEXT("parentClass"), TEXT("Pawn")));
	Step(TEXT("122_reparent_blueprint_bad_class"), TEXT("reparent_blueprint"), FArgs().S(TEXT("assetPath"), Reparent).S(TEXT("parentClass"), TEXT("NoSuchParentClassAnywhere")));
	Step(TEXT("123_reparent_blueprint_back"), TEXT("reparent_blueprint"), FArgs().S(TEXT("assetPath"), Reparent).S(TEXT("parentClass"), TEXT("Actor")));
	Step(TEXT("124_flush_inheritable_component_handler"), TEXT("flush_inheritable_component_handler"), At());
	Step(TEXT("125_flush_blueprint_component_templates"), TEXT("flush_blueprint_component_templates"), At());
	Step(TEXT("126_flush_blueprint_component_templates_missing"), TEXT("flush_blueprint_component_templates"), FArgs().S(TEXT("assetPath"), Missing));

	// Removals last, each with its no-op repeat.
	Step(TEXT("130_remove_component"), TEXT("remove_component"), At().S(TEXT("componentName"), TEXT("Child")));
	Step(TEXT("131_remove_component_again"), TEXT("remove_component"), At().S(TEXT("componentName"), TEXT("Child")));
	Step(TEXT("132_remove_component_with_children"), TEXT("remove_component"), At().S(TEXT("componentName"), TEXT("Mesh")));
	Step(TEXT("133_delete_variable"), TEXT("delete_variable"), At().S(TEXT("name"), TEXT("Scores")));
	Step(TEXT("134_delete_variable_again"), TEXT("delete_variable"), At().S(TEXT("name"), TEXT("Scores")));
	Step(TEXT("135_delete_variable_missing_bp"), TEXT("delete_variable"), FArgs().S(TEXT("assetPath"), Missing).S(TEXT("name"), TEXT("X")));
	Step(TEXT("136_compile_blueprint_after"), TEXT("compile_blueprint"), At());

	FString Note;
	const TArray<FString> Breaks = UEMCPCharacterisation::CompareWithFixture(Run, TEXT("blueprint-responses"), Note);
	if (!Note.IsEmpty()) AddInfo(Note);
	for (const FString& Break : Breaks) AddError(Break);
	return Breaks.Num() == 0;
}

#endif // WITH_DEV_AUTOMATION_TESTS
