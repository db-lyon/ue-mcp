// Test script CRUD and run lifecycle handlers. Members of FPIEHandlers.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "HandlerWorld.h"
#include "Handlers/PIE/MCPPIETestScript.h"
#include "Handlers/PIE/MCPPIEObservationProfile.h"
#include "Handlers/PIE/PIEScriptRunner.h"
#include "Handlers/PIE/PIEScriptValidation.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
	using namespace MCPPIE;

	const TCHAR* const ScriptDefaultPackage = TEXT("/Game/PIEStudio/Scripts");

	UMCPPIETestScript* LoadTestScriptAsset(const FString& Path)
	{
		UMCPPIETestScript* Asset = LoadObject<UMCPPIETestScript>(nullptr, *Path);
		if (!Asset && !Path.Contains(TEXT(".")))
		{
			Asset = LoadObject<UMCPPIETestScript>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
		}
		return Asset;
	}

	void TestScriptFieldsToJson(const UMCPPIETestScript* Asset, const FString& Path, TSharedPtr<FJsonObject> R)
	{
		TArray<FScriptStepDef> Steps;
		ScriptStepsFromAsset(Asset, Steps);
		R->SetStringField(TEXT("path"), Path);
		R->SetStringField(TEXT("name"), Asset->GetName());
		R->SetStringField(TEXT("description"), Asset->Description);
		R->SetStringField(TEXT("profile"), Asset->Profile);
		R->SetStringField(TEXT("map"), Asset->Map);
		R->SetNumberField(TEXT("default_within_ms"), Asset->DefaultWithinMs);
		R->SetBoolField(TEXT("manual_only"), Asset->bManualOnly);
		R->SetArrayField(TEXT("steps"), ScriptStepDefsToJson(Steps));
		R->SetNumberField(TEXT("step_count"), Steps.Num());
	}

	// The fields pie_script_update takes, as they stand now: the inverse of an update or replace.
	TSharedPtr<FJsonObject> TestScriptUpdatePayload(const UMCPPIETestScript* Asset, const FString& Path)
	{
		TArray<FScriptStepDef> Steps;
		ScriptStepsFromAsset(Asset, Steps);
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("path"), Path);
		P->SetArrayField(TEXT("steps"), ScriptStepDefsToJson(Steps));
		P->SetStringField(TEXT("description"), Asset->Description);
		P->SetStringField(TEXT("profile"), Asset->Profile);
		P->SetStringField(TEXT("map"), Asset->Map);
		P->SetNumberField(TEXT("default_within_ms"), Asset->DefaultWithinMs);
		P->SetBoolField(TEXT("manual_only"), Asset->bManualOnly);
		return P;
	}

	FString TestScriptStateKey(const UMCPPIETestScript* Asset)
	{
		TSharedPtr<FJsonObject> P = TestScriptUpdatePayload(Asset, FString());
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(P.ToSharedRef(), W);
		return Out;
	}

	bool TestScriptProfileExists(const FString& Profile)
	{
		return Profile.IsEmpty() || LoadObject<UMCPPIEObservationProfile>(nullptr, *Profile) != nullptr;
	}

	// The checks script_create and script_update share: the profile and map exist, and each step is
	// one the observer can check against the level and that profile. Issues already found (parse
	// failures) come in through Issues. Returns the structured refusal, or null.
	TSharedPtr<FJsonValue> CheckTestScript(const TArray<FScriptStepDef>& Steps, const FString& Profile, const FString& Map,
		bool bManualOnly, TArray<FScriptStepIssue>& Issues)
	{
		TSet<int32> Failed;
		for (const FScriptStepIssue& I : Issues)
		{
			if (I.Step != INDEX_NONE) Failed.Add(I.Step);
		}
		if (!TestScriptProfileExists(Profile))
		{
			TArray<FString> Profiles;
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			TArray<FAssetData> Assets;
			ARM.Get().GetAssetsByClass(UMCPPIEObservationProfile::StaticClass()->GetClassPathName(), Assets, true);
			for (const FAssetData& A : Assets) Profiles.Add(A.PackageName.ToString());
			FScriptStepIssue Issue;
			Issue.Field = TEXT("profile");
			Issue.Message = FString::Printf(TEXT("Profile not found: %s"), *Profile);
			Issue.Candidates = RankScriptCandidates(Profile, Profiles);
			Issues.Add(MoveTemp(Issue));
		}
		const FString MapError = Map.IsEmpty() ? FString() : MCPCheckMapExists(Map);
		if (!MapError.IsEmpty())
		{
			TArray<FString> Maps;
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			TArray<FAssetData> Assets;
			ARM.Get().GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), Assets, true);
			for (const FAssetData& A : Assets) Maps.Add(A.PackageName.ToString());
			FScriptStepIssue Issue;
			Issue.Field = TEXT("map");
			Issue.Message = FString::Printf(TEXT("Map not found: %s is not a level asset"), *MCPMapPackageName(Map));
			Issue.Candidates = RankScriptCandidates(MCPMapPackageName(Map), Maps);
			Issues.Add(MoveTemp(Issue));
		}
		if (TestScriptProfileExists(Profile) && MapError.IsEmpty() && Steps.Num() > 0)
		{
			FScriptValidationContext Ctx = MakeEditorScriptValidationContext(Profile);
			const UWorld* Open = GetEditorWorld();
			if (!Map.IsEmpty() && (!Open || Open->GetOutermost()->GetName() != MCPMapPackageName(Map)))
			{
				// Actors live in the script's level, which is not the one open, so they resolve when the run opens it.
				Ctx.ResolveActor = [](const FString&) { FScriptLookup L; L.bFound = true; return L; };
			}
			ValidateScriptSteps(Steps, bManualOnly, Ctx, Issues, Failed);
		}
		if (Issues.Num() == 0) return nullptr;
		return MakeShared<FJsonValueObject>(ScriptIssuesErrorObject(Issues));
	}

	FString ScriptVerdictForStatus(EScriptStepStatus Status)
	{
		switch (Status)
		{
		case EScriptStepStatus::Passed: return TEXT("pass");
		case EScriptStepStatus::Failed: return TEXT("fail");
		case EScriptStepStatus::Skipped: return TEXT("skip");
		default: return FString();
		}
	}

	void ScriptRunFieldsToJson(const FScriptRun& Run, TSharedPtr<FJsonObject> R, int32 SinceSeq)
	{
		R->SetStringField(TEXT("run_id"), Run.RunId);
		R->SetStringField(TEXT("script"), Run.ScriptPath);
		R->SetStringField(TEXT("script_name"), Run.ScriptName);
		R->SetStringField(TEXT("state"), ScriptRunStateToString(Run.State));
		R->SetStringField(TEXT("mode"), ScriptRunModeToString(Run));
		R->SetNumberField(TEXT("current_step"), Run.CurrentStep);
		R->SetStringField(TEXT("output_dir"), Run.OutputDir);
		R->SetStringField(TEXT("results_path"), Run.ResultsPath);
		if (!Run.ObservationRunId.IsEmpty()) R->SetStringField(TEXT("observation_run_id"), Run.ObservationRunId);
		if (!Run.RecordingId.IsEmpty()) R->SetStringField(TEXT("recording_id"), Run.RecordingId);
		if (Run.bReplay) R->SetStringField(TEXT("replay_of"), Run.ReplayOf);
		R->SetNumberField(TEXT("elapsed_ms"), Run.NowMs);

		TArray<TSharedPtr<FJsonValue>> Steps;
		for (int32 I = 0; I < Run.Steps.Num(); ++I)
		{
			Steps.Add(MakeShared<FJsonValueObject>(ScriptStepRunToJson(Run.Steps[I], I)));
		}
		R->SetArrayField(TEXT("steps"), Steps);

		TArray<TSharedPtr<FJsonValue>> Awaiting;
		for (int32 I : ScriptAwaitingSteps(Run))
		{
			TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetNumberField(TEXT("index"), I);
			A->SetStringField(TEXT("instruction"), Run.Steps[I].Def.Instruction);
			A->SetBoolField(TEXT("agent_judgeable"), Run.Steps[I].Def.bAgentJudgeable);
			Awaiting.Add(MakeShared<FJsonValueObject>(A));
		}
		R->SetArrayField(TEXT("awaiting"), Awaiting);

		TArray<TSharedPtr<FJsonValue>> Events;
		for (const FScriptEvent& E : Run.Events)
		{
			if (E.Seq > SinceSeq) Events.Add(MakeShared<FJsonValueObject>(ScriptEventToJson(E, Run.RunId)));
		}
		R->SetArrayField(TEXT("events"), Events);
		R->SetNumberField(TEXT("last_seq"), Run.NextSeq - 1);
		R->SetObjectField(TEXT("summary"), ScriptSummaryToJson(SummarizeScriptRun(Run), Run.bReplay));
	}

	// One run as script_runs lists it, and script_list's last_run without the steps: its results.json minus the events.
	TSharedRef<FJsonObject> ScriptRunListRow(const TSharedPtr<FJsonObject>& Results, const FString& ResultsPath, bool bLive, bool bSteps)
	{
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		static const TCHAR* const Fields[] = {
			TEXT("run_id"), TEXT("script"), TEXT("script_name"), TEXT("state"), TEXT("mode"), TEXT("started_at"),
			TEXT("ended_at"), TEXT("stop_reason"), TEXT("replay_of"), TEXT("recording_id"), TEXT("observation_run_id") };
		for (const TCHAR* Field : Fields)
		{
			FString Value;
			if (Results->TryGetStringField(Field, Value) && !Value.IsEmpty()) Row->SetStringField(Field, Value);
		}
		const TSharedPtr<FJsonObject>* Summary = nullptr;
		if (Results->TryGetObjectField(TEXT("summary"), Summary)) Row->SetObjectField(TEXT("summary"), *Summary);
		Row->SetStringField(TEXT("results_path"), ResultsPath);
		Row->SetBoolField(TEXT("live"), bLive);
		const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
		if (bSteps && Results->TryGetArrayField(TEXT("steps"), Steps)) Row->SetArrayField(TEXT("steps"), *Steps);
		return Row;
	}

	struct FScriptRunListEntry
	{
		// The script's package path, empty for an inline run.
		FString Script;
		FString StartedAt;
		FDateTime Written;
		TSharedRef<FJsonObject> Row;
	};

	// Every script run the editor knows, newest first: the one it holds while it is active,
	// then each results.json under Saved/MCPObservations.
	TArray<FScriptRunListEntry> ListScriptRunEntries(bool bSteps)
	{
		TArray<FScriptRunListEntry> Out;
		TSet<FString> Seen;
		auto Add = [&Out, &Seen, bSteps](const TSharedPtr<FJsonObject>& Results, const FString& ResultsPath, bool bLive, const FDateTime& Written)
		{
			FString RunId, Script, StartedAt;
			if (!Results.IsValid() || !Results->TryGetStringField(TEXT("run_id"), RunId) || RunId.IsEmpty() || Seen.Contains(RunId)) return;
			Seen.Add(RunId);
			Results->TryGetStringField(TEXT("script"), Script);
			Results->TryGetStringField(TEXT("started_at"), StartedAt);
			Out.Add({ Script.IsEmpty() ? FString() : FPackageName::ObjectPathToPackageName(Script), StartedAt, Written,
				ScriptRunListRow(Results, ResultsPath, bLive, bSteps) });
		};
		const FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
		const FScriptRun* Held = Runner.GetRun();
		if (Held && Runner.IsActive())
		{
			Add(ScriptRunToResultsJson(*Held), Held->ResultsPath, true, FDateTime::UtcNow());
		}
		const FString Root = DefaultScriptObservationsRoot();
		TArray<FString> Dirs;
		IFileManager::Get().FindFiles(Dirs, *(Root / TEXT("*")), false, true);
		for (const FString& Dir : Dirs)
		{
			const FString Path = Root / Dir / TEXT("results.json");
			// An observation with no script run has no results.json.
			const FDateTime Written = IFileManager::Get().GetTimeStamp(*Path);
			if (Written == FDateTime::MinValue()) continue;
			Add(LoadJsonFile(Path), Path, false, Written);
		}
		Out.StableSort([](const FScriptRunListEntry& A, const FScriptRunListEntry& B)
		{
			// started_at is ISO 8601 UTC, so it orders as text; a run that never started orders by its file.
			if (A.StartedAt != B.StartedAt) return A.StartedAt > B.StartedAt;
			return A.Written > B.Written;
		});
		return Out;
	}
}

void FPIEHandlers::RegisterScriptHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	const TCHAR* const PathDoc = TEXT("Test script asset path");
	const TCHAR* const RunIdDoc = TEXT("Script run id (default the current or last run)");
	const TCHAR* const WaitDoc = TEXT("Block until the run finishes or awaits a verdict");
	const TCHAR* const TimeoutDoc = TEXT("Most seconds to wait when wait is set");
	const TCHAR* const PieInstanceDoc = TEXT("FWorldContext PIEInstance to observe (default the active PIE world)");
	const TCHAR* const ClientIdDoc = TEXT("Local-player index inside the selected PIE world (default 0)");

	// One step: {instruction, judge auto|manual, agent_judgeable, resolve_in_pie, arrange, act, expect {value, op, target, within_ms}}.
	// The shape is validated in the handler, so the replay actions' inline steps and these share one key type.
	const TCHAR* const StepsShape = TEXT("{instruction, judge: auto|manual (default auto), agent_judgeable (manual steps: false means only a person gives the verdict, default true), resolve_in_pie (true: expect.value and the actors arrange and act name only resolve once PIE runs, so script_create and script_update do not check them), arrange: [{kind: spawn, class, at?, rotation?, scale?} | {kind: set, target, path, value} | {kind: call, target, func, args?}] (run when the step starts), act: [{client?, place: {at, offset?, face?} | {location, yaw?}} | {client?, press: {action, frames?}} | {client?, inject: {action, value}} | {client?, hold: {action, value, ms}} | {client?, tape: {action, values}} | {client?, console} | {client?, capture} | {wait_ms}] (run after arrange, as uat_run runs a step's do; within_ms counts from its end), expect: {value (tracked value path or last segment, actor id for exists, widget for visible), op: decreasing|increasing|eq|gt|lt|visible|exists, target (number for eq/gt/lt, boolean for visible/exists, default true), within_ms}}");
	const TCHAR* const MapDoc = TEXT("Level the script plays in, as a package or object path such as /Game/Maps/L_Arena. A run opens it before PIE and refuses when the open level has unsaved changes; empty plays whatever level is open");
	const TCHAR* const ManualOnlyDoc = TEXT("Every step is judged by a verdict: allows a script with no auto step that has an expect (default false)");

	Registry.RegisterHandler(TEXT("pie_script_create"), &PieScriptCreate, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Asset name")),
		MCPParam::Optional(TEXT("package_path"), EType::String, TEXT("Package folder (default /Game/PIEStudio/Scripts)")),
		MCPParam::Required(TEXT("steps"), EType::Array, *FString::Printf(TEXT("Steps, run in order: %s"), StepsShape)).Items(EType::Object),
		MCPParam::Optional(TEXT("description"), EType::String, TEXT("What the script tests")),
		MCPParam::Optional(TEXT("profile"), EType::String, TEXT("Observation profile asset path")),
		MCPParam::Optional(TEXT("map"), EType::String, MapDoc),
		MCPParam::Optional(TEXT("default_within_ms"), EType::Integer, TEXT("Default auto-step window in game ms (default 5000)")).Min(1),
		MCPParam::Optional(TEXT("manual_only"), EType::Boolean, ManualOnlyDoc),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When the asset exists: skip (default), update or replace (overwrite it), error")).Enum({
			TEXT("skip"), TEXT("update"), TEXT("replace"), TEXT("error") }),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a script asset")));
	Registry.RegisterHandler(TEXT("pie_script_read"), &PieScriptRead, {
		MCPParam::Required(TEXT("path"), EType::String, PathDoc),
	});
	Registry.RegisterHandler(TEXT("pie_script_update"), &PieScriptUpdate, {
		MCPParam::Required(TEXT("path"), EType::String, PathDoc),
		MCPParam::Optional(TEXT("steps"), EType::Array, *FString::Printf(TEXT("Replacement steps: %s"), StepsShape)).Items(EType::Object),
		MCPParam::Optional(TEXT("description"), EType::String, TEXT("What the script tests")),
		MCPParam::Optional(TEXT("profile"), EType::String, TEXT("Observation profile asset path; empty clears it")),
		MCPParam::Optional(TEXT("map"), EType::String, TEXT("Level the script plays in; empty clears it")),
		MCPParam::Optional(TEXT("default_within_ms"), EType::Integer, TEXT("Default auto-step window in game ms")).Min(1),
		MCPParam::Optional(TEXT("manual_only"), EType::Boolean, ManualOnlyDoc),
	});
	Registry.RegisterHandler(TEXT("pie_script_validate"), &PieScriptValidate, {
		MCPParam::Required(TEXT("steps"), EType::Array, *FString::Printf(TEXT("Steps to check, as script_create takes them: %s"), StepsShape)).Items(EType::Object),
		MCPParam::Optional(TEXT("profile"), EType::String, TEXT("Observation profile asset path the script would use")),
		MCPParam::Optional(TEXT("map"), EType::String, TEXT("Level the script would play in")),
		MCPParam::Optional(TEXT("manual_only"), EType::Boolean, ManualOnlyDoc),
	});
	Registry.RegisterHandler(TEXT("pie_script_values"), &PieScriptValues, {
		MCPParam::Optional(TEXT("target"), EType::String, TEXT("Actor class or Blueprint to report on: its actors in the level, and whether it is the player pawn")),
		MCPParam::Optional(TEXT("profile"), EType::String, TEXT("Observation profile asset path whose tracked values to list")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most entries per list (default 400)")).Min(1),
	});
	Registry.RegisterHandler(TEXT("pie_script_delete"), &PieScriptDelete, {
		MCPParam::Required(TEXT("path"), EType::String, PathDoc),
		MCPParam::Optional(TEXT("confirm"), EType::Boolean, TEXT("Must be true to delete")),
	});
	Registry.RegisterHandler(TEXT("pie_script_list"), &PieScriptList, {
		MCPParam::Optional(TEXT("directory"), EType::String, TEXT("Content folder to list under (default /Game)")),
	});
	Registry.RegisterHandler(TEXT("pie_script_run"), &PieScriptRun, {
		MCPParam::Optional(TEXT("script"), EType::String, TEXT("Test script asset path")),
		MCPParam::Optional(TEXT("steps"), EType::Array, *FString::Printf(TEXT("Inline steps instead of a script asset: %s"), StepsShape)).Items(EType::Object),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Name for an inline run (default inline)")),
		MCPParam::Optional(TEXT("profile"), EType::String, TEXT("Observation profile asset path; overrides the script's")),
		MCPParam::Optional(TEXT("map"), EType::String, TEXT("Level to open before PIE; overrides the script's. Mode manual leaves the open level as it is")),
		MCPParam::Optional(TEXT("record"), EType::Boolean, TEXT("Record input so pie_script_replay can rerun it (default true)")),
		MCPParam::Optional(TEXT("start_pie"), EType::Boolean, TEXT("Start PIE when it is not running (default false)")),
		MCPParam::Optional(TEXT("block_on_manual"), EType::Boolean, TEXT("Hold the run at each manual block until every step in it has a verdict (default true)")),
		MCPParam::Optional(TEXT("mode"), EType::String, TEXT("full (default) plays the script in PIE; manual starts no PIE, observer or recording, leaves auto steps unchecked and puts every manual step up for its verdict at once")).Enum({
			TEXT("full"), TEXT("manual") }),
		MCPParam::Optional(TEXT("default_within_ms"), EType::Integer, TEXT("Default auto-step window in game ms; overrides the script's (default 5000)")).Min(1),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, PieInstanceDoc).Min(0),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, ClientIdDoc).Min(0),
		MCPParam::Optional(TEXT("wait"), EType::Boolean, WaitDoc),
		MCPParam::Optional(TEXT("timeout_seconds"), EType::Number, TimeoutDoc).Min(0),
	}, MCPSpec::ExactlyOne({ { TEXT("script") }, { TEXT("steps") } })
		.ContractExempt(TEXT("Arms the observer and recorder and can start PIE")));
	Registry.RegisterHandler(TEXT("pie_script_status"), &PieScriptStatus, {
		MCPParam::Optional(TEXT("run_id"), EType::String, RunIdDoc),
		MCPParam::Optional(TEXT("since_seq"), EType::Integer, TEXT("Only events with a larger seq (default 0, every event)")).Min(0),
		MCPParam::Optional(TEXT("wait"), EType::Boolean, WaitDoc),
		MCPParam::Optional(TEXT("timeout_seconds"), EType::Number, TimeoutDoc).Min(0),
	});
	Registry.RegisterHandler(TEXT("pie_script_stop"), &PieScriptStop, {
		MCPParam::Optional(TEXT("run_id"), EType::String, TEXT("Run to stop; must be the active run when given")),
		MCPParam::Optional(TEXT("reason"), EType::String, TEXT("Recorded as the run's stop reason")),
	});
	Registry.RegisterHandler(TEXT("pie_script_verdict"), &PieScriptVerdict, {
		MCPParam::Required(TEXT("step"), EType::Integer, TEXT("Manual step index, 0-based")).Min(0),
		MCPParam::Required(TEXT("verdict"), EType::String, TEXT("Verdict")).Enum({ TEXT("pass"), TEXT("fail"), TEXT("skip") }),
		MCPParam::Optional(TEXT("note"), EType::String, TEXT("Tester's note")),
		MCPParam::Optional(TEXT("run_id"), EType::String, TEXT("Run the step belongs to (default the current or last run)")),
		MCPParam::Optional(TEXT("source"), EType::String, TEXT("Who judged the step: human (default) or agent. An agent verdict needs evidence and is refused on a step with agent_judgeable false. elicitation is the verdict form's human channel, action an alias of human")).Enum({
			TEXT("human"), TEXT("agent"), TEXT("elicitation"), TEXT("action") }),
		MCPParam::Optional(TEXT("evidence"), EType::Object, TEXT("What the verdict rests on; required when source is agent")).WithFields({
			MCPParam::RequiredField(TEXT("note"), EType::String, TEXT("What was observed")),
			MCPParam::OptionalField(TEXT("captures"), EType::Array, TEXT("Capture files that show it: absolute, or relative to the run's output_dir; each must exist")).Items(EType::String),
		}),
	});
	Registry.RegisterHandler(TEXT("pie_script_results"), &PieScriptResults, {
		MCPParam::Optional(TEXT("run_id"), EType::String, RunIdDoc),
		MCPParam::Optional(TEXT("results_path"), EType::String, TEXT("results.json path, instead of a run id")),
	});
	Registry.RegisterHandler(TEXT("pie_script_runs"), &PieScriptRuns, {
		MCPParam::Optional(TEXT("script"), EType::String, TEXT("Test script asset path: only that script's runs")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most runs, newest first (default 50)")).Min(1),
		MCPParam::Optional(TEXT("include_steps"), EType::Boolean, TEXT("Each run's per-step results (default true)")),
	});
	Registry.RegisterHandler(TEXT("pie_script_replay"), &PieScriptReplay, {
		MCPParam::Optional(TEXT("run_id"), EType::String, TEXT("Finished run to replay (default the last run)")),
		MCPParam::Optional(TEXT("results_path"), EType::String, TEXT("results.json of the run to replay, instead of a run id")),
		MCPParam::Optional(TEXT("time_scale"), EType::Number, TEXT("Global time dilation during the replay (default 1)")).Min(0),
		MCPParam::Optional(TEXT("auto_stop_pie"), EType::Boolean, TEXT("End PIE when the replay finishes (default true)")),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, PieInstanceDoc).Min(0),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, ClientIdDoc).Min(0),
		MCPParam::Optional(TEXT("wait"), EType::Boolean, WaitDoc),
		MCPParam::Optional(TEXT("timeout_seconds"), EType::Number, TimeoutDoc).Min(0),
	}, MCPSpec::ContractExempt(TEXT("Arms the replayer and observer and starts PIE")));
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptCreate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Name;
	TSharedPtr<FJsonValue> NameErr = RequireString(Params, TEXT("name"), Name);
	const FString PackagePath = OptionalString(Params, TEXT("package_path"), ScriptDefaultPackage);
	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	TSharedPtr<FJsonValue> StepsErr = RequireArray(Params, TEXT("steps"), StepsArr);
	const FString Description = OptionalString(Params, TEXT("description"));
	const FString Profile = OptionalString(Params, TEXT("profile"));
	const FString Map = OptionalString(Params, TEXT("map")).TrimStartAndEnd();
	const int32 DefaultWithinMs = OptionalInt(Params, TEXT("default_within_ms"), 5000);
	const bool bManualOnly = OptionalBool(Params, TEXT("manual_only"), false);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip")).ToLower();
	if (NameErr) return NameErr;
	if (StepsErr) return StepsErr;

	if (OnConflict != TEXT("skip") && OnConflict != TEXT("update") && OnConflict != TEXT("replace") && OnConflict != TEXT("error"))
	{
		return MCPError(FString::Printf(TEXT("onConflict must be skip, update, replace or error, got '%s'"), *OnConflict));
	}
	if (DefaultWithinMs <= 0) return MCPError(TEXT("default_within_ms must be at least 1"));
	TArray<FScriptStepDef> Steps;
	TArray<FScriptStepIssue> Issues;
	ParseScriptStepsJsonIssues(*StepsArr, Steps, Issues);
	if (TSharedPtr<FJsonValue> Refusal = CheckTestScript(Steps, Profile, Map, bManualOnly, Issues)) return Refusal;

	const FString AssetPath = PackagePath / Name;
	if (OnConflict == TEXT("update") || OnConflict == TEXT("replace"))
	{
		if (UMCPPIETestScript* Existing = LoadTestScriptAsset(AssetPath))
		{
			const FString Before = TestScriptStateKey(Existing);
			TSharedPtr<FJsonObject> Inverse = TestScriptUpdatePayload(Existing, AssetPath);
			ScriptStepsToAsset(Steps, Existing);
			Existing->Description = Description;
			Existing->Profile = Profile;
			Existing->Map = Map;
			Existing->DefaultWithinMs = DefaultWithinMs;
			Existing->bManualOnly = bManualOnly;
			auto Result = MCPSuccess();
			if (TestScriptStateKey(Existing) == Before)
			{
				Result->SetBoolField(TEXT("unchanged"), true);
				MCPSetExisted(Result);
			}
			else
			{
				if (!SaveAssetPackage(Existing)) return MCPError(FString::Printf(TEXT("Failed to save %s"), *AssetPath));
				MCPSetUpdated(Result);
				MCPSetRollback(Result, TEXT("pie_script_update"), Inverse);
			}
			TestScriptFieldsToJson(Existing, AssetPath, Result);
			return MCPResult(Result);
		}
	}

	auto Create = MCPCreateAssetIdempotentNewObject<UMCPPIETestScript>(
		Name, PackagePath, OnConflict == TEXT("error") ? OnConflict : FString(TEXT("skip")), TEXT("TestScript"));
	if (Create.EarlyReturn) return Create.EarlyReturn;

	UMCPPIETestScript* Script = Create.Asset;
	ScriptStepsToAsset(Steps, Script);
	Script->Description = Description;
	Script->Profile = Profile;
	Script->Map = Map;
	Script->DefaultWithinMs = DefaultWithinMs;
	Script->bManualOnly = bManualOnly;
	FString SaveError;
	if (!SaveAssetPackageChecked(Script, SaveError))
	{
		return MCPError(FString::Printf(TEXT("Saving the script failed: %s"), *SaveError));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	TestScriptFieldsToJson(Script, AssetPath, Result);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetBoolField(TEXT("confirm"), true);
	MCPSetRollback(Result, TEXT("pie_script_delete"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptRead(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;

	UMCPPIETestScript* Script = LoadTestScriptAsset(Path);
	if (!Script) return MCPError(FString::Printf(TEXT("Test script not found: %s"), *Path));

	auto Result = MCPSuccess();
	TestScriptFieldsToJson(Script, Path, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptUpdate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("path"), Path);
	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	const bool bHasSteps = TryGetArrayParam(Params, TEXT("steps"), StepsArr) && StepsArr;
	FString Description;
	const bool bHasDescription = TryGetStringParam(Params, TEXT("description"), Description);
	FString Profile;
	const bool bHasProfile = TryGetStringParam(Params, TEXT("profile"), Profile);
	FString Map;
	const bool bHasMap = TryGetStringParam(Params, TEXT("map"), Map);
	Map.TrimStartAndEndInline();
	int32 DefaultWithinMs = 0;
	const bool bHasWithin = TryGetNumberParam(Params, TEXT("default_within_ms"), DefaultWithinMs);
	bool bManualOnly = false;
	const bool bHasManualOnly = TryGetBoolParam(Params, TEXT("manual_only"), bManualOnly);
	if (PathErr) return PathErr;
	if (bHasWithin && DefaultWithinMs <= 0) return MCPError(TEXT("default_within_ms must be at least 1"));

	UMCPPIETestScript* Script = LoadTestScriptAsset(Path);
	if (!Script) return MCPError(FString::Printf(TEXT("Test script not found: %s"), *Path));

	// The script as it would stand after the update is checked whenever its steps, profile, map or
	// manual_only change; a description or window change alone leaves the steps as they were.
	TArray<FScriptStepDef> Steps;
	if (bHasSteps || bHasProfile || bHasMap || bHasManualOnly)
	{
		TArray<FScriptStepIssue> Issues;
		if (bHasSteps) ParseScriptStepsJsonIssues(*StepsArr, Steps, Issues);
		else ScriptStepsFromAsset(Script, Steps);
		if (TSharedPtr<FJsonValue> Refusal = CheckTestScript(Steps, bHasProfile ? Profile : Script->Profile,
			bHasMap ? Map : Script->Map, bHasManualOnly ? bManualOnly : Script->bManualOnly, Issues))
		{
			return Refusal;
		}
	}

	const FString Before = TestScriptStateKey(Script);
	TSharedPtr<FJsonObject> Inverse = TestScriptUpdatePayload(Script, Path);
	if (bHasSteps) ScriptStepsToAsset(Steps, Script);
	if (bHasDescription) Script->Description = Description;
	if (bHasProfile) Script->Profile = Profile;
	if (bHasMap) Script->Map = Map;
	if (bHasWithin) Script->DefaultWithinMs = DefaultWithinMs;
	if (bHasManualOnly) Script->bManualOnly = bManualOnly;

	auto Result = MCPSuccess();
	if (TestScriptStateKey(Script) == Before)
	{
		Result->SetBoolField(TEXT("unchanged"), true);
	}
	else
	{
		if (!SaveAssetPackage(Script)) return MCPError(FString::Printf(TEXT("Failed to save %s"), *Path));
		MCPSetUpdated(Result);
		MCPSetRollback(Result, TEXT("pie_script_update"), Inverse);
	}
	TestScriptFieldsToJson(Script, Path, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptValidate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	TSharedPtr<FJsonValue> StepsErr = RequireArray(Params, TEXT("steps"), StepsArr);
	const FString Profile = OptionalString(Params, TEXT("profile"));
	const FString Map = OptionalString(Params, TEXT("map")).TrimStartAndEnd();
	const bool bManualOnly = OptionalBool(Params, TEXT("manual_only"), false);
	if (StepsErr) return StepsErr;

	TArray<FScriptStepDef> Steps;
	TArray<FScriptStepIssue> Issues;
	ParseScriptStepsJsonIssues(*StepsArr, Steps, Issues);
	const TSharedPtr<FJsonValue> Refusal = CheckTestScript(Steps, Profile, Map, bManualOnly, Issues);

	// The same checks script_create runs, answered rather than refused, so a caller can draft in a loop.
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("valid"), !Refusal.IsValid());
	TArray<TSharedPtr<FJsonValue>> Errors;
	for (const FScriptStepIssue& I : Issues) Errors.Add(MakeShared<FJsonValueObject>(ScriptIssueToJson(I)));
	Result->SetArrayField(TEXT("errors"), Errors);
	if (Refusal.IsValid()) Result->SetStringField(TEXT("summary"), Refusal->AsObject()->GetStringField(TEXT("error")));
	Result->SetArrayField(TEXT("steps"), ScriptStepDefsToJson(Steps));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptValues(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Target = OptionalString(Params, TEXT("target"));
	const FString Profile = OptionalString(Params, TEXT("profile"));
	const int32 Limit = OptionalInt(Params, TEXT("limit"), 400);
	if (Limit < 1) return MCPError(TEXT("limit must be at least 1"));
	if (!TestScriptProfileExists(Profile)) return MCPError(FString::Printf(TEXT("Profile not found: %s"), *Profile));

	TSharedRef<FJsonObject> Values = ScriptWatchableValuesToJson(Target, Profile, Limit);
	auto Result = MCPSuccess();
	for (const auto& JsonEntry : Values->Values)
	{
		const TPair<FString, TSharedPtr<FJsonValue>> Field(JsonEntry.Key, JsonEntry.Value);
		Result->SetField(Field.Key, Field.Value);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptDelete(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("path"), Path);
	const bool bConfirm = OptionalBool(Params, TEXT("confirm"), false);
	if (PathErr) return PathErr;
	if (!bConfirm) return MCPError(TEXT("confirm=true required to delete a test script"));
	if (MCPIsProtectedAssetPath(Path)) return MCPError(FString::Printf(TEXT("Refusing to delete a protected path: %s"), *Path));

	if (!UEditorAssetLibrary::DoesAssetExist(Path))
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("already_deleted"), true);
		Result->SetBoolField(TEXT("changed"), false);
		Result->SetStringField(TEXT("path"), Path);
		return MCPResult(Result);
	}

	UMCPPIETestScript* Script = LoadTestScriptAsset(Path);
	if (!Script) return MCPError(FString::Printf(TEXT("%s is not a test script"), *Path));

	TArray<FScriptStepDef> Steps;
	ScriptStepsFromAsset(Script, Steps);
	TSharedPtr<FJsonObject> Recreate = MakeShared<FJsonObject>();
	Recreate->SetStringField(TEXT("name"), Script->GetName());
	Recreate->SetStringField(TEXT("package_path"), FPackageName::GetLongPackagePath(Script->GetOutermost()->GetName()));
	Recreate->SetArrayField(TEXT("steps"), ScriptStepDefsToJson(Steps));
	Recreate->SetStringField(TEXT("description"), Script->Description);
	Recreate->SetStringField(TEXT("profile"), Script->Profile);
	Recreate->SetStringField(TEXT("map"), Script->Map);
	Recreate->SetNumberField(TEXT("default_within_ms"), Script->DefaultWithinMs);
	Recreate->SetBoolField(TEXT("manual_only"), Script->bManualOnly);

	if (!UEditorAssetLibrary::DeleteAsset(Path)) return MCPError(FString::Printf(TEXT("Failed to delete: %s"), *Path));

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetStringField(TEXT("path"), Path);
	MCPSetRollback(Result, TEXT("pie_script_create"), Recreate);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptList(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByClass(UMCPPIETestScript::StaticClass()->GetClassPathName(), Assets, true);

	// Each script's newest run, from the list script_runs answers with.
	TMap<FString, TSharedRef<FJsonObject>> LastRuns;
	for (const FScriptRunListEntry& E : ListScriptRunEntries(false))
	{
		if (!E.Script.IsEmpty() && !LastRuns.Contains(E.Script)) LastRuns.Add(E.Script, E.Row);
	}

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FAssetData& A : Assets)
	{
		const FString PackageName = A.PackageName.ToString();
		if (!PackageName.StartsWith(Directory)) continue;
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), PackageName);
		Item->SetStringField(TEXT("object_path"), A.GetObjectPathString());
		Item->SetStringField(TEXT("name"), A.AssetName.ToString());
		FString Map;
		A.GetTagValue(GET_MEMBER_NAME_CHECKED(UMCPPIETestScript, Map), Map);
		Item->SetStringField(TEXT("map"), Map);
		if (const TSharedRef<FJsonObject>* Last = LastRuns.Find(PackageName)) Item->SetObjectField(TEXT("last_run"), *Last);
		else Item->SetField(TEXT("last_run"), MakeShared<FJsonValueNull>());
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("scripts"), Items);
	Result->SetNumberField(TEXT("count"), Items.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptRun(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	MCPReadParamsAhead(Params, { TEXT("wait"), TEXT("timeout_seconds") });
	int32 PIEInstance = INDEX_NONE;
	FString PIEInstanceError;
	const bool bPIEInstanceOk = FPIEHandlers::ParsePIEInstance(Params, PIEInstance, PIEInstanceError);
	const FString ScriptPath = OptionalString(Params, TEXT("script"));
	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	const bool bHasSteps = TryGetArrayParam(Params, TEXT("steps"), StepsArr) && StepsArr;
	const FString Name = OptionalString(Params, TEXT("name"));
	FString Profile;
	const bool bHasProfile = TryGetStringParam(Params, TEXT("profile"), Profile);
	FString Map;
	const bool bHasMap = TryGetStringParam(Params, TEXT("map"), Map);
	Map.TrimStartAndEndInline();
	const bool bRecord = OptionalBool(Params, TEXT("record"), true);
	const bool bStartPIE = OptionalBool(Params, TEXT("start_pie"), false);
	const bool bBlockOnManual = OptionalBool(Params, TEXT("block_on_manual"), true);
	int32 DefaultWithinMs = 0;
	const bool bHasWithin = TryGetNumberParam(Params, TEXT("default_within_ms"), DefaultWithinMs);
	const int32 ClientId = OptionalInt(Params, TEXT("client_id"), 0);
	const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("full")).ToLower();
	if (!bPIEInstanceOk) return MCPError(PIEInstanceError);
	if (Mode != TEXT("full") && Mode != TEXT("manual")) return MCPError(FString::Printf(TEXT("mode must be full or manual, got '%s'"), *Mode));
	const bool bManual = Mode == TEXT("manual");

	if (ScriptPath.IsEmpty() == !bHasSteps)
	{
		return MCPError(TEXT("Give exactly one of script (asset path) or steps (inline)"));
	}
	if (bHasWithin && DefaultWithinMs <= 0) return MCPError(TEXT("default_within_ms must be at least 1"));
	if (ClientId < 0) return MCPError(TEXT("client_id must be 0 or more"));

	FScriptRunConfig Cfg;
	if (!ScriptPath.IsEmpty())
	{
		UMCPPIETestScript* Script = LoadTestScriptAsset(ScriptPath);
		if (!Script) return MCPError(FString::Printf(TEXT("Test script not found: %s"), *ScriptPath));
		Cfg.Script = ScriptDefFromAsset(Script, ScriptPath);
		if (Cfg.Script.Steps.Num() == 0) return MCPError(FString::Printf(TEXT("Test script %s has no steps"), *ScriptPath));
	}
	else
	{
		FString ParseErr;
		if (!ParseScriptStepsJson(*StepsArr, Cfg.Script.Steps, ParseErr)) return MCPError(ParseErr);
		Cfg.Script.Name = Name.IsEmpty() ? FString(TEXT("inline")) : Name;
	}
	if (!Name.IsEmpty()) Cfg.Script.Name = Name;
	if (bHasProfile) Cfg.Script.Profile = Profile;
	if (bHasMap) Cfg.Script.Map = Map;
	if (bHasWithin) Cfg.Script.DefaultWithinMs = DefaultWithinMs;
	Cfg.bManualMode = bManual;
	Cfg.bRecord = bRecord && !bManual;
	Cfg.bStartPIE = bStartPIE && !bManual;
	Cfg.bBlockOnManual = bBlockOnManual;
	Cfg.ClientId = ClientId;
	Cfg.PIEInstance = PIEInstance;

	FString Err, Msg;
	FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
	if (!Runner.Start(Cfg, Err, Msg)) return MCPError(Err);

	const FScriptRun* Run = Runner.GetRun();
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	ScriptRunFieldsToJson(*Run, Result, 0);
	Result->SetBoolField(TEXT("pie_started"), Run->bStartedPIE);
	Result->SetBoolField(TEXT("pie_running"), GEditor && GEditor->PlayWorld != nullptr);
	if (!Run->Map.IsEmpty()) Result->SetStringField(TEXT("map"), Run->Map);
	Result->SetStringField(TEXT("message"), Msg);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("run_id"), Run->RunId);
	Payload->SetStringField(TEXT("reason"), TEXT("rollback"));
	MCPSetRollback(Result, TEXT("pie_script_stop"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptStatus(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	MCPReadParamsAhead(Params, { TEXT("wait"), TEXT("timeout_seconds") });
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	const int32 SinceSeq = FMath::Max(0, OptionalInt(Params, TEXT("since_seq"), 0));

	const FScriptRun* Run = FPIEScriptRunner::Get().GetRun();
	auto Result = MCPSuccess();
	if (!Run || (!RunId.IsEmpty() && Run->RunId != RunId))
	{
		// A finished run the editor no longer holds answers from its results.json.
		const TSharedPtr<FJsonObject> Saved = RunId.IsEmpty() ? nullptr
			: LoadJsonFile(DefaultScriptObservationsRoot() / RunId / TEXT("results.json"));
		if (Saved.IsValid())
		{
			for (const auto& JsonEntry : Saved->Values)
			{
				const TPair<FString, TSharedPtr<FJsonValue>> Field(JsonEntry.Key, JsonEntry.Value);
				if (Field.Key != TEXT("events")) Result->SetField(Field.Key, Field.Value);
			}
			TArray<TSharedPtr<FJsonValue>> Events;
			const TArray<TSharedPtr<FJsonValue>>* AllEvents = nullptr;
			if (Saved->TryGetArrayField(TEXT("events"), AllEvents))
			{
				for (const TSharedPtr<FJsonValue>& E : *AllEvents)
				{
					const TSharedPtr<FJsonObject>* EO = nullptr;
					double Seq = 0;
					if (E->TryGetObject(EO) && (*EO)->TryGetNumberField(TEXT("seq"), Seq) && Seq > SinceSeq) Events.Add(E);
				}
			}
			TArray<TSharedPtr<FJsonValue>> Awaiting;
			const TArray<TSharedPtr<FJsonValue>>* SavedSteps = nullptr;
			if (Saved->TryGetArrayField(TEXT("steps"), SavedSteps))
			{
				for (const TSharedPtr<FJsonValue>& S : *SavedSteps)
				{
					const TSharedPtr<FJsonObject>* SO = nullptr;
					FString Status;
					if (S->TryGetObject(SO) && (*SO)->TryGetStringField(TEXT("status"), Status) && Status == TEXT("awaiting_verdict"))
					{
						TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
						A->SetNumberField(TEXT("index"), (*SO)->GetNumberField(TEXT("index")));
						A->SetStringField(TEXT("instruction"), (*SO)->GetStringField(TEXT("instruction")));
						Awaiting.Add(MakeShared<FJsonValueObject>(A));
					}
				}
			}
			Result->SetArrayField(TEXT("events"), Events);
			Result->SetArrayField(TEXT("awaiting"), Awaiting);
			Result->SetBoolField(TEXT("active"), false);
			Result->SetBoolField(TEXT("live"), false);
			return MCPResult(Result);
		}
		Result->SetStringField(TEXT("state"), TEXT("idle"));
		if (!RunId.IsEmpty()) Result->SetStringField(TEXT("run_id"), RunId);
		Result->SetArrayField(TEXT("steps"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetArrayField(TEXT("awaiting"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetArrayField(TEXT("events"), TArray<TSharedPtr<FJsonValue>>());
		Result->SetNumberField(TEXT("last_seq"), SinceSeq);
		return MCPResult(Result);
	}
	ScriptRunFieldsToJson(*Run, Result, SinceSeq);
	Result->SetBoolField(TEXT("active"), FPIEScriptRunner::Get().IsActive());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptStop(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	const FString Reason = OptionalString(Params, TEXT("reason"));

	FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
	const FScriptRun* Run = Runner.GetRun();
	if (!Runner.IsActive())
	{
		TSharedPtr<FJsonObject> Already = MCPErrorObject(TEXT("No script run is active, so nothing was stopped."));
		Already->SetBoolField(TEXT("alreadyStopped"), true);
		Already->SetBoolField(TEXT("changed"), false);
		if (Run && (RunId.IsEmpty() || Run->RunId == RunId))
		{
			Already->SetStringField(TEXT("run_id"), Run->RunId);
			Already->SetStringField(TEXT("state"), ScriptRunStateToString(Run->State));
			Already->SetStringField(TEXT("results_path"), Run->ResultsPath);
		}
		return MCPResult(Already);
	}
	if (!RunId.IsEmpty() && Run->RunId != RunId)
	{
		return MCPError(FString::Printf(TEXT("Run %s is not the active run (%s)"), *RunId, *Run->RunId));
	}

	FString Err;
	if (!Runner.Stop(Reason, Err)) return MCPError(Err);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("stopped"), true);
	Result->SetBoolField(TEXT("changed"), true);
	ScriptRunFieldsToJson(*Runner.GetRun(), Result, 0);
	MCPSetNoRollback(Result, TEXT("A stopped run cannot be resumed; start a new run with pie_script_run."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptVerdict(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	int32 Step = 0;
	TSharedPtr<FJsonValue> StepErr = RequireNumber(Params, TEXT("step"), Step);
	FString Verdict;
	TSharedPtr<FJsonValue> VerdictErr = RequireString(Params, TEXT("verdict"), Verdict);
	const FString Note = OptionalString(Params, TEXT("note"));
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	const FString Source = OptionalString(Params, TEXT("source"), TEXT("human"));
	if (StepErr) return StepErr;
	if (VerdictErr) return VerdictErr;

	FScriptVerdictEvidence Evidence;
	TArray<FString> RawCaptures;
	if (HasParam(Params, TEXT("evidence")))
	{
		const TSharedPtr<FJsonObject>* EvObj = nullptr;
		if (RequireObject(Params, TEXT("evidence"), EvObj))
		{
			return MCPError(TEXT("evidence must be an object {note, captures?}"));
		}
		(*EvObj)->TryGetStringField(TEXT("note"), Evidence.Note);
		if ((*EvObj)->HasField(TEXT("captures")))
		{
			const TArray<TSharedPtr<FJsonValue>>* Captures = nullptr;
			if (!(*EvObj)->TryGetArrayField(TEXT("captures"), Captures) || !Captures)
			{
				return MCPError(TEXT("evidence.captures must be an array of file paths"));
			}
			for (const TSharedPtr<FJsonValue>& C : *Captures)
			{
				FString Path;
				if (!C.IsValid() || !C->TryGetString(Path) || Path.TrimStartAndEnd().IsEmpty())
				{
					return MCPError(TEXT("evidence.captures must be an array of non-empty file paths"));
				}
				RawCaptures.Add(Path.TrimStartAndEnd());
			}
		}
	}

	FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
	const FScriptRun* Run = Runner.GetRun();
	if (!Run) return MCPError(TEXT("No script run to give a verdict on"));
	if (!RunId.IsEmpty() && Run->RunId != RunId)
	{
		return MCPError(FString::Printf(TEXT("Run %s is not the current run (%s)"), *RunId, *Run->RunId));
	}

	// A capture is resolved against the run's folder and must exist, so the evidence trail points at real files.
	for (const FString& Raw : RawCaptures)
	{
		FString Full = FPaths::IsRelative(Raw) ? FPaths::Combine(Run->OutputDir, Raw) : Raw;
		Full = FPaths::ConvertRelativePathToFull(Full);
		FPaths::NormalizeFilename(Full);
		if (!FPaths::FileExists(Full) && !FPaths::DirectoryExists(Full))
		{
			return MCPError(FString::Printf(TEXT("evidence capture '%s' does not exist (resolved to %s)"), *Raw, *Full));
		}
		Evidence.Captures.Add(Full);
	}

	const bool bValidStep = Run->Steps.IsValidIndex(Step);
	const FString PrevVerdict = bValidStep ? ScriptVerdictForStatus(Run->Steps[Step].Status) : FString();
	const FString PrevNote = bValidStep ? Run->Steps[Step].Note : FString();
	const FString PrevSource = bValidStep ? Run->Steps[Step].VerdictSource : FString();
	const FScriptEvidence PrevEvidence = bValidStep ? Run->Steps[Step].Evidence : FScriptEvidence();

	bool bUnchanged = false;
	FString Err;
	if (!Runner.Verdict(Step, Verdict, Note, Source, Evidence, bUnchanged, Err)) return MCPError(Err);

	Run = Runner.GetRun();
	const FScriptStepRun& St = Run->Steps[Step];
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("run_id"), Run->RunId);
	Result->SetNumberField(TEXT("step"), Step);
	Result->SetStringField(TEXT("status"), ScriptStepStatusToString(St.Status));
	Result->SetStringField(TEXT("verdict"), Verdict.ToLower());
	Result->SetStringField(TEXT("verdict_source"), St.VerdictSource);
	if (!St.Note.IsEmpty()) Result->SetStringField(TEXT("note"), St.Note);
	if (!St.Evidence.Note.IsEmpty() || St.Evidence.Captures.Num() > 0)
	{
		TSharedPtr<FJsonObject> Ev = MakeShared<FJsonObject>();
		if (!St.Evidence.Note.IsEmpty()) Ev->SetStringField(TEXT("note"), St.Evidence.Note);
		TArray<TSharedPtr<FJsonValue>> Caps;
		for (const FString& C : St.Evidence.Captures) Caps.Add(MakeShared<FJsonValueString>(C));
		if (Caps.Num() > 0) Ev->SetArrayField(TEXT("captures"), Caps);
		Result->SetObjectField(TEXT("evidence"), Ev);
	}
	Result->SetStringField(TEXT("state"), ScriptRunStateToString(Run->State));
	TArray<TSharedPtr<FJsonValue>> Awaiting;
	for (int32 I : ScriptAwaitingSteps(*Run)) Awaiting.Add(MakeShared<FJsonValueNumber>(I));
	Result->SetArrayField(TEXT("awaiting"), Awaiting);
	if (bUnchanged)
	{
		Result->SetBoolField(TEXT("unchanged"), true);
		MCPSetExisted(Result);
		return MCPResult(Result);
	}
	MCPSetUpdated(Result);
	if (!PrevVerdict.IsEmpty() && PrevSource != TEXT("source_run"))
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("run_id"), Run->RunId);
		Payload->SetNumberField(TEXT("step"), Step);
		Payload->SetStringField(TEXT("verdict"), PrevVerdict);
		Payload->SetStringField(TEXT("note"), PrevNote);
		Payload->SetStringField(TEXT("source"), PrevSource.IsEmpty() ? FString(TEXT("human")) : PrevSource == TEXT("action") ? FString(TEXT("human")) : PrevSource);
		if (!PrevEvidence.Note.IsEmpty() || PrevEvidence.Captures.Num() > 0)
		{
			TSharedPtr<FJsonObject> Ev = MakeShared<FJsonObject>();
			Ev->SetStringField(TEXT("note"), PrevEvidence.Note);
			TArray<TSharedPtr<FJsonValue>> Caps;
			for (const FString& C : PrevEvidence.Captures) Caps.Add(MakeShared<FJsonValueString>(C));
			if (Caps.Num() > 0) Ev->SetArrayField(TEXT("captures"), Caps);
			Payload->SetObjectField(TEXT("evidence"), Ev);
		}
		MCPSetRollback(Result, TEXT("pie_script_verdict"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("The step was awaiting its first verdict; a verdict cannot be withdrawn, only replaced."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptResults(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	FString ResultsPath = OptionalString(Params, TEXT("results_path"));

	const FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
	const FScriptRun* Run = Runner.GetRun();
	if (ResultsPath.IsEmpty())
	{
		const FString Id = !RunId.IsEmpty() ? RunId : (Run ? Run->RunId : FString());
		if (Id.IsEmpty()) return MCPError(TEXT("No script run yet; give run_id or results_path"));
		if (Run && Run->RunId == Id && Runner.IsActive())
		{
			TSharedPtr<FJsonObject> Live = ScriptRunToResultsJson(*Run);
			Live->SetBoolField(TEXT("success"), true);
			Live->SetBoolField(TEXT("live"), true);
			return MCPResult(Live);
		}
		ResultsPath = DefaultScriptObservationsRoot() / Id / TEXT("results.json");
	}

	TSharedPtr<FJsonObject> Obj = LoadJsonFile(ResultsPath);
	if (!Obj.IsValid()) return MCPError(FString::Printf(TEXT("results.json not found or unreadable: %s"), *ResultsPath));
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetBoolField(TEXT("live"), false);
	Obj->SetStringField(TEXT("results_path"), ResultsPath);
	return MCPResult(Obj);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptRuns(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Script = OptionalString(Params, TEXT("script"));
	const int32 Limit = OptionalInt(Params, TEXT("limit"), 50);
	const bool bSteps = OptionalBool(Params, TEXT("include_steps"), true);
	if (Limit < 1) return MCPError(TEXT("limit must be at least 1"));
	const FString Wanted = Script.IsEmpty() ? FString() : FPackageName::ObjectPathToPackageName(Script);

	TArray<TSharedPtr<FJsonValue>> Runs;
	int32 Total = 0;
	for (const FScriptRunListEntry& E : ListScriptRunEntries(bSteps))
	{
		if (!Wanted.IsEmpty() && E.Script != Wanted) continue;
		++Total;
		if (Runs.Num() < Limit) Runs.Add(MakeShared<FJsonValueObject>(E.Row));
	}

	auto Result = MCPSuccess();
	if (!Wanted.IsEmpty()) Result->SetStringField(TEXT("script"), Wanted);
	Result->SetArrayField(TEXT("runs"), Runs);
	Result->SetNumberField(TEXT("count"), Runs.Num());
	Result->SetNumberField(TEXT("total"), Total);
	Result->SetStringField(TEXT("observations_dir"), DefaultScriptObservationsRoot());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieScriptReplay(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	MCPReadParamsAhead(Params, { TEXT("wait"), TEXT("timeout_seconds") });
	int32 PIEInstance = INDEX_NONE;
	FString PIEInstanceError;
	const bool bPIEInstanceOk = FPIEHandlers::ParsePIEInstance(Params, PIEInstance, PIEInstanceError);
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	const FString ResultsPath = OptionalString(Params, TEXT("results_path"));
	const double TimeScale = OptionalNumber(Params, TEXT("time_scale"), 1.0);
	const bool bAutoStop = OptionalBool(Params, TEXT("auto_stop_pie"), true);
	const int32 ClientId = OptionalInt(Params, TEXT("client_id"), 0);
	if (!bPIEInstanceOk) return MCPError(PIEInstanceError);
	if (!(TimeScale > 0.0)) return MCPError(TEXT("time_scale must be greater than 0"));
	if (ClientId < 0) return MCPError(TEXT("client_id must be 0 or more"));

	FScriptReplayConfig Cfg;
	Cfg.SourceRunId = RunId;
	Cfg.ResultsPath = ResultsPath;
	Cfg.TimeScale = static_cast<float>(TimeScale);
	Cfg.bAutoStopPIE = bAutoStop;
	Cfg.ClientId = ClientId;
	Cfg.PIEInstance = PIEInstance;

	FString Err, Msg;
	FPIEScriptRunner& Runner = FPIEScriptRunner::Get();
	if (!Runner.StartReplay(Cfg, Err, Msg)) return MCPError(Err);

	const FScriptRun* Run = Runner.GetRun();
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	ScriptRunFieldsToJson(*Run, Result, 0);
	Result->SetBoolField(TEXT("pie_started"), true);
	Result->SetBoolField(TEXT("auto_stop_pie"), bAutoStop);
	Result->SetNumberField(TEXT("time_scale"), TimeScale);
	Result->SetStringField(TEXT("message"), Msg);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("run_id"), Run->RunId);
	Payload->SetStringField(TEXT("reason"), TEXT("rollback"));
	MCPSetRollback(Result, TEXT("pie_script_stop"), Payload);
	return MCPResult(Result);
}
