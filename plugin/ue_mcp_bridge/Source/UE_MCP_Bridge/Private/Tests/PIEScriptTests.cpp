// Automation coverage for the test script runner's model: expectation
// evaluation, step parsing, sequencing with verdicts, results.json and replay.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEScriptRunner.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MCPPIEScriptTests
{
	using namespace MCPPIE;

	TSharedPtr<FJsonValue> ParseJsonValue(const FString& Text)
	{
		TSharedPtr<FJsonValue> Out;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Out);
		return Out;
	}

	FScriptExpect MakeExpect(EScriptExpectOp Op, const FString& Value, bool bHasTarget = false, double Target = 0.0, int32 WithinMs = 0)
	{
		FScriptExpect E;
		E.Op = Op;
		E.Value = Value;
		E.bHasTarget = bHasTarget;
		E.Target = Target;
		E.WithinMs = WithinMs;
		return E;
	}

	FScriptStepDef AutoStep(const FScriptExpect& E)
	{
		FScriptStepDef S;
		S.Instruction = TEXT("auto");
		S.Judge = EScriptJudge::Auto;
		S.bHasExpect = true;
		S.Expect = E;
		return S;
	}

	FScriptStepDef ManualStep(const FString& Instruction)
	{
		FScriptStepDef S;
		S.Instruction = Instruction;
		S.Judge = EScriptJudge::Manual;
		return S;
	}

	int32 CountEvents(const FScriptRun& Run, const FString& Type)
	{
		int32 N = 0;
		for (const FScriptEvent& E : Run.Events)
		{
			if (E.Type == Type) N++;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptEvaluatorTest,
	"UE.MCP.PIE.Script.Evaluator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptEvaluatorTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptTests;

	TestTrue(TEXT("decreasing: lower holds"), EvaluateScriptCondition(MakeExpect(EScriptExpectOp::Decreasing, TEXT("H")), 100.0, 99.0));
	TestFalse(TEXT("decreasing: equal does not hold"), EvaluateScriptCondition(MakeExpect(EScriptExpectOp::Decreasing, TEXT("H")), 100.0, 100.0));
	TestTrue(TEXT("increasing: higher holds"), EvaluateScriptCondition(MakeExpect(EScriptExpectOp::Increasing, TEXT("H")), 1.0, 2.0));
	TestFalse(TEXT("increasing: lower does not hold"), EvaluateScriptCondition(MakeExpect(EScriptExpectOp::Increasing, TEXT("H")), 1.0, 0.5));

	const FScriptExpect Eq = MakeExpect(EScriptExpectOp::Eq, TEXT("H"), true, 1000.0);
	TestTrue(TEXT("eq: within relative epsilon"), EvaluateScriptCondition(Eq, 0.0, 1000.05));
	TestFalse(TEXT("eq: outside epsilon"), EvaluateScriptCondition(Eq, 0.0, 1000.5));
	const FScriptExpect EqZero = MakeExpect(EScriptExpectOp::Eq, TEXT("H"), true, 0.0);
	TestTrue(TEXT("eq: absolute epsilon near zero"), EvaluateScriptCondition(EqZero, 0.0, 0.00005));
	TestTrue(TEXT("gt"), EvaluateScriptCondition(MakeExpect(EScriptExpectOp::Gt, TEXT("H"), true, 5.0), 0.0, 5.1));
	TestFalse(TEXT("gt: equal"), EvaluateScriptCondition(MakeExpect(EScriptExpectOp::Gt, TEXT("H"), true, 5.0), 0.0, 5.0));
	TestTrue(TEXT("lt"), EvaluateScriptCondition(MakeExpect(EScriptExpectOp::Lt, TEXT("H"), true, 5.0), 0.0, 4.9));

	TestTrue(TEXT("visible: default target true"), EvaluateScriptCondition(MakeExpect(EScriptExpectOp::Visible, TEXT("W")), 0.0, 1.0));
	TestFalse(TEXT("visible: hidden fails default"), EvaluateScriptCondition(MakeExpect(EScriptExpectOp::Visible, TEXT("W")), 0.0, 0.0));
	TestTrue(TEXT("exists: target false means gone"), EvaluateScriptCondition(MakeExpect(EScriptExpectOp::Exists, TEXT("A"), true, 0.0), 0.0, 0.0));

	const FScriptExpect Gt = MakeExpect(EScriptExpectOp::Gt, TEXT("H"), true, 5.0);
	TestTrue(TEXT("frame: holds in window passes"), EvaluateScriptExpectFrame(Gt, true, 0.0, 6.0, 100.0, 500) == EScriptExpectOutcome::Passed);
	TestTrue(TEXT("frame: not yet holds stays pending"), EvaluateScriptExpectFrame(Gt, true, 0.0, 4.0, 100.0, 500) == EScriptExpectOutcome::Pending);
	TestTrue(TEXT("frame: unreadable stays pending"), EvaluateScriptExpectFrame(Gt, false, 0.0, 0.0, 100.0, 500) == EScriptExpectOutcome::Pending);
	TestTrue(TEXT("frame: deadline fails"), EvaluateScriptExpectFrame(Gt, true, 0.0, 4.0, 500.0, 500) == EScriptExpectOutcome::Failed);
	TestTrue(TEXT("frame: late hold still fails"), EvaluateScriptExpectFrame(Gt, true, 0.0, 6.0, 501.0, 500) == EScriptExpectOutcome::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptStepParsingTest,
	"UE.MCP.PIE.Script.StepParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptStepParsingTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptTests;

	auto Parse = [](const FString& Json, FScriptStepDef& Out, FString& Err)
	{
		return ParseScriptStepJson(ParseJsonValue(Json), 0, Out, Err);
	};

	FScriptStepDef Def;
	FString Err;
	TestTrue(TEXT("valid auto step"), Parse(TEXT("{\"instruction\":\"Take damage\",\"judge\":\"auto\",\"expect\":{\"value\":\"Health\",\"op\":\"decreasing\",\"within_ms\":3000}}"), Def, Err));
	TestTrue(TEXT("op parsed"), Def.Expect.Op == EScriptExpectOp::Decreasing);
	TestEqual(TEXT("within parsed"), Def.Expect.WithinMs, 3000);
	TestTrue(TEXT("has expect"), Def.bHasExpect);

	TestTrue(TEXT("boolean target"), Parse(TEXT("{\"instruction\":\"Close menu\",\"expect\":{\"value\":\"WBP_Menu\",\"op\":\"visible\",\"target\":false}}"), Def, Err));
	TestTrue(TEXT("boolean target read"), Def.Expect.bHasTarget && Def.Expect.Target == 0.0);
	TestFalse(TEXT("target false means not visible"), ScriptExpectTargetBool(Def.Expect));

	TestTrue(TEXT("manual step without expect"), Parse(TEXT("{\"instruction\":\"Does it look right?\",\"judge\":\"manual\"}"), Def, Err));
	TestTrue(TEXT("judge manual"), Def.Judge == EScriptJudge::Manual);

	TestFalse(TEXT("unknown op refused"), Parse(TEXT("{\"instruction\":\"x\",\"expect\":{\"value\":\"H\",\"op\":\"between\"}}"), Def, Err));
	TestTrue(TEXT("unknown op error names it"), Err.Contains(TEXT("between")));
	TestFalse(TEXT("missing value refused"), Parse(TEXT("{\"instruction\":\"x\",\"expect\":{\"op\":\"gt\",\"target\":1}}"), Def, Err));
	TestTrue(TEXT("missing value error"), Err.Contains(TEXT("value")));
	TestFalse(TEXT("eq needs target"), Parse(TEXT("{\"instruction\":\"x\",\"expect\":{\"value\":\"H\",\"op\":\"eq\"}}"), Def, Err));
	TestTrue(TEXT("target error"), Err.Contains(TEXT("target")));
	TestFalse(TEXT("relative op takes no target"), Parse(TEXT("{\"instruction\":\"x\",\"expect\":{\"value\":\"H\",\"op\":\"increasing\",\"target\":1}}"), Def, Err));
	TestFalse(TEXT("manual with expect refused"), Parse(TEXT("{\"instruction\":\"x\",\"judge\":\"manual\",\"expect\":{\"value\":\"H\",\"op\":\"gt\",\"target\":1}}"), Def, Err));
	TestFalse(TEXT("bad judge refused"), Parse(TEXT("{\"instruction\":\"x\",\"judge\":\"robot\"}"), Def, Err));
	TestFalse(TEXT("missing instruction refused"), Parse(TEXT("{\"judge\":\"auto\"}"), Def, Err));
	TestFalse(TEXT("string target refused"), Parse(TEXT("{\"instruction\":\"x\",\"expect\":{\"value\":\"H\",\"op\":\"gt\",\"target\":\"5\"}}"), Def, Err));

	TArray<FScriptStepDef> Steps;
	TestFalse(TEXT("empty steps refused"), ParseScriptStepsJson(TArray<TSharedPtr<FJsonValue>>(), Steps, Err));

	// Serialised steps parse back to the same definition.
	FScriptStepDef Original = AutoStep(MakeExpect(EScriptExpectOp::Lt, TEXT("Speed"), true, 12.5, 250));
	FScriptStepDef Back;
	TestTrue(TEXT("round trip parses"), ParseScriptStepJson(MakeShared<FJsonValueObject>(ScriptStepDefToJson(Original)), 0, Back, Err));
	TestTrue(TEXT("round trip op"), Back.Expect.Op == EScriptExpectOp::Lt);
	TestEqual(TEXT("round trip target"), Back.Expect.Target, 12.5);
	TestEqual(TEXT("round trip within"), Back.Expect.WithinMs, 250);

	// Observed names: profile-covered values are not added again.
	TArray<FScriptStepDef> Mixed = {
		AutoStep(MakeExpect(EScriptExpectOp::Decreasing, TEXT("Health"))),
		AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("Ammo"), true, 0.0)),
		AutoStep(MakeExpect(EScriptExpectOp::Visible, TEXT("WBP_HUD"))),
		AutoStep(MakeExpect(EScriptExpectOp::Exists, TEXT("BP_Door"))),
	};
	TArray<FString> Values, Actors, Widgets;
	CollectScriptObservedNames(Mixed, { TEXT("AbilitySystem.Health") }, Values, Actors, Widgets);
	TestEqual(TEXT("one extra value"), Values.Num(), 1);
	TestTrue(TEXT("Ammo added"), Values.Contains(TEXT("Ammo")));
	TestTrue(TEXT("widget collected"), Widgets.Contains(TEXT("WBP_HUD")));
	TestTrue(TEXT("actor collected"), Actors.Contains(TEXT("BP_Door")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptSequencingTest,
	"UE.MCP.PIE.Script.Sequencing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptSequencingTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptTests;

	TMap<FString, double> Live;
	auto Reader = [&Live](const FScriptExpect& E, double& Out)
	{
		if (const double* V = Live.Find(E.Value)) { Out = *V; return true; }
		return false;
	};

	FScriptRun Run;
	Run.DefaultWithinMs = 1000;
	Run.bBlockOnManual = true;
	InitScriptRun(Run, {
		AutoStep(MakeExpect(EScriptExpectOp::Decreasing, TEXT("Health"))),
		ManualStep(TEXT("Hit flash visible?")),
		ManualStep(TEXT("Sound played?")),
		AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("Missing"), true, 1.0, 200)),
	});
	Run.State = EScriptRunState::Running;

	Live.Add(TEXT("Health"), 100.0);
	AdvanceScriptRun(Run, 0.0, 0, Reader);
	TestTrue(TEXT("step 0 running"), Run.Steps[0].Status == EScriptStepStatus::Running);
	TestEqual(TEXT("baseline captured"), Run.Steps[0].Evidence.AtStart, 100.0);

	Live[TEXT("Health")] = 90.0;
	AdvanceScriptRun(Run, 16.0, 1, Reader);
	TestTrue(TEXT("step 0 passed on decrease"), Run.Steps[0].Status == EScriptStepStatus::Passed);
	TestEqual(TEXT("evidence at_end"), Run.Steps[0].Evidence.AtEnd, 90.0);
	TestEqual(TEXT("evidence samples"), Run.Steps[0].Evidence.Samples, 2);
	TestEqual(TEXT("verdict frame"), Run.Steps[0].Evidence.VerdictFrame, static_cast<int64>(1));
	TestTrue(TEXT("manual block awaiting"), Run.State == EScriptRunState::AwaitingVerdict);
	TestEqual(TEXT("both manual steps awaiting"), ScriptAwaitingSteps(Run).Num(), 2);
	TestEqual(TEXT("one awaiting_verdict event for the block"), CountEvents(Run, TEXT("awaiting_verdict")), 1);

	bool bUnchanged = false;
	FString Err;
	TestFalse(TEXT("verdict on auto step refused"), ApplyScriptVerdict(Run, 0, TEXT("pass"), FString(), TEXT("action"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestFalse(TEXT("verdict on pending step refused"), ApplyScriptVerdict(Run, 3, TEXT("pass"), FString(), TEXT("action"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestFalse(TEXT("unknown verdict refused"), ApplyScriptVerdict(Run, 1, TEXT("maybe"), FString(), TEXT("action"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestFalse(TEXT("unknown source refused"), ApplyScriptVerdict(Run, 1, TEXT("pass"), FString(), TEXT("chat"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestFalse(TEXT("out of range refused"), ApplyScriptVerdict(Run, 9, TEXT("pass"), FString(), TEXT("action"), FScriptVerdictEvidence(), bUnchanged, Err));

	TestTrue(TEXT("verdict 1"), ApplyScriptVerdict(Run, 1, TEXT("pass"), TEXT("flash ok"), TEXT("elicitation"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestFalse(TEXT("first verdict changes"), bUnchanged);
	TestEqual(TEXT("verdict source stored"), Run.Steps[1].VerdictSource, FString(TEXT("elicitation")));
	TestTrue(TEXT("repeat verdict"), ApplyScriptVerdict(Run, 1, TEXT("pass"), TEXT("flash ok"), TEXT("elicitation"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestTrue(TEXT("repeat verdict unchanged"), bUnchanged);

	AdvanceScriptRun(Run, 32.0, 2, Reader);
	TestTrue(TEXT("still blocked on step 2"), Run.State == EScriptRunState::AwaitingVerdict);
	TestTrue(TEXT("step 3 not started while blocked"), Run.Steps[3].Status == EScriptStepStatus::Pending);

	TestTrue(TEXT("verdict 2"), ApplyScriptVerdict(Run, 2, TEXT("fail"), TEXT("silent"), TEXT("action"), FScriptVerdictEvidence(), bUnchanged, Err));
	AdvanceScriptRun(Run, 48.0, 3, Reader);
	TestTrue(TEXT("step 2 failed by verdict"), Run.Steps[2].Status == EScriptStepStatus::Failed);
	TestTrue(TEXT("step 3 running"), Run.Steps[3].Status == EScriptStepStatus::Running);

	AdvanceScriptRun(Run, 300.0, 4, Reader);
	TestTrue(TEXT("unreadable value fails at deadline"), Run.Steps[3].Status == EScriptStepStatus::Failed);
	TestFalse(TEXT("evidence not observed"), Run.Steps[3].Evidence.bObserved);
	TestFalse(TEXT("failure carries a reason"), Run.Steps[3].Evidence.Reason.IsEmpty());
	TestTrue(TEXT("run settled"), IsScriptRunSettled(Run));

	const FScriptSummary S = SummarizeScriptRun(Run);
	TestEqual(TEXT("summary passed"), S.Passed, 2);
	TestEqual(TEXT("summary failed"), S.Failed, 2);
	TestEqual(TEXT("summary pending"), S.Pending, 0);

	int32 LastSeq = 0;
	bool bMonotonic = true;
	for (const FScriptEvent& E : Run.Events)
	{
		bMonotonic &= E.Seq == LastSeq + 1;
		LastSeq = E.Seq;
	}
	TestTrue(TEXT("event seq is 1-based and monotonic"), bMonotonic);

	// Non-blocking manual: the run moves on and waits for the verdict at the end.
	FScriptRun Loose;
	Loose.bBlockOnManual = false;
	InitScriptRun(Loose, { ManualStep(TEXT("Looks ok?")), AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("Health"), true, 1.0)) });
	Loose.State = EScriptRunState::Running;
	AdvanceScriptRun(Loose, 0.0, 0, Reader);
	TestTrue(TEXT("non-blocking: auto step passed"), Loose.Steps[1].Status == EScriptStepStatus::Passed);
	TestTrue(TEXT("non-blocking: run awaits remaining verdict"), Loose.State == EScriptRunState::AwaitingVerdict);
	TestFalse(TEXT("non-blocking: not settled"), IsScriptRunSettled(Loose));
	TestTrue(TEXT("non-blocking: skip verdict"), ApplyScriptVerdict(Loose, 0, TEXT("skip"), FString(), FString(), FScriptVerdictEvidence(), bUnchanged, Err));
	TestTrue(TEXT("non-blocking: skipped"), Loose.Steps[0].Status == EScriptStepStatus::Skipped);
	TestEqual(TEXT("default source is action"), Loose.Steps[0].VerdictSource, FString(TEXT("action")));
	TestTrue(TEXT("non-blocking: settled"), IsScriptRunSettled(Loose));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptResultsRoundTripTest,
	"UE.MCP.PIE.Script.ResultsRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptResultsRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptTests;

	TMap<FString, double> Live;
	Live.Add(TEXT("Speed"), 0.0);
	auto Reader = [&Live](const FScriptExpect& E, double& Out)
	{
		if (const double* V = Live.Find(E.Value)) { Out = *V; return true; }
		return false;
	};

	FScriptRun Run;
	Run.RunId = TEXT("script_test_20260101_000000");
	Run.ScriptPath = TEXT("/Game/PIEStudio/Scripts/Test");
	Run.ScriptName = TEXT("Test");
	Run.RecordingId = Run.RunId;
	Run.DefaultWithinMs = 2000;
	InitScriptRun(Run, {
		AutoStep(MakeExpect(EScriptExpectOp::Increasing, TEXT("Speed"))),
		ManualStep(TEXT("Smooth?")),
		AutoStep(MakeExpect(EScriptExpectOp::Exists, TEXT("BP_Door"), true, 0.0)),
	});
	Run.State = EScriptRunState::Running;
	AdvanceScriptRun(Run, 0.0, 0, Reader);
	Live[TEXT("Speed")] = 300.0;
	AdvanceScriptRun(Run, 500.0, 30, Reader);
	bool bUnchanged = false;
	FString Err;
	ApplyScriptVerdict(Run, 1, TEXT("pass"), TEXT("yes"), TEXT("action"), FScriptVerdictEvidence(), bUnchanged, Err);
	AdvanceScriptRun(Run, 600.0, 36, Reader);
	Run.State = EScriptRunState::Finished;

	const TSharedRef<FJsonObject> Json = ScriptRunToResultsJson(Run);
	FScriptRun Back;
	TestTrue(TEXT("results parse back"), ScriptRunFromResultsJson(Json, Back, Err));
	TestEqual(TEXT("run id"), Back.RunId, Run.RunId);
	TestEqual(TEXT("recording id"), Back.RecordingId, Run.RecordingId);
	TestTrue(TEXT("state"), Back.State == EScriptRunState::Finished);
	TestEqual(TEXT("step count"), Back.Steps.Num(), 3);
	if (Back.Steps.Num() == 3)
	{
		TestTrue(TEXT("step 0 status"), Back.Steps[0].Status == EScriptStepStatus::Passed);
		TestEqual(TEXT("step 0 started_at_ms"), Back.Steps[0].StartedAtMs, 0.0);
		TestEqual(TEXT("step 0 at_start"), Back.Steps[0].Evidence.AtStart, 0.0);
		TestEqual(TEXT("step 0 at_end"), Back.Steps[0].Evidence.AtEnd, 300.0);
		TestEqual(TEXT("step 1 note"), Back.Steps[1].Note, FString(TEXT("yes")));
		TestEqual(TEXT("step 1 source"), Back.Steps[1].VerdictSource, FString(TEXT("action")));
		TestTrue(TEXT("step 2 op"), Back.Steps[2].Def.Expect.Op == EScriptExpectOp::Exists);
		TestFalse(TEXT("step 2 boolean target false"), ScriptExpectTargetBool(Back.Steps[2].Def.Expect));
		TestTrue(TEXT("step 2 status"), Back.Steps[2].Status == Run.Steps[2].Status);
	}
	const TSharedPtr<FJsonObject>* Summary = nullptr;
	TestTrue(TEXT("summary present"), Json->TryGetObjectField(TEXT("summary"), Summary) && Summary);

	TSharedRef<FJsonObject> Bad = MakeShared<FJsonObject>();
	Bad->SetNumberField(TEXT("version"), 99);
	TestFalse(TEXT("future version refused"), ScriptRunFromResultsJson(Bad, Back, Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptReplayModelTest,
	"UE.MCP.PIE.Script.ReplayWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptReplayModelTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptTests;

	FScriptRun Source;
	Source.RunId = TEXT("script_src");
	Source.DefaultWithinMs = 1000;
	InitScriptRun(Source, {
		AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("Score"), true, 10.0)),
		ManualStep(TEXT("Fun?")),
		AutoStep(MakeExpect(EScriptExpectOp::Lt, TEXT("Score"), true, 5.0)),
		AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("Score"), true, 0.0)),
	});
	Source.Steps[0].Status = EScriptStepStatus::Passed;
	Source.Steps[0].StartedAtMs = 100.0;
	Source.Steps[1].Status = EScriptStepStatus::Passed;
	Source.Steps[1].Note = TEXT("fun");
	Source.Steps[2].Status = EScriptStepStatus::Failed;
	Source.Steps[2].StartedAtMs = 2000.0;

	FScriptRun Replay;
	PrepareScriptReplayRun(Source, 500.0, Replay);
	TestTrue(TEXT("replay flag"), Replay.bReplay);
	TestEqual(TEXT("replay_of"), Replay.ReplayOf, Source.RunId);
	TestEqual(TEXT("window start offset by settle"), Replay.Steps[0].WindowStartMs, 600.0);
	TestEqual(TEXT("window end"), Replay.Steps[0].WindowEndMs, 1600.0);
	TestTrue(TEXT("manual carried"), Replay.Steps[1].Status == EScriptStepStatus::Passed);
	TestEqual(TEXT("manual source"), Replay.Steps[1].VerdictSource, FString(TEXT("source_run")));
	TestTrue(TEXT("unreached step skipped"), Replay.Steps[3].Status == EScriptStepStatus::Skipped);

	double Score = 0.0;
	auto Reader = [&Score](const FScriptExpect&, double& Out) { Out = Score; return true; };
	Replay.State = EScriptRunState::Running;

	AdvanceScriptReplay(Replay, 300.0, 1, Reader);
	TestTrue(TEXT("before window pending"), Replay.Steps[0].Status == EScriptStepStatus::Pending);
	AdvanceScriptReplay(Replay, 700.0, 2, Reader);
	TestTrue(TEXT("in window running"), Replay.Steps[0].Status == EScriptStepStatus::Running);
	AdvanceScriptReplay(Replay, 1700.0, 3, Reader);
	TestTrue(TEXT("window missed fails"), Replay.Steps[0].Status == EScriptStepStatus::Failed);
	TestTrue(TEXT("passed in source, failed now: regression"), Replay.Steps[0].bRegression);

	AdvanceScriptReplay(Replay, 2600.0, 4, Reader);
	TestTrue(TEXT("step 2 passes in its own window"), Replay.Steps[2].Status == EScriptStepStatus::Passed);
	TestFalse(TEXT("fixed in replay is no regression"), Replay.Steps[2].bRegression);

	TestTrue(TEXT("replay settled"), IsScriptRunSettled(Replay));
	const FScriptSummary S = SummarizeScriptRun(Replay);
	TestEqual(TEXT("one regression"), S.Regressions, 1);

	const TSharedRef<FJsonObject> Json = ScriptRunToResultsJson(Replay);
	FString ReplayOf;
	TestTrue(TEXT("results carry replay_of"), Json->TryGetStringField(TEXT("replay_of"), ReplayOf) && ReplayOf == Source.RunId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptAgentVerdictTest,
	"UE.MCP.PIE.Script.AgentVerdict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptAgentVerdictTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptTests;

	// agent_judgeable parses on manual steps, defaults true, and is refused on auto steps.
	FScriptStepDef Def;
	FString Err;
	TestTrue(TEXT("agent_judgeable false parses"), ParseScriptStepJson(
		ParseJsonValue(TEXT("{\"instruction\":\"Feels right?\",\"judge\":\"manual\",\"agent_judgeable\":false}")), 0, Def, Err));
	TestFalse(TEXT("agent_judgeable stored"), Def.bAgentJudgeable);
	bool bWritten = true;
	TestTrue(TEXT("agent_judgeable written back"), ScriptStepDefToJson(Def)->TryGetBoolField(TEXT("agent_judgeable"), bWritten));
	TestFalse(TEXT("agent_judgeable written false"), bWritten);
	TestTrue(TEXT("manual default parses"), ParseScriptStepJson(ParseJsonValue(TEXT("{\"instruction\":\"Ok?\",\"judge\":\"manual\"}")), 0, Def, Err));
	TestTrue(TEXT("agent_judgeable defaults true"), Def.bAgentJudgeable);
	TestFalse(TEXT("agent_judgeable false on auto refused"), ParseScriptStepJson(
		ParseJsonValue(TEXT("{\"instruction\":\"x\",\"agent_judgeable\":false}")), 0, Def, Err));
	TestFalse(TEXT("agent_judgeable non-boolean refused"), ParseScriptStepJson(
		ParseJsonValue(TEXT("{\"instruction\":\"x\",\"judge\":\"manual\",\"agent_judgeable\":\"no\"}")), 0, Def, Err));

	FScriptStepDef HumanOnly = ManualStep(TEXT("Feels right?"));
	HumanOnly.bAgentJudgeable = false;
	FScriptRun Run;
	Run.RunId = TEXT("script_agent_20260101_000000");
	Run.bBlockOnManual = false;
	InitScriptRun(Run, { ManualStep(TEXT("Door is open?")), HumanOnly, ManualStep(TEXT("Lights on?")) });
	Run.State = EScriptRunState::Running;
	auto Reader = [](const FScriptExpect&, double& Out) { Out = 0.0; return true; };
	AdvanceScriptRun(Run, 0.0, 0, Reader);
	TestEqual(TEXT("all three awaiting"), ScriptAwaitingSteps(Run).Num(), 3);

	FScriptVerdictEvidence Evidence;
	bool bUnchanged = false;
	TestFalse(TEXT("agent verdict without evidence refused"), ApplyScriptVerdict(Run, 0, TEXT("pass"), FString(), TEXT("agent"), Evidence, bUnchanged, Err));
	Evidence.Note = TEXT("   ");
	TestFalse(TEXT("agent verdict with a blank note refused"), ApplyScriptVerdict(Run, 0, TEXT("pass"), FString(), TEXT("agent"), Evidence, bUnchanged, Err));
	Evidence.Note = TEXT("Door.OpenAngle read 85 in the capture");
	Evidence.Captures = { TEXT("C:/Shots/door.png") };
	TestFalse(TEXT("agent verdict on a human-only step refused"), ApplyScriptVerdict(Run, 1, TEXT("pass"), FString(), TEXT("agent"), Evidence, bUnchanged, Err));
	TestTrue(TEXT("refusal names agent_judgeable"), Err.Contains(TEXT("agent_judgeable")));
	TestTrue(TEXT("human-only step still awaiting"), Run.Steps[1].Status == EScriptStepStatus::AwaitingVerdict);

	TestTrue(TEXT("agent verdict with evidence"), ApplyScriptVerdict(Run, 0, TEXT("pass"), FString(), TEXT("agent"), Evidence, bUnchanged, Err));
	TestEqual(TEXT("agent source recorded"), Run.Steps[0].VerdictSource, FString(TEXT("agent")));
	TestEqual(TEXT("evidence note recorded"), Run.Steps[0].Evidence.Note, Evidence.Note);
	TestEqual(TEXT("evidence captures recorded"), Run.Steps[0].Evidence.Captures.Num(), 1);
	TestTrue(TEXT("repeat agent verdict unchanged"), ApplyScriptVerdict(Run, 0, TEXT("pass"), FString(), TEXT("agent"), Evidence, bUnchanged, Err) && bUnchanged);

	TestTrue(TEXT("human verdict on the human-only step"), ApplyScriptVerdict(Run, 1, TEXT("fail"), TEXT("jarring"), TEXT("human"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestEqual(TEXT("human source recorded as action"), Run.Steps[1].VerdictSource, FString(TEXT("action")));
	TestTrue(TEXT("elicitation verdict"), ApplyScriptVerdict(Run, 2, TEXT("skip"), FString(), TEXT("elicitation"), FScriptVerdictEvidence(), bUnchanged, Err));

	const FScriptSummary S = SummarizeScriptRun(Run);
	TestEqual(TEXT("one agent verdict"), S.AgentVerdicts, 1);
	TestEqual(TEXT("two human verdicts"), S.HumanVerdicts, 2);

	// The verdict event carries the source and the evidence.
	const FScriptEvent* AgentEvent = nullptr;
	for (const FScriptEvent& E : Run.Events)
	{
		if (E.Type == TEXT("verdict") && E.Step == 0) AgentEvent = &E;
	}
	TestNotNull(TEXT("agent verdict event"), AgentEvent);
	if (AgentEvent && AgentEvent->Data.IsValid())
	{
		FString Source;
		TestTrue(TEXT("event verdict_source agent"), AgentEvent->Data->TryGetStringField(TEXT("verdict_source"), Source) && Source == TEXT("agent"));
		const TSharedPtr<FJsonObject>* Ev = nullptr;
		TestTrue(TEXT("event carries evidence"), AgentEvent->Data->TryGetObjectField(TEXT("evidence"), Ev) && Ev);
	}

	// A human verdict replacing the agent's clears the agent evidence.
	TestTrue(TEXT("human replaces agent"), ApplyScriptVerdict(Run, 0, TEXT("fail"), TEXT("it is ajar"), TEXT("human"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestTrue(TEXT("evidence cleared"), Run.Steps[0].Evidence.Note.IsEmpty() && Run.Steps[0].Evidence.Captures.Num() == 0);
	TestEqual(TEXT("counts follow the replacement"), SummarizeScriptRun(Run).AgentVerdicts, 0);
	TestTrue(TEXT("agent again"), ApplyScriptVerdict(Run, 0, TEXT("pass"), FString(), TEXT("agent"), Evidence, bUnchanged, Err));

	// results.json round trip keeps the source, the evidence, the flag and the counts.
	Run.State = EScriptRunState::Finished;
	const TSharedRef<FJsonObject> Json = ScriptRunToResultsJson(Run);
	const TSharedPtr<FJsonObject>* Summary = nullptr;
	double AgentCount = -1.0, HumanCount = -1.0;
	TestTrue(TEXT("summary present"), Json->TryGetObjectField(TEXT("summary"), Summary) && Summary);
	if (Summary)
	{
		TestTrue(TEXT("summary agent_verdicts"), (*Summary)->TryGetNumberField(TEXT("agent_verdicts"), AgentCount) && AgentCount == 1.0);
		TestTrue(TEXT("summary human_verdicts"), (*Summary)->TryGetNumberField(TEXT("human_verdicts"), HumanCount) && HumanCount == 2.0);
	}
	FScriptRun Back;
	TestTrue(TEXT("results parse back"), ScriptRunFromResultsJson(Json, Back, Err));
	if (Back.Steps.Num() == 3)
	{
		TestEqual(TEXT("source round trips"), Back.Steps[0].VerdictSource, FString(TEXT("agent")));
		TestEqual(TEXT("note round trips"), Back.Steps[0].Evidence.Note, Evidence.Note);
		TestEqual(TEXT("captures round trip"), Back.Steps[0].Evidence.Captures, Evidence.Captures);
		TestFalse(TEXT("agent_judgeable round trips"), Back.Steps[1].Def.bAgentJudgeable);
		TestTrue(TEXT("agent_judgeable default round trips"), Back.Steps[2].Def.bAgentJudgeable);
	}

	// A replay keeps the agent's evidence on the carried verdict.
	FScriptRun Replay;
	PrepareScriptReplayRun(Back, 0.0, Replay);
	TestEqual(TEXT("replay keeps evidence"), Replay.Steps[0].Evidence.Note, Evidence.Note);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptManualModeTest,
	"UE.MCP.PIE.Script.ManualMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptManualModeTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptTests;

	FScriptRun Run;
	Run.RunId = TEXT("script_manual_test");
	Run.bManualMode = true;
	Run.bBlockOnManual = false;
	InitScriptRun(Run, {
		AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("Health"), true, 0.0)),
		ManualStep(TEXT("The door looks right")),
		AutoStep(MakeExpect(EScriptExpectOp::Decreasing, TEXT("Ammo"))),
		ManualStep(TEXT("The sound plays")),
	});
	AddScriptEvent(Run, TEXT("run_started"), -1, nullptr);
	StartScriptManualSteps(Run);

	TestTrue(TEXT("awaiting at once"), Run.State == EScriptRunState::AwaitingVerdict);
	TestTrue(TEXT("both manual steps await"), ScriptAwaitingSteps(Run) == TArray<int32>({ 1, 3 }));
	TestTrue(TEXT("auto steps stay pending"), Run.Steps[0].Status == EScriptStepStatus::Pending && Run.Steps[2].Status == EScriptStepStatus::Pending);
	FScriptSummary S = SummarizeScriptRun(Run);
	TestEqual(TEXT("auto steps unchecked"), S.Unchecked, 2);
	TestEqual(TEXT("unchecked are not pending"), S.Pending, 2);
	TestEqual(TEXT("awaiting"), S.Awaiting, 2);
	TestEqual(TEXT("one non-blocking announcement"), CountEvents(Run, TEXT("awaiting_verdict")), 1);
	bool bAllCarryMode = true;
	for (const FScriptEvent& E : Run.Events)
	{
		FString Mode;
		bAllCarryMode &= E.Data.IsValid() && E.Data->TryGetStringField(TEXT("mode"), Mode) && Mode == TEXT("manual");
	}
	TestTrue(TEXT("every event carries mode"), bAllCarryMode);

	// Verdicts in any order: the last step first.
	bool bUnchanged = false;
	FString Err;
	TestTrue(TEXT("last step first"), ApplyScriptVerdict(Run, 3, TEXT("fail"), TEXT("silent"), TEXT("human"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestFalse(TEXT("not settled yet"), AreScriptManualStepsSettled(Run));
	TestFalse(TEXT("an auto step takes no verdict"), ApplyScriptVerdict(Run, 0, TEXT("pass"), FString(), TEXT("human"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestTrue(TEXT("then the first"), ApplyScriptVerdict(Run, 1, TEXT("pass"), FString(), TEXT("human"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestTrue(TEXT("settled once every manual step has a verdict"), AreScriptManualStepsSettled(Run));
	S = SummarizeScriptRun(Run);
	TestEqual(TEXT("passed"), S.Passed, 1);
	TestEqual(TEXT("failed"), S.Failed, 1);
	TestEqual(TEXT("still unchecked"), S.Unchecked, 2);
	TestEqual(TEXT("nothing pending"), S.Pending, 0);

	// results.json says manual, and reads back as manual.
	Run.State = EScriptRunState::Finished;
	const TSharedRef<FJsonObject> Json = ScriptRunToResultsJson(Run);
	TestEqual(TEXT("results mode"), Json->GetStringField(TEXT("mode")), FString(TEXT("manual")));
	TestEqual(TEXT("summary unchecked"), Json->GetObjectField(TEXT("summary"))->GetNumberField(TEXT("unchecked")), 2.0);
	FScriptRun Back;
	TestTrue(TEXT("parses back"), ScriptRunFromResultsJson(Json, Back, Err));
	TestTrue(TEXT("mode round trips"), Back.bManualMode);

	// A full run of the same steps starts clean.
	FScriptRun Full;
	InitScriptRun(Full, { ManualStep(TEXT("The door looks right")) });
	TestTrue(TEXT("full run has no verdict"), Full.Steps[0].Status == EScriptStepStatus::Pending && Full.Steps[0].VerdictSource.IsEmpty());
	TestEqual(TEXT("full run mode"), ScriptRunModeToString(Full), FString(TEXT("full")));
	return true;
}

namespace MCPPIEScriptTests
{
	// Stands in for the editor's driver: an act takes TicksToRun ticks after BeginAct, then ends as told.
	class FFakeStepDriver : public IScriptStepDriver
	{
	public:
		int32 TicksToRun = 2;
		bool bFailBegin = false;
		bool bFailAct = false;
		TArray<int32> Begun;
		int32 Cancelled = 0;

		virtual bool BeginAct(int32 Index, const FScriptStepDef& Def, double NowMs, FScriptActReport& Report) override
		{
			Begun.Add(Index);
			Remaining = TicksToRun;
			Report.Arranged = Def.Arrange.Num();
			if (bFailBegin)
			{
				Report.Reason = TEXT("arrange[0] set: actor 'Nope' not found");
				return false;
			}
			return true;
		}

		virtual EScriptActOutcome TickAct(double NowMs, FScriptActReport& Report) override
		{
			if (Remaining-- > 0) return EScriptActOutcome::Running;
			TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("action"), TEXT("place"));
			A->SetBoolField(TEXT("ok"), !bFailAct);
			Report.Actions.Add(MakeShared<FJsonValueObject>(A));
			if (bFailAct)
			{
				Report.Reason = TEXT("act[0] could not run: place: client 0 has no pawn");
				return EScriptActOutcome::Failed;
			}
			return EScriptActOutcome::Done;
		}

		virtual void Cancel() override { ++Cancelled; }

	private:
		int32 Remaining = 0;
	};

	FScriptStepDef WithAct(FScriptStepDef S)
	{
		S.Arrange = ParseJsonValue(TEXT("[{\"kind\":\"set\",\"target\":\"Door\",\"path\":\"OpenAngle\",\"value\":0}]"))->AsArray();
		S.Act = ParseJsonValue(TEXT("[{\"place\":{\"at\":\"Door\",\"offset\":[-150,0,0]}},{\"press\":{\"action\":\"/Game/Input/IA_Interact.IA_Interact\"}}]"))->AsArray();
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptArrangeActParseTest,
	"UE.MCP.PIE.Script.ArrangeAct.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptArrangeActParseTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptTests;

	FScriptStepDef Def;
	FString Err, Field;
	TestTrue(TEXT("door step parses"), ParseScriptStepJson(ParseJsonValue(TEXT(
		"{\"instruction\":\"Open the door\",\"arrange\":[{\"kind\":\"set\",\"target\":\"Door\",\"path\":\"OpenAngle\",\"value\":0}],"
		"\"act\":[{\"place\":{\"at\":\"Door\",\"offset\":[-150,0,0]}},{\"press\":{\"action\":\"/Game/Input/IA_Interact.IA_Interact\"}},{\"wait_ms\":100}],"
		"\"expect\":{\"value\":\"Door.OpenAngle\",\"op\":\"gt\",\"target\":80}}")), 0, Def, Err, &Field));
	TestEqual(TEXT("arrange kept"), Def.Arrange.Num(), 1);
	TestEqual(TEXT("act kept"), Def.Act.Num(), 3);
	TestTrue(TEXT("expect kept"), Def.bHasExpect);

	const TSharedRef<FJsonObject> Back = ScriptStepDefToJson(Def);
	FScriptStepDef Again;
	TestTrue(TEXT("written step parses again"), ParseScriptStepJson(MakeShared<FJsonValueObject>(Back), 0, Again, Err));
	TestEqual(TEXT("round trip arrange"), Again.Arrange.Num(), 1);
	TestEqual(TEXT("round trip act"), Again.Act.Num(), 3);

	TestFalse(TEXT("unknown act verb refused"), ParseScriptStepJson(ParseJsonValue(TEXT(
		"{\"instruction\":\"x\",\"act\":[{\"teleport\":{\"at\":\"Door\"}}]}")), 2, Def, Err, &Field));
	TestEqual(TEXT("unknown verb names act"), Field, FString(TEXT("act")));
	TestTrue(TEXT("unknown verb message says where"), Err.StartsWith(TEXT("steps[2].act[0]:")));

	TestFalse(TEXT("arrange set without target refused"), ParseScriptStepJson(ParseJsonValue(TEXT(
		"{\"instruction\":\"x\",\"arrange\":[{\"kind\":\"set\",\"path\":\"A\",\"value\":1}]}")), 1, Def, Err, &Field));
	TestEqual(TEXT("arrange problem names arrange"), Field, FString(TEXT("arrange")));
	TestTrue(TEXT("arrange message uses the scenario's rule"), Err.Contains(TEXT("steps[1].arrange[0] set needs 'target'")));

	TestFalse(TEXT("act must be an array"), ParseScriptStepJson(ParseJsonValue(TEXT(
		"{\"instruction\":\"x\",\"act\":{\"wait_ms\":5}}")), 0, Def, Err, &Field));
	TestEqual(TEXT("act shape names act"), Field, FString(TEXT("act")));

	TestTrue(TEXT("a manual step may carry act"), ParseScriptStepJson(ParseJsonValue(TEXT(
		"{\"instruction\":\"Looks open?\",\"judge\":\"manual\",\"act\":[{\"console\":\"stat fps\"}]}")), 0, Def, Err));
	TestTrue(TEXT("resolve_in_pie applies to a step with act"), ParseScriptStepJson(ParseJsonValue(TEXT(
		"{\"instruction\":\"x\",\"resolve_in_pie\":true,\"act\":[{\"place\":{\"at\":\"BP_Spawned_C_0\"}}]}")), 0, Def, Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptArrangeActRunTest,
	"UE.MCP.PIE.Script.ArrangeAct.Run",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptArrangeActRunTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptTests;

	double Angle = 0.0;
	auto Reader = [&Angle](const FScriptExpect&, double& Out) { Out = Angle; return true; };

	// The window opens when act ends: within_ms 50 counts from 100 ms, not from the step's start at 0.
	FScriptRun Run;
	Run.RunId = TEXT("script_act");
	Run.DefaultWithinMs = 1000;
	InitScriptRun(Run, {
		WithAct(AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("Door.OpenAngle"), true, 80.0, 50))),
		AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("Door.OpenAngle"), true, 80.0)),
	});
	Run.State = EScriptRunState::Running;
	FFakeStepDriver Driver;
	AdvanceScriptRun(Run, 0.0, 0, Reader, &Driver);
	TestEqual(TEXT("arrange and act began once"), Driver.Begun.Num(), 1);
	TestTrue(TEXT("acting step runs"), Run.Steps[0].Status == EScriptStepStatus::Running);
	TestTrue(TEXT("act running"), Run.Steps[0].Act.State == EScriptActState::Running);
	TestEqual(TEXT("expect not sampled while acting"), Run.Steps[0].Evidence.Samples, 0);
	AdvanceScriptRun(Run, 50.0, 1, Reader, &Driver);
	TestTrue(TEXT("act still running"), Run.Steps[0].Act.State == EScriptActState::Running);
	TestTrue(TEXT("next step waits for the act"), Run.Steps[1].Status == EScriptStepStatus::Pending);
	AdvanceScriptRun(Run, 100.0, 2, Reader, &Driver);
	TestTrue(TEXT("act done"), Run.Steps[0].Act.State == EScriptActState::Done);
	TestEqual(TEXT("act ended at 100 ms"), Run.Steps[0].Act.EndedAtMs, 100.0);
	TestEqual(TEXT("one step_acted event"), CountEvents(Run, TEXT("step_acted")), 1);
	Angle = 90.0;
	AdvanceScriptRun(Run, 140.0, 3, Reader, &Driver);
	TestTrue(TEXT("passes 40 ms into its window"), Run.Steps[0].Status == EScriptStepStatus::Passed);
	TestEqual(TEXT("verdict_at_ms counts from the end of act"), Run.Steps[0].Evidence.VerdictAtMs, 40.0);

	const TSharedRef<FJsonObject> StepJson = ScriptStepRunToJson(Run.Steps[0], 0);
	const TSharedPtr<FJsonObject>* ActResult = nullptr;
	TestTrue(TEXT("results carry act_result"), StepJson->TryGetObjectField(TEXT("act_result"), ActResult) && ActResult);
	if (ActResult)
	{
		double Duration = -1.0;
		TestTrue(TEXT("act_result has its duration"), (*ActResult)->TryGetNumberField(TEXT("duration_ms"), Duration) && Duration == 100.0);
		TestEqual(TEXT("act_result status"), (*ActResult)->GetStringField(TEXT("status")), FString(TEXT("done")));
		TestEqual(TEXT("act_result lists the actions"), (*ActResult)->GetArrayField(TEXT("actions")).Num(), 1);
		TestEqual(TEXT("act_result counts arrange"), static_cast<int32>((*ActResult)->GetNumberField(TEXT("arranged"))), 1);
	}

	FScriptRun Back;
	FString Err;
	TestTrue(TEXT("results.json reads back"), ScriptRunFromResultsJson(ScriptRunToResultsJson(Run), Back, Err));
	TestEqual(TEXT("act end read back"), Back.Steps[0].Act.EndedAtMs, 100.0);
	TestEqual(TEXT("act steps read back"), Back.Steps[0].Def.Act.Num(), 2);

	// A replay does not act again, and its window opens where the source act ended.
	Back.Steps[0].Status = EScriptStepStatus::Passed;
	FScriptRun Replay;
	PrepareScriptReplayRun(Back, 500.0, Replay);
	TestEqual(TEXT("replay window from the act's end"), Replay.Steps[0].WindowStartMs, 600.0);
	TestTrue(TEXT("replay skips act"), Replay.Steps[0].Act.State == EScriptActState::Skipped);

	// An act that fails fails its step with the act's reason, and the run moves on.
	FScriptRun Failing;
	InitScriptRun(Failing, { WithAct(AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("A"), true, 1.0))), AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("A"), true, 1.0)) });
	Failing.State = EScriptRunState::Running;
	FFakeStepDriver Broken;
	Broken.TicksToRun = 0;
	Broken.bFailAct = true;
	AdvanceScriptRun(Failing, 0.0, 0, Reader, &Broken);
	TestTrue(TEXT("failed act fails the step"), Failing.Steps[0].Status == EScriptStepStatus::Failed);
	TestTrue(TEXT("with the act's reason"), Failing.Steps[0].Evidence.Reason.Contains(TEXT("no pawn")));
	TestTrue(TEXT("the run moves on"), Failing.Steps[1].Status == EScriptStepStatus::Passed);

	FScriptRun BadArrange;
	InitScriptRun(BadArrange, { WithAct(AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("A"), true, 1.0))) });
	BadArrange.State = EScriptRunState::Running;
	FFakeStepDriver NoActor;
	NoActor.bFailBegin = true;
	AdvanceScriptRun(BadArrange, 0.0, 0, Reader, &NoActor);
	TestTrue(TEXT("arrange that cannot apply fails the step"), BadArrange.Steps[0].Status == EScriptStepStatus::Failed);
	TestTrue(TEXT("act report failed"), BadArrange.Steps[0].Act.State == EScriptActState::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptArrangeActManualTest,
	"UE.MCP.PIE.Script.ArrangeAct.Manual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptArrangeActManualTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptTests;

	double Value = 0.0;
	auto Reader = [&Value](const FScriptExpect&, double& Out) { Out = Value; return true; };

	// A manual step's act sets the scene before its verdict is asked for.
	FScriptRun Run;
	Run.bBlockOnManual = true;
	InitScriptRun(Run, { WithAct(ManualStep(TEXT("The door swings smoothly?"))), ManualStep(TEXT("No pop?")) });
	Run.State = EScriptRunState::Running;
	FFakeStepDriver Driver;
	Driver.TicksToRun = 1;
	AdvanceScriptRun(Run, 0.0, 0, Reader, &Driver);
	TestTrue(TEXT("acting manual step runs"), Run.Steps[0].Status == EScriptStepStatus::Running);
	TestTrue(TEXT("the next manual step waits"), Run.Steps[1].Status == EScriptStepStatus::Pending);
	bool bUnchanged = false;
	FString Err;
	TestFalse(TEXT("no verdict while acting"), ApplyScriptVerdict(Run, 0, TEXT("pass"), FString(), TEXT("action"), FScriptVerdictEvidence(), bUnchanged, Err));
	TestTrue(TEXT("the refusal says why"), Err.Contains(TEXT("arrange and act")));
	AdvanceScriptRun(Run, 16.0, 1, Reader, &Driver);
	TestTrue(TEXT("awaits its verdict once act ended"), Run.Steps[0].Status == EScriptStepStatus::AwaitingVerdict);
	TestTrue(TEXT("the block awaits both"), Run.State == EScriptRunState::AwaitingVerdict);
	TestEqual(TEXT("both awaiting"), ScriptAwaitingSteps(Run).Num(), 2);
	TestTrue(TEXT("verdict after act"), ApplyScriptVerdict(Run, 0, TEXT("pass"), FString(), TEXT("action"), FScriptVerdictEvidence(), bUnchanged, Err));

	// A manual run has no PIE: arrange and act are skipped, and the step says so.
	FScriptRun Manual;
	Manual.bManualMode = true;
	InitScriptRun(Manual, { WithAct(ManualStep(TEXT("Looks right?"))), WithAct(AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("A"), true, 1.0))) });
	StartScriptManualSteps(Manual);
	TestTrue(TEXT("manual run skips act"), Manual.Steps[0].Act.State == EScriptActState::Skipped);
	TestTrue(TEXT("and says why"), Manual.Steps[0].Act.Reason.Contains(TEXT("no PIE")));
	TestTrue(TEXT("the manual step still awaits"), Manual.Steps[0].Status == EScriptStepStatus::AwaitingVerdict);
	const TSharedPtr<FJsonObject>* ActResult = nullptr;
	TestTrue(TEXT("the step's result says it was skipped"), ScriptStepRunToJson(Manual.Steps[0], 0)->TryGetObjectField(TEXT("act_result"), ActResult)
		&& ActResult && (*ActResult)->GetStringField(TEXT("status")) == TEXT("skipped"));

	// Ending a run mid act fails the act and the step.
	FScriptRun Stopped;
	InitScriptRun(Stopped, { WithAct(AutoStep(MakeExpect(EScriptExpectOp::Gt, TEXT("A"), true, 1.0))) });
	Stopped.State = EScriptRunState::Running;
	FFakeStepDriver Slow;
	Slow.TicksToRun = 10;
	AdvanceScriptRun(Stopped, 0.0, 0, Reader, &Slow);
	SettleUnfinishedScriptSteps(Stopped, TEXT("stopped"));
	TestTrue(TEXT("act failed on stop"), Stopped.Steps[0].Act.State == EScriptActState::Failed);
	TestTrue(TEXT("step failed on stop"), Stopped.Steps[0].Status == EScriptStepStatus::Failed);
	return true;
}

#endif
