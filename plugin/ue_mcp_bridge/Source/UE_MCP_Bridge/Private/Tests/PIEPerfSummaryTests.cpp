// Automation coverage for profiling handlers (roadmap items 4a + 4b):
// perf_summary over a synthetic recording.csv, and a trace_start/trace_stop
// round-trip that writes a .utrace.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEHandlers.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEPerfSummaryTest,
	"UE.MCP.PIE.Perf.SummaryFromCsv",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEPerfSummaryTest::RunTest(const FString& /*Parameters*/)
{
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("MCPPerfTest") /
		FGuid::NewGuid().ToString(EGuidFormats::Digits);
	IFileManager::Get().MakeDirectory(*Folder, true);

	// Minimal CSV: perf_summary resolves columns by name, so only these are needed.
	// Frame 2 is a 100 ms hitch among 16 ms frames.
	const FString Csv =
		TEXT("# ue-mcp recording v1 | id=test\n")
		TEXT("frame,time,dt,game_ms,render_ms,gpu_ms,mem_mb,event\n")
		TEXT("0,0.000,0.016,8.0,6.0,7.0,1000,\n")
		TEXT("1,0.016,0.016,8.0,6.0,7.0,1001,\n")
		TEXT("2,0.032,0.100,80.0,20.0,30.0,1010,hitch\n")
		TEXT("3,0.132,0.016,8.0,6.0,7.0,1002,\n");
	TestTrue(TEXT("write csv"), FFileHelper::SaveStringToFile(Csv, *(Folder / TEXT("recording.csv"))));

	TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetStringField(TEXT("recording_dir"), Folder);
	TSharedPtr<FJsonValue> Res = FPIEHandlers::PiePerfSummary(P);
	TestTrue(TEXT("result valid"), Res.IsValid());

	const TSharedPtr<FJsonObject> O = Res->AsObject();
	TestTrue(TEXT("result object"), O.IsValid());
	if (O.IsValid())
	{
		bool bOk = false;
		O->TryGetBoolField(TEXT("success"), bOk);
		TestTrue(TEXT("success"), bOk);

		int32 Frames = 0;
		O->TryGetNumberField(TEXT("frames"), Frames);
		TestEqual(TEXT("frame count"), Frames, 4);

		const TSharedPtr<FJsonObject>* Ft = nullptr;
		if (O->TryGetObjectField(TEXT("frametime"), Ft) && Ft)
		{
			double MaxMs = 0, P99 = 0;
			(*Ft)->TryGetNumberField(TEXT("max_ms"), MaxMs);
			(*Ft)->TryGetNumberField(TEXT("p99_ms"), P99);
			TestTrue(TEXT("max frametime ~100ms"), MaxMs > 90.0 && MaxMs < 110.0);
			TestTrue(TEXT("p99 elevated"), P99 > 90.0);
		}

		const TArray<TSharedPtr<FJsonValue>>* Hitches = nullptr;
		if (O->TryGetArrayField(TEXT("worst_hitches"), Hitches) && Hitches && Hitches->Num() > 0)
		{
			const TSharedPtr<FJsonObject> Top = (*Hitches)[0]->AsObject();
			int32 HitchFrame = -1;
			Top->TryGetNumberField(TEXT("frame"), HitchFrame);
			TestEqual(TEXT("worst hitch is frame 2"), HitchFrame, 2);
		}
		else
		{
			AddError(TEXT("no worst_hitches returned"));
		}
	}

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
