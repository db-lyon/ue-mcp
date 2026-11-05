// Automation coverage for reproduction tests (roadmap item 3a):
// test_scaffold -> test_list -> test_run (pass and fail) against a drift.json.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEHandlers.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEReproTestTest,
	"UE.MCP.PIE.ReproTest.ScaffoldListRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

static bool WriteDrift(const FString& Folder, float PosCm, int32 Errors)
{
	using namespace MCPPIE;
	FDriftReport D;
	D.MaxPositionDriftCm = PosCm;
	D.MaxVelocityDriftCms = 20.f;
	D.MaxRotationDriftDeg = 1.f;
	D.Summary.bValid = true;
	D.Summary.ErrorsDuringRun = Errors;
	FString Err;
	return SaveDrift(Folder / TEXT("drift.json"), D, Err);
}

bool FPIEReproTestTest::RunTest(const FString& /*Parameters*/)
{
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("MCPReproTest") /
		FGuid::NewGuid().ToString(EGuidFormats::Digits);
	IFileManager::Get().MakeDirectory(*Folder, true);
	// Scaffold requires a recording.csv to exist.
	FFileHelper::SaveStringToFile(FString(TEXT("# rec\nframe,time,dt,pos_x,event\n0,0,0.016,0,\n")),
		*(Folder / TEXT("recording.csv")));

	// Scaffold.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("recording_dir"), Folder);
		P->SetStringField(TEXT("name"), TEXT("repro"));
		P->SetNumberField(TEXT("max_position_drift_cm"), 10.0);
		TSharedPtr<FJsonValue> Res = FPIEHandlers::PieTestScaffold(P);
		const TSharedPtr<FJsonObject> O = Res.IsValid() ? Res->AsObject() : nullptr;
		TestTrue(TEXT("scaffold ok"), O.IsValid());
		FString TestPath;
		if (O.IsValid()) O->TryGetStringField(TEXT("test_path"), TestPath);
		TestTrue(TEXT("test.json written"), FPaths::FileExists(TestPath));
	}

	// List.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("recording_dir"), Folder);
		TSharedPtr<FJsonValue> Res = FPIEHandlers::PieTestList(P);
		const TSharedPtr<FJsonObject> O = Res.IsValid() ? Res->AsObject() : nullptr;
		int32 Count = 0;
		if (O.IsValid()) O->TryGetNumberField(TEXT("count"), Count);
		TestTrue(TEXT("list finds the test"), Count >= 1);
	}

	// Run - passing case (drift 5 <= 10, 0 errors).
	{
		TestTrue(TEXT("write passing drift"), WriteDrift(Folder, 5.f, 0));
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("recording_dir"), Folder);
		P->SetStringField(TEXT("name"), TEXT("repro"));
		P->SetBoolField(TEXT("auto_run"), false);
		TSharedPtr<FJsonValue> Res = FPIEHandlers::PieTestRun(P);
		const TSharedPtr<FJsonObject> O = Res.IsValid() ? Res->AsObject() : nullptr;
		bool bPassed = false;
		if (O.IsValid()) O->TryGetBoolField(TEXT("passed"), bPassed);
		TestTrue(TEXT("passes within thresholds"), bPassed);
	}

	// Run - failing case (drift 50 > 10).
	{
		TestTrue(TEXT("write failing drift"), WriteDrift(Folder, 50.f, 0));
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("recording_dir"), Folder);
		P->SetStringField(TEXT("name"), TEXT("repro"));
		P->SetBoolField(TEXT("auto_run"), false);
		TSharedPtr<FJsonValue> Res = FPIEHandlers::PieTestRun(P);
		const TSharedPtr<FJsonObject> O = Res.IsValid() ? Res->AsObject() : nullptr;
		bool bPassed = true;
		const TArray<TSharedPtr<FJsonValue>>* Failures = nullptr;
		if (O.IsValid())
		{
			O->TryGetBoolField(TEXT("passed"), bPassed);
			O->TryGetArrayField(TEXT("failures"), Failures);
		}
		TestFalse(TEXT("fails over threshold"), bPassed);
		TestTrue(TEXT("has a failure message"), Failures && Failures->Num() >= 1);
	}

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
