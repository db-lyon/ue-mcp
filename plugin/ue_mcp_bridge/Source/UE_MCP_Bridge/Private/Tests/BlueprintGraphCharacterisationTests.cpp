// The response of every Blueprint graph and member mutator, recorded before the
// F1 port and held to the contract after it (plans/domain-design.md 0 and 8).
// One scripted run covers each handler's success path, its no-op path where it
// has one, and the refusals a caller meets most. The fixture is
// tests/golden/cpp/blueprint-graph-responses.json; a run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "Misc/AutomationTest.h"
#include "Tests/MCPCharacterisation.h"
#include "Tests/MCPScopedTestMount.h"

/** Compiles and refusals log on purpose here; the responses carry the outcome. */
class FMCPBlueprintGraphCharTestBase : public FAutomationTestBase
{
public:
	FMCPBlueprintGraphCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual bool SuppressLogErrors() override { return true; }
	virtual bool SuppressLogWarnings() override { return true; }

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Append({ TEXT("LogBlueprint"), TEXT("LogK2Compiler"), TEXT("LogBlueprintUserMessages"),
			TEXT("LogBlueprintEditor"), TEXT("LogEdGraph"), TEXT("LogUObjectGlobals"), TEXT("LogLinker"),
			TEXT("LogExporter"), TEXT("LogSavePackage"), TEXT("LogUE_MCP"), TEXT("LogMCP") });
		return Categories;
	}
};

namespace MCPBlueprintGraphCharacterisation
{
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

	TSharedPtr<FJsonValue> Str(const FString& Value) { return MakeShared<FJsonValueString>(Value); }

	/** One {name, type} signature entry. */
	TSharedPtr<FJsonValue> Param(const FString& Name, const FString& Type)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("type"), Type);
		return MakeShared<FJsonValueObject>(Obj);
	}

	TSharedPtr<FJsonValue> Link(const FString& SourceNode, const FString& SourcePin, const FString& TargetNode, const FString& TargetPin)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("sourceNode"), SourceNode);
		Obj->SetStringField(TEXT("sourcePin"), SourcePin);
		Obj->SetStringField(TEXT("targetNode"), TargetNode);
		Obj->SetStringField(TEXT("targetPin"), TargetPin);
		return MakeShared<FJsonValueObject>(Obj);
	}

	/** The GUID of the entry node of function graph Name, as the handlers print it. */
	FString FunctionEntryId(const FString& AssetPath, const FString& Name)
	{
		UBlueprint* Blueprint = LoadAssetByPath<UBlueprint>(AssetPath);
		if (!Blueprint) return FString();
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (!Graph || Graph->GetName() != Name) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->IsA<UK2Node_FunctionEntry>()) return Node->NodeGuid.ToString();
			}
		}
		return FString();
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPBlueprintGraphCharacterisationTest, FMCPBlueprintGraphCharTestBase,
	"UE.MCP.Blueprint.Characterisation.GraphResponses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintGraphCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace MCPBlueprintGraphCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPBlueprintGraphChar/"), TEXT("UEMCPBlueprintGraphChar"));
	const FString BP = Mount.RootPath + TEXT("BP_GraphChar");
	const FString BPI = Mount.RootPath + TEXT("BPI_GraphChar");
	const FString BPIClass = BPI + TEXT(".BPI_GraphChar_C");
	const FString Missing = Mount.RootPath + TEXT("BP_Nope");

	FMCPHandlerRegistry Registry;
	FBlueprintHandlers::RegisterHandlers(Registry);

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

	// Fixtures, built through the bridge itself.
	Step(TEXT("01_create_blueprint"), TEXT("create_blueprint"), FArgs().S(TEXT("assetPath"), BP).S(TEXT("parentClass"), TEXT("Actor")));
	Step(TEXT("02_create_interface"), TEXT("create_blueprint_interface"), FArgs().S(TEXT("assetPath"), BPI));
	Step(TEXT("03_add_variable"), TEXT("add_variable"), At().S(TEXT("name"), TEXT("Health")).S(TEXT("varType"), TEXT("float")));
	Step(TEXT("04_create_function"), TEXT("create_function"), At().S(TEXT("functionName"), TEXT("DoThing")));
	Step(TEXT("05_create_function_doomed"), TEXT("create_function"), At().S(TEXT("functionName"), TEXT("DoomedFn")));
	Step(TEXT("06_add_dispatcher"), TEXT("add_event_dispatcher"), FArgs().S(TEXT("blueprintPath"), BP).S(TEXT("name"), TEXT("OnChanged")));
	Step(TEXT("07_add_local_variable"), TEXT("add_local_variable"), At().S(TEXT("functionName"), TEXT("DoThing")).S(TEXT("name"), TEXT("Counter")).S(TEXT("varType"), TEXT("int")));
	Step(TEXT("08_add_interface"), TEXT("add_blueprint_interface"), FArgs().S(TEXT("blueprintPath"), BP).S(TEXT("interfacePath"), BPIClass));

	// add_node.
	TSharedPtr<FJsonObject> PrintParams = MakeShared<FJsonObject>();
	PrintParams->SetStringField(TEXT("functionName"), TEXT("PrintString"));
	PrintParams->SetStringField(TEXT("targetClass"), TEXT("KismetSystemLibrary"));
	const FString Print = Field(Step(TEXT("10_add_node_print"), TEXT("add_node"), At().S(TEXT("nodeClass"), TEXT("CallFunction")).O(TEXT("nodeParams"), PrintParams)), TEXT("nodeId"));
	const FString Branch = Field(Step(TEXT("11_add_node_branch"), TEXT("add_node"), At().S(TEXT("nodeClass"), TEXT("Branch"))), TEXT("nodeId"));
	const FString Sequence = Field(Step(TEXT("12_add_node_sequence"), TEXT("add_node"), At().S(TEXT("nodeClass"), TEXT("Sequence"))), TEXT("nodeId"));
	Step(TEXT("13_add_node_function_graph"), TEXT("add_node"), At().S(TEXT("graphName"), TEXT("DoThing")).S(TEXT("nodeClass"), TEXT("Branch")));
	Step(TEXT("14_add_node_unknown_class"), TEXT("add_node"), At().S(TEXT("nodeClass"), TEXT("NoSuchNodeClass")));
	Step(TEXT("15_add_node_missing_graph"), TEXT("add_node"), At().S(TEXT("graphName"), TEXT("NoSuchGraph")).S(TEXT("nodeClass"), TEXT("Branch")));
	Step(TEXT("16_add_node_missing_blueprint"), TEXT("add_node"), FArgs().S(TEXT("assetPath"), Missing).S(TEXT("nodeClass"), TEXT("Branch")));

	// connect_pins.
	auto Connect = [&](const FString& SourceNode, const FString& SourcePin, const FString& TargetNode, const FString& TargetPin)
	{
		return At().S(TEXT("sourceNodeId"), SourceNode).S(TEXT("sourcePin"), SourcePin).S(TEXT("targetNodeId"), TargetNode).S(TEXT("targetPin"), TargetPin);
	};
	Step(TEXT("20_connect"), TEXT("connect_pins"), Connect(Branch, TEXT("then"), Print, TEXT("execute")));
	Step(TEXT("21_connect_again"), TEXT("connect_pins"), Connect(Branch, TEXT("then"), Print, TEXT("execute")));
	Step(TEXT("22_connect_break_existing"), TEXT("connect_pins"), Connect(Sequence, TEXT("then_0"), Print, TEXT("execute")).B(TEXT("breakExistingTarget"), true));
	Step(TEXT("23_connect_bad_source_pin"), TEXT("connect_pins"), Connect(Branch, TEXT("NoPin"), Print, TEXT("execute")));
	Step(TEXT("24_connect_bad_target_pin"), TEXT("connect_pins"), Connect(Branch, TEXT("then"), Print, TEXT("NoPin")));
	Step(TEXT("25_connect_missing_source"), TEXT("connect_pins"), Connect(TEXT("NoSuchNode"), TEXT("then"), Print, TEXT("execute")));
	Step(TEXT("26_connect_missing_target"), TEXT("connect_pins"), Connect(Branch, TEXT("then"), TEXT("NoSuchNode"), TEXT("execute")));
	Step(TEXT("27_connect_incompatible"), TEXT("connect_pins"), Connect(Branch, TEXT("then"), Print, TEXT("InString")));
	Step(TEXT("28_connect_missing_graph"), TEXT("connect_pins"), Connect(Branch, TEXT("then"), Print, TEXT("execute")).S(TEXT("graphName"), TEXT("NoSuchGraph")));

	// connect_pins_batch.
	Step(TEXT("30_connect_batch"), TEXT("connect_pins_batch"), At().A(TEXT("connections"), {
		Link(Branch, TEXT("else"), Print, TEXT("execute")),
		Link(Sequence, TEXT("then_0"), Print, TEXT("execute")),
		Link(TEXT("NoSuchNode"), TEXT("then"), Print, TEXT("execute")),
		Link(Branch, TEXT("NoPin"), Print, TEXT("execute")) }));
	Step(TEXT("31_connect_batch_missing_graph"), TEXT("connect_pins_batch"), At().S(TEXT("graphName"), TEXT("NoSuchGraph")).A(TEXT("connections"), {}));

	// set_node_property.
	auto NodeArgs = [&](const FString& NodeId) { return At().S(TEXT("nodeId"), NodeId); };
	Step(TEXT("40_set_pin_default"), TEXT("set_node_property"), NodeArgs(Print).S(TEXT("propertyName"), TEXT("InString")).S(TEXT("value"), TEXT("hello")));
	Step(TEXT("41_set_pin_default_same"), TEXT("set_node_property"), NodeArgs(Print).S(TEXT("propertyName"), TEXT("InString")).S(TEXT("value"), TEXT("hello")));
	Step(TEXT("42_set_bool_pin"), TEXT("set_node_property"), NodeArgs(Print).S(TEXT("propertyName"), TEXT("bPrintToLog")).S(TEXT("value"), TEXT("false")));
	Step(TEXT("43_set_unknown_property"), TEXT("set_node_property"), NodeArgs(Print).S(TEXT("propertyName"), TEXT("NoSuchThing")).S(TEXT("value"), TEXT("1")));
	Step(TEXT("44_set_missing_node"), TEXT("set_node_property"), NodeArgs(TEXT("NoSuchNode")).S(TEXT("propertyName"), TEXT("InString")).S(TEXT("value"), TEXT("x")));

	// disconnect_pins.
	Step(TEXT("50_disconnect_filtered"), TEXT("disconnect_pins"), NodeArgs(Print).S(TEXT("pinName"), TEXT("execute")).S(TEXT("linkedNodeId"), Sequence).S(TEXT("linkedPinName"), TEXT("then_0")));
	Step(TEXT("51_disconnect_all"), TEXT("disconnect_pins"), NodeArgs(Print).S(TEXT("pinName"), TEXT("execute")));
	Step(TEXT("52_disconnect_again"), TEXT("disconnect_pins"), NodeArgs(Print).S(TEXT("pinName"), TEXT("execute")));
	Step(TEXT("53_disconnect_bad_pin"), TEXT("disconnect_pins"), NodeArgs(Print).S(TEXT("pinName"), TEXT("NoPin")));
	Step(TEXT("54_disconnect_missing_node"), TEXT("disconnect_pins"), NodeArgs(TEXT("NoSuchNode")).S(TEXT("pinName"), TEXT("execute")));

	// refresh_node.
	Step(TEXT("60_refresh"), TEXT("refresh_node"), NodeArgs(Print));
	Step(TEXT("61_refresh_break_orphans"), TEXT("refresh_node"), NodeArgs(Print).B(TEXT("breakOrphanedPins"), true));
	Step(TEXT("62_refresh_missing_node"), TEXT("refresh_node"), NodeArgs(TEXT("NoSuchNode")));

	// set_node_position and auto_layout_graph.
	Step(TEXT("70_set_position"), TEXT("set_node_position"), NodeArgs(Print).N(TEXT("posX"), 400).N(TEXT("posY"), 200));
	Step(TEXT("71_set_position_same"), TEXT("set_node_position"), NodeArgs(Print).N(TEXT("posX"), 400).N(TEXT("posY"), 200));
	Step(TEXT("72_set_position_missing_node"), TEXT("set_node_position"), NodeArgs(TEXT("NoSuchNode")).N(TEXT("posX"), 0).N(TEXT("posY"), 0));
	Step(TEXT("73_auto_layout"), TEXT("auto_layout_graph"), At().B(TEXT("capturePreviousPositions"), true));
	Step(TEXT("74_auto_layout_again"), TEXT("auto_layout_graph"), At());
	Step(TEXT("75_auto_layout_missing_graph"), TEXT("auto_layout_graph"), At().S(TEXT("graphName"), TEXT("NoSuchGraph")));

	// export_nodes_t3d feeds import_nodes_t3d; only the import is a mutator.
	FString T3D;
	{
		const TSharedPtr<FJsonValue> Exported = Registry.ExecuteHandler(TEXT("export_nodes_t3d"),
			At().A(TEXT("nodeIds"), { Str(Branch) }).Obj);
		if (Exported.IsValid() && Exported->Type == EJson::Object) Exported->AsObject()->TryGetStringField(TEXT("t3d"), T3D);
	}
	Step(TEXT("80_import_t3d"), TEXT("import_nodes_t3d"), At().S(TEXT("t3d"), T3D).N(TEXT("posX"), 800).N(TEXT("posY"), 0));
	Step(TEXT("81_import_t3d_empty"), TEXT("import_nodes_t3d"), At().S(TEXT("t3d"), TEXT("")));
	Step(TEXT("82_import_t3d_garbage"), TEXT("import_nodes_t3d"), At().S(TEXT("t3d"), TEXT("this is not t3d")));
	Step(TEXT("83_import_t3d_missing_graph"), TEXT("import_nodes_t3d"), At().S(TEXT("graphName"), TEXT("NoSuchGraph")).S(TEXT("t3d"), T3D));

	// delete_node.
	Step(TEXT("90_delete_node"), TEXT("delete_node"), NodeArgs(Sequence));
	Step(TEXT("91_delete_node_again"), TEXT("delete_node"), NodeArgs(Sequence));
	Step(TEXT("92_delete_function_entry"), TEXT("delete_node"), NodeArgs(FunctionEntryId(BP, TEXT("DoThing"))).S(TEXT("graphName"), TEXT("DoThing")));
	Step(TEXT("93_delete_node_missing_graph"), TEXT("delete_node"), NodeArgs(Print).S(TEXT("graphName"), TEXT("NoSuchGraph")));

	// cleanup_graph and compile_blueprints.
	Step(TEXT("100_cleanup_graph"), TEXT("cleanup_graph"), At().S(TEXT("graphName"), TEXT("EventGraph")));
	Step(TEXT("101_cleanup_graph_again"), TEXT("cleanup_graph"), At().S(TEXT("graphName"), TEXT("EventGraph")));
	Step(TEXT("102_cleanup_all_graphs"), TEXT("cleanup_graph"), At());
	Step(TEXT("103_cleanup_missing_graph"), TEXT("cleanup_graph"), At().S(TEXT("graphName"), TEXT("NoSuchGraph")));
	Step(TEXT("104_compile_blueprints"), TEXT("compile_blueprints"), FArgs().A(TEXT("assetPaths"), { Str(BP), Str(Missing) }));
	Step(TEXT("105_compile_blueprints_no_save"), TEXT("compile_blueprints"), FArgs().A(TEXT("assetPaths"), { Str(BP) }).B(TEXT("save"), false));

	// Members: function properties.
	auto Fn = [&](const TCHAR* Name) { return At().S(TEXT("functionName"), Name); };
	Step(TEXT("110_set_function_properties"), TEXT("set_function_properties"), Fn(TEXT("DoThing")).B(TEXT("pure"), true)
		.S(TEXT("category"), TEXT("Char")).S(TEXT("tooltip"), TEXT("Does the thing")).S(TEXT("accessSpecifier"), TEXT("protected")));
	Step(TEXT("111_set_function_properties_same"), TEXT("set_function_properties"), Fn(TEXT("DoThing")).B(TEXT("pure"), true)
		.S(TEXT("category"), TEXT("Char")).S(TEXT("tooltip"), TEXT("Does the thing")).S(TEXT("accessSpecifier"), TEXT("protected")));
	Step(TEXT("112_set_function_properties_bad_access"), TEXT("set_function_properties"), Fn(TEXT("DoThing")).S(TEXT("accessSpecifier"), TEXT("sideways")));
	Step(TEXT("113_set_function_properties_missing"), TEXT("set_function_properties"), Fn(TEXT("NoSuchFn")).B(TEXT("pure"), false));
	Step(TEXT("114_set_function_properties_restore"), TEXT("set_function_properties"), Fn(TEXT("DoThing")).B(TEXT("pure"), false).S(TEXT("accessSpecifier"), TEXT("public")));

	// edit_graph_parameters.
	auto GraphParam = [&](const TCHAR* Op) { return Fn(TEXT("DoThing")).S(TEXT("op"), Op); };
	Step(TEXT("120_param_add"), TEXT("edit_graph_parameters"), GraphParam(TEXT("add")).S(TEXT("parameterName"), TEXT("Amount")).S(TEXT("parameterType"), TEXT("int")));
	Step(TEXT("121_param_add_second"), TEXT("edit_graph_parameters"), GraphParam(TEXT("add")).S(TEXT("parameterName"), TEXT("Label")).S(TEXT("parameterType"), TEXT("string")));
	Step(TEXT("122_param_add_duplicate"), TEXT("edit_graph_parameters"), GraphParam(TEXT("add")).S(TEXT("parameterName"), TEXT("Amount")).S(TEXT("parameterType"), TEXT("int")));
	Step(TEXT("123_param_add_output"), TEXT("edit_graph_parameters"), GraphParam(TEXT("add")).B(TEXT("isOutput"), true).S(TEXT("parameterName"), TEXT("Ok")).S(TEXT("parameterType"), TEXT("bool")));
	Step(TEXT("124_param_rename"), TEXT("edit_graph_parameters"), GraphParam(TEXT("rename")).S(TEXT("parameterName"), TEXT("Amount")).S(TEXT("newName"), TEXT("Count")));
	Step(TEXT("125_param_set_type"), TEXT("edit_graph_parameters"), GraphParam(TEXT("set_type")).S(TEXT("parameterName"), TEXT("Count")).S(TEXT("parameterType"), TEXT("float")));
	Step(TEXT("126_param_set_default"), TEXT("edit_graph_parameters"), GraphParam(TEXT("set_default")).S(TEXT("parameterName"), TEXT("Count")).S(TEXT("defaultValue"), TEXT("2.5")));
	Step(TEXT("127_param_reorder"), TEXT("edit_graph_parameters"), GraphParam(TEXT("reorder")).A(TEXT("order"), { Str(TEXT("Label")), Str(TEXT("Count")) }));
	Step(TEXT("128_param_reorder_partial"), TEXT("edit_graph_parameters"), GraphParam(TEXT("reorder")).A(TEXT("order"), { Str(TEXT("Label")) }));
	Step(TEXT("129_param_remove"), TEXT("edit_graph_parameters"), GraphParam(TEXT("remove")).S(TEXT("parameterName"), TEXT("Label")));
	Step(TEXT("130_param_remove_missing"), TEXT("edit_graph_parameters"), GraphParam(TEXT("remove")).S(TEXT("parameterName"), TEXT("Label")));
	Step(TEXT("131_param_bad_op"), TEXT("edit_graph_parameters"), GraphParam(TEXT("juggle")).S(TEXT("parameterName"), TEXT("Count")));
	Step(TEXT("132_param_missing_function"), TEXT("edit_graph_parameters"), Fn(TEXT("NoSuchFn")).S(TEXT("op"), TEXT("add")).S(TEXT("parameterName"), TEXT("X")).S(TEXT("parameterType"), TEXT("int")));
	Step(TEXT("133_param_dispatcher_add"), TEXT("edit_graph_parameters"), At().S(TEXT("functionName"), TEXT("OnChanged")).S(TEXT("op"), TEXT("add"))
		.S(TEXT("parameterName"), TEXT("NewValue")).S(TEXT("parameterType"), TEXT("float")));

	// Variables: rename and metadata.
	Step(TEXT("140_rename_variable"), TEXT("rename_blueprint_variable"), At().S(TEXT("oldName"), TEXT("Health")).S(TEXT("newName"), TEXT("Vitality")));
	Step(TEXT("141_rename_variable_gone"), TEXT("rename_blueprint_variable"), At().S(TEXT("oldName"), TEXT("Health")).S(TEXT("newName"), TEXT("Other")));
	Step(TEXT("142_rename_variable_same"), TEXT("rename_blueprint_variable"), At().S(TEXT("oldName"), TEXT("Vitality")).S(TEXT("newName"), TEXT("Vitality")));
	{
		TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
		Meta->SetStringField(TEXT("UIMin"), TEXT("0"));
		Meta->SetStringField(TEXT("UIMax"), TEXT("100"));
		Step(TEXT("143_set_variable_metadata"), TEXT("set_blueprint_variable_metadata"), At().S(TEXT("name"), TEXT("Vitality")).O(TEXT("metadata"), Meta));
		Step(TEXT("144_set_variable_metadata_same"), TEXT("set_blueprint_variable_metadata"), At().S(TEXT("name"), TEXT("Vitality")).O(TEXT("metadata"), Meta));
	}
	{
		TSharedPtr<FJsonObject> Remove = MakeShared<FJsonObject>();
		Remove->SetField(TEXT("UIMax"), MakeShared<FJsonValueNull>());
		Step(TEXT("145_remove_variable_metadata"), TEXT("set_blueprint_variable_metadata"), At().S(TEXT("name"), TEXT("Vitality")).O(TEXT("metadata"), Remove));
		TSharedPtr<FJsonObject> Local = MakeShared<FJsonObject>();
		Local->SetStringField(TEXT("tooltip"), TEXT("a counter"));
		Step(TEXT("146_set_local_variable_metadata"), TEXT("set_blueprint_variable_metadata"), At().S(TEXT("name"), TEXT("Counter"))
			.S(TEXT("functionName"), TEXT("DoThing")).O(TEXT("metadata"), Local));
		Step(TEXT("147_set_variable_metadata_missing"), TEXT("set_blueprint_variable_metadata"), At().S(TEXT("name"), TEXT("NoSuchVar")).O(TEXT("metadata"), Local));
	}

	// edit_local_variable.
	auto LocalVar = [&](const TCHAR* Name, const TCHAR* Op) { return Fn(TEXT("DoThing")).S(TEXT("name"), Name).S(TEXT("op"), Op); };
	Step(TEXT("150_local_rename"), TEXT("edit_local_variable"), LocalVar(TEXT("Counter"), TEXT("rename")).S(TEXT("newName"), TEXT("Tally")));
	Step(TEXT("151_local_set_type"), TEXT("edit_local_variable"), LocalVar(TEXT("Tally"), TEXT("set_type")).S(TEXT("varType"), TEXT("float")));
	Step(TEXT("152_local_set_default"), TEXT("edit_local_variable"), LocalVar(TEXT("Tally"), TEXT("set_default")).S(TEXT("defaultValue"), TEXT("1.5")));
	Step(TEXT("153_local_bad_op"), TEXT("edit_local_variable"), LocalVar(TEXT("Tally"), TEXT("juggle")));
	Step(TEXT("154_local_missing"), TEXT("edit_local_variable"), LocalVar(TEXT("NoSuchLocal"), TEXT("remove")));
	Step(TEXT("155_local_remove"), TEXT("edit_local_variable"), LocalVar(TEXT("Tally"), TEXT("remove")));

	// Custom events.
	Step(TEXT("160_add_custom_event"), TEXT("add_custom_event"), At().S(TEXT("eventName"), TEXT("OnPoke"))
		.A(TEXT("parameters"), { Param(TEXT("Strength"), TEXT("float")), Param(TEXT("Who"), TEXT("name")) }).N(TEXT("posX"), 0).N(TEXT("posY"), 600));
	Step(TEXT("161_add_custom_event_again"), TEXT("add_custom_event"), At().S(TEXT("eventName"), TEXT("OnPoke")));
	Step(TEXT("162_add_custom_event_replicated"), TEXT("add_custom_event"), At().S(TEXT("eventName"), TEXT("OnServerPoke")).S(TEXT("netMode"), TEXT("server")).B(TEXT("reliable"), false));
	Step(TEXT("163_add_custom_event_bad_net_mode"), TEXT("add_custom_event"), At().S(TEXT("eventName"), TEXT("OnBad")).S(TEXT("netMode"), TEXT("sideways")));
	Step(TEXT("164_add_custom_event_bad_param_type"), TEXT("add_custom_event"), At().S(TEXT("eventName"), TEXT("OnBadParam"))
		.A(TEXT("parameters"), { Param(TEXT("X"), TEXT("NoSuchType")) }));
	Step(TEXT("165_event_param_add"), TEXT("edit_graph_parameters"), At().S(TEXT("eventName"), TEXT("OnPoke")).S(TEXT("op"), TEXT("add"))
		.S(TEXT("parameterName"), TEXT("Extra")).S(TEXT("parameterType"), TEXT("int")));

	// Macros.
	Step(TEXT("170_create_macro"), TEXT("create_macro"), At().S(TEXT("macroName"), TEXT("M_Char"))
		.A(TEXT("inputs"), { Param(TEXT("In"), TEXT("int")) }).A(TEXT("outputs"), { Param(TEXT("Out"), TEXT("bool")) }));
	Step(TEXT("171_create_macro_again"), TEXT("create_macro"), At().S(TEXT("macroName"), TEXT("M_Char")));
	Step(TEXT("172_create_macro_conflict_error"), TEXT("create_macro"), At().S(TEXT("macroName"), TEXT("M_Char")).S(TEXT("onConflict"), TEXT("error")));
	Step(TEXT("173_macro_param_add"), TEXT("edit_graph_parameters"), At().S(TEXT("functionName"), TEXT("M_Char")).S(TEXT("op"), TEXT("add"))
		.S(TEXT("parameterName"), TEXT("In2")).S(TEXT("parameterType"), TEXT("float")));
	Step(TEXT("174_delete_macro"), TEXT("delete_macro"), At().S(TEXT("macroName"), TEXT("M_Char")));
	Step(TEXT("175_delete_macro_again"), TEXT("delete_macro"), At().S(TEXT("macroName"), TEXT("M_Char")));

	// Graphs.
	Step(TEXT("180_delete_graph"), TEXT("delete_graph"), At().S(TEXT("graphName"), TEXT("DoomedFn")));
	Step(TEXT("181_delete_graph_again"), TEXT("delete_graph"), At().S(TEXT("graphName"), TEXT("DoomedFn")));
	Step(TEXT("182_delete_event_graph_refused"), TEXT("delete_graph"), At().S(TEXT("graphName"), TEXT("EventGraph")));

	// Dispatchers and interfaces.
	Step(TEXT("190_remove_dispatcher"), TEXT("remove_event_dispatcher"), At().S(TEXT("name"), TEXT("OnChanged")));
	Step(TEXT("191_remove_dispatcher_again"), TEXT("remove_event_dispatcher"), At().S(TEXT("name"), TEXT("OnChanged")));
	Step(TEXT("192_remove_interface_unresolved"), TEXT("remove_blueprint_interface"), At().S(TEXT("interfacePath"), TEXT("NoSuchInterface")));
	Step(TEXT("193_remove_interface_preserve"), TEXT("remove_blueprint_interface"), At().S(TEXT("interfacePath"), BPIClass).B(TEXT("preserveFunctions"), true));
	Step(TEXT("194_remove_interface_again"), TEXT("remove_blueprint_interface"), At().S(TEXT("interfacePath"), BPIClass));
	Step(TEXT("195_remove_interface_missing_blueprint"), TEXT("remove_blueprint_interface"), FArgs().S(TEXT("assetPath"), Missing).S(TEXT("interfacePath"), BPIClass));

	FString Note;
	const TArray<FString> Breaks = UEMCPCharacterisation::CompareWithFixture(Run, TEXT("blueprint-graph-responses"), Note);
	if (!Note.IsEmpty()) AddInfo(Note);
	for (const FString& Break : Breaks) AddError(Break);
	return Breaks.Num() == 0;
}

#endif
