// Reproduction-test handlers: test_scaffold, test_run, test_list (item 3a).
// Turns a recording into a durable, self-verifying scenario: a test.json with
// assertions (max drift + max errors). test_run drives the recording (optional)
// and evaluates the finalised drift.json against those assertions, returning
// pass/fail + artifacts. This is the "reproduce + verify a fix" loop.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Handlers/PIE/PIEInputReplayer.h"
#include "Handlers/PIE/PIEPredicateEvaluator.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using namespace MCPPIE;

	FString ResolveRecordingFolder(const FString& Id, const FString& RecDir)
	{
		FString Folder;
		if (!RecDir.IsEmpty() && Id.IsEmpty()) Folder = RecDir;
		else Folder = (RecDir.IsEmpty() ? (FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPRecordings")) : RecDir) / Id;
		Folder.RemoveFromEnd(TEXT("/"));
		Folder.RemoveFromEnd(TEXT("\\"));
		return Folder;
	}

	// The most recent contact sheet in <RunDir>/captures, if any.
	FString FindContactSheet(const FString& RunDir)
	{
		const FString CapturesDir = RunDir / TEXT("captures");
		TArray<FString> Sheets;
		IFileManager::Get().FindFiles(Sheets, *(CapturesDir / TEXT("contact_*.jpg")), true, false);
		if (Sheets.Num() == 0) return FString();
		Sheets.Sort();
		return CapturesDir / Sheets.Last();
	}

	// Resolve the per-frame series CSV for a run directory: observation.csv (observe
	// runs) preferred, else recording.csv (record/replay runs).
	FString ResolveSeriesCsv(const FString& RunDir)
	{
		const FString Obs = RunDir / TEXT("observation.csv");
		if (FPaths::FileExists(Obs)) return Obs;
		return RunDir / TEXT("recording.csv");
	}

	// Auto-derive a starter predicate set from a recording's series. Delegates to the
	// shared evaluator helper so scenario_scaffold and test_scaffold stay in sync.
	TArray<TSharedPtr<FJsonValue>> DerivePredicates(const FString& CsvPath, int32 MaxErrors)
	{
		return MCPPIE::FPIEPredicateEvaluator::DeriveStarterPredicates(CsvPath, MaxErrors);
	}
}

TSharedPtr<FJsonValue> FPIEHandlers::PieTestScaffold(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Id = OptionalString(Params, TEXT("recording_id"));
	const FString RecDir = OptionalString(Params, TEXT("recording_dir"));
	const FString Folder = ResolveRecordingFolder(Id, RecDir);

	if (!FPaths::FileExists(Folder / TEXT("recording.csv")))
	{
		return MCPError(FString::Printf(TEXT("No recording.csv in %s; scaffold a test from a real recording"), *Folder));
	}

	const FString Name = OptionalString(Params, TEXT("name"), TEXT("repro"));

	TSharedRef<FJsonObject> Asserts = MakeShared<FJsonObject>();
	Asserts->SetNumberField(TEXT("max_position_drift_cm"), OptionalNumber(Params, TEXT("max_position_drift_cm"), 10.0));
	Asserts->SetNumberField(TEXT("max_velocity_drift_cms"), OptionalNumber(Params, TEXT("max_velocity_drift_cms"), 50.0));
	Asserts->SetNumberField(TEXT("max_rotation_drift_deg"), OptionalNumber(Params, TEXT("max_rotation_drift_deg"), 5.0));
	Asserts->SetNumberField(TEXT("max_errors"), OptionalNumber(Params, TEXT("max_errors"), 0.0));

	TSharedRef<FJsonObject> Scenario = MakeShared<FJsonObject>();
	Scenario->SetStringField(TEXT("name"), Name);
	Scenario->SetStringField(TEXT("source_recording_dir"), Folder);
	Scenario->SetStringField(TEXT("drive"), TEXT("replay"));
	Scenario->SetObjectField(TEXT("assertions"), Asserts);

	// Roadmap v2 Phase A: seed a predicate set derived from the recorded series so
	// the scaffold asserts intent (ranges, montages, no errors), not just drift.
	const int32 MaxErrorsInt = static_cast<int32>(OptionalNumber(Params, TEXT("max_errors"), 0.0));
	Scenario->SetArrayField(TEXT("predicates"), DerivePredicates(ResolveSeriesCsv(Folder), MaxErrorsInt));

	const FString TestsDir = Folder / TEXT("tests");
	IFileManager::Get().MakeDirectory(*TestsDir, true);
	const FString Path = TestsDir / (Name + TEXT(".json"));

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Scenario, W);
	if (!FFileHelper::SaveStringToFile(Out, *Path))
	{
		return MCPError(FString::Printf(TEXT("Failed to write %s"), *Path));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("test_path"), Path);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetObjectField(TEXT("scenario"), Scenario);
	Result->SetStringField(TEXT("next"), TEXT("Run it with test_run(name, recording_dir); it replays the recording and checks drift + errors against the assertions."));
	MCPSetNoRollback(Result, TEXT("test.json is written beside the recording; record_delete removes the recording and its tests"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieTestList(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Id = OptionalString(Params, TEXT("recording_id"));
	const FString RecDir = OptionalString(Params, TEXT("recording_dir"));

	TArray<FString> Folders;
	if (!Id.IsEmpty() || !RecDir.IsEmpty())
	{
		Folders.Add(ResolveRecordingFolder(Id, RecDir));
	}
	else
	{
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPRecordings");
		TArray<FString> Dirs;
		IFileManager::Get().FindFiles(Dirs, *(Root / TEXT("*")), false, true);
		for (const FString& D : Dirs) Folders.Add(Root / D);
	}

	TArray<TSharedPtr<FJsonValue>> Tests;
	for (const FString& Folder : Folders)
	{
		const FString TestsDir = Folder / TEXT("tests");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(TestsDir / TEXT("*.json")), true, false);
		for (const FString& F : Files)
		{
			TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
			T->SetStringField(TEXT("name"), FPaths::GetBaseFilename(F));
			T->SetStringField(TEXT("path"), TestsDir / F);
			T->SetStringField(TEXT("recording_dir"), Folder);
			Tests.Add(MakeShared<FJsonValueObject>(T));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("tests"), Tests);
	Result->SetNumberField(TEXT("count"), Tests.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieTestRun(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	// Locate the scenario: explicit test_path, or name under a recording's tests/.
	FString TestPath = OptionalString(Params, TEXT("test_path"));
	if (TestPath.IsEmpty())
	{
		const FString Folder = ResolveRecordingFolder(OptionalString(Params, TEXT("recording_id")), OptionalString(Params, TEXT("recording_dir")));
		const FString Name = OptionalString(Params, TEXT("name"), TEXT("repro"));
		TestPath = Folder / TEXT("tests") / (Name + TEXT(".json"));
	}

	TSharedPtr<FJsonObject> Scenario = LoadJsonFile(TestPath);
	if (!Scenario.IsValid())
	{
		return MCPError(FString::Printf(TEXT("Test scenario not found/parseable: %s"), *TestPath));
	}

	FString Source;
	Scenario->TryGetStringField(TEXT("source_recording_dir"), Source);
	const FString DriftPath = Source / TEXT("drift.json");

	const bool bAutoRun = OptionalBool(Params, TEXT("auto_run"), true);
	const bool bDriftExists = FPaths::FileExists(DriftPath);

	// If asked to run and no fresh drift is present, drive the recording now.
	if (bAutoRun && !bDriftExists)
	{
		if (!GEditor) return MCPError(TEXT("test_run auto_run requires the editor"));
		if (GEditor->PlayWorld != nullptr) return MCPError(TEXT("A PIE session is already running; stop it before test_run"));

		FReplayerArmConfig Cfg;
		Cfg.SourceDir = Source;
		Cfg.SourceRecordingId = FPaths::GetCleanFilename(Source);
		Cfg.bAutoStopPIE = true;
		Cfg.bRecordDrift = true;
		FString Err, Msg;
		if (!FPIEInputReplayer::Get().Arm(Cfg, Err, Msg)) return MCPError(Err);
		FRequestPlaySessionParams PlayParams;
		GEditor->RequestPlaySession(PlayParams);

		auto Started = MCPSuccess();
		Started->SetBoolField(TEXT("started"), true);
		Started->SetStringField(TEXT("test_path"), TestPath);
		Started->SetStringField(TEXT("poll"), TEXT("Poll replay_status until pie_active=false, then call test_run again to evaluate."));
		return MCPResult(Started);
	}

	if (!bDriftExists)
	{
		return MCPError(FString::Printf(TEXT("No drift.json at %s; run the replay first (auto_run=true) or replay_run the recording."), *DriftPath));
	}

	// Evaluate assertions against the finalised drift report.
	FDriftReport D;
	FString Err;
	if (!LoadDrift(DriftPath, D, Err))
	{
		return MCPError(FString::Printf(TEXT("Failed to read drift report: %s"), *Err));
	}

	const TSharedPtr<FJsonObject>* A = nullptr;
	Scenario->TryGetObjectField(TEXT("assertions"), A);
	auto Thr = [&A](const TCHAR* Key, double Def) -> double
	{
		double V = Def;
		if (A) (*A)->TryGetNumberField(Key, V);
		return V;
	};
	const double MaxPos = Thr(TEXT("max_position_drift_cm"), 10.0);
	const double MaxVel = Thr(TEXT("max_velocity_drift_cms"), 50.0);
	const double MaxRot = Thr(TEXT("max_rotation_drift_deg"), 5.0);
	const int32 MaxErrors = static_cast<int32>(Thr(TEXT("max_errors"), 0.0));

	TArray<TSharedPtr<FJsonValue>> Failures;
	auto Check = [&Failures](bool bCond, const FString& Msg)
	{
		if (!bCond) Failures.Add(MakeShared<FJsonValueString>(Msg));
	};
	Check(D.MaxPositionDriftCm <= MaxPos,
		FString::Printf(TEXT("position drift %.1f cm > %.1f"), D.MaxPositionDriftCm, MaxPos));
	Check(D.MaxVelocityDriftCms <= MaxVel,
		FString::Printf(TEXT("velocity drift %.1f cm/s > %.1f"), D.MaxVelocityDriftCms, MaxVel));
	Check(D.MaxRotationDriftDeg <= MaxRot,
		FString::Printf(TEXT("rotation drift %.1f deg > %.1f"), D.MaxRotationDriftDeg, MaxRot));
	Check(D.Summary.ErrorsDuringRun <= MaxErrors,
		FString::Printf(TEXT("%d errors logged > %d allowed"), D.Summary.ErrorsDuringRun, MaxErrors));

	bool bPassed = Failures.Num() == 0;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("test_path"), TestPath);
	Result->SetStringField(TEXT("drift_report_path"), DriftPath);
	Result->SetArrayField(TEXT("failures"), Failures);
	Result->SetNumberField(TEXT("max_position_drift_cm"), D.MaxPositionDriftCm);
	Result->SetNumberField(TEXT("max_velocity_drift_cms"), D.MaxVelocityDriftCms);
	Result->SetNumberField(TEXT("errors_during_run"), D.Summary.ErrorsDuringRun);

	// Roadmap v2 Phase A: evaluate the scenario's predicates against the recorded
	// series and merge their verdict with the drift asserts.
	const TArray<TSharedPtr<FJsonValue>>* PredArr = nullptr;
	if (Scenario->TryGetArrayField(TEXT("predicates"), PredArr) && PredArr && PredArr->Num() > 0)
	{
		TArray<MCPPIE::FPredicate> Preds;
		FString PErr;
		if (!MCPPIE::FPIEPredicateEvaluator::ParsePredicates(*PredArr, Preds, PErr))
		{
			return MCPError(FString::Printf(TEXT("predicate parse error: %s"), *PErr));
		}
		TArray<MCPPIE::FPredicateResult> PResults;
		if (MCPPIE::FPIEPredicateEvaluator::Evaluate(
				ResolveSeriesCsv(Source), Source / TEXT("session_errors.json"),
				Source / TEXT("manifest.json"), Preds, PResults, PErr))
		{
			bool bPredPassed = true;
			TSharedRef<FJsonObject> PBlock = MCPPIE::FPIEPredicateEvaluator::ResultsToJson(PResults, bPredPassed);
			Result->SetObjectField(TEXT("predicates"), PBlock);
			bPassed = bPassed && bPredPassed;
		}
		else
		{
			Result->SetStringField(TEXT("predicates_error"), PErr);
		}
	}

	Result->SetBoolField(TEXT("passed"), bPassed);

	// Surface the contact sheet for a visual check when present.
	const FString Sheet = FindContactSheet(Source);
	if (!Sheet.IsEmpty()) Result->SetStringField(TEXT("contact_sheet_path"), Sheet);

	MCPSetIdempotencyUnobservable(Result, TEXT("Each run replays the recording again and rewrites its drift report"));
	MCPSetNoRollback(Result, TEXT("A replay that ran cannot be undone; its drift.json is replaced on the next run"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieAssertEval(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	// Resolve the run directory: an observation run (run_id under output_dir), a
	// recording dir, or an explicit csv_path.
	FString CsvPath = OptionalString(Params, TEXT("csv_path"));
	FString RunDir;
	if (CsvPath.IsEmpty())
	{
		const FString RunId = OptionalString(Params, TEXT("run_id"));
		if (!RunId.IsEmpty())
		{
			const FString Root = OptionalString(Params, TEXT("output_dir"),
				FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPObservations"));
			RunDir = Root / RunId;
		}
		else
		{
			RunDir = ResolveRecordingFolder(OptionalString(Params, TEXT("recording_id")),
				OptionalString(Params, TEXT("recording_dir")));
		}
		if (RunDir.IsEmpty())
		{
			return MCPError(TEXT("assert_eval needs run_id (+output_dir), recording_dir/recording_id, or csv_path"));
		}
		CsvPath = ResolveSeriesCsv(RunDir);
	}
	else
	{
		RunDir = FPaths::GetPath(CsvPath);
	}

	if (!FPaths::FileExists(CsvPath))
	{
		return MCPError(FString::Printf(TEXT("series CSV not found: %s"), *CsvPath));
	}

	// Assertions: inline array, or the predicates block of a saved scenario.
	const TArray<TSharedPtr<FJsonValue>>* AArr = nullptr;
	TArray<TSharedPtr<FJsonValue>> FromTest;
	if (!TryGetArrayParam(Params, TEXT("assertions"), AArr) || !AArr)
	{
		const FString TestPath = OptionalString(Params, TEXT("test_path"));
		if (!TestPath.IsEmpty())
		{
			TSharedPtr<FJsonObject> Scenario = LoadJsonFile(TestPath);
			const TArray<TSharedPtr<FJsonValue>>* PArr = nullptr;
			if (Scenario.IsValid() && Scenario->TryGetArrayField(TEXT("predicates"), PArr) && PArr)
			{
				FromTest = *PArr;
				AArr = &FromTest;
			}
		}
	}
	if (!AArr || AArr->Num() == 0)
	{
		return MCPError(TEXT("assert_eval needs a non-empty 'assertions' array or a 'test_path' with predicates"));
	}

	TArray<MCPPIE::FPredicate> Preds;
	FString Err;
	if (!MCPPIE::FPIEPredicateEvaluator::ParsePredicates(*AArr, Preds, Err))
	{
		return MCPError(FString::Printf(TEXT("predicate parse error: %s"), *Err));
	}

	TArray<MCPPIE::FPredicateResult> Results;
	if (!MCPPIE::FPIEPredicateEvaluator::Evaluate(
			CsvPath, RunDir / TEXT("session_errors.json"), RunDir / TEXT("manifest.json"),
			Preds, Results, Err))
	{
		return MCPError(FString::Printf(TEXT("evaluate failed: %s"), *Err));
	}

	bool bPassed = true;
	TSharedRef<FJsonObject> Verdict = MCPPIE::FPIEPredicateEvaluator::ResultsToJson(Results, bPassed);
	Verdict->SetStringField(TEXT("csv_path"), CsvPath);
	const FString Sheet = FindContactSheet(RunDir);
	if (!Sheet.IsEmpty()) Verdict->SetStringField(TEXT("contact_sheet_path"), Sheet);

	// Persist the verdict next to the run for later inspection.
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Verdict, W);
	FFileHelper::SaveStringToFile(Out, *(RunDir / TEXT("verdict.json")));

	TSharedPtr<FJsonObject> VerdictResult = Verdict;
	MCPSetIdempotencyUnobservable(VerdictResult, TEXT("verdict.json is rewritten from the same inputs on every call"));
	MCPSetNoRollback(VerdictResult, TEXT("verdict.json is overwritten in place and the previous one is not kept"));
	return MCPResult(VerdictResult);
}
