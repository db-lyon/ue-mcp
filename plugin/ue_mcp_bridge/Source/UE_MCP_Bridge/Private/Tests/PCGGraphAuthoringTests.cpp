// PCG graph authoring through the registry: graph pins, selectors, settings readback, import upserts,
// graph parameters and graph instances. Graphs live on a private mount, never the open project.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/PCG/PCGHandlers.h"
#include "Tests/MCPScopedTestMount.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "StructUtils/PropertyBag.h"

namespace PCGGraphAuthoringTests
{
	const TCHAR* const Root = TEXT("/UEMCPPCGAuthoring/");

	TSharedPtr<FJsonObject> Invoke(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : MakeShared<FJsonObject>();
	}

	bool Ok(const TSharedPtr<FJsonObject>& Result)
	{
		bool bSuccess = false;
		return Result->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	/** A new empty graph on the test mount; its object path. */
	FString NewGraph(FMCPHandlerRegistry& Registry)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("name"), TEXT("PCG_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Params->SetStringField(TEXT("packagePath"), FString(Root) + TEXT("Graphs"));
		const TSharedPtr<FJsonObject> Created = Invoke(Registry, TEXT("create_pcg_graph"), Params);
		return Ok(Created) ? Created->GetStringField(TEXT("path")) : FString();
	}

	TArray<FString> Labels(const TArray<TObjectPtr<UPCGPin>>& Pins)
	{
		TArray<FString> Out;
		for (const UPCGPin* Pin : Pins) if (Pin) Out.Add(Pin->Properties.Label.ToString());
		return Out;
	}

	/** add_pcg_node; the new node's engine name, empty on failure. */
	FString AddNode(FMCPHandlerRegistry& Registry, const FString& GraphPath, const TCHAR* NodeType)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), GraphPath);
		Params->SetStringField(TEXT("nodeType"), NodeType);
		const TSharedPtr<FJsonObject> Added = Invoke(Registry, TEXT("add_pcg_node"), Params);
		return Ok(Added) ? Added->GetStringField(TEXT("nodeName")) : FString();
	}

	TSharedPtr<FJsonObject> WriteSettings(FMCPHandlerRegistry& Registry, const FString& GraphPath, const FString& NodeName, const TSharedPtr<FJsonObject>& Settings)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), GraphPath);
		Params->SetStringField(TEXT("nodeName"), NodeName);
		Params->SetObjectField(TEXT("settings"), Settings);
		return Invoke(Registry, TEXT("set_pcg_node_settings"), Params);
	}

	UPCGNode* FindNode(UPCGGraph* Graph, const FString& Name)
	{
		for (UPCGNode* Node : Graph->GetNodes()) if (Node && Node->GetName() == Name) return Node;
		return nullptr;
	}

	/** A selector property on a node's settings, in editor syntax; empty when the node or property is missing. */
	FString SelectorText(UPCGGraph* Graph, const FString& NodeName, const TCHAR* Property)
	{
		const UPCGNode* Node = FindNode(Graph, NodeName);
		const UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
		const FStructProperty* Prop = Settings ? CastField<FStructProperty>(Settings->GetClass()->FindPropertyByName(Property)) : nullptr;
		if (!Prop) return FString();
		return Prop->ContainerPtrToValuePtr<FPCGAttributePropertySelector>(const_cast<UPCGSettings*>(Settings))->GetDisplayText().ToString();
	}

	/** One import_graph node entry. */
	TSharedPtr<FJsonValue> ImportNode(const TCHAR* Name, const TCHAR* Class, const TSharedPtr<FJsonObject>& Settings = nullptr)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("class"), Class);
		if (Settings) Obj->SetObjectField(TEXT("settings"), Settings);
		return MakeShared<FJsonValueObject>(Obj);
	}

	TSharedPtr<FJsonObject> ImportGraph(FMCPHandlerRegistry& Registry, const FString& GraphPath, const TArray<TSharedPtr<FJsonValue>>& Nodes,
		const TArray<TSharedPtr<FJsonValue>>& Connections = {}, bool bReplace = false)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), GraphPath);
		Params->SetArrayField(TEXT("nodes"), Nodes);
		if (Connections.Num() > 0) Params->SetArrayField(TEXT("connections"), Connections);
		Params->SetBoolField(TEXT("replace"), bReplace);
		return Invoke(Registry, TEXT("import_pcg_graph"), Params);
	}

	TSharedPtr<FJsonValue> Pin(const TCHAR* Label, const TCHAR* Type = nullptr)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("label"), Label);
		if (Type) Obj->SetStringField(TEXT("type"), Type);
		return MakeShared<FJsonValueObject>(Obj);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGGraphPinsTest,
	"UE.MCP.PCG.Authoring.GraphPins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGGraphPinsTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	if (!TestFalse(TEXT("a graph is created"), GraphPath.IsEmpty())) return false;
	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!TestNotNull(TEXT("the graph loads"), Graph)) return false;

	auto SetPins = [&](const TCHAR* Side, const TArray<TSharedPtr<FJsonValue>>& Pins, const TCHAR* Mode)
	{
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), GraphPath);
		Params->SetStringField(TEXT("side"), Side);
		Params->SetArrayField(TEXT("pins"), Pins);
		if (Mode) Params->SetStringField(TEXT("mode"), Mode);
		return Invoke(Registry, TEXT("set_pcg_graph_pins"), Params);
	};

	const TArray<FString> Before = Labels(Graph->GetInputNode()->GetOutputPins());
	const TSharedPtr<FJsonObject> Appended = SetPins(TEXT("input"), { Pin(TEXT("Mass"), TEXT("Point")) }, nullptr);
	TestTrue(TEXT("append succeeds"), Ok(Appended));
	TestTrue(TEXT("the input node grows the pin"), Labels(Graph->GetInputNode()->GetOutputPins()).Contains(TEXT("Mass")));
	TestEqual(TEXT("existing pins are kept"), Labels(Graph->GetInputNode()->GetOutputPins()).Num(), Before.Num() + 1);
	TestTrue(TEXT("a rollback restores the previous Pins"), Appended->HasField(TEXT("rollback")));

	TestTrue(TEXT("an output pin is added"), Ok(SetPins(TEXT("output"), { Pin(TEXT("Debug"), TEXT("Param")) }, TEXT("append"))));
	TestTrue(TEXT("the output node grows the pin"), Labels(Graph->GetOutputNode()->GetInputPins()).Contains(TEXT("Debug")));

	TestTrue(TEXT("remove succeeds"), Ok(SetPins(TEXT("input"), { Pin(TEXT("Mass")) }, TEXT("remove"))));
	TestFalse(TEXT("the pin is gone"), Labels(Graph->GetInputNode()->GetOutputPins()).Contains(TEXT("Mass")));

	const TArray<FString> Unchanged = Labels(Graph->GetInputNode()->GetOutputPins());
	TestFalse(TEXT("an unknown type is refused"), Ok(SetPins(TEXT("input"), { Pin(TEXT("Bad"), TEXT("NotAType")) }, nullptr)));
	TestFalse(TEXT("removing a missing label is refused"), Ok(SetPins(TEXT("input"), { Pin(TEXT("Nope")) }, TEXT("remove"))));
	TestFalse(TEXT("a bad side is refused"), Ok(SetPins(TEXT("middle"), { Pin(TEXT("X")) }, nullptr)));
	TestEqual(TEXT("refusals change nothing"), Labels(Graph->GetInputNode()->GetOutputPins()), Unchanged);

	TestTrue(TEXT("replace succeeds"), Ok(SetPins(TEXT("input"), { Pin(TEXT("A"), TEXT("Point")), Pin(TEXT("B"), TEXT("Spatial")) }, TEXT("replace"))));
	TestEqual(TEXT("the pins are exactly the list"), Labels(Graph->GetInputNode()->GetOutputPins()), TArray<FString>{ TEXT("A"), TEXT("B") });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGSelectorSyntaxTest,
	"UE.MCP.PCG.Authoring.SelectorSyntax",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGSelectorSyntaxTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	UPCGGraph* Graph = GraphPath.IsEmpty() ? nullptr : LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!TestNotNull(TEXT("the graph loads"), Graph)) return false;
	const FString Filter = AddNode(Registry, GraphPath, TEXT("PCGAttributeFilteringSettings"));
	if (!TestFalse(TEXT("a filter node is added"), Filter.IsEmpty())) return false;

	for (const TCHAR* Text : { TEXT("ST_Meadow_RedGrass"), TEXT("$Density"), TEXT("$Position.Z"), TEXT("@Last") })
	{
		auto Settings = MakeShared<FJsonObject>();
		Settings->SetStringField(TEXT("TargetAttribute"), Text);
		TestTrue(FString::Printf(TEXT("'%s' is accepted"), Text), Ok(WriteSettings(Registry, GraphPath, Filter, Settings)));
		TestEqual(FString::Printf(TEXT("'%s' reads back as written"), Text), SelectorText(Graph, Filter, TEXT("TargetAttribute")), FString(Text));
	}

	// Through import_graph as well.
	auto NodeSettings = MakeShared<FJsonObject>();
	NodeSettings->SetStringField(TEXT("TargetAttribute"), TEXT("$Position.Y"));
	auto Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("name"), TEXT("ImportedFilter"));
	Node->SetStringField(TEXT("class"), TEXT("PCGAttributeFilteringSettings"));
	Node->SetObjectField(TEXT("settings"), NodeSettings);
	auto Import = MakeShared<FJsonObject>();
	Import->SetStringField(TEXT("assetPath"), GraphPath);
	Import->SetArrayField(TEXT("nodes"), { MakeShared<FJsonValueObject>(Node) });
	TestTrue(TEXT("import_graph accepts selector syntax"), Ok(Invoke(Registry, TEXT("import_pcg_graph"), Import)));
	TestEqual(TEXT("the imported selector keeps its extra name"), SelectorText(Graph, TEXT("ImportedFilter"), TEXT("TargetAttribute")), FString(TEXT("$Position.Y")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGSelectorReplaceTest,
	"UE.MCP.PCG.Authoring.SelectorReplaceAndDroppedEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGSelectorReplaceTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	UPCGGraph* Graph = GraphPath.IsEmpty() ? nullptr : LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!TestNotNull(TEXT("the graph loads"), Graph)) return false;
	const FString Maths = AddNode(Registry, GraphPath, TEXT("PCGMetadataMathsSettings"));
	if (!TestFalse(TEXT("a maths node is added"), Maths.IsEmpty())) return false;

	// An object replaces the whole selector: ExtraNames from the previous value do not carry over.
	auto Settings = MakeShared<FJsonObject>();
	Settings->SetStringField(TEXT("InputSource1"), TEXT("$Position.X"));
	TestTrue(TEXT("a selector with an extra name is written"), Ok(WriteSettings(Registry, GraphPath, Maths, Settings)));
	auto Replacement = MakeShared<FJsonObject>();
	Replacement->SetStringField(TEXT("Selection"), TEXT("Attribute"));
	Replacement->SetStringField(TEXT("AttributeName"), TEXT("@Last"));
	Settings = MakeShared<FJsonObject>();
	Settings->SetObjectField(TEXT("InputSource1"), Replacement);
	const TSharedPtr<FJsonObject> Replaced = WriteSettings(Registry, GraphPath, Maths, Settings);
	TestTrue(TEXT("the object write succeeds"), Ok(Replaced));
	TestEqual(TEXT("no stale extra name survives"), SelectorText(Graph, Maths, TEXT("InputSource1")), FString(TEXT("@Last")));
	const TSharedPtr<FJsonObject>* Resolved = nullptr;
	TestTrue(TEXT("resolvedSelectors echoes the stored selector"),
		Replaced->TryGetObjectField(TEXT("resolvedSelectors"), Resolved) && (*Resolved)->GetStringField(TEXT("InputSource1")) == TEXT("@Last"));

	// A ternary op wired on InC, switched to a unary op, loses that pin and reports the edge.
	Settings = MakeShared<FJsonObject>();
	Settings->SetStringField(TEXT("Operation"), TEXT("MulAdd"));
	TestTrue(TEXT("MulAdd is written"), Ok(WriteSettings(Registry, GraphPath, Maths, Settings)));
	auto Connect = MakeShared<FJsonObject>();
	Connect->SetStringField(TEXT("assetPath"), GraphPath);
	Connect->SetStringField(TEXT("sourceNode"), Graph->GetInputNode()->GetName());
	Connect->SetStringField(TEXT("targetNode"), Maths);
	Connect->SetStringField(TEXT("targetPin"), TEXT("InC"));
	if (!TestTrue(TEXT("InC is wired"), Ok(Invoke(Registry, TEXT("connect_pcg_nodes"), Connect)))) return false;
	Settings = MakeShared<FJsonObject>();
	Settings->SetStringField(TEXT("Operation"), TEXT("Sign"));
	const TSharedPtr<FJsonObject> Unary = WriteSettings(Registry, GraphPath, Maths, Settings);
	TestTrue(TEXT("Sign is written"), Ok(Unary));
	const TArray<TSharedPtr<FJsonValue>>* Removed = nullptr;
	TestTrue(TEXT("the dropped InC edge is reported"), Unary->TryGetArrayField(TEXT("removedEdges"), Removed) && Removed->Num() == 1);
	TestTrue(TEXT("a warning names it"), Unary->HasField(TEXT("warnings")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGSettingsReadBackTest,
	"UE.MCP.PCG.Authoring.SettingsReadBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGSettingsReadBackTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	const FString Node = GraphPath.IsEmpty() ? FString() : AddNode(Registry, GraphPath, TEXT("PCGTransformPointsSettings"));
	if (!TestFalse(TEXT("a transform node is added"), Node.IsEmpty())) return false;

	auto Settings = MakeShared<FJsonObject>();
	Settings->SetStringField(TEXT("OffsetMin"), TEXT("(X=1,Y=2,Z=3)"));
	const TSharedPtr<FJsonObject> Full = WriteSettings(Registry, GraphPath, Node, Settings);
	TestTrue(TEXT("full struct text is written"), Ok(Full));
	TestFalse(TEXT("a complete text raises no warning"), Full->HasField(TEXT("warnings")));
	const TSharedPtr<FJsonObject>* ReadBack = nullptr;
	TestTrue(TEXT("readBack carries the stored value"), Full->TryGetObjectField(TEXT("readBack"), ReadBack) && (*ReadBack)->HasField(TEXT("OffsetMin")));

	// Text that omits Y and Z keeps their old values: the call says so instead of reporting a clean write.
	Settings = MakeShared<FJsonObject>();
	Settings->SetStringField(TEXT("OffsetMin"), TEXT("(X=5)"));
	const TSharedPtr<FJsonObject> Partial = WriteSettings(Registry, GraphPath, Node, Settings);
	TestTrue(TEXT("partial text is written"), Ok(Partial));
	const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
	TestTrue(TEXT("a warning names the merged fields"), Partial->TryGetArrayField(TEXT("warnings"), Warnings) && Warnings->Num() == 1
		&& (*Warnings)[0]->AsString().Contains(TEXT("OffsetMin")));
	TestTrue(TEXT("readBack shows Y survived"), Partial->TryGetObjectField(TEXT("readBack"), ReadBack)
		&& (*ReadBack)->GetStringField(TEXT("OffsetMin")).Contains(TEXT("Y=2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGSelectorRoundTripTest,
	"UE.MCP.PCG.Authoring.SelectorExportRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGSelectorRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	UPCGGraph* Graph = GraphPath.IsEmpty() ? nullptr : LoadObject<UPCGGraph>(nullptr, *GraphPath);
	const FString Filter = Graph ? AddNode(Registry, GraphPath, TEXT("PCGAttributeFilteringSettings")) : FString();
	if (!TestFalse(TEXT("a filter node is added"), Filter.IsEmpty())) return false;
	auto Settings = MakeShared<FJsonObject>();
	Settings->SetStringField(TEXT("TargetAttribute"), TEXT("$Position.Z"));
	TestTrue(TEXT("the selector is written"), Ok(WriteSettings(Registry, GraphPath, Filter, Settings)));

	auto ExportParams = MakeShared<FJsonObject>();
	ExportParams->SetStringField(TEXT("assetPath"), GraphPath);
	const TSharedPtr<FJsonObject> Exported = Invoke(Registry, TEXT("export_pcg_graph"), ExportParams);
	if (!TestTrue(TEXT("export succeeds"), Ok(Exported))) return false;
	FString ExportedText;
	for (const TSharedPtr<FJsonValue>& Node : Exported->GetArrayField(TEXT("nodes")))
	{
		const TSharedPtr<FJsonObject>* NodeSettings = nullptr;
		if (Node->AsObject()->GetStringField(TEXT("name")) == Filter && Node->AsObject()->TryGetObjectField(TEXT("settings"), NodeSettings))
		{
			(*NodeSettings)->TryGetStringField(TEXT("TargetAttribute"), ExportedText);
		}
	}
	TestEqual(TEXT("export emits the selector text with its extra name"), ExportedText, FString(TEXT("$Position.Z")));

	auto Import = MakeShared<FJsonObject>();
	Import->SetStringField(TEXT("assetPath"), GraphPath);
	Import->SetBoolField(TEXT("replace"), true);
	Import->SetArrayField(TEXT("nodes"), Exported->GetArrayField(TEXT("nodes")));
	Import->SetArrayField(TEXT("connections"), Exported->GetArrayField(TEXT("connections")));
	TestTrue(TEXT("the export imports back"), Ok(Invoke(Registry, TEXT("import_pcg_graph"), Import)));
	FString RoundTripped;
	for (UPCGNode* Node : Graph->GetNodes())
	{
		if (Node && Node->GetSettings() && Node->GetSettings()->GetClass()->GetName() == TEXT("PCGAttributeFilteringSettings"))
		{
			RoundTripped = SelectorText(Graph, Node->GetName(), TEXT("TargetAttribute"));
		}
	}
	TestEqual(TEXT("the selector survives export and import"), RoundTripped, FString(TEXT("$Position.Z")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGImportUpsertTest,
	"UE.MCP.PCG.Authoring.ImportUpsertByName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGImportUpsertTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	UPCGGraph* Graph = GraphPath.IsEmpty() ? nullptr : LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!TestNotNull(TEXT("the graph loads"), Graph)) return false;
	// The edge comes from another Distance node: point data into point data on every engine. From the graph
	// input (Any) into Source (Point), 5.9 asks for a conversion node first.
	TestTrue(TEXT("the named nodes are created"), Ok(ImportGraph(Registry, GraphPath,
		{ ImportNode(TEXT("FT_Src"), TEXT("PCGDistanceSettings")), ImportNode(TEXT("FT_BankDist"), TEXT("PCGDistanceSettings")) })));
	UPCGNode* Original = FindNode(Graph, TEXT("FT_BankDist"));
	if (!TestNotNull(TEXT("it keeps its name"), Original)) return false;
	const int32 NodeCount = Graph->GetNodes().Num();

	// Same name and class: updated in place, and the edge naming it lands on it.
	auto Settings = MakeShared<FJsonObject>();
	Settings->SetNumberField(TEXT("MaximumDistance"), 1234.0);
	auto Edge = MakeShared<FJsonObject>();
	Edge->SetStringField(TEXT("from"), TEXT("FT_Src"));
	Edge->SetStringField(TEXT("to"), TEXT("FT_BankDist"));
	const TSharedPtr<FJsonObject> Upserted = ImportGraph(Registry, GraphPath,
		{ ImportNode(TEXT("FT_BankDist"), TEXT("PCGDistanceSettings"), Settings) }, { MakeShared<FJsonValueObject>(Edge) });
	TestTrue(TEXT("the upsert succeeds"), Ok(Upserted));
	TestEqual(TEXT("no duplicate node is made"), Graph->GetNodes().Num(), NodeCount);
	TestEqual(TEXT("nodesUpdated counts it"), Upserted->GetIntegerField(TEXT("nodesUpdated")), 1);
	TestEqual(TEXT("nodesCreated is zero"), Upserted->GetIntegerField(TEXT("nodesCreated")), 0);
	TestTrue(TEXT("the edge lands on the existing node"), Original->GetInputPins().Num() > 0 && Original->GetInputPins()[0]->Edges.Num() == 1);
	const FDoubleProperty* MaxProp = CastField<FDoubleProperty>(Original->GetSettings()->GetClass()->FindPropertyByName(TEXT("MaximumDistance")));
	TestTrue(TEXT("the settings land on the existing node"), MaxProp && MaxProp->GetPropertyValue_InContainer(Original->GetSettings()) == 1234.0);

	// Same name, another class: refused before anything changes.
	const TSharedPtr<FJsonObject> Conflict = ImportGraph(Registry, GraphPath,
		{ ImportNode(TEXT("Fresh"), TEXT("PCGDistanceSettings")), ImportNode(TEXT("FT_BankDist"), TEXT("PCGAddTagSettings")) });
	TestFalse(TEXT("a class conflict is refused"), Ok(Conflict));
	TestEqual(TEXT("the refusal says why"), Conflict->GetStringField(TEXT("reason")), FString(TEXT("node_name_conflict")));
	TestNull(TEXT("nothing from the refused call was created"), FindNode(Graph, TEXT("Fresh")));
	TestFalse(TEXT("a name given twice is refused"), Ok(ImportGraph(Registry, GraphPath,
		{ ImportNode(TEXT("Twice"), TEXT("PCGDistanceSettings")), ImportNode(TEXT("Twice"), TEXT("PCGDistanceSettings")) })));
	TestEqual(TEXT("refusals leave the node count alone"), Graph->GetNodes().Num(), NodeCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGImportMinimalRollbackTest,
	"UE.MCP.PCG.Authoring.ImportMinimalRollbackAndNamedAdd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGImportMinimalRollbackTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	UPCGGraph* Graph = GraphPath.IsEmpty() ? nullptr : LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!TestNotNull(TEXT("the graph loads"), Graph)) return false;

	// add_node takes a name, and the same name and class again is reported, not duplicated.
	auto AddParams = MakeShared<FJsonObject>();
	AddParams->SetStringField(TEXT("assetPath"), GraphPath);
	AddParams->SetStringField(TEXT("nodeType"), TEXT("PCGDistanceSettings"));
	AddParams->SetStringField(TEXT("name"), TEXT("Dist"));
	TestTrue(TEXT("a named node is added"), Ok(Invoke(Registry, TEXT("add_pcg_node"), AddParams)));
	TestNotNull(TEXT("it has the name"), FindNode(Graph, TEXT("Dist")));
	const TSharedPtr<FJsonObject> Again = Invoke(Registry, TEXT("add_pcg_node"), AddParams);
	TestTrue(TEXT("the same request is reported as existed"), Ok(Again) && Again->HasField(TEXT("existed")));
	AddParams->SetStringField(TEXT("nodeType"), TEXT("PCGAddTagSettings"));
	TestFalse(TEXT("another class under that name is refused"), Ok(Invoke(Registry, TEXT("add_pcg_node"), AddParams)));
	const int32 Before = Graph->GetNodes().Num();

	// An additive import's rollback names what it made and what it overwrote, nothing else.
	auto Settings = MakeShared<FJsonObject>();
	Settings->SetNumberField(TEXT("MaximumDistance"), 777.0);
	const TSharedPtr<FJsonObject> Imported = ImportGraph(Registry, GraphPath,
		{ ImportNode(TEXT("Added"), TEXT("PCGAddTagSettings")), ImportNode(TEXT("Dist"), TEXT("PCGDistanceSettings"), Settings) });
	if (!TestTrue(TEXT("the import succeeds"), Ok(Imported))) return false;
	const TSharedPtr<FJsonObject>* Rollback = nullptr;
	const TSharedPtr<FJsonObject>* Payload = nullptr;
	if (!TestTrue(TEXT("a rollback is offered"), Imported->TryGetObjectField(TEXT("rollback"), Rollback) && (*Rollback)->TryGetObjectField(TEXT("payload"), Payload))) return false;
	TestFalse(TEXT("the rollback does not replace the graph"), (*Payload)->GetBoolField(TEXT("replace")));
	TestEqual(TEXT("it removes the created node"), (*Payload)->GetArrayField(TEXT("removeNodes")).Num(), 1);
	TestEqual(TEXT("it restores only the updated node"), (*Payload)->GetArrayField(TEXT("nodes")).Num(), 1);

	TestTrue(TEXT("the rollback replays"), Ok(Invoke(Registry, TEXT("import_pcg_graph"), *Payload)));
	TestNull(TEXT("the created node is gone"), FindNode(Graph, TEXT("Added")));
	TestEqual(TEXT("the node count is back"), Graph->GetNodes().Num(), Before);
	const UPCGNode* Dist = FindNode(Graph, TEXT("Dist"));
	const FDoubleProperty* MaxProp = Dist ? CastField<FDoubleProperty>(Dist->GetSettings()->GetClass()->FindPropertyByName(TEXT("MaximumDistance"))) : nullptr;
	TestTrue(TEXT("the overwritten value is back"), MaxProp && MaxProp->GetPropertyValue_InContainer(Dist->GetSettings()) != 777.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGGraphParametersTest,
	"UE.MCP.PCG.Authoring.GraphParametersAndInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGGraphParametersTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	UPCGGraph* Graph = GraphPath.IsEmpty() ? nullptr : LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!TestNotNull(TEXT("the graph loads"), Graph)) return false;

	auto Param = [](const TCHAR* Name, const TCHAR* Type, const TSharedPtr<FJsonValue>& Default)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		if (Type) Obj->SetStringField(TEXT("type"), Type);
		if (Default) Obj->SetField(TEXT("default"), Default);
		return MakeShared<FJsonValueObject>(Obj);
	};
	auto SetParams = [&](const TArray<TSharedPtr<FJsonValue>>& Entries, const TArray<TSharedPtr<FJsonValue>>& Remove)
	{
		auto P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("assetPath"), GraphPath);
		if (Entries.Num() > 0) P->SetArrayField(TEXT("graphParameters"), Entries);
		if (Remove.Num() > 0) P->SetArrayField(TEXT("removeParameters"), Remove);
		return Invoke(Registry, TEXT("set_pcg_graph_parameters"), P);
	};
	auto HasParam = [Graph](const TCHAR* Name)
	{
		const UPropertyBag* Bag = Graph->GetUserParametersStruct()->GetPropertyBagStruct();
		return Bag && Bag->FindPropertyDescByName(Name) != nullptr;
	};

	const TSharedPtr<FJsonObject> Added = SetParams({ Param(TEXT("Spacing"), TEXT("double"), MakeShared<FJsonValueNumber>(250.0)), Param(TEXT("Count"), TEXT("int32"), nullptr) }, {});
	TestTrue(TEXT("parameters are added"), Ok(Added));
	TestTrue(TEXT("the graph declares Spacing"), HasParam(TEXT("Spacing")));
	TestTrue(TEXT("a rollback is offered"), Added->HasField(TEXT("rollback")));
	TestFalse(TEXT("a new name without a type is refused"), Ok(SetParams({ Param(TEXT("Untyped"), nullptr, nullptr) }, {})));
	TestFalse(TEXT("a bad default refuses everything"), Ok(SetParams({ Param(TEXT("Other"), TEXT("int32"), nullptr), Param(TEXT("Spacing"), nullptr, MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(1.0), MakeShared<FJsonValueNumber>(2.0) })) }, {})));
	TestFalse(TEXT("nothing from the refused call landed"), HasParam(TEXT("Other")));

	// A Get Graph Parameter node bound by name through import, and exported the same way.
	auto Node = MakeShared<FJsonObject>();
	Node->SetStringField(TEXT("name"), TEXT("GetSpacing"));
	Node->SetStringField(TEXT("class"), TEXT("PCGUserParameterGetSettings"));
	Node->SetStringField(TEXT("parameter"), TEXT("Spacing"));
	TestTrue(TEXT("a Get Graph Parameter node imports by name"), Ok(ImportGraph(Registry, GraphPath, { MakeShared<FJsonValueObject>(Node) })));
	const UPCGNode* Get = FindNode(Graph, TEXT("GetSpacing"));
	TestTrue(TEXT("it has an output pin for the parameter"), Get && Get->GetOutputPins().Num() > 0);
	auto ExportParams = MakeShared<FJsonObject>();
	ExportParams->SetStringField(TEXT("assetPath"), GraphPath);
	FString ExportedParameter;
	// Held in a local: the range-for would otherwise iterate an array owned by a destroyed temporary.
	const TSharedPtr<FJsonObject> Exported = Invoke(Registry, TEXT("export_pcg_graph"), ExportParams);
	for (const TSharedPtr<FJsonValue>& N : Exported->GetArrayField(TEXT("nodes")))
	{
		if (N->AsObject()->GetStringField(TEXT("name")) == TEXT("GetSpacing")) N->AsObject()->TryGetStringField(TEXT("parameter"), ExportedParameter);
	}
	TestEqual(TEXT("export names the parameter"), ExportedParameter, FString(TEXT("Spacing")));

	// A graph instance asset with an override, then the override changed and cleared.
	auto Overrides = MakeShared<FJsonObject>();
	Overrides->SetNumberField(TEXT("Spacing"), 500.0);
	auto Create = MakeShared<FJsonObject>();
	Create->SetStringField(TEXT("name"), TEXT("GI_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Create->SetStringField(TEXT("packagePath"), FString(Root) + TEXT("Instances"));
	Create->SetStringField(TEXT("graphPath"), GraphPath);
	Create->SetObjectField(TEXT("overrides"), Overrides);
	const TSharedPtr<FJsonObject> Instance = Invoke(Registry, TEXT("create_pcg_graph_instance"), Create);
	if (!TestTrue(TEXT("the instance is created with its override"), Ok(Instance))) return false;
	UPCGGraphInstance* Asset = LoadObject<UPCGGraphInstance>(nullptr, *Instance->GetStringField(TEXT("path")));
	if (!TestNotNull(TEXT("the instance loads"), Asset)) return false;
	TestTrue(TEXT("it runs the graph"), Asset->GetGraph() == Graph);
	const FPropertyBagPropertyDesc* Spacing = Asset->GetUserParametersStruct()->GetPropertyBagStruct()->FindPropertyDescByName(TEXT("Spacing"));
	TestTrue(TEXT("Spacing is overridden"), Spacing && Asset->IsPropertyOverridden(Spacing->CachedProperty));

	auto Clear = MakeShared<FJsonObject>();
	Clear->SetField(TEXT("Spacing"), MakeShared<FJsonValueNull>());
	auto SetOverrides = MakeShared<FJsonObject>();
	SetOverrides->SetStringField(TEXT("assetPath"), Asset->GetPathName());
	SetOverrides->SetObjectField(TEXT("overrides"), Clear);
	TestTrue(TEXT("null clears the override"), Ok(Invoke(Registry, TEXT("set_pcg_graph_instance_overrides"), SetOverrides)));
	TestFalse(TEXT("Spacing is no longer overridden"), Spacing && Asset->IsPropertyOverridden(Spacing->CachedProperty));
	auto Unknown = MakeShared<FJsonObject>();
	Unknown->SetNumberField(TEXT("Nope"), 1.0);
	SetOverrides->SetObjectField(TEXT("overrides"), Unknown);
	TestFalse(TEXT("an undeclared name is refused"), Ok(Invoke(Registry, TEXT("set_pcg_graph_instance_overrides"), SetOverrides)));

	TestTrue(TEXT("a parameter is removed"), Ok(SetParams({}, { MakeShared<FJsonValueString>(TEXT("Count")) })));
	TestFalse(TEXT("Count is gone"), HasParam(TEXT("Count")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPPCGReadMeshSelectorTest,
	"UE.MCP.PCG.Authoring.ReadExpandsMeshSelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPPCGReadMeshSelectorTest::RunTest(const FString& Parameters)
{
	using namespace PCGGraphAuthoringTests;
	const FMCPScopedTestMount Mount{ FString(Root), TEXT("UEMCPPCGAuthoring") };
	FMCPHandlerRegistry Registry;
	FPCGHandlers::RegisterHandlers(Registry);
	const FString GraphPath = NewGraph(Registry);
	const FString Spawner = GraphPath.IsEmpty() ? FString() : AddNode(Registry, GraphPath, TEXT("PCGStaticMeshSpawnerSettings"));
	if (!TestFalse(TEXT("a spawner is added"), Spawner.IsEmpty())) return false;

	auto Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("mesh"), TEXT("/Engine/BasicShapes/Cube.Cube"));
	Entry->SetNumberField(TEXT("weight"), 3);
	auto Meshes = MakeShared<FJsonObject>();
	Meshes->SetStringField(TEXT("assetPath"), GraphPath);
	Meshes->SetStringField(TEXT("nodeName"), Spawner);
	Meshes->SetArrayField(TEXT("entries"), { MakeShared<FJsonValueObject>(Entry) });
	if (!TestTrue(TEXT("a weighted entry is written"), Ok(Invoke(Registry, TEXT("set_static_mesh_spawner_meshes"), Meshes)))) return false;

	auto Read = MakeShared<FJsonObject>();
	Read->SetStringField(TEXT("assetPath"), GraphPath);
	Read->SetStringField(TEXT("nodeName"), Spawner);
	const TSharedPtr<FJsonObject> Settings = Invoke(Registry, TEXT("read_pcg_node_settings"), Read);
	const TSharedPtr<FJsonObject>* All = nullptr;
	const TSharedPtr<FJsonObject>* Selector = nullptr;
	const TSharedPtr<FJsonObject>* Instanced = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	const bool bExpanded = Settings->TryGetObjectField(TEXT("settings"), All) && (*All)->TryGetObjectField(TEXT("MeshSelectorParameters"), Selector)
		&& (*Selector)->TryGetObjectField(TEXT("instanced"), Instanced) && (*Instanced)->TryGetArrayField(TEXT("meshEntries"), Entries);
	if (!TestTrue(TEXT("the selector is expanded with its entries"), bExpanded && Entries->Num() == 1)) return false;
	TestTrue(TEXT("the entry names its mesh"), (*Entries)[0]->AsObject()->GetStringField(TEXT("mesh")).Contains(TEXT("Cube")));
	TestEqual(TEXT("the entry carries its weight"), (*Entries)[0]->AsObject()->GetIntegerField(TEXT("weight")), 3);
	TestTrue(TEXT("the selector's own properties are listed"), (*Instanced)->HasField(TEXT("properties")));
	return true;
}

#endif
