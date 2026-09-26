// Coverage for the world scope of the trace actions (#933).
//
// line_trace, bulk_line_trace and snap_actor_to_floor used to resolve the
// editor world unconditionally, so world='pie' during a PIE session answered
// from the pre-play editor world and reported it as the running game's
// geometry. Nothing about that answer looked wrong, which is what made it
// expensive. These assertions pin the two halves of the fix that hold without a
// running PIE session: the request is refused when the named world does not
// exist, and the response names the world that answered.

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"

namespace
{
	TSharedPtr<FJsonObject> MCPTraceWorldTestRun(
		FMCPHandlerRegistry& Registry,
		const TCHAR* Method,
		const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}

	/** A downward trace request, so the shape is the same in every case below. */
	TSharedPtr<FJsonObject> MCPTraceWorldTestRequest(const TCHAR* WorldScope)
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();

		TSharedPtr<FJsonObject> Start = MakeShared<FJsonObject>();
		Start->SetNumberField(TEXT("x"), 0.0);
		Start->SetNumberField(TEXT("y"), 0.0);
		Start->SetNumberField(TEXT("z"), 10000.0);
		Request->SetObjectField(TEXT("start"), Start);

		TSharedPtr<FJsonObject> End = MakeShared<FJsonObject>();
		End->SetNumberField(TEXT("x"), 0.0);
		End->SetNumberField(TEXT("y"), 0.0);
		End->SetNumberField(TEXT("z"), -10000.0);
		Request->SetObjectField(TEXT("end"), End);

		if (WorldScope) Request->SetStringField(TEXT("world"), WorldScope);
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelTraceWorldScopeTest,
	"UE.MCP.Level.Trace.HonoursTheRequestedWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelTraceWorldScopeTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	for (const TCHAR* Method : { TEXT("line_trace"), TEXT("bulk_line_trace"), TEXT("snap_actor_to_floor") })
	{
		TestTrue(FString::Printf(TEXT("%s is registered"), Method), Registry.HasHandler(Method));
	}

	// The default scope is the editor world, and the response says so. Without
	// that field a caller comparing an editor trace against a PIE trace has no
	// way to tell which one it is holding.
	{
		const TSharedPtr<FJsonObject> Response =
			MCPTraceWorldTestRun(Registry, TEXT("line_trace"), MCPTraceWorldTestRequest(nullptr));
		if (Response.IsValid() && Response->GetBoolField(TEXT("success")))
		{
			FString AnsweringWorld;
			TestTrue(TEXT("the response names the world that answered"),
				Response->TryGetStringField(TEXT("world"), AnsweringWorld));
			TestEqual(TEXT("and the default scope is the editor world"), AnsweringWorld, FString(TEXT("editor")));
		}
	}

	// The regression itself: with no PIE session, world='pie' has no world to
	// trace. It must be refused by name. Answering from the editor world is
	// exactly the silent wrong answer #933 reported.
	const bool bPIERunning = GEditor != nullptr && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor);
	if (!bPIERunning)
	{
		const TSharedPtr<FJsonObject> Response =
			MCPTraceWorldTestRun(Registry, TEXT("line_trace"), MCPTraceWorldTestRequest(TEXT("pie")));
		if (Response.IsValid())
		{
			TestFalse(TEXT("world='pie' without a PIE session does not silently trace the editor world"),
				Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and the error names the scope that could not be resolved"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("pie")));
		}

		// Same for the batch and for the floor snap, which resolve the world
		// through the identical helper.
		TSharedPtr<FJsonObject> BulkRequest = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Traces;
		Traces.Add(MakeShared<FJsonValueObject>(MCPTraceWorldTestRequest(nullptr)));
		BulkRequest->SetArrayField(TEXT("traces"), Traces);
		BulkRequest->SetStringField(TEXT("world"), TEXT("pie"));
		const TSharedPtr<FJsonObject> BulkResponse =
			MCPTraceWorldTestRun(Registry, TEXT("bulk_line_trace"), BulkRequest);
		if (BulkResponse.IsValid())
		{
			TestFalse(TEXT("bulk_line_trace refuses world='pie' without a PIE session"),
				BulkResponse->GetBoolField(TEXT("success")));
		}

		TSharedPtr<FJsonObject> SnapRequest = MakeShared<FJsonObject>();
		SnapRequest->SetStringField(TEXT("actorLabel"), TEXT("UEMCP_NoSuchActor_TraceWorldScope"));
		SnapRequest->SetStringField(TEXT("world"), TEXT("pie"));
		const TSharedPtr<FJsonObject> SnapResponse =
			MCPTraceWorldTestRun(Registry, TEXT("snap_actor_to_floor"), SnapRequest);
		if (SnapResponse.IsValid())
		{
			TestFalse(TEXT("snap_actor_to_floor refuses world='pie' without a PIE session"),
				SnapResponse->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and fails on the world rather than on the actor lookup"),
				SnapResponse->GetStringField(TEXT("error")).Contains(TEXT("pie")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
