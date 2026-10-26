#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Templates/Function.h"

class UMCPPIETestScript;

/**
 * Test script runner. Runs one script at a time against a PIE session: arms an
 * observation run (and by default a recording) under the script run id, walks
 * the steps in order, judges auto steps from the observed values and waits for
 * verdicts on manual ones, then writes results.json beside observation.csv.
 *
 * Times are game milliseconds since the run's first observed frame, the same
 * clock the observer and the replayer use, so a replay under time dilation
 * lines up with the source run without rescaling.
 *
 * The model (step parsing, evaluation, verdicts, results JSON) is plain data
 * and free functions so the automation tests drive it without an editor.
 */
namespace MCPPIE
{
	enum class EScriptJudge : uint8 { Auto, Manual };
	enum class EScriptExpectOp : uint8 { Decreasing, Increasing, Eq, Gt, Lt, Visible, Exists };
	enum class EScriptStepStatus : uint8 { Pending, Running, Passed, Failed, AwaitingVerdict, Skipped };
	enum class EScriptRunState : uint8 { Idle, Armed, Running, AwaitingVerdict, Finished, Stopped };
	enum class EScriptExpectOutcome : uint8 { Pending, Passed, Failed };
	// A step's arrange and act: none to run, running, done, failed, or skipped (a manual or replay run).
	enum class EScriptActState : uint8 { None, Running, Done, Failed, Skipped };
	enum class EScriptActOutcome : uint8 { Running, Done, Failed };

	FString ScriptJudgeToString(EScriptJudge Judge);
	bool ParseScriptJudge(const FString& Str, EScriptJudge& Out);
	FString ScriptExpectOpToString(EScriptExpectOp Op);
	bool ParseScriptExpectOp(const FString& Str, EScriptExpectOp& Out);
	FString ScriptStepStatusToString(EScriptStepStatus Status);
	bool ParseScriptStepStatus(const FString& Str, EScriptStepStatus& Out);
	FString ScriptRunStateToString(EScriptRunState State);
	bool ParseScriptRunState(const FString& Str, EScriptRunState& Out);
	FString ScriptActStateToString(EScriptActState State);
	bool ParseScriptActState(const FString& Str, EScriptActState& Out);

	// visible / exists read a boolean.
	bool IsScriptBooleanOp(EScriptExpectOp Op);
	// decreasing / increasing compare against the value at step start.
	bool IsScriptRelativeOp(EScriptExpectOp Op);
	// eq / gt / lt need a target.
	bool ScriptOpNeedsTarget(EScriptExpectOp Op);
	// passed, failed, skipped.
	bool IsScriptStepSettled(EScriptStepStatus Status);

	struct FScriptExpect
	{
		FString Value;
		EScriptExpectOp Op = EScriptExpectOp::Eq;
		bool bHasTarget = false;
		double Target = 0.0;
		// The target was given as true or false rather than a number. Draft-time checks only.
		bool bTargetIsBool = false;
		// 0 = the run's default.
		int32 WithinMs = 0;
	};

	struct FScriptStepDef
	{
		FString Instruction;
		EScriptJudge Judge = EScriptJudge::Auto;
		bool bHasExpect = false;
		FScriptExpect Expect;
		// Manual steps: false refuses a verdict with source agent.
		bool bAgentJudgeable = true;
		// expect.value, and the actors arrange and act name, only resolve once PIE runs (a spawned
		// actor, a widget created at runtime), so the draft-time check leaves them alone.
		bool bResolveInPIE = false;
		// Run before the expect window opens, in this order. Arrange entries are a scenario's
		// {kind: spawn|set|call} (FPIEScenario::ApplyArrange); act entries are uat_run's step
		// actions (FStepActionRunner). Raw JSON, checked when the step is parsed.
		TArray<TSharedPtr<FJsonValue>> Arrange;
		TArray<TSharedPtr<FJsonValue>> Act;

		bool HasArrangeOrAct() const { return Arrange.Num() > 0 || Act.Num() > 0; }
	};

	// What a step's arrange and act did.
	struct FScriptActReport
	{
		EScriptActState State = EScriptActState::None;
		// Arrange entries applied.
		int32 Arranged = 0;
		// Game ms from the observer's first frame. EndedAtMs opens the expect window.
		double StartedAtMs = -1.0;
		double EndedAtMs = -1.0;
		// Wall milliseconds the act took.
		double WallMs = 0.0;
		// Why it failed or was skipped.
		FString Reason;
		// One per act entry reached: {index, action, ok, error?, at_ms}.
		TArray<TSharedPtr<FJsonValue>> Actions;
		// Absolute paths of the captures act wrote.
		TArray<FString> Captures;
	};

	struct FScriptDef
	{
		FString Name;
		FString AssetPath;
		FString Description;
		FString Profile;
		// Level opened before PIE; empty plays whatever level is open.
		FString Map;
		int32 DefaultWithinMs = 5000;
		TArray<FScriptStepDef> Steps;
	};

	// ── Step definitions: JSON and asset ────────────────────────────────

	// {instruction, judge, agent_judgeable, resolve_in_pie, arrange:[...], act:[...], expect:{value, op, target (number or boolean), within_ms}}.
	// OutField, when given, names the field the error is about, such as instruction or expect.op.
	bool ParseScriptStepJson(const TSharedPtr<FJsonValue>& Value, int32 Index, FScriptStepDef& Out, FString& OutError, FString* OutField = nullptr);
	bool ParseScriptStepsJson(const TArray<TSharedPtr<FJsonValue>>& Values, TArray<FScriptStepDef>& Out, FString& OutError);
	TSharedRef<FJsonObject> ScriptExpectToJson(const FScriptExpect& Expect);
	TSharedRef<FJsonObject> ScriptStepDefToJson(const FScriptStepDef& Step);
	TArray<TSharedPtr<FJsonValue>> ScriptStepDefsToJson(const TArray<FScriptStepDef>& Steps);

	void ScriptStepsFromAsset(const UMCPPIETestScript* Asset, TArray<FScriptStepDef>& Out);
	void ScriptStepsToAsset(const TArray<FScriptStepDef>& Steps, UMCPPIETestScript* Asset);
	FScriptDef ScriptDefFromAsset(const UMCPPIETestScript* Asset, const FString& AssetPath);

	// The names a script's expectations read, split by what observes them. A
	// numeric value already covered by a profile path (exact or last segment) is left out.
	void CollectScriptObservedNames(const TArray<FScriptStepDef>& Steps,
		const TArray<FString>& ProfileValuePaths,
		TArray<FString>& OutValues, TArray<FString>& OutActors, TArray<FString>& OutWidgets);

	// ── Evaluation ──────────────────────────────────────────────────────

	// Target as a boolean for visible / exists: true unless a target of 0 was given.
	bool ScriptExpectTargetBool(const FScriptExpect& Expect);
	// Whether Current satisfies the expectation. AtStart is the baseline for relative ops.
	bool EvaluateScriptCondition(const FScriptExpect& Expect, double AtStart, double Current);
	// One frame: passed when observed and the condition holds within WithinMs,
	// failed once ElapsedMs reaches WithinMs, pending otherwise.
	EScriptExpectOutcome EvaluateScriptExpectFrame(const FScriptExpect& Expect, bool bObserved,
		double AtStart, double Current, double ElapsedMs, int32 WithinMs);

	// ── Runs ────────────────────────────────────────────────────────────

	struct FScriptEvidence
	{
		FString Value;
		bool bHasAtStart = false;
		double AtStart = 0.0;
		bool bHasAtEnd = false;
		double AtEnd = 0.0;
		int32 Samples = 0;
		bool bObserved = false;
		int64 VerdictFrame = -1;
		double VerdictAtMs = -1.0;
		FString Reason;
		// Manual steps: what the verdict's author cites. Required for an agent verdict.
		FString Note;
		TArray<FString> Captures;
	};

	// The evidence a verdict carries: a note plus capture file paths.
	struct FScriptVerdictEvidence
	{
		FString Note;
		TArray<FString> Captures;
	};

	struct FScriptStepRun
	{
		FScriptStepDef Def;
		EScriptStepStatus Status = EScriptStepStatus::Pending;
		int32 WithinMs = 0;
		double StartedAtMs = -1.0;
		double EndedAtMs = -1.0;
		FString StartedAt;
		FString EndedAt;
		FScriptEvidence Evidence;
		FString Note;
		// auto | action | elicitation | agent | source_run. action and elicitation are human verdicts.
		FString VerdictSource;
		// Replay only.
		bool bHasSourceStatus = false;
		EScriptStepStatus SourceStatus = EScriptStepStatus::Pending;
		bool bRegression = false;
		double WindowStartMs = -1.0;
		double WindowEndMs = -1.0;
		FScriptActReport Act;

		// Where within_ms counts from: the end of act, else the step's start.
		double ExpectFromMs() const { return Act.EndedAtMs >= 0.0 ? Act.EndedAtMs : StartedAtMs; }
	};

	struct FScriptEvent
	{
		int32 Seq = 0;
		FString Type;
		int32 Step = -1;
		double AtMs = 0.0;
		FString WallTime;
		TSharedPtr<FJsonObject> Data;
	};

	struct FScriptSummary
	{
		int32 Total = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		int32 Skipped = 0;
		int32 Pending = 0;
		int32 Awaiting = 0;
		int32 Regressions = 0;
		// Manual steps settled by a person (action, elicitation) and by an agent.
		int32 HumanVerdicts = 0;
		int32 AgentVerdicts = 0;
		// Manual runs: auto steps nothing observed, so they stay pending and are counted here instead.
		int32 Unchecked = 0;
	};

	struct FScriptRun
	{
		FString RunId;
		FString ScriptPath;
		FString ScriptName;
		FString Description;
		FString Profile;
		FString Map;
		FString OutputDir;
		FString ResultsPath;
		FString RecordingId;
		FString RecordingDir;
		FString ObservationRunId;
		FString ReplayOf;
		EScriptRunState State = EScriptRunState::Idle;
		bool bBlockOnManual = true;
		int32 DefaultWithinMs = 5000;
		bool bReplay = false;
		// mode manual: no PIE, observer or recording; every manual step awaits its verdict at once.
		bool bManualMode = false;
		double ManualStartSeconds = 0.0;
		double TimeScale = 1.0;
		double ReplayOffsetMs = 0.0;
		TArray<FScriptStepRun> Steps;
		TArray<FScriptEvent> Events;
		int32 NextSeq = 1;
		int32 CurrentStep = 0;
		FString StartedAt;
		FString EndedAt;
		FString StopReason;
		double NowMs = 0.0;
		int64 Frame = -1;
		bool bClockStarted = false;
		int32 AnnouncedBlockStart = -1;

		// Runtime ownership, not serialised.
		bool bOwnsRecorder = false;
		bool bOwnsReplayer = false;
		/** Replay runs: end PIE once every step has settled. */
		bool bEndPIEWhenSettled = false;
		bool bStartedPIE = false;
	};

	// Reads the live value an expectation names; false while unreadable. Boolean ops yield 1 or 0.
	using FScriptValueReader = TFunctionRef<bool(const FScriptExpect&, double&)>;

	// Runs a step's arrange and act against PIE. The run model calls it, so tests drive the model with a fake.
	class IScriptStepDriver
	{
	public:
		virtual ~IScriptStepDriver() = default;
		// Applies the step's arrange and starts its act. False, with Report.Reason, when either cannot start.
		virtual bool BeginAct(int32 Index, const FScriptStepDef& Def, double NowMs, FScriptActReport& Report) = 0;
		// Moves the act on. Done once every entry ran and its input played out; Failed with Report.Reason.
		virtual EScriptActOutcome TickAct(double NowMs, FScriptActReport& Report) = 0;
		// The run ended: stop whatever the act left running.
		virtual void Cancel() {}
	};

	TSharedRef<FJsonObject> ScriptActReportToJson(const FScriptActReport& Report);

	void InitScriptRun(FScriptRun& Run, const TArray<FScriptStepDef>& Steps);
	// full or manual.
	FString ScriptRunModeToString(const FScriptRun& Run);
	// A manual run: every manual step starts and awaits its verdict, auto steps stay pending.
	void StartScriptManualSteps(FScriptRun& Run);
	// A manual run is done once every manual step has its verdict.
	bool AreScriptManualStepsSettled(const FScriptRun& Run);
	void AddScriptEvent(FScriptRun& Run, const FString& Type, int32 Step, const TSharedPtr<FJsonObject>& Data);
	// Sequential mode: steps start in order; a running auto step holds the run, and so does a step's act.
	// Without a driver a step's arrange and act are skipped.
	void AdvanceScriptRun(FScriptRun& Run, double NowMs, int64 Frame, FScriptValueReader Reader, IScriptStepDriver* Driver = nullptr);
	// Replay mode: every auto step is judged in its own recorded window.
	void AdvanceScriptReplay(FScriptRun& Run, double NowMs, int64 Frame, FScriptValueReader Reader);
	// Manual steps only. Source is human (recorded as action), elicitation or agent; an agent
	// verdict needs an evidence note and is refused on a step with agent_judgeable false.
	// A repeated identical verdict succeeds with bOutUnchanged.
	bool ApplyScriptVerdict(FScriptRun& Run, int32 Step, const FString& Verdict, const FString& Note,
		const FString& Source, const FScriptVerdictEvidence& Evidence, bool& bOutUnchanged, FString& OutError);
	// The recorded verdict_source for a pie_script_verdict source param, or empty when unknown.
	FString NormalizeScriptVerdictSource(const FString& Source);
	bool IsScriptRunSettled(const FScriptRun& Run);
	// A running auto step fails with Reason; pending and awaiting steps stay as they are.
	void SettleUnfinishedScriptSteps(FScriptRun& Run, const FString& Reason);
	FScriptSummary SummarizeScriptRun(const FScriptRun& Run);
	TArray<int32> ScriptAwaitingSteps(const FScriptRun& Run);

	TSharedRef<FJsonObject> ScriptSummaryToJson(const FScriptSummary& Summary, bool bReplay);
	TSharedRef<FJsonObject> ScriptStepRunToJson(const FScriptStepRun& Step, int32 Index);
	TSharedRef<FJsonObject> ScriptEventToJson(const FScriptEvent& Event, const FString& RunId);
	TSharedRef<FJsonObject> ScriptRunToResultsJson(const FScriptRun& Run);
	bool ScriptRunFromResultsJson(const TSharedPtr<FJsonObject>& Obj, FScriptRun& Out, FString& OutError);
	// Steps of a replay run from a finished source run. OffsetMs is the replay's settle time.
	void PrepareScriptReplayRun(const FScriptRun& Source, double OffsetMs, FScriptRun& OutReplay);

	FString DefaultScriptObservationsRoot();
	// <Prefix>_<name>_<YYYYMMDD_HHMMSS>, suffixed when that folder already exists.
	FString MakeScriptRunId(const FString& Prefix, const FString& Name);

	struct FScriptRunConfig
	{
		FScriptDef Script;
		bool bRecord = true;
		bool bStartPIE = false;
		bool bBlockOnManual = true;
		int32 ClientId = 0;
		int32 PIEInstance = INDEX_NONE;
		bool bManualMode = false;
	};

	struct FScriptReplayConfig
	{
		FString SourceRunId;
		FString ResultsPath;
		float TimeScale = 1.0f;
		bool bAutoStopPIE = true;
		int32 ClientId = 0;
		int32 PIEInstance = INDEX_NONE;
	};

	class FPIEScriptRunner
	{
	public:
		static FPIEScriptRunner& Get();

		void Init();
		void Shutdown();

		// True while a run is armed, running or awaiting verdicts.
		bool IsActive() const;

		bool Start(const FScriptRunConfig& Cfg, FString& OutError, FString& OutMessage);
		bool StartReplay(const FScriptReplayConfig& Cfg, FString& OutError, FString& OutMessage);
		bool Stop(const FString& Reason, FString& OutError);
		bool Verdict(int32 Step, const FString& Verdict, const FString& Note, const FString& Source,
			const FScriptVerdictEvidence& Evidence, bool& bOutUnchanged, FString& OutError);

		// The active run, or the last finished one; null before the first run.
		const FScriptRun* GetRun() const { return bHasRun ? &Run : nullptr; }

	private:
		void OnEndFrame();
		void OnEndPIE(bool bIsSimulating);
		void BindEndFrame();
		void UnbindEndFrame();
		void Finish(EScriptRunState FinalState, const FString& Reason);
		bool WriteResults();
		bool ReadLive(const FScriptExpect& Expect, double& OutValue) const;
		void AdvanceNow();

		FScriptRun Run;
		bool bHasRun = false;
		TSharedPtr<IScriptStepDriver> StepDriver;

		FDelegateHandle EndPIEHandle;
		FDelegateHandle EndFrameHandle;
		bool bEndFrameBound = false;
	};
}
