// Coverage for landscape(sample) parameter parity (#939).
//
// The action documented "Params: x, y" and the handler required an object
// called `point`, so every documented call shape answered "Missing 'point'
// parameter" and there was no working call at all. These assertions pin the
// contract rather than the terrain: each accepted position shape gets PAST
// parameter validation, and a request with no position at all is refused with
// an error that names every shape that would have worked.
//
// The terrain half (heights, weights) needs a landscape in the level and is
// exercised by the live smoke run; what regressed here was the parameter
// handling, and that is checkable with no landscape at all.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "Handlers/Landscape/LandscapeHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPLandscapeSampleTestRun(
		FMCPHandlerRegistry& Registry,
		const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("sample_landscape"), Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	/** The error text a request is refused with, or empty when it succeeded. */
	FString MCPLandscapeSampleTestError(const TSharedPtr<FJsonObject>& Response)
	{
		if (!Response.IsValid()) return FString();
		if (Response->GetBoolField(TEXT("success"))) return FString();
		return Response->GetStringField(TEXT("error"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLandscapeSampleAcceptsItsDocumentedArgumentsTest,
	"UE.MCP.Landscape.Sample.AcceptsItsDocumentedArguments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandscapeSampleAcceptsItsDocumentedArgumentsTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLandscapeHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("sample_landscape is registered"), Registry.HasHandler(TEXT("sample_landscape")));

	// No position at all is the one request that SHOULD be refused, and the
	// refusal has to name every shape that works. The old message named only
	// `point`, which is the shape the schema never advertised.
	{
		const TSharedPtr<FJsonObject> Response =
			MCPLandscapeSampleTestRun(Registry, MakeShared<FJsonObject>());
		if (Response.IsValid())
		{
			TestFalse(TEXT("a request with no position is refused"), Response->GetBoolField(TEXT("success")));
			const FString Error = Response->GetStringField(TEXT("error"));
			TestTrue(TEXT("and the refusal names the x/y shape"), Error.Contains(TEXT("x and y")));
			TestTrue(TEXT("and the point shape"), Error.Contains(TEXT("point")));
			TestTrue(TEXT("and the worldX/worldY shape"), Error.Contains(TEXT("worldX")));
		}
	}

	// Every accepted shape must get past parameter validation. Whether a
	// landscape exists in the level is a separate question, so the assertion is
	// on the error NOT being the missing-position one.
	auto TestShapeIsAccepted = [this, &Registry](const TCHAR* What, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonObject> Response = MCPLandscapeSampleTestRun(Registry, Request);
		if (!Response.IsValid()) return;
		const FString Error = MCPLandscapeSampleTestError(Response);
		TestFalse(FString::Printf(TEXT("%s is not refused as a missing position"), What),
			Error.Contains(TEXT("Missing sample position")));
		TestFalse(FString::Printf(TEXT("%s is not refused for a missing 'point'"), What),
			Error.Contains(TEXT("Missing 'point'")));
	};

	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetNumberField(TEXT("x"), 1000.0);
		Request->SetNumberField(TEXT("y"), 2000.0);
		TestShapeIsAccepted(TEXT("x + y"), Request);
	}
	{
		TSharedPtr<FJsonObject> Point = MakeShared<FJsonObject>();
		Point->SetNumberField(TEXT("x"), 1000.0);
		Point->SetNumberField(TEXT("y"), 2000.0);
		Point->SetNumberField(TEXT("z"), 0.0);
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetObjectField(TEXT("point"), Point);
		TestShapeIsAccepted(TEXT("point {x, y, z}"), Request);
	}
	{
		// The z is optional: the surface height is what the action answers, so
		// requiring one as an input would be a riddle.
		TSharedPtr<FJsonObject> Point = MakeShared<FJsonObject>();
		Point->SetNumberField(TEXT("x"), 1000.0);
		Point->SetNumberField(TEXT("y"), 2000.0);
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetObjectField(TEXT("point"), Point);
		TestShapeIsAccepted(TEXT("point {x, y}"), Request);
	}
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetNumberField(TEXT("worldX"), 1000.0);
		Request->SetNumberField(TEXT("worldY"), 2000.0);
		TestShapeIsAccepted(TEXT("worldX + worldY"), Request);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
