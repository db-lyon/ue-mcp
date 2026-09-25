// Engine-free coverage for reflection actions that must refuse before they
// touch the project: nothing here writes an ini or loads content.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/ReflectionHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReflectionCreateTagRefusesInvalidTest,
	"UE.MCP.Reflection.CreateTag.RefusesInvalidTagBeforeWriting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReflectionCreateTagRefusesInvalidTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FReflectionHandlers::RegisterHandlers(Registry);

	// #1106: an invalid tag string is refused up front with the manager's
	// reason, rather than written to the ini and failing later.
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("tag"), TEXT(".UEMCP Invalid Tag."));
	const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("create_gameplay_tag"), Params);
	if (TestTrue(TEXT("create_gameplay_tag returns an object"), Response.IsValid() && Response->Type == EJson::Object))
	{
		const TSharedPtr<FJsonObject> Object = Response->AsObject();
		TestFalse(TEXT("an invalid tag is refused"), Object->GetBoolField(TEXT("success")));
		TestTrue(TEXT("the refusal says the tag is invalid"),
			Object->GetStringField(TEXT("error")).Contains(TEXT("not a valid gameplay tag")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReflectionListStructsTest,
	"UE.MCP.Reflection.ListStructs.FiltersByPackageAndName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReflectionListStructsTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FReflectionHandlers::RegisterHandlers(Registry);

	// #1088: a bare module name resolves to /Script/<Module>, and the row
	// carries both the registered name and the F-prefixed C++ name.
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("package"), TEXT("Engine"));
	Params->SetStringField(TEXT("filter"), TEXT("FTableRowBase"));
	const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("list_structs"), Params);
	if (!TestTrue(TEXT("list_structs returns an object"), Response.IsValid() && Response->Type == EJson::Object))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Object = Response->AsObject();
	TestTrue(TEXT("list_structs succeeds"), Object->GetBoolField(TEXT("success")));

	const TArray<TSharedPtr<FJsonValue>>* Structs = nullptr;
	if (!TestTrue(TEXT("rows come back under structs"), Object->TryGetArrayField(TEXT("structs"), Structs) && Structs))
	{
		return false;
	}

	bool bFound = false;
	for (const TSharedPtr<FJsonValue>& Value : *Structs)
	{
		const TSharedPtr<FJsonObject> Row = Value->AsObject();
		TestEqual(TEXT("every row is in /Script/Engine"), Row->GetStringField(TEXT("package")), FString(TEXT("/Script/Engine")));
		if (Row->GetStringField(TEXT("path")) == TEXT("/Script/Engine.TableRowBase"))
		{
			bFound = true;
			TestEqual(TEXT("name is the registered spelling"), Row->GetStringField(TEXT("name")), FString(TEXT("TableRowBase")));
			TestEqual(TEXT("cppName carries the F prefix"), Row->GetStringField(TEXT("cppName")), FString(TEXT("FTableRowBase")));
			TestTrue(TEXT("TableRowBase is a table row"), Row->GetBoolField(TEXT("tableRow")));
		}
	}
	TestTrue(TEXT("FTableRowBase is found through its F-prefixed name"), bFound);

	return true;
}

#endif
