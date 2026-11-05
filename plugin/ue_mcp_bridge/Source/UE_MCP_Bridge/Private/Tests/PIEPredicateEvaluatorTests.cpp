// Automation coverage for the assertion layer (Roadmap v2, Phase A):
// parse -> evaluate predicates over a synthetic series, checking hold semantics,
// deadlines, montage events, pseudo-channels, and witness frames.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEPredicateEvaluator.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEPredicateEvaluatorTest,
	"UE.MCP.PIE.Assertions.Predicates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	using namespace MCPPIE;

	TSharedPtr<FJsonValue> Pred(const TFunction<void(TSharedRef<FJsonObject>&)>& Fill)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		Fill(O);
		return MakeShared<FJsonValueObject>(O);
	}

	// Find a named result.
	const FPredicateResult* Find(const TArray<FPredicateResult>& R, const TCHAR* Name)
	{
		for (const FPredicateResult& X : R) if (X.Name == Name) return &X;
		return nullptr;
	}
}

bool FPIEPredicateEvaluatorTest::RunTest(const FString& /*Parameters*/)
{
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("MCPPredTest") /
		FGuid::NewGuid().ToString(EGuidFormats::Digits);
	IFileManager::Get().MakeDirectory(*Folder, true);

	// Synthetic series: pos_z dips to -140 at frame 2; Boss.Health hits 0 at frame 2;
	// Dodge montage plays frames 1-2.
	const FString CSV =
		TEXT("# synthetic\n")
		TEXT("frame,time,dt,pos_z,speed2d,montage,t:Boss.Health,game_ms\n")
		TEXT("0,0.00,0.016,100,0,None,500,5\n")
		TEXT("1,0.10,0.016,90,10,Dodge,400,5\n")
		TEXT("2,0.20,0.016,-140,20,Dodge,0,5\n")
		TEXT("3,0.30,0.016,80,5,None,0,5\n");
	const FString CsvPath = Folder / TEXT("observation.csv");
	TestTrue(TEXT("write csv"), FFileHelper::SaveStringToFile(CSV, *CsvPath));

	const FString ErrPath = Folder / TEXT("session_errors.json");
	FFileHelper::SaveStringToFile(
		FString(TEXT("{\"error_count\":0,\"warning_count\":0,\"issues\":[]}")), *ErrPath);

	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Add(Pred([](TSharedRef<FJsonObject>& O){
		O->SetStringField(TEXT("name"), TEXT("floor"));
		O->SetStringField(TEXT("channel"), TEXT("pos_z"));
		O->SetStringField(TEXT("op"), TEXT("gte"));
		O->SetNumberField(TEXT("value"), -50);
		O->SetStringField(TEXT("hold"), TEXT("always")); }));
	Arr.Add(Pred([](TSharedRef<FJsonObject>& O){
		O->SetStringField(TEXT("name"), TEXT("boss_dies"));
		O->SetStringField(TEXT("channel"), TEXT("Boss.Health"));   // resolves t:Boss.Health
		O->SetStringField(TEXT("op"), TEXT("lte"));
		O->SetNumberField(TEXT("value"), 0);
		O->SetStringField(TEXT("hold"), TEXT("eventually"));
		O->SetNumberField(TEXT("within_s"), 8); }));
	Arr.Add(Pred([](TSharedRef<FJsonObject>& O){
		O->SetStringField(TEXT("name"), TEXT("dodge_fired"));
		O->SetStringField(TEXT("event"), TEXT("montage"));
		O->SetStringField(TEXT("montage"), TEXT("Dodge"));
		O->SetStringField(TEXT("hold"), TEXT("eventually")); }));
	Arr.Add(Pred([](TSharedRef<FJsonObject>& O){
		O->SetStringField(TEXT("name"), TEXT("no_errors"));
		O->SetStringField(TEXT("channel"), TEXT("$errors"));
		O->SetStringField(TEXT("op"), TEXT("eq"));
		O->SetNumberField(TEXT("value"), 0);
		O->SetStringField(TEXT("hold"), TEXT("always")); }));

	TArray<FPredicate> Preds;
	FString Err;
	TestTrue(TEXT("parse ok"), FPIEPredicateEvaluator::ParsePredicates(Arr, Preds, Err));
	TestEqual(TEXT("parsed count"), Preds.Num(), 4);

	TArray<FPredicateResult> Results;
	TestTrue(TEXT("evaluate ok"),
		FPIEPredicateEvaluator::Evaluate(CsvPath, ErrPath, FString(), Preds, Results, Err));

	const FPredicateResult* Floor = Find(Results, TEXT("floor"));
	TestTrue(TEXT("floor evaluated"), Floor != nullptr);
	if (Floor)
	{
		TestFalse(TEXT("floor fails"), Floor->bPassed);
		TestEqual(TEXT("floor witness frame"), (int32)Floor->WitnessFrame, 2);
		TestTrue(TEXT("floor witness value negative"), Floor->Actual < -100.0);
	}

	const FPredicateResult* Boss = Find(Results, TEXT("boss_dies"));
	TestTrue(TEXT("boss passes"), Boss && Boss->bPassed);
	if (Boss) TestEqual(TEXT("boss witness frame"), (int32)Boss->WitnessFrame, 2);

	const FPredicateResult* Dodge = Find(Results, TEXT("dodge_fired"));
	TestTrue(TEXT("dodge passes"), Dodge && Dodge->bPassed);

	const FPredicateResult* NoErr = Find(Results, TEXT("no_errors"));
	TestTrue(TEXT("no_errors passes"), NoErr && NoErr->bPassed);

	bool bAll = true;
	FPIEPredicateEvaluator::ResultsToJson(Results, bAll);
	TestFalse(TEXT("overall fails because floor fails"), bAll);

	// A deadline miss: Boss.Health hits 0 at t=0.20; within_s 0.05 must fail late.
	{
		TArray<TSharedPtr<FJsonValue>> A2;
		A2.Add(Pred([](TSharedRef<FJsonObject>& O){
			O->SetStringField(TEXT("name"), TEXT("boss_dies_fast"));
			O->SetStringField(TEXT("channel"), TEXT("Boss.Health"));
			O->SetStringField(TEXT("op"), TEXT("lte"));
			O->SetNumberField(TEXT("value"), 0);
			O->SetStringField(TEXT("hold"), TEXT("eventually"));
			O->SetNumberField(TEXT("within_s"), 0.05); }));
		TArray<FPredicate> P2; TArray<FPredicateResult> R2;
		TestTrue(TEXT("parse2"), FPIEPredicateEvaluator::ParsePredicates(A2, P2, Err));
		TestTrue(TEXT("eval2"), FPIEPredicateEvaluator::Evaluate(CsvPath, ErrPath, FString(), P2, R2, Err));
		TestTrue(TEXT("deadline miss fails"), R2.Num() == 1 && !R2[0].bPassed);
	}

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
