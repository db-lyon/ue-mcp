// Automation coverage for replay_state (roadmap item 2a).
// Verifies deterministic scrub: interpolated pawn transform at an arbitrary time.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEHandlers.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEReplayStateScrubTest,
	"UE.MCP.PIE.StateReplay.ScrubInterpolates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEReplayStateScrubTest::RunTest(const FString& /*Parameters*/)
{
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("MCPStateReplayTest") /
		FGuid::NewGuid().ToString(EGuidFormats::Digits);
	IFileManager::Get().MakeDirectory(*Folder, true);

	// pos_x goes 0 -> 100 -> 200 over t = 0, 1, 2. Sampling at t=0.5 must give 50.
	const FString Csv =
		TEXT("# rec\n")
		TEXT("frame,time,dt,pos_x,pos_y,pos_z,rot_yaw,rot_pitch,rot_roll,vel_x,vel_y,vel_z,speed2d,montage,event\n")
		TEXT("0,0.000,0.5,0,0,0,0,0,0,0,0,0,0,,\n")
		TEXT("1,1.000,0.5,100,0,0,0,0,0,0,0,0,0,,\n")
		TEXT("2,2.000,0.5,200,0,0,0,0,0,0,0,0,0,,\n");
	TestTrue(TEXT("write csv"), FFileHelper::SaveStringToFile(Csv, *(Folder / TEXT("recording.csv"))));

	// Scrub to t=0.5 -> pos_x should interpolate to 50.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("recording_dir"), Folder);
		P->SetNumberField(TEXT("at_time"), 0.5);
		TSharedPtr<FJsonValue> Res = FPIEHandlers::PieReplayState(P);
		const TSharedPtr<FJsonObject> O = Res.IsValid() ? Res->AsObject() : nullptr;
		TestTrue(TEXT("result object"), O.IsValid());
		if (O.IsValid())
		{
			bool bDet = false; O->TryGetBoolField(TEXT("deterministic"), bDet);
			TestTrue(TEXT("deterministic"), bDet);
			const TSharedPtr<FJsonObject>* Pawn = nullptr;
			if (O->TryGetObjectField(TEXT("pawn_state"), Pawn) && Pawn)
			{
				const TSharedPtr<FJsonObject>* Loc = nullptr;
				if ((*Pawn)->TryGetObjectField(TEXT("location"), Loc) && Loc)
				{
					double X = -1; (*Loc)->TryGetNumberField(TEXT("x"), X);
					TestTrue(TEXT("interpolated pos_x ~ 50"), FMath::IsNearlyEqual(X, 50.0, 0.01));
				}
			}
			int32 Total = 0; O->TryGetNumberField(TEXT("total_frames"), Total);
			TestEqual(TEXT("total frames"), Total, 3);
		}
	}

	// at_frame=2 -> pos_x exactly 200.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("recording_dir"), Folder);
		P->SetNumberField(TEXT("at_frame"), 2);
		TSharedPtr<FJsonValue> Res = FPIEHandlers::PieReplayState(P);
		const TSharedPtr<FJsonObject> O = Res.IsValid() ? Res->AsObject() : nullptr;
		if (O.IsValid())
		{
			const TSharedPtr<FJsonObject>* Pawn = nullptr;
			const TSharedPtr<FJsonObject>* Loc = nullptr;
			if (O->TryGetObjectField(TEXT("pawn_state"), Pawn) && Pawn &&
				(*Pawn)->TryGetObjectField(TEXT("location"), Loc) && Loc)
			{
				double X = -1; (*Loc)->TryGetNumberField(TEXT("x"), X);
				TestTrue(TEXT("frame 2 pos_x = 200"), FMath::IsNearlyEqual(X, 200.0, 0.01));
			}
		}
	}

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
