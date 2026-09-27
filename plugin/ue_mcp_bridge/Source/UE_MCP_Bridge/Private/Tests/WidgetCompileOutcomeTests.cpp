#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Widget/WidgetGuidMap.h"
#include "Blueprint/UserWidget.h"
#include "WidgetBlueprintGeneratedClass.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetCompileOutcomeTest,
	"UE.MCP.Widget.CompileOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWidgetCompileOutcomeTest::RunTest(const FString& Parameters)
{
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UWidgetBlueprint::StaticClass(), TEXT("MCPCompileOutcome")),
		BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
	if (!TestNotNull(TEXT("transient widget blueprint exists"), Blueprint)) return false;
	const MCPWidgetGuidMap::FSyncReport Compiled = MCPWidgetGuidMap::CompileChecked(Blueprint);
	TestTrue(TEXT("valid blueprint invokes compiler"), Compiled.bCompileAttempted);
	TestTrue(TEXT("valid blueprint compiles"), Compiled.bCompiled);
	TestEqual(TEXT("valid blueprint has no compiler errors"), Compiled.CompileErrors, 0);

	// Exercise the failure response contract separately from GUID refusal.
	MCPWidgetGuidMap::FSyncReport Failed;
	Failed.bCompileAttempted = true;
	Failed.CompileErrors = 1;
	Failed.CompileMessages.Add(TEXT("Missing function in widget graph"));
	const TSharedPtr<FJsonObject> Error = MCPWidgetGuidMap::BlockedError(TEXT("/Game/TestWidget"), Failed)->AsObject();
	TestFalse(TEXT("compile failure is not successful"), Error->GetBoolField(TEXT("success")));
	TestFalse(TEXT("compile failure is not compiled"), Error->GetBoolField(TEXT("compiled")));
	TestEqual(TEXT("compiler error count survives"), Error->GetIntegerField(TEXT("errors")), 1);
	TestTrue(TEXT("compiler diagnostic survives"), Error->GetStringField(TEXT("error")).Contains(Failed.CompileMessages[0]));
	TestTrue(TEXT("failure explains unsaved in-memory edits"), Error->GetStringField(TEXT("error")).Contains(TEXT("In-memory edits remain")));
	TestFalse(TEXT("failure does not claim compiler never ran"), Error->GetStringField(TEXT("error")).Contains(TEXT("Nothing was compiled")));

	MCPWidgetGuidMap::FSyncReport Refused;
	Refused.Unusable.Add(TEXT("DuplicateVariable"));
	Refused.Defects.Add(TEXT("duplicate GUID"));
	const FString Refusal = MCPWidgetGuidMap::BlockedError(TEXT("/Game/TestWidget"), Refused)->AsObject()->GetStringField(TEXT("error"));
	TestTrue(TEXT("GUID refusal keeps its preflight explanation"), Refusal.Contains(TEXT("Nothing was compiled or saved")));
	return true;
}

#endif
