// The runtime widget actions resolve their PIE world from pieInstance, so a
// caller naming a client is answered about that client, never the server. No
// PIE runs here: an instance that does not exist must be refused by name.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Widget/WidgetHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPWidgetRuntimeWorldPieInstanceTest,
	"UE.MCP.Widget.RuntimeWorld.HonoursPieInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPWidgetRuntimeWorldPieInstanceTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FWidgetHandlers::RegisterHandlers(Registry);

	for (const TCHAR* Method : { TEXT("list_runtime_widgets"), TEXT("get_runtime_widget"),
		TEXT("add_to_viewport"), TEXT("invoke_runtime_function"), TEXT("get_runtime_delegates") })
	{
		auto Args = MakeShared<FJsonObject>();
		Args->SetNumberField(TEXT("pieInstance"), 97);
		Args->SetStringField(TEXT("widgetName"), TEXT("WBP_NoSuchWidget"));
		Args->SetStringField(TEXT("assetPath"), TEXT("/Game/NoSuchWidget"));
		Args->SetStringField(TEXT("functionName"), TEXT("NoSuchFunction"));
		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(Method, Args);
		const TSharedPtr<FJsonObject> Result = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s answered"), Method), Result.Get())) continue;
		bool bSuccess = true;
		Result->TryGetBoolField(TEXT("success"), bSuccess);
		TestFalse(FString::Printf(TEXT("%s fails for a PIE instance that is not running"), Method), bSuccess);
		FString Error;
		Result->TryGetStringField(TEXT("error"), Error);
		TestTrue(FString::Printf(TEXT("%s names the pieInstance it could not find (%s)"), Method, *Error), Error.Contains(TEXT("pieInstance")));
	}
	return true;
}

#endif
