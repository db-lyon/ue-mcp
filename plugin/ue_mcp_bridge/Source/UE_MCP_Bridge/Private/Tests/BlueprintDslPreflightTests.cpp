// #1908: a Graph DSL rewrite deletes the loose nodes, comment boxes and
// collapsed graphs its text does not carry. blueprint(preflight_dsl_write)
// lists them, and epic(call_tool) refuses a BlueprintDslToolset Write or Edit
// that would lose any unless allowDestructive is passed; a construction script
// tombstone is refused outright. The guard runs before the toolset registry is
// asked, so it is testable on every engine. Assets live in a private temp mount.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerDslPreflight.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Handlers/Epic/EpicHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPDslPreflightTests
{
TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
{
	const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Params);
	return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
}

bool BoolField(const TSharedPtr<FJsonObject>& Result, const TCHAR* Field)
{
	bool bValue = false;
	return Result.IsValid() && Result->TryGetBoolField(Field, bValue) && bValue;
}

bool ListsField(const TSharedPtr<FJsonObject>& Report, const TCHAR* ArrayField, const TCHAR* Key, const FString& Value)
{
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Report.IsValid() || !Report->TryGetArrayField(ArrayField, Rows) || !Rows) return false;
	for (const TSharedPtr<FJsonValue>& Row : *Rows)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FString Found;
		if (Row.IsValid() && Row->TryGetObject(Object) && Object && (*Object)->TryGetStringField(Key, Found) && Found == Value) return true;
	}
	return false;
}

int32 Count(const TSharedPtr<FJsonObject>& Report, const TCHAR* ArrayField)
{
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	return Report.IsValid() && Report->TryGetArrayField(ArrayField, Rows) && Rows ? Rows->Num() : -1;
}

TSharedPtr<FJsonObject> Preflight(const FString& PackageName, const FString& Dsl)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), PackageName);
	Params->SetStringField(TEXT("dsl"), Dsl);
	return Params;
}

TSharedPtr<FJsonObject> EpicWrite(const FString& PackageName, const FString& Dsl, bool bAllowDestructive)
{
	TSharedPtr<FJsonObject> Input = MakeShared<FJsonObject>();
	Input->SetStringField(TEXT("assetPath"), PackageName);
	Input->SetStringField(TEXT("dsl"), Dsl);
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("toolset"), TEXT("BlueprintToolsets.BlueprintDslToolset"));
	Params->SetStringField(TEXT("tool"), TEXT("Write"));
	Params->SetObjectField(TEXT("input"), Input);
	if (bAllowDestructive) Params->SetBoolField(TEXT("allowDestructive"), true);
	return Params;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintDslPreflightTest,
	"UE.MCP.Blueprint.DslPreflight.RefusesLossyRewrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintDslPreflightTest::RunTest(const FString& Parameters)
{
	using namespace MCPDslPreflightTests;
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPDslPreflight_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPDslPreflight"));
	const FString PackageName = Mount.RootPath + TEXT("BP_DslPreflight");

	UPackage* Package = CreatePackage(*PackageName);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), Package, FName(TEXT("BP_DslPreflight")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("Blueprint fixture created"), Blueprint)) return false;
	if (!TestTrue(TEXT("fixture has an event graph"), Blueprint->UbergraphPages.Num() > 0)) return false;
	UEdGraph* Graph = Blueprint->UbergraphPages[0];
	const FString GraphName = Graph->GetName();

	FMCPHandlerRegistry Registry;
	FBlueprintHandlers::RegisterHandlers(Registry);
	FEpicHandlers::RegisterHandlers(Registry);

	// A Print String no entry reaches: a loose impure node.
	TSharedPtr<FJsonObject> Add = MakeShared<FJsonObject>();
	Add->SetStringField(TEXT("assetPath"), PackageName);
	Add->SetStringField(TEXT("graphName"), GraphName);
	Add->SetStringField(TEXT("nodeClass"), TEXT("CallFunction"));
	Add->SetBoolField(TEXT("save"), false);
	TSharedPtr<FJsonObject> NodeParams = MakeShared<FJsonObject>();
	NodeParams->SetStringField(TEXT("functionName"), TEXT("PrintString"));
	NodeParams->SetStringField(TEXT("className"), TEXT("/Script/Engine.KismetSystemLibrary"));
	Add->SetObjectField(TEXT("nodeParams"), NodeParams);
	const TSharedPtr<FJsonObject> Added = Call(Registry, TEXT("add_node"), Add);
	if (!TestTrue(TEXT("add_node succeeds"), BoolField(Added, TEXT("success")))) return false;
	const FString LooseId = Added->GetStringField(TEXT("nodeId"));

	// A comment box away from every node.
	UEdGraphNode_Comment* Comment = NewObject<UEdGraphNode_Comment>(Graph);
	Comment->CreateNewGuid();
	Comment->NodeComment = TEXT("Keep this note");
	Comment->NodePosX = 5000;
	Comment->NodePosY = 5000;
	Comment->NodeWidth = 200;
	Comment->NodeHeight = 100;
	Graph->AddNode(Comment, /*bFromUI*/ false, /*bSelectNewNode*/ false);
	const FString CommentId = Comment->NodeGuid.ToString();

	// A write of the BeginPlay entry rewrites the event graph.
	const FString Dsl = TEXT("(event `self.ReceiveBeginPlay`\n  (`KismetSystemLibrary.PrintString` \"hi\"))");
	const TSharedPtr<FJsonObject> Report = Call(Registry, TEXT("preflight_dsl_write"), Preflight(PackageName, Dsl));
	if (!TestTrue(TEXT("preflight_dsl_write succeeds"), BoolField(Report, TEXT("success")))) return false;
	TestTrue(TEXT("preflight reports destructive"), BoolField(Report, TEXT("destructive")));
	TestTrue(TEXT("the event graph is a target"), Report->GetArrayField(TEXT("graphs")).ContainsByPredicate(
		[&GraphName](const TSharedPtr<FJsonValue>& V) { return V.IsValid() && V->AsString() == GraphName; }));
	TestTrue(TEXT("the loose Print String is listed"), ListsField(Report, TEXT("looseNodes"), TEXT("nodeId"), LooseId));
	TestTrue(TEXT("the comment box is listed"), ListsField(Report, TEXT("commentBoxes"), TEXT("nodeId"), CommentId));
	TestTrue(TEXT("a way forward is offered"), Report->HasField(TEXT("wayForward")));
	TestFalse(TEXT("no construction script tombstone"), BoolField(Report, TEXT("constructionScriptTombstone")));

	// The same document carrying the box's text as a '; text' run keeps it.
	const TSharedPtr<FJsonObject> WithNote = Call(Registry, TEXT("preflight_dsl_write"),
		Preflight(PackageName, TEXT("; Keep this note\n") + Dsl));
	TestEqual(TEXT("a comment the text carries is not listed"), Count(WithNote, TEXT("commentBoxes")), 0);
	TestTrue(TEXT("the loose node is still listed"), ListsField(WithNote, TEXT("looseNodes"), TEXT("nodeId"), LooseId));

	// A function entry touches only its graph, so the event graph is not reported.
	const TSharedPtr<FJsonObject> OtherGraph = Call(Registry, TEXT("preflight_dsl_write"),
		Preflight(PackageName, TEXT("(fn `UserConstructionScript`)")));
	TestFalse(TEXT("rewriting the construction script loses nothing here"), BoolField(OtherGraph, TEXT("destructive")));

	// An Edit with no entry head in it is checked against every graph, and a
	// box no reached node sits under is one Read cannot have written back.
	TSharedPtr<FJsonObject> EditParams = MakeShared<FJsonObject>();
	EditParams->SetStringField(TEXT("assetPath"), PackageName);
	TSharedPtr<FJsonObject> Replacement = MakeShared<FJsonObject>();
	Replacement->SetStringField(TEXT("oldString"), TEXT("\"hi\""));
	Replacement->SetStringField(TEXT("newString"), TEXT("\"hello\""));
	EditParams->SetArrayField(TEXT("edits"), { MakeShared<FJsonValueObject>(Replacement) });
	const TSharedPtr<FJsonObject> EditReport = Call(Registry, TEXT("preflight_dsl_write"), EditParams);
	TestEqual(TEXT("edit preflight runs in edit mode"), EditReport->GetStringField(TEXT("mode")), FString(TEXT("edit")));
	TestTrue(TEXT("edit preflight lists the loose node"), ListsField(EditReport, TEXT("looseNodes"), TEXT("nodeId"), LooseId));
	TestTrue(TEXT("edit preflight lists the uncovered box"), ListsField(EditReport, TEXT("commentBoxes"), TEXT("nodeId"), CommentId));

	const int32 NodesBefore = Graph->Nodes.Num();

	// epic(call_tool) refuses the lossy Write before the registry is asked.
	const TSharedPtr<FJsonObject> Refused = Call(Registry, TEXT("epic_call_tool"), EpicWrite(PackageName, Dsl, false));
	if (!TestNotNull(TEXT("epic_call_tool answers"), Refused.Get())) return false;
	TestFalse(TEXT("the lossy Write is refused"), BoolField(Refused, TEXT("success")));
	TestEqual(TEXT("refusal reason"), Refused->GetStringField(TEXT("reason")), FString(TEXT("dsl_rewrite_destructive")));
	TestTrue(TEXT("refusal names allowDestructive as the way forward"), Refused->GetStringField(TEXT("error")).Contains(TEXT("allowDestructive=true")));
	const TSharedPtr<FJsonObject>* RefusedReport = nullptr;
	TestTrue(TEXT("refusal carries the preflight"), Refused->TryGetObjectField(TEXT("preflight"), RefusedReport) && RefusedReport
		&& ListsField(*RefusedReport, TEXT("looseNodes"), TEXT("nodeId"), LooseId));
	TestEqual(TEXT("nothing in the graph changed"), Graph->Nodes.Num(), NodesBefore);

	// With allowDestructive the guard lets the call through to the registry.
	// Asked of the guard itself: whether the toolset is there to answer next
	// depends on the engine and its enabled plugins.
	FString InputJson;
	{
		const TSharedPtr<FJsonObject> Wire = EpicWrite(PackageName, Dsl, true);
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&InputJson);
		FJsonSerializer::Serialize(Wire->GetObjectField(TEXT("input")).ToSharedRef(), Writer);
	}
	TestTrue(TEXT("the guard refuses the same input without allowDestructive"),
		MCPDslPreflight::GuardCall(TEXT("BlueprintToolsets.BlueprintDslToolset"), TEXT("Write"), InputJson, false).IsValid());
	TestFalse(TEXT("allowDestructive passes the guard"),
		MCPDslPreflight::GuardCall(TEXT("BlueprintToolsets.BlueprintDslToolset"), TEXT("Write"), InputJson, true).IsValid());
	TestFalse(TEXT("a tool that is not a DSL rewrite is not guarded"),
		MCPDslPreflight::GuardCall(TEXT("BlueprintToolsets.BlueprintDslToolset"), TEXT("Read"), InputJson, false).IsValid());

	// A construction script tombstone is refused whatever allowDestructive says.
	const FString Tombstone = TEXT("(fn `UserConstructionScript` :!delete)");
	const TSharedPtr<FJsonObject> TombReport = Call(Registry, TEXT("preflight_dsl_write"), Preflight(PackageName, Tombstone));
	TestTrue(TEXT("preflight flags the tombstone"), BoolField(TombReport, TEXT("constructionScriptTombstone")));
	TestTrue(TEXT("preflight explains the refusal"), TombReport->HasField(TEXT("refusal")));
	const TSharedPtr<FJsonObject> TombRefused = Call(Registry, TEXT("epic_call_tool"), EpicWrite(PackageName, Tombstone, true));
	TestFalse(TEXT("the tombstone is refused"), BoolField(TombRefused, TEXT("success")));
	TestEqual(TEXT("tombstone refusal reason"), TombRefused->GetStringField(TEXT("reason")), FString(TEXT("construction_script_tombstone")));
	TestTrue(TEXT("the refusal offers the empty-body form"), TombRefused->GetStringField(TEXT("error")).Contains(TEXT("no statements")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
