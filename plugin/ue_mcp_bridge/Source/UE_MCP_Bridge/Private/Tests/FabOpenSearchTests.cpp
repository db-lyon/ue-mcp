// fab_open_search's open path (#1485). The launcher is swapped through
// FFabHandlers::SetUrlLauncherForTesting, so no browser opens. The URL
// building with open=false is covered by tests/live/bcov-fab.test.ts.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Fab/FabHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPFabOpenSearchRun(FMCPHandlerRegistry& Registry, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("fab_open_search"), Request);
		if (!Response.IsValid() || Response->Type != EJson::Object) return nullptr;
		return Response->AsObject();
	}

	struct FMCPFabLauncherRestore
	{
		~FMCPFabLauncherRestore() { FFabHandlers::SetUrlLauncherForTesting(nullptr); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFabOpenSearchLaunchesTest,
	"UE.MCP.Fab.OpenSearch.LaunchesTheSearchUrl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFabOpenSearchLaunchesTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FFabHandlers::RegisterHandlers(Registry);
	FMCPFabLauncherRestore Restore;

	TArray<FString> Launched;
	FFabHandlers::SetUrlLauncherForTesting([&Launched](const FString& Url, FString& OutError)
	{
		Launched.Add(Url);
	});

	// The default call opens the page: the launcher gets the URL it reports.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("query"), TEXT("stone wall"));
		const TSharedPtr<FJsonObject> Response = MCPFabOpenSearchRun(Registry, Request);
		if (!TestTrue(TEXT("fab_open_search answers with an object"), Response.IsValid())) return false;
		TestTrue(TEXT("the default call succeeds"), Response->GetBoolField(TEXT("success")));
		TestTrue(TEXT("and reports opened"), Response->GetBoolField(TEXT("opened")));
		const FString Expected = TEXT("https://www.fab.com/search?q=stone%20wall&asset_formats=unreal-engine");
		TestEqual(TEXT("the reported url"), Response->GetStringField(TEXT("url")), Expected);
		TestEqual(TEXT("the launcher ran once"), Launched.Num(), 1);
		if (Launched.Num() == 1) TestEqual(TEXT("with the reported url"), Launched[0], Expected);
	}

	// open=false never reaches the launcher.
	{
		Launched.Reset();
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("query"), TEXT("tree"));
		Request->SetBoolField(TEXT("open"), false);
		const TSharedPtr<FJsonObject> Response = MCPFabOpenSearchRun(Registry, Request);
		if (!TestTrue(TEXT("open=false answers with an object"), Response.IsValid())) return false;
		TestFalse(TEXT("open=false reports not opened"), Response->GetBoolField(TEXT("opened")));
		TestEqual(TEXT("open=false launches nothing"), Launched.Num(), 0);
	}

	// A launch failure is an error naming the URL, not opened=true.
	{
		FFabHandlers::SetUrlLauncherForTesting([](const FString& Url, FString& OutError)
		{
			OutError = TEXT("no browser registered");
		});
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("query"), TEXT("rock"));
		const TSharedPtr<FJsonObject> Response = MCPFabOpenSearchRun(Registry, Request);
		if (!TestTrue(TEXT("a failed launch answers with an object"), Response.IsValid())) return false;
		TestFalse(TEXT("a failed launch fails"), Response->GetBoolField(TEXT("success")));
		TestTrue(TEXT("and carries the launch error"),
			Response->GetStringField(TEXT("error")).Contains(TEXT("no browser registered")));
	}
	return true;
}

#endif
