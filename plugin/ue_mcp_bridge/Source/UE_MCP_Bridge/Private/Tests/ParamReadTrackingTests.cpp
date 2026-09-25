// #1057: a parameter that reaches a handler and is never read had no effect,
// and the caller used to get a plain success. Handlers of a reporting category
// now come back with `paramsNotRead`. These pin that down: an unread key is
// named, a read key is not, routing names are never blamed, a nested key is
// not a top-level one, a failure is left alone, and a category outside the
// pilot reports nothing.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/AnimationHandlers.h"
#include "Misc/AutomationTest.h"

namespace MCPParamReadTests
{
	/** Reads `readKey`, and `inner` off the `nested` object. */
	TSharedPtr<FJsonValue> ProbeHandler(const TSharedPtr<FJsonObject>& Params)
	{
		OptionalString(Params, TEXT("readKey"));
		const TSharedPtr<FJsonObject>* Nested = nullptr;
		if (TryGetObjectParam(Params, TEXT("nested"), Nested) && Nested)
		{
			OptionalString(*Nested, TEXT("inner"));
		}
		return MCPResult(MCPSuccess());
	}

	TSharedPtr<FJsonValue> FailingProbeHandler(const TSharedPtr<FJsonObject>& Params)
	{
		OptionalString(Params, TEXT("readKey"));
		return MCPError(TEXT("probe failure"));
	}

	TSharedPtr<FJsonObject> MakeProbeParams()
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("readKey"), TEXT("value"));
		Params->SetNumberField(TEXT("unreadKey"), 1.0);
		TSharedPtr<FJsonObject> Nested = MakeShared<FJsonObject>();
		Nested->SetStringField(TEXT("inner"), TEXT("read"));
		Nested->SetStringField(TEXT("other"), TEXT("never read, but not top-level"));
		Params->SetObjectField(TEXT("nested"), Nested);
		for (const FString& Routing : MCPRoutingParamNames())
		{
			Params->SetStringField(Routing, TEXT("routing"));
		}
		return Params;
	}

	/** The `paramsNotRead` names on a result, and whether the field was there at all. */
	TArray<FString> NotReadOf(const TSharedPtr<FJsonValue>& Result, bool& bOutPresent)
	{
		TArray<FString> Names;
		bOutPresent = false;
		if (!Result.IsValid() || Result->Type != EJson::Object) return Names;
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Result->AsObject()->TryGetArrayField(TEXT("paramsNotRead"), Array) || !Array) return Names;
		bOutPresent = true;
		for (const TSharedPtr<FJsonValue>& Entry : *Array)
		{
			FString Name;
			if (Entry.IsValid() && Entry->TryGetString(Name)) Names.Add(Name);
		}
		return Names;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPParamReadTrackingTest,
	"UE.MCP.Bridge.ParamRead.Tracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPParamReadTrackingTest::RunTest(const FString& Parameters)
{
	using namespace MCPParamReadTests;

	TestTrue(TEXT("animation is a reporting category"), FMCPHandlerRegistry::ReportsUnreadParams(TEXT("animation")));
	TestFalse(TEXT("level is not"), FMCPHandlerRegistry::ReportsUnreadParams(TEXT("level")));

	FMCPHandlerRegistry Registry;
	{
		FMCPHandlerRegistry::FCategoryScope Scope(Registry, TEXT("animation"));
		Registry.RegisterHandler(TEXT("mcp_test_anim_probe"), &ProbeHandler);
		Registry.RegisterHandler(TEXT("mcp_test_anim_failing_probe"), &FailingProbeHandler);
	}
	{
		FMCPHandlerRegistry::FCategoryScope Scope(Registry, TEXT("level"));
		Registry.RegisterHandler(TEXT("mcp_test_level_probe"), &ProbeHandler);
	}
	Registry.RegisterHandler(TEXT("mcp_test_untagged_probe"), &ProbeHandler);

	// An unread key is reported, and only it: the read key, the nested object's
	// keys and every routing name stay out of the list.
	{
		bool bPresent = false;
		const TArray<FString> NotRead = NotReadOf(
			Registry.ExecuteHandler(TEXT("mcp_test_anim_probe"), MakeProbeParams()), bPresent);
		TestTrue(TEXT("a pilot handler reports paramsNotRead"), bPresent);
		TestEqual(TEXT("exactly one key is unread"), NotRead.Num(), 1);
		TestTrue(TEXT("and it is unreadKey"), NotRead.Contains(TEXT("unreadKey")));
		TestFalse(TEXT("readKey was read"), NotRead.Contains(TEXT("readKey")));
		TestFalse(TEXT("nested was read"), NotRead.Contains(TEXT("nested")));
		TestFalse(TEXT("a nested key is not a top-level key"), NotRead.Contains(TEXT("other")));
		for (const FString& Routing : MCPRoutingParamNames())
		{
			TestFalse(*FString::Printf(TEXT("routing name '%s' is never reported"), *Routing), NotRead.Contains(Routing));
		}
	}

	// Every key read: no field at all.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("readKey"), TEXT("value"));
		Params->SetNumberField(TEXT("timeoutMs"), 5000.0);
		bool bPresent = true;
		NotReadOf(Registry.ExecuteHandler(TEXT("mcp_test_anim_probe"), Params), bPresent);
		TestFalse(TEXT("nothing unread means no paramsNotRead"), bPresent);
	}

	// A failure is returned as the handler wrote it.
	{
		bool bPresent = true;
		NotReadOf(Registry.ExecuteHandler(TEXT("mcp_test_anim_failing_probe"), MakeProbeParams()), bPresent);
		TestFalse(TEXT("a failed call carries no paramsNotRead"), bPresent);
	}

	// Outside the pilot, the same handler reports nothing.
	{
		bool bPresent = true;
		NotReadOf(Registry.ExecuteHandler(TEXT("mcp_test_level_probe"), MakeProbeParams()), bPresent);
		TestFalse(TEXT("a non-pilot category reports nothing"), bPresent);
		bPresent = true;
		NotReadOf(Registry.ExecuteHandler(TEXT("mcp_test_untagged_probe"), MakeProbeParams()), bPresent);
		TestFalse(TEXT("an untagged handler reports nothing"), bPresent);
	}

	// The scope closes with the dispatch, and without one a read notes nothing.
	TestNull(TEXT("no scope outlives its dispatch"), FMCPParamReadScope::Active());
	TestEqual(TEXT("helpers still read with no scope open"),
		OptionalString(MakeProbeParams(), TEXT("readKey")), FString(TEXT("value")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPParamReadAnimationActionTest,
	"UE.MCP.Bridge.ParamRead.AnimationAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPParamReadAnimationActionTest::RunTest(const FString& Parameters)
{
	using namespace MCPParamReadTests;

	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);

	// A folder that holds nothing, so the listing succeeds in any project.
	const FString EmptyDirectory = TEXT("/Game/UEMCP/ParamReadTest_NoSuchFolder");

	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("directory"), EmptyDirectory);
		Params->SetNumberField(TEXT("limit"), 10.0);
		Params->SetBoolField(TEXT("bogusParam"), true);
		Params->SetNumberField(TEXT("timeoutMs"), 5000.0);
		const TSharedPtr<FJsonValue> Result = Registry.ExecuteHandler(TEXT("list_anim_assets"), Params);
		TestTrue(TEXT("list_anim_assets answers an object"), Result.IsValid() && Result->Type == EJson::Object);
		bool bSuccess = false;
		if (Result.IsValid() && Result->Type == EJson::Object)
		{
			Result->AsObject()->TryGetBoolField(TEXT("success"), bSuccess);
		}
		TestTrue(TEXT("list_anim_assets succeeds on an empty folder"), bSuccess);

		bool bPresent = false;
		const TArray<FString> NotRead = NotReadOf(Result, bPresent);
		TestTrue(TEXT("an unread key on an animation action is reported"), bPresent);
		TestEqual(TEXT("only the unread key"), NotRead.Num(), 1);
		TestTrue(TEXT("and it is bogusParam"), NotRead.Contains(TEXT("bogusParam")));
	}

	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("directory"), EmptyDirectory);
		Params->SetNumberField(TEXT("limit"), 10.0);
		bool bPresent = true;
		NotReadOf(Registry.ExecuteHandler(TEXT("list_anim_assets"), Params), bPresent);
		TestFalse(TEXT("an animation action that read every key reports nothing"), bPresent);
	}
	return true;
}

#endif
