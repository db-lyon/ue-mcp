// pie_script_run ends the PIE session it started when the run ends, and
// leaves alone a session someone else asked for, or its own with keep_pie.
//
// A PIE request is queued and comes up on a later engine tick, which an
// automation test run through the bridge never gets. So the cases here act
// on the queued request: the run that asked for it drops it when it ends,
// and a run that did not ask for it, or was told to keep it, leaves it
// queued. Ending a session that is up is asserted by the live suite
// (tests/live/bcov-pie-scripts.test.ts).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPlaySession.h"
#include "Handlers/PIE/PIEHandlers.h"
#include "Handlers/PIE/PIEScriptRunner.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"

namespace MCPPIEScriptEndsPIETests
{
	using namespace MCPPIE;

	TSharedPtr<FJsonObject> Call(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		return Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : MakeShared<FJsonObject>();
	}

	bool Flag(const TSharedPtr<FJsonObject>& Response, const TCHAR* Name)
	{
		bool bValue = false;
		return Response->TryGetBoolField(Name, bValue) && bValue;
	}

	FString Field(const TSharedPtr<FJsonObject>& Response, const TCHAR* Name)
	{
		FString Value;
		Response->TryGetStringField(Name, Value);
		return Value;
	}

	// A manual step holds the run, so it is still active when the test stops it.
	TSharedPtr<FJsonObject> RunRequest(const TCHAR* Name, bool bKeepPIE)
	{
		TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("instruction"), TEXT("Hold the run"));
		Step->SetStringField(TEXT("judge"), TEXT("manual"));
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetArrayField(TEXT("steps"), { MakeShared<FJsonValueObject>(Step) });
		P->SetStringField(TEXT("name"), Name);
		P->SetBoolField(TEXT("record"), false);
		P->SetBoolField(TEXT("start_pie"), true);
		if (bKeepPIE) P->SetBoolField(TEXT("keep_pie"), true);
		return P;
	}

	TSharedPtr<FJsonObject> StopRequest(const FString& RunId)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("run_id"), RunId);
		P->SetStringField(TEXT("reason"), TEXT("test"));
		return P;
	}

	FScriptRun DecisionRun(bool bStarted, bool bKeep, bool bManual)
	{
		FScriptRun Run;
		Run.bStartedPIE = bStarted;
		Run.bKeepPIE = bKeep;
		Run.bManualMode = bManual;
		return Run;
	}

	// Starts a run and stops it at once. Returns whether a PIE request was still queued after the stop.
	bool StartAndStop(FAutomationTestBase& Test, FMCPHandlerRegistry& Registry, const TCHAR* Name, bool bKeepPIE, bool bExpectStarted)
	{
		const TSharedPtr<FJsonObject> Started = Call(Registry, TEXT("pie_script_run"), RunRequest(Name, bKeepPIE));
		if (!Test.TestTrue(FString::Printf(TEXT("%s: script_run succeeds: %s"), Name, *Field(Started, TEXT("error"))), Flag(Started, TEXT("success"))))
		{
			return GEditor->IsPlaySessionRequestQueued();
		}
		Test.TestEqual(FString::Printf(TEXT("%s: pie_started"), Name), Flag(Started, TEXT("pie_started")), bExpectStarted);
		Test.TestTrue(FString::Printf(TEXT("%s: a PIE session is queued while the run is armed"), Name), GEditor->IsPlaySessionRequestQueued());
		const TSharedPtr<FJsonObject> Stopped = Call(Registry, TEXT("pie_script_stop"), StopRequest(Field(Started, TEXT("run_id"))));
		Test.TestTrue(FString::Printf(TEXT("%s: script_stop stops the run"), Name), Flag(Stopped, TEXT("stopped")));
		Test.TestEqual(FString::Printf(TEXT("%s: the run is stopped"), Name), Field(Stopped, TEXT("state")), FString(TEXT("stopped")));
		return GEditor->IsPlaySessionRequestQueued();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptEndsPIETest,
	"UE.MCP.PIE.Script.EndsStartedPIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptEndsPIETest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptEndsPIETests;

	// Only PIE the run started is ended, never with keep_pie, and never by a manual run, which starts none.
	TestTrue(TEXT("a run that started PIE ends it"), ShouldScriptRunEndPIE(DecisionRun(true, false, false)));
	TestFalse(TEXT("a run that found PIE playing leaves it"), ShouldScriptRunEndPIE(DecisionRun(false, false, false)));
	TestFalse(TEXT("keep_pie keeps PIE the run started"), ShouldScriptRunEndPIE(DecisionRun(true, true, false)));
	TestFalse(TEXT("a manual run ends no PIE"), ShouldScriptRunEndPIE(DecisionRun(true, false, true)));

	if (!GEditor || GEditor->PlayWorld || GEditor->IsPlaySessionInProgress() || FPIEScriptRunner::Get().IsActive())
	{
		AddInfo(TEXT("PIE is running or a script run is active, so the run cases are skipped: they queue PIE sessions of their own."));
		return true;
	}

	FMCPHandlerRegistry Registry;
	FPIEHandlers::RegisterHandlers(Registry);

	// The run asked for PIE, so ending it drops the request and no session outlives the run.
	TestFalse(TEXT("a stopped run drops the PIE session it queued"),
		StartAndStop(*this, Registry, TEXT("EndsPIE_Own"), /*bKeepPIE*/ false, /*bExpectStarted*/ true));

	// keep_pie: the run's own session is left to come up.
	TestTrue(TEXT("keep_pie leaves the run's PIE session queued after the run ends"),
		StartAndStop(*this, Registry, TEXT("EndsPIE_Keep"), /*bKeepPIE*/ true, /*bExpectStarted*/ true));
	if (GEditor->IsPlaySessionRequestQueued()) GEditor->CancelRequestPlaySession();

	// Someone else asked for PIE first: the run neither claims nor drops that session.
	MCPPlaySession::Request(/*bNeedsEditorWorld=*/true);
	TestTrue(TEXT("a run leaves alone a PIE session it did not ask for"),
		StartAndStop(*this, Registry, TEXT("EndsPIE_Others"), /*bKeepPIE*/ false, /*bExpectStarted*/ false));
	if (GEditor->IsPlaySessionRequestQueued()) GEditor->CancelRequestPlaySession();

	TestFalse(TEXT("the test leaves no PIE session queued"), GEditor->IsPlaySessionInProgress());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
