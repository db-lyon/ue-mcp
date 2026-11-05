// Automation coverage for the drift summary (roadmap item 1c).
// Verifies the synthesised lead (first divergence + top channels + error counts)
// round-trips through SaveDrift/LoadDrift and lands in drift.json.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIESequenceFormat.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEDriftSummaryRoundTripTest,
	"UE.MCP.PIE.Drift.SummaryRoundTrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEDriftSummaryRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;

	FDriftReport D;
	D.SourceRecordingId = TEXT("unit-test-rec");
	D.FramesCompared = 100;
	D.MaxVelocityDriftCms = 440.f;
	D.Summary.bValid = true;
	D.Summary.First.bFound = true;
	D.Summary.First.Frame = 340;
	D.Summary.First.Time = 5.67;
	D.Summary.First.Channel = TEXT("velocity");
	D.Summary.First.Delta = 440.0;
	D.Summary.First.Threshold = 25.0;
	D.Summary.First.SourceValue = 620.0;
	D.Summary.First.ReplayValue = 180.0;
	D.Summary.TopChannels.Add(TPair<FString, float>(TEXT("velocity_cms"), 440.f));
	D.Summary.TopChannels.Add(TPair<FString, float>(TEXT("position_cm"), 120.f));
	D.Summary.ErrorsDuringRun = 2;
	D.Summary.WarningsDuringRun = 1;
	D.Summary.TopError = TEXT("Ensure failed: capsule half-height 0");
	D.Summary.TopErrorFrame = 339;
	D.Summary.SessionDir = TEXT("C:/tmp/session");

	const FString Path = FPaths::ProjectSavedDir() / TEXT("MCPDriftTest") /
		(FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));

	FString Err;
	TestTrue(TEXT("SaveDrift"), SaveDrift(Path, D, Err));

	// Raw file carries the summary block.
	FString Raw;
	TestTrue(TEXT("read back file"), FFileHelper::LoadFileToString(Raw, *Path));
	TestTrue(TEXT("json has summary"), Raw.Contains(TEXT("\"summary\"")));
	TestTrue(TEXT("json has first_divergence"), Raw.Contains(TEXT("\"first_divergence\"")));

	// Structured round-trip.
	FDriftReport Loaded;
	TestTrue(TEXT("LoadDrift"), LoadDrift(Path, Loaded, Err));
	TestTrue(TEXT("summary valid"), Loaded.Summary.bValid);
	TestTrue(TEXT("divergence found"), Loaded.Summary.First.bFound);
	TestEqual(TEXT("divergence frame"), (int32)Loaded.Summary.First.Frame, 340);
	TestEqual(TEXT("divergence channel"), Loaded.Summary.First.Channel, FString(TEXT("velocity")));
	TestEqual(TEXT("source value"), Loaded.Summary.First.SourceValue, 620.0);
	TestEqual(TEXT("replay value"), Loaded.Summary.First.ReplayValue, 180.0);
	TestEqual(TEXT("errors during run"), Loaded.Summary.ErrorsDuringRun, 2);
	TestEqual(TEXT("warnings during run"), Loaded.Summary.WarningsDuringRun, 1);
	TestTrue(TEXT("top channels preserved"), Loaded.Summary.TopChannels.Num() == 2);
	if (Loaded.Summary.TopChannels.Num() == 2)
	{
		TestEqual(TEXT("top channel name"), Loaded.Summary.TopChannels[0].Key, FString(TEXT("velocity_cms")));
	}
	TestEqual(TEXT("top error"), Loaded.Summary.TopError, FString(TEXT("Ensure failed: capsule half-height 0")));

	IFileManager::Get().Delete(*Path);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
