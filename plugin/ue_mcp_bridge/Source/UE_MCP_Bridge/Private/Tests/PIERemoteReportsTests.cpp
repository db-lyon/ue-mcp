// PIE remote control: what the server keeps of a remote player's reports. A report is stored only for a request the
// server sent that player, from that player's own controller, with a bounded count and bounded text, and is gone
// once the component ends play with the session.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCPPIERemoteControl.h"
#include "Handlers/PIE/PIEHandlers.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "HandlerWorld.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"

namespace
{
	FString RemoteTestId(const TCHAR* Prefix)
	{
		return FString::Printf(TEXT("%s-%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12).ToLower());
	}

	TSharedPtr<FJsonObject> RemoteAnswerObject(const TSharedPtr<FJsonValue>& Res)
	{
		return Res.IsValid() ? Res->AsObject() : nullptr;
	}

	FString RemoteAnswerError(const TSharedPtr<FJsonValue>& Res)
	{
		const TSharedPtr<FJsonObject> O = RemoteAnswerObject(Res);
		FString Error;
		if (O.IsValid()) O->TryGetStringField(TEXT("error"), Error);
		return Error;
	}

	TSharedPtr<FJsonValue> ReadRemoteResult(const FString& Id)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("request_id"), Id);
		return FPIEHandlers::PieRemoteResult(P);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIERemoteReportsBoundsTest,
	"UE.MCP.PIE.RemoteControl.ReportsBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIERemoteReportsBoundsTest::RunTest(const FString& /*Parameters*/)
{
	FMCPPIERemoteReports Reports;

	// A report for an id the server never sent is not stored: the client cannot add entries.
	TestFalse(TEXT("a report on an unsent id is refused"), Reports.AcceptReport(TEXT("forged"), true, true, FString()));
	TestFalse(TEXT("a trigger on an unsent id is refused"), Reports.AcceptTrigger(TEXT("forged"), FVector::OneVector));
	TestEqual(TEXT("nothing was stored for the forged id"), Reports.NumResults() + Reports.NumTriggers(), 0);

	// Past the cap the oldest request goes, with its result.
	const int32 Extra = 44;
	for (int32 I = 0; I < FMCPPIERemoteReports::MaxRequests + Extra; ++I)
	{
		const FString Id = FString::Printf(TEXT("req-%d"), I);
		Reports.Expect(Id, /*bFresh*/ true);
		TestTrue(TEXT("a sent request's report is stored"), Reports.AcceptReport(Id, true, false, FString()));
	}
	TestEqual(TEXT("requests held stop at the cap"), Reports.NumExpected(), FMCPPIERemoteReports::MaxRequests);
	TestEqual(TEXT("results held stop at the cap"), Reports.NumResults(), FMCPPIERemoteReports::MaxRequests);
	TestFalse(TEXT("the oldest request was dropped"), Reports.IsExpected(TEXT("req-0")));
	TestNull(TEXT("the oldest request's result was dropped with it"), Reports.FindResult(TEXT("req-0")));
	TestFalse(TEXT("a late report on a dropped request is refused"), Reports.AcceptReport(TEXT("req-0"), true, true, FString()));
	TestTrue(TEXT("the first request still held is the next oldest"), Reports.IsExpected(FString::Printf(TEXT("req-%d"), Extra)));

	// A follow-up keeps a running request: it moves to the newest end and keeps its result.
	const FString Running = FString::Printf(TEXT("req-%d"), Extra);
	Reports.Expect(Running, /*bFresh*/ false);
	TestNotNull(TEXT("a follow-up keeps the request's result"), Reports.FindResult(Running));
	Reports.Expect(TEXT("req-next"), /*bFresh*/ true);
	TestTrue(TEXT("the refreshed request outlives one sent before it"), Reports.IsExpected(Running));
	TestFalse(TEXT("the oldest untouched request went instead"), Reports.IsExpected(FString::Printf(TEXT("req-%d"), Extra + 1)));

	// Error text is cut to the bound.
	const FString LongError = FString::ChrN(FMCPPIERemoteReports::MaxTextLength * 4, TEXT('e'));
	TestTrue(TEXT("a long error report is accepted"), Reports.AcceptReport(Running, false, true, LongError));
	const FMCPPIERemoteResult* Cut = Reports.FindResult(Running);
	TestEqual(TEXT("error text is cut to the bound"), Cut ? Cut->Error.Len() : -1, FMCPPIERemoteReports::MaxTextLength);

	// Sending the same id again as a new request drops the old result and trigger.
	TestTrue(TEXT("a trigger on a sent id is stored"), Reports.AcceptTrigger(Running, FVector(1.0, 0.0, 0.0)));
	Reports.Expect(Running, /*bFresh*/ true);
	TestNull(TEXT("a fresh request drops the old result"), Reports.FindResult(Running));
	TestNull(TEXT("a fresh request drops the old trigger"), Reports.FindTrigger(Running));

	Reports.Reset();
	TestEqual(TEXT("reset forgets every request"), Reports.NumExpected(), 0);
	TestEqual(TEXT("reset forgets every result"), Reports.NumResults(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIERemoteReportsOwnershipTest,
	"UE.MCP.PIE.RemoteControl.ReportsOwnedAndClearedAtEndPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIERemoteReportsOwnershipTest::RunTest(const FString& /*Parameters*/)
{
	// The requests below run locally on a component no remote player owns, so each one's report is refused.
	AddExpectedError(TEXT("PIEStudio"), EAutomationExpectedErrorFlags::Contains, 0, /*IsRegex*/ false);

	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, /*bInformEngineOfWorld=*/false);
	if (!World)
	{
		AddError(TEXT("could not create a test world"));
		return false;
	}
	ON_SCOPE_EXIT
	{
		MCPDestroyDisposableWorld(World);
	};

	// Only a player controller the server holds for a remote player may report.
	TestFalse(TEXT("no owner may not report"), UMCPPIERemoteControl::IsRemotePlayerController(nullptr));
	AActor* Plain = World->SpawnActor<AActor>();
	APlayerController* HostPC = World->SpawnActor<APlayerController>();
	if (!Plain || !HostPC)
	{
		AddError(TEXT("could not spawn the test actors"));
		return false;
	}
	TestFalse(TEXT("an actor that is not a player controller may not report"), UMCPPIERemoteControl::IsRemotePlayerController(Plain));
	TestFalse(TEXT("the host's own controller may not report"), UMCPPIERemoteControl::IsRemotePlayerController(HostPC));

	UMCPPIERemoteControl* Control = NewObject<UMCPPIERemoteControl>(Plain);
	UMCPPIERemoteControl* Other = NewObject<UMCPPIERemoteControl>(Plain);
	Control->RegisterComponent();
	Other->RegisterComponent();
	// An editor world does not begin play, so the component is started the way a PIE world would start it.
	UActorComponent* ControlBase = Control;
	ControlBase->RegisterAllComponentTickFunctions(true);
	ControlBase->BeginPlay();

	// The client RPC runs in this process; its report comes back from an owner that is not a remote player's controller.
	const FString Sent = RemoteTestId(TEXT("console"));
	Control->SendConsole(Sent, TEXT("stat none"));
	MCPPIE::PIERemotePlayers::RememberId(Sent, Control);
	TestTrue(TEXT("the server holds the request it sent"), Control->OwnsRequest(Sent));
	TestNull(TEXT("a report from a non-remote owner is not stored"), Control->FindResult(Sent));

	const TSharedPtr<FJsonValue> UnreportedRes = ReadRemoteResult(Sent);
	TestEqual(TEXT("remote_result answers for a held request"), RemoteAnswerError(UnreportedRes), FString());
	const TSharedPtr<FJsonObject> Unreported = RemoteAnswerObject(UnreportedRes);
	bool bReported = true;
	if (Unreported.IsValid()) Unreported->TryGetBoolField(TEXT("reported"), bReported);
	TestFalse(TEXT("remote_result says nothing was reported"), bReported);

	// One player's id cannot be claimed for another player's request.
	FString ClaimError;
	TestFalse(TEXT("another player cannot claim a held id"), MCPPIE::PIERemotePlayers::CanClaimId(Sent, Other, ClaimError));
	TestTrue(TEXT("the refusal names the clash"), ClaimError.Contains(TEXT("another remote player")));
	TestTrue(TEXT("the holder may reuse its own id"), MCPPIE::PIERemotePlayers::CanClaimId(Sent, Control, ClaimError));
	const FString TooLong = FString::ChrN(FMCPPIERemoteReports::MaxIdLength + 1, TEXT('x'));
	TestFalse(TEXT("an id past the length bound is refused"), MCPPIE::PIERemotePlayers::CanClaimId(TooLong, Control, ClaimError));

	// A request pushed out by newer ones can no longer be read.
	for (int32 I = 0; I < FMCPPIERemoteReports::MaxRequests; ++I)
	{
		const FString Newer = RemoteTestId(TEXT("face"));
		Control->SendFace(Newer, FRotator::ZeroRotator);
		MCPPIE::PIERemotePlayers::RememberId(Newer, Control);
	}
	TestFalse(TEXT("the first request was dropped"), Control->OwnsRequest(Sent));
	TestTrue(TEXT("remote_result says a dropped request is no longer held"), RemoteAnswerError(ReadRemoteResult(Sent)).Contains(TEXT("no longer held")));
	TestTrue(TEXT("the remembered ids are pruned with the requests"),
		MCPPIE::PIERemotePlayers::NumRememberedIds() <= FMCPPIERemoteReports::MaxRequests + 1);

	// The session ending clears what the server held for the player.
	TestEqual(TEXT("requests are held before end play"), Control->GetReports().NumExpected(), FMCPPIERemoteReports::MaxRequests);
	ControlBase->EndPlay(EEndPlayReason::EndPlayInEditor);
	TestEqual(TEXT("end play forgets every request"), Control->GetReports().NumExpected(), 0);
	TestEqual(TEXT("end play forgets every result"), Control->GetReports().NumResults(), 0);
	TestFalse(TEXT("end play leaves the client not ready"), Control->IsClientReady());
	return true;
}

#endif
