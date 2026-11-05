// Automation coverage for observe_read file=series (roadmap item 2b).
// Writes a synthetic observation.csv and asserts per-channel min/max/first-cross.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEHandlers.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEObserveSeriesTest,
	"UE.MCP.PIE.Observe.SeriesFromCsv",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEObserveSeriesTest::RunTest(const FString& /*Parameters*/)
{
	const FString Root = FPaths::ProjectSavedDir() / TEXT("MCPObserveSeriesTest") /
		FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString RunId = TEXT("run0");
	IFileManager::Get().MakeDirectory(*(Root / RunId), true);

	const FString Csv =
		TEXT("# obs\n")
		TEXT("frame,time,dt,speed2d,t:Health,game_ms,render_ms,gpu_ms,mem_mb,event\n")
		TEXT("0,0.000,0.016,100,50,8,6,7,1000,\n")
		TEXT("1,0.016,0.016,200,40,8,6,7,1000,\n")
		TEXT("2,0.032,0.016,300,10,8,6,7,1000,\n");
	TestTrue(TEXT("write csv"), FFileHelper::SaveStringToFile(Csv, *(Root / RunId / TEXT("observation.csv"))));

	TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetStringField(TEXT("run_id"), RunId);
	P->SetStringField(TEXT("output_dir"), Root);
	P->SetStringField(TEXT("file"), TEXT("series"));
	P->SetStringField(TEXT("channels"), TEXT("t:Health,speed2d"));
	P->SetNumberField(TEXT("threshold"), 250.0);

	TSharedPtr<FJsonValue> Res = FPIEHandlers::PieObserveRead(P);
	const TSharedPtr<FJsonObject> O = Res.IsValid() ? Res->AsObject() : nullptr;
	TestTrue(TEXT("result object"), O.IsValid());

	const TArray<TSharedPtr<FJsonValue>>* Chans = nullptr;
	if (O.IsValid() && O->TryGetArrayField(TEXT("channels"), Chans) && Chans)
	{
		bool bSawHealth = false, bSawSpeed = false;
		for (const TSharedPtr<FJsonValue>& V : *Chans)
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			if (!C.IsValid()) continue;
			FString Name;
			C->TryGetStringField(TEXT("channel"), Name);
			double MinV = 0, MaxV = 0, First = 0, Last = 0;
			C->TryGetNumberField(TEXT("min"), MinV);
			C->TryGetNumberField(TEXT("max"), MaxV);
			C->TryGetNumberField(TEXT("first"), First);
			C->TryGetNumberField(TEXT("last"), Last);
			if (Name == TEXT("t:Health"))
			{
				bSawHealth = true;
				TestEqual(TEXT("health min"), MinV, 10.0);
				TestEqual(TEXT("health max"), MaxV, 50.0);
				TestEqual(TEXT("health first"), First, 50.0);
				TestEqual(TEXT("health last"), Last, 10.0);
			}
			else if (Name == TEXT("speed2d"))
			{
				bSawSpeed = true;
				TestEqual(TEXT("speed max"), MaxV, 300.0);
				int32 FirstOver = -1;
				C->TryGetNumberField(TEXT("first_frame_over"), FirstOver);
				TestEqual(TEXT("speed crosses 250 at frame 2"), FirstOver, 2);
			}
		}
		TestTrue(TEXT("health channel present"), bSawHealth);
		TestTrue(TEXT("speed channel present"), bSawSpeed);
	}
	else
	{
		AddError(TEXT("no channels array returned"));
	}

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
