// Recorder and replayer configuration (#1690): action path normalization,
// tracked values replaced rather than appended between sessions, t.MaxFPS
// restored after a pin, and the time-aligned drift fields round-tripping.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEFrameSampler.h"
#include "Handlers/PIE/PIEMaxFPSPin.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIERecorderActionPathTest,
	"UE.MCP.PIE.Recorder.ActionPathNormalizes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIERecorderActionPathTest::RunTest(const FString& /*Parameters*/)
{
	using MCPPIE::FPIEFrameSampler;
	const FString Object = TEXT("/Game/Input/IA_Move.IA_Move");
	TestEqual(TEXT("package path"), FPIEFrameSampler::NormalizeActionPath(TEXT("/Game/Input/IA_Move")), Object);
	TestEqual(TEXT("object path unchanged"), FPIEFrameSampler::NormalizeActionPath(Object), Object);
	TestEqual(TEXT("export text"), FPIEFrameSampler::NormalizeActionPath(TEXT("InputAction'/Game/Input/IA_Move.IA_Move'")), Object);
	TestEqual(TEXT("whitespace trimmed"), FPIEFrameSampler::NormalizeActionPath(TEXT("  /Game/Input/IA_Move ")), Object);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIERecorderTrackedValuesReplaceTest,
	"UE.MCP.PIE.Recorder.TrackedValuesReplacedBetweenSessions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIERecorderTrackedValuesReplaceTest::RunTest(const FString& /*Parameters*/)
{
	using MCPPIE::FPIEFrameSampler;
	FPIEFrameSampler Sampler;

	FPIEFrameSampler::FConfig First;
	First.TrackedValuePaths = { TEXT("CustomTimeDilation") };
	Sampler.Reset();
	Sampler.SetConfig(First);
	TestEqual(TEXT("first session tracks one path"), Sampler.GetTrackedValues().Num(), 1);

	// The recorder resets, then configures, every session.
	FPIEFrameSampler::FConfig Second;
	Sampler.Reset();
	Sampler.SetConfig(Second);
	TestEqual(TEXT("second session without track_values tracks none"), Sampler.GetTrackedValues().Num(), 0);

	FPIEFrameSampler::FConfig Third;
	Third.TrackedValuePaths = { TEXT("A"), TEXT("B") };
	Sampler.Reset();
	Sampler.SetConfig(Third);
	TestEqual(TEXT("third session tracks its own two"), Sampler.GetTrackedValues().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEMaxFPSPinRestoreTest,
	"UE.MCP.PIE.Recorder.MaxFPSPinRestores",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEMaxFPSPinRestoreTest::RunTest(const FString& /*Parameters*/)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
	if (!TestNotNull(TEXT("t.MaxFPS exists"), CVar)) return false;

	const FString Before = CVar->GetString();
	const uint32 BeforeSetBy = static_cast<uint32>(CVar->GetFlags()) & ECVF_SetByMask;

	MCPPIE::FPIEMaxFPSPin Pin;
	TestTrue(TEXT("pin applied"), Pin.Apply(37));
	TestEqual(TEXT("pinned value"), CVar->GetInt(), 37);
	Pin.Restore();
	TestEqual(TEXT("value restored"), CVar->GetString(), Before);
	TestEqual(TEXT("set-by restored"), static_cast<uint32>(CVar->GetFlags()) & ECVF_SetByMask, BeforeSetBy);

	// Someone else changed it after the pin: leave their value alone.
	TestTrue(TEXT("pin applied again"), Pin.Apply(41));
	CVar->Set(53, ECVF_SetByConsole);
	Pin.Restore();
	TestEqual(TEXT("foreign write kept"), CVar->GetInt(), 53);

	// Put the editor back the way the test found it.
	CVar->Set(*Before, ECVF_SetByConsole);
	const uint32 Others = static_cast<uint32>(CVar->GetFlags()) & ~static_cast<uint32>(ECVF_SetByMask);
	CVar->SetFlags(static_cast<EConsoleVariableFlags>(Others | BeforeSetBy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEDriftAlignmentRoundTripTest,
	"UE.MCP.PIE.Drift.AlignmentRoundTrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEDriftAlignmentRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;

	FDriftReport D;
	D.SourceRecordingId = TEXT("unit-test-align");
	D.Alignment = TEXT("time");
	D.ReplayOffsetMs = 500.0;
	D.bHasInputOnset = true;
	D.SourceInputOnsetMs = 1200.0;
	D.ReplayInputOnsetMs = 1216.0;
	D.ReplayCSVFile = TEXT("replay.csv");
	FDriftFrameEntry E;
	E.Frame = 64;
	E.SourceFrame = 34;
	E.Time = 0.55;
	E.PositionDelta = FVector(-12.0, 3.0, 0.0);
	E.VelocityDelta = FVector(40.0, 0.0, 0.0);
	E.PositionDeltaCm = static_cast<float>(E.PositionDelta.Size());
	E.VelocityDeltaCms = 40.f;
	D.FramesOverThreshold.Add(E);

	const FString Path = FPaths::ProjectSavedDir() / TEXT("MCPDriftTest") /
		(FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	FString Err;
	TestTrue(TEXT("SaveDrift"), SaveDrift(Path, D, Err));

	FString Raw;
	TestTrue(TEXT("read back file"), FFileHelper::LoadFileToString(Raw, *Path));
	TestTrue(TEXT("json has offset_ms"), Raw.Contains(TEXT("\"offset_ms\"")));

	FDriftReport L;
	TestTrue(TEXT("LoadDrift"), LoadDrift(Path, L, Err));
	TestEqual(TEXT("alignment"), L.Alignment, FString(TEXT("time")));
	TestEqual(TEXT("replay offset"), L.ReplayOffsetMs, 500.0);
	TestTrue(TEXT("onset present"), L.bHasInputOnset);
	TestEqual(TEXT("replay onset"), L.ReplayInputOnsetMs, 1216.0);
	TestEqual(TEXT("replay csv"), L.ReplayCSVFile, FString(TEXT("replay.csv")));
	if (TestEqual(TEXT("one frame entry"), L.FramesOverThreshold.Num(), 1))
	{
		const FDriftFrameEntry& LE = L.FramesOverThreshold[0];
		TestEqual(TEXT("source frame"), static_cast<int32>(LE.SourceFrame), 34);
		TestEqual(TEXT("time"), LE.Time, 0.55);
		TestEqual(TEXT("signed position x"), LE.PositionDelta.X, -12.0);
		TestEqual(TEXT("signed velocity x"), LE.VelocityDelta.X, 40.0);
	}

	IFileManager::Get().Delete(*Path);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
