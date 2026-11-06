// Replay timing (#1657): tapes paced by Hz on the world clock, the source
// anchor a start_when gate resumes at, start_when parsing, and the start_gate
// block in drift.json.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEInputReplayer.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "MCPPIEInputInjector.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIETapeIndexAtTest,
	"UE.MCP.PIE.Replay.TapeIndexFollowsTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIETapeIndexAtTest::RunTest(const FString& /*Parameters*/)
{
	using MCPPIE::FPIEInputInjector;
	TestEqual(TEXT("start"), FPIEInputInjector::TapeIndexAt(0.0, 30), 0);
	TestEqual(TEXT("just before the second entry"), FPIEInputInjector::TapeIndexAt(0.033, 30), 0);
	TestEqual(TEXT("exactly one period"), FPIEInputInjector::TapeIndexAt(1.0 / 30.0, 30), 1);
	// A 27 fps frame lands past entry 1 after three frames, not on entry 3.
	TestEqual(TEXT("three 27 fps frames at 30 Hz"), FPIEInputInjector::TapeIndexAt(3.0 / 27.0, 30), 3);
	TestEqual(TEXT("ninety entries at 60 Hz take 1.5 s"), FPIEInputInjector::TapeIndexAt(1.5, 60), 90);
	TestEqual(TEXT("negative elapsed clamps"), FPIEInputInjector::TapeIndexAt(-1.0, 30), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIESourceRestAnchorTest,
	"UE.MCP.PIE.Replay.SourceRestAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIESourceRestAnchorTest::RunTest(const FString& /*Parameters*/)
{
	using MCPPIE::FindSourceRestAnchorMs;
	// Held still in the air, falls, lands at 2000 ms, idles, first input at 5000 ms.
	TArray<double> T;
	TArray<float> V;
	for (int32 i = 0; i <= 60; ++i)
	{
		const double Ms = i * 100.0;
		T.Add(Ms);
		V.Add(Ms < 500.0 ? 0.f : (Ms < 2000.0 ? 400.f : 0.f));
	}
	TestEqual(TEXT("rest that leads into the input, plus hold"), FindSourceRestAnchorMs(T, V, 5000.0, 5.f, 500), 2500.0);
	TestEqual(TEXT("capped at the onset"), FindSourceRestAnchorMs(T, V, 2200.0, 5.f, 500), 2200.0);
	TestEqual(TEXT("moving at the onset"), FindSourceRestAnchorMs(T, V, 1500.0, 5.f, 500), -1.0);
	TestEqual(TEXT("nothing before the onset"), FindSourceRestAnchorMs(T, V, 0.0, 5.f, 500), -1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEStartGateParseTest,
	"UE.MCP.PIE.Replay.StartWhenParses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEStartGateParseTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;
	FString Err;

	TSharedPtr<FJsonObject> Empty = MakeShared<FJsonObject>();
	FReplayStartGate Defaults;
	TestTrue(TEXT("empty object takes the defaults"), ParseReplayStartGate(Empty, Defaults, Err));
	TestTrue(TEXT("enabled"), Defaults.bEnabled);
	TestTrue(TEXT("pawn at rest by default"), Defaults.bPawnAtRest);
	TestEqual(TEXT("hold default"), Defaults.HoldMs, 500);

	TSharedPtr<FJsonObject> Full = MakeShared<FJsonObject>();
	Full->SetBoolField(TEXT("pawn_at_rest"), false);
	Full->SetStringField(TEXT("log_contains"), TEXT("Intro finished"));
	Full->SetNumberField(TEXT("hold_ms"), 250);
	Full->SetNumberField(TEXT("timeout_ms"), 9000);
	FReplayStartGate G;
	TestTrue(TEXT("log-only gate"), ParseReplayStartGate(Full, G, Err));
	TestFalse(TEXT("no rest check"), G.bPawnAtRest);
	TestEqual(TEXT("log text"), G.LogContains, FString(TEXT("Intro finished")));
	TestEqual(TEXT("hold"), G.HoldMs, 250);
	TestEqual(TEXT("timeout"), G.TimeoutMs, 9000);

	TSharedPtr<FJsonObject> Nothing = MakeShared<FJsonObject>();
	Nothing->SetBoolField(TEXT("pawn_at_rest"), false);
	TestFalse(TEXT("a gate with nothing to wait on is refused"), ParseReplayStartGate(Nothing, G, Err));

	TSharedPtr<FJsonObject> Typo = MakeShared<FJsonObject>();
	Typo->SetNumberField(TEXT("hold"), 100);
	TestFalse(TEXT("an unknown field is refused"), ParseReplayStartGate(Typo, G, Err));
	TestTrue(TEXT("the error names it"), Err.Contains(TEXT("start_when.hold")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEStartGateDriftRoundTripTest,
	"UE.MCP.PIE.Drift.StartGateRoundTrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEStartGateDriftRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;
	FDriftReport D;
	D.Alignment = TEXT("start_gate");
	D.bHasStartGate = true;
	D.bStartGatePassed = false;
	D.StartGateAnchor = TEXT("source_rest");
	D.StartGateSourceMs = 2500.0;
	D.StartGateReplayMs = -1.0;
	D.StartGateBlocker = TEXT("pawn speed 12.0 cm/s (at most 5.0)");

	const FString Path = FPaths::ProjectSavedDir() / TEXT("MCPDriftTest") /
		(FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	FString Err;
	TestTrue(TEXT("SaveDrift"), SaveDrift(Path, D, Err));
	FDriftReport L;
	TestTrue(TEXT("LoadDrift"), LoadDrift(Path, L, Err));
	TestTrue(TEXT("gate present"), L.bHasStartGate);
	TestFalse(TEXT("not passed"), L.bStartGatePassed);
	TestEqual(TEXT("anchor"), L.StartGateAnchor, FString(TEXT("source_rest")));
	TestEqual(TEXT("source ms"), L.StartGateSourceMs, 2500.0);
	TestEqual(TEXT("blocker"), L.StartGateBlocker, D.StartGateBlocker);
	IFileManager::Get().Delete(*Path);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
