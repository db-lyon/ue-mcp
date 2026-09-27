// A number parameter reads only a JSON number or a numeric string. The engine's
// TryGetNumberField reads any string as 0, which made a node name such as
// "Alpha" resolve to index 0 in connect_expressions_in_function.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerParams.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNumberParamReadTest,
	"UE.MCP.Params.NumberReadsOnlyNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNumberParamReadTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetNumberField(TEXT("number"), 3);
	Params->SetStringField(TEXT("numeric"), TEXT(" 4 "));
	Params->SetStringField(TEXT("name"), TEXT("Alpha"));
	Params->SetStringField(TEXT("empty"), TEXT(""));
	Params->SetBoolField(TEXT("flag"), true);

	int32 Out = -1;
	TestTrue(TEXT("a JSON number reads"), TryGetNumberParam(Params, TEXT("number"), Out));
	TestEqual(TEXT("its value"), Out, 3);

	Out = -1;
	TestTrue(TEXT("a numeric string reads"), TryGetNumberParam(Params, TEXT("numeric"), Out));
	TestEqual(TEXT("its value"), Out, 4);

	Out = -1;
	TestFalse(TEXT("a name does not read as a number"), TryGetNumberParam(Params, TEXT("name"), Out));
	TestEqual(TEXT("and leaves the output alone"), Out, -1);

	TestFalse(TEXT("an empty string does not read"), TryGetNumberParam(Params, TEXT("empty"), Out));
	TestFalse(TEXT("a bool does not read"), TryGetNumberParam(Params, TEXT("flag"), Out));
	TestFalse(TEXT("a missing key does not read"), TryGetNumberParam(Params, TEXT("absent"), Out));

	TestEqual(TEXT("an optional number falls back on a name"), OptionalInt(Params, TEXT("name"), 7), 7);
	TestNotNull(TEXT("a required number refuses a name"), RequireNumber(Params, TEXT("name"), Out).Get());
	return true;
}

#endif
