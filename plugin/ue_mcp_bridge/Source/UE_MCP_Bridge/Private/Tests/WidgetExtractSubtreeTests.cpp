#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWidgetExtractSubtreeRegistrationTest,
	"UE.MCP.Widget.ExtractSubtree.RegistrationAndPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWidgetExtractSubtreeRegistrationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FWidgetHandlers::RegisterHandlers(Registry);

	TestTrue(TEXT("canonical handler is registered"), Registry.HasHandler(TEXT("extract_widget_subtree")));

	const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(
		TEXT("extract_widget_subtree"), MakeShared<FJsonObject>());
	TestTrue(TEXT("missing-parameter preflight returns an object"), Response.IsValid() && Response->Type == EJson::Object);
	if (Response.IsValid() && Response->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Response->AsObject();
		TestFalse(TEXT("invalid preflight is unsuccessful"), Object->GetBoolField(TEXT("success")));
		TestTrue(TEXT("invalid preflight identifies the first required field"),
			Object->GetStringField(TEXT("error")).Contains(TEXT("sourceAssetPath")));
	}
	return true;
}

#endif
