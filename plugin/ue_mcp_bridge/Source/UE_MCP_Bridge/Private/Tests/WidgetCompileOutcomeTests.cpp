// #1195: a widget edit that breaks the compile is reported, left unsaved and
// carries its rollback; errors the blueprint already had do not block an edit.
#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Widget/WidgetGuidMap.h"
#include "Handlers/Widget/WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Tests/MCPScopedTestMount.h"

namespace MCPWidgetCompileOutcomeTests
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

TSharedPtr<FJsonObject> AddWidgetParams(const FString& AssetPath, const TCHAR* Class, const TCHAR* Name, const TCHAR* Parent)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), AssetPath);
	Params->SetStringField(TEXT("widgetClass"), Class);
	Params->SetStringField(TEXT("widgetName"), Name);
	if (Parent) Params->SetStringField(TEXT("parentWidgetName"), Parent);
	return Params;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetCompileOutcomeTest,
	"UE.MCP.Widget.CompileOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWidgetCompileOutcomeTest::RunTest(const FString& Parameters)
{
	using namespace MCPWidgetCompileOutcomeTests;
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPWidgetCompile_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPWidgetCompile"));
	const FString PackageName = Mount.RootPath + TEXT("WBP_CompileProbe");

	UPackage* Package = CreatePackage(*PackageName);
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(), Package, FName(TEXT("WBP_CompileProbe")), BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
	if (!TestNotNull(TEXT("widget blueprint fixture created"), Blueprint)) return false;
	if (!TestNotNull(TEXT("fixture has a widget tree"), Blueprint->WidgetTree.Get())) return false;
	FString FixtureSaveError;
	const bool bFixtureSaved = SaveAssetPackageChecked(Blueprint, FixtureSaveError);
	if (!TestTrue(TEXT("fixture saves: ") + FixtureSaveError, bFixtureSaved)) return false;

	FMCPHandlerRegistry Registry;
	FWidgetHandlers::RegisterHandlers(Registry);

	// 1. A clean edit compiles and saves.
	const TSharedPtr<FJsonObject> Root = Call(Registry, TEXT("add_widget"),
		AddWidgetParams(PackageName, TEXT("CanvasPanel"), TEXT("RootPanel"), nullptr));
	if (!TestTrue(TEXT("clean add_widget succeeds"), BoolField(Root, TEXT("success")))) return false;
	TestTrue(TEXT("clean add_widget reports compiled"), BoolField(Root, TEXT("compiled")));
	TestTrue(TEXT("clean add_widget saved"), BoolField(Root, TEXT("saved")));
	TestFalse(TEXT("package is clean after the save"), Package->IsDirty());

	// 2. An edit whose compile fails. The graph gets a call to an Actor function
	// with its Target pin unconnected, which a widget cannot satisfy as self.
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("fixture has an event graph"), EventGraph)) return false;
	UFunction* SetLocation = AActor::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(AActor, K2_SetActorLocation));
	if (!TestNotNull(TEXT("K2_SetActorLocation exists"), SetLocation)) return false;
	FGraphNodeCreator<UK2Node_CallFunction> Creator(*EventGraph);
	UK2Node_CallFunction* Broken = Creator.CreateNode();
	Broken->SetFromFunction(SetLocation);
	Creator.Finalize();

	const TSharedPtr<FJsonObject> Failed = Call(Registry, TEXT("add_widget"),
		AddWidgetParams(PackageName, TEXT("TextBlock"), TEXT("FailedLabel"), TEXT("RootPanel")));
	if (!TestTrue(TEXT("failing add_widget returns an object"), Failed.IsValid())) return false;
	TestFalse(TEXT("failing compile withdraws success"), BoolField(Failed, TEXT("success")));
	TestFalse(TEXT("failing compile reports compiled:false"), BoolField(Failed, TEXT("compiled")));
	TestFalse(TEXT("failing compile is not saved"), BoolField(Failed, TEXT("saved")));
	TestTrue(TEXT("failing compile says the edit is still applied"), BoolField(Failed, TEXT("appliedInMemory")));
	TestTrue(TEXT("failing compile reports compiler errors"), Failed->GetIntegerField(TEXT("compileErrors")) > 0);
	TestTrue(TEXT("every error is new without a baseline"),
		Failed->GetIntegerField(TEXT("introducedCompileErrors")) == Failed->GetIntegerField(TEXT("compileErrors")));
	TestTrue(TEXT("compiler messages are returned"), Failed->GetArrayField(TEXT("compileMessages")).Num() > 0);
	TestEqual(TEXT("compileStatus is error"), Failed->GetStringField(TEXT("compileStatus")), FString(TEXT("error")));
	TestTrue(TEXT("rollback is carried"), Failed->HasTypedField<EJson::Object>(TEXT("rollback")));
	TestTrue(TEXT("package stays dirty because nothing was written"), Package->IsDirty());
	// The response says the edit is applied in memory; the tree must agree.
	TestNotNull(TEXT("the unsaved widget is in the in-memory tree"),
		Blueprint->WidgetTree->FindWidget(FName(TEXT("FailedLabel"))));

	// 3. The blueprint is now BS_Error for a reason unrelated to the next edit.
	// The baseline taken before it accounts for every error, so it saves.
	const TSharedPtr<FJsonObject> Unrelated = Call(Registry, TEXT("add_widget"),
		AddWidgetParams(PackageName, TEXT("TextBlock"), TEXT("UnrelatedLabel"), TEXT("RootPanel")));
	if (!TestTrue(TEXT("unrelated add_widget returns an object"), Unrelated.IsValid())) return false;
	TestTrue(TEXT("pre-existing errors do not block an unrelated edit"), BoolField(Unrelated, TEXT("success")));
	TestFalse(TEXT("an edit outside play carries no play warning (#1553)"), Unrelated->HasField(TEXT("playSessionWarning")));
	TestTrue(TEXT("the unrelated edit is saved"), BoolField(Unrelated, TEXT("saved")));
	TestFalse(TEXT("the blueprint still does not compile"), BoolField(Unrelated, TEXT("compiled")));
	TestEqual(TEXT("no error is attributed to the unrelated edit"), Unrelated->GetIntegerField(TEXT("introducedCompileErrors")), 0);
	TestTrue(TEXT("pre-existing errors are counted"), Unrelated->GetIntegerField(TEXT("preexistingCompileErrors")) > 0);
	TestTrue(TEXT("a compile warning names them"), Unrelated->HasTypedField<EJson::String>(TEXT("compileWarning")));
	TestFalse(TEXT("package is clean after the save"), Package->IsDirty());

	// 4. The GUID preflight refusal keeps its own wording.
	MCPWidgetGuidMap::FSyncReport Refused;
	Refused.Unusable.Add(TEXT("DuplicateVariable"));
	Refused.Defects.Add(TEXT("duplicate GUID"));
	TestTrue(TEXT("a report that never compiled reads as refused"), Refused.WasRefused());
	const FString Refusal = MCPWidgetGuidMap::BlockedError(TEXT("/Game/TestWidget"), Refused)->AsObject()->GetStringField(TEXT("error"));
	TestTrue(TEXT("GUID refusal keeps its preflight explanation"), Refusal.Contains(TEXT("Nothing was compiled or saved")));
	return true;
}

// #1553: a compile that still runs during play reports the reinstanced widgets.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetCompileDuringPlayTest,
	"UE.MCP.Widget.CompileDuringPlayWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWidgetCompileDuringPlayTest::RunTest(const FString& Parameters)
{
	MCPWidgetGuidMap::FSyncReport Editing;
	Editing.bCompileAttempted = true;
	Editing.bCompiled = true;
	TSharedPtr<FJsonObject> Quiet = MakeShared<FJsonObject>();
	MCPSetWidgetGuidOutcome(Quiet, Editing, TEXT("/Game/WBP_Page"));
	TestFalse(TEXT("no warning outside play"), Quiet->HasField(TEXT("playSessionWarning")));
	TestFalse(TEXT("no reinstance count outside play"), Quiet->HasField(TEXT("reinstancedPlayWidgets")));

	MCPWidgetGuidMap::FSyncReport Playing = Editing;
	Playing.bCompiledDuringPlay = true;
	Playing.LivePlayWidgets = 2;
	TSharedPtr<FJsonObject> Warned = MakeShared<FJsonObject>();
	Warned->SetBoolField(TEXT("success"), true);
	MCPSetWidgetGuidOutcome(Warned, Playing, TEXT("/Game/WBP_Page"));
	TestEqual(TEXT("reinstanced widgets are counted"), Warned->GetIntegerField(TEXT("reinstancedPlayWidgets")), 2);
	const FString Warning = Warned->GetStringField(TEXT("playSessionWarning"));
	TestTrue(TEXT("the warning names the asset"), Warning.Contains(TEXT("/Game/WBP_Page")));
	TestTrue(TEXT("the warning gives the count"), Warning.Contains(TEXT("2 live widget(s)")));
	TestTrue(TEXT("the warning names the way to avoid it"), Warning.Contains(TEXT("pieAction=\"stop\"")));
	TestTrue(TEXT("the warning does not withdraw success"), Warned->GetBoolField(TEXT("success")));

	TestTrue(TEXT("no live widget reads as none found"),
		MCPWidgetPlaySessionWarning(TEXT("/Game/WBP_Page"), 0).Contains(TEXT("no live widget made from it")));
	return true;
}

// #1553: set_property during play writes the live widget and leaves its runtime
// content alone; the compile and save wait for play to end.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetSetPropertyDuringPlayTest,
	"UE.MCP.Widget.SetPropertyDuringPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWidgetSetPropertyDuringPlayTest::RunTest(const FString& Parameters)
{
	using namespace MCPWidgetCompileOutcomeTests;
	FMCPScopedTestMount Mount(
		TEXT("/UEMCPWidgetPlay_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPWidgetPlay"));
	const FString PackageName = Mount.RootPath + TEXT("WBP_PlayProbe");

	UPackage* Package = CreatePackage(*PackageName);
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(), Package, FName(TEXT("WBP_PlayProbe")), BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
	if (!TestNotNull(TEXT("widget blueprint fixture created"), Blueprint)) return false;
	FString FixtureSaveError;
	if (!TestTrue(TEXT("fixture saves"), SaveAssetPackageChecked(Blueprint, FixtureSaveError))) return false;

	FMCPHandlerRegistry Registry;
	FWidgetHandlers::RegisterHandlers(Registry);
	if (!TestTrue(TEXT("root panel added"), BoolField(Call(Registry, TEXT("add_widget"),
		AddWidgetParams(PackageName, TEXT("VerticalBox"), TEXT("Page"), nullptr)), TEXT("success")))) return false;
	if (!TestTrue(TEXT("label added"), BoolField(Call(Registry, TEXT("add_widget"),
		AddWidgetParams(PackageName, TEXT("TextBlock"), TEXT("Title"), TEXT("Page"))), TEXT("success")))) return false;

	// The live screen, with a child the game added at runtime.
	UClass* ClassBefore = Blueprint->GeneratedClass;
	UUserWidget* Live = NewObject<UUserWidget>(GetTransientPackage(), ClassBefore);
	if (!TestTrue(TEXT("live widget initializes"), Live->Initialize())) return false;
	Live->TakeWidget();
	UPanelWidget* LivePage = Cast<UPanelWidget>(Live->GetWidgetFromName(TEXT("Page")));
	if (!TestNotNull(TEXT("live page exists"), LivePage)) return false;
	UTextBlock* RuntimeChild = NewObject<UTextBlock>(Live->WidgetTree);
	LivePage->AddChild(RuntimeChild);
	const int32 ChildrenBefore = LivePage->GetChildrenCount();

	TSharedPtr<FJsonObject> Result;
	{
		MCPWidgetPlaySession::FScopedTestPlaySession Play({ Live });
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), PackageName);
		Params->SetStringField(TEXT("widgetName"), TEXT("Title"));
		Params->SetStringField(TEXT("propertyName"), TEXT("Text"));
		Params->SetStringField(TEXT("value"), TEXT("Live value"));
		Result = Call(Registry, TEXT("set_widget_property"), Params);
	}
	if (!TestTrue(TEXT("set_property during play succeeds"), BoolField(Result, TEXT("success")))) return false;
	TestTrue(TEXT("the compile is deferred"), BoolField(Result, TEXT("compileDeferred")));
	TestFalse(TEXT("nothing is saved during play"), BoolField(Result, TEXT("saved")));
	TestEqual(TEXT("the live widget was written"), Result->GetIntegerField(TEXT("liveWidgetsUpdated")), 1);
	TestFalse(TEXT("no reinstance warning, nothing was compiled"), Result->HasField(TEXT("playSessionWarning")));
	TestTrue(TEXT("the result says why"), Result->HasTypedField<EJson::String>(TEXT("playSessionNote")));

	TestTrue(TEXT("the class was not regenerated"), Blueprint->GeneratedClass == ClassBefore && Live->GetClass() == ClassBefore);
	UTextBlock* LiveTitle = Cast<UTextBlock>(Live->GetWidgetFromName(TEXT("Title")));
	if (!TestNotNull(TEXT("live title still exists"), LiveTitle)) return false;
	TestEqual(TEXT("the live widget shows the new value"), LiveTitle->GetText().ToString(), FString(TEXT("Live value")));
	TestEqual(TEXT("runtime content survives"), LivePage->GetChildrenCount(), ChildrenBefore);
	TestTrue(TEXT("the runtime child is still parented"), RuntimeChild->GetParent() == LivePage);
	TestTrue(TEXT("the blueprint waits for its compile"), MCPWidgetPlaySession::IsDeferred(Blueprint));
	TestTrue(TEXT("the package holds the unsaved edit"), Package->IsDirty());

	// Play ends: the deferred compile runs and saves the template edit.
	MCPWidgetPlaySession::FlushDeferred();
	TestFalse(TEXT("nothing is left deferred"), MCPWidgetPlaySession::IsDeferred(Blueprint));
	TestFalse(TEXT("the edit is saved once play ends"), Package->IsDirty());
	TestTrue(TEXT("the blueprint compiles once play ends"), Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings);
	UTextBlock* Template = Cast<UTextBlock>(Blueprint->WidgetTree->FindWidget(FName(TEXT("Title"))));
	if (!TestNotNull(TEXT("template title exists"), Template)) return false;
	TestEqual(TEXT("the template keeps the value"), Template->GetText().ToString(), FString(TEXT("Live value")));
	return true;
}

#endif
