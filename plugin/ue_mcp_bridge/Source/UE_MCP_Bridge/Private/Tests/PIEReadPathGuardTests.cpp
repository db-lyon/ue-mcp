// PIE actions that read from a caller-chosen folder or file keep it inside the
// project, Saved, temp or the recordings root, the same folders their writes use.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEHandlers.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"

namespace
{
	FString ErrorOf(const TSharedPtr<FJsonValue>& Res)
	{
		const TSharedPtr<FJsonObject> O = Res.IsValid() ? Res->AsObject() : nullptr;
		FString Error;
		if (O.IsValid()) O->TryGetStringField(TEXT("error"), Error);
		return Error;
	}

	bool Refused(const TSharedPtr<FJsonValue>& Res)
	{
		return ErrorOf(Res).Contains(TEXT("outside the folders"));
	}

	TSharedPtr<FJsonObject> ParamsWith(const TCHAR* Key, const FString& Value)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(Key, Value);
		return P;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEReadPathGuardTest,
	"UE.MCP.PIE.ReadPaths.StayInsideAllowedFolders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEReadPathGuardTest::RunTest(const FString& /*Parameters*/)
{
	// The engine folder is outside the project, Saved and temp on every layout this runs on.
	const FString Outside = FPaths::ConvertRelativePathToFull(FPaths::EngineDir()) / TEXT("ue-mcp-outside");

	TestTrue(TEXT("pie_record_list refuses a recording_dir outside"),
		Refused(FPIEHandlers::PieRecordList(ParamsWith(TEXT("recording_dir"), Outside))));
	TestFalse(TEXT("pie_record_list takes the default recordings root"),
		Refused(FPIEHandlers::PieRecordList(MakeShared<FJsonObject>())));

	{
		TSharedPtr<FJsonObject> P = ParamsWith(TEXT("recording_dir"), Outside);
		P->SetStringField(TEXT("a_id"), TEXT("a"));
		P->SetStringField(TEXT("b_id"), TEXT("b"));
		TestTrue(TEXT("pie_record_diff refuses a recording_dir outside"), Refused(FPIEHandlers::PieRecordDiff(P)));
	}
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("a_id"), TEXT("../../../ue-mcp-escape"));
		P->SetStringField(TEXT("b_id"), TEXT("b"));
		TestTrue(TEXT("pie_record_diff refuses an id that climbs out of the root"), Refused(FPIEHandlers::PieRecordDiff(P)));
	}

	TestTrue(TEXT("script results refuse a results_path outside"),
		Refused(FPIEHandlers::PieScriptResults(ParamsWith(TEXT("results_path"), Outside / TEXT("results.json")))));
	TestTrue(TEXT("script results refuse a run_id that climbs out"),
		Refused(FPIEHandlers::PieScriptResults(ParamsWith(TEXT("run_id"), TEXT("../../../ue-mcp-escape")))));
	TestTrue(TEXT("script replay refuses a results_path outside"),
		Refused(FPIEHandlers::PieScriptReplay(ParamsWith(TEXT("results_path"), Outside / TEXT("results.json")))));
	TestTrue(TEXT("script replay refuses a run_id that climbs out"),
		Refused(FPIEHandlers::PieScriptReplay(ParamsWith(TEXT("run_id"), TEXT("../../../ue-mcp-escape")))));

	TestTrue(TEXT("replay refuses a sequence_path outside"),
		Refused(FPIEHandlers::PieReplayArm(ParamsWith(TEXT("sequence_path"), Outside / TEXT("sequence.json")))));
	FPIEHandlers::PieReplayDisarm(MakeShared<FJsonObject>());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
