// Automation coverage for playtest clips (#2097): the JSON and text forms, the structural checks, note-level
// edits, track evaluation, comping a take and judging check notes. All plain data, no PIE.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEClip.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MCPPIEClipTests
{
	using namespace MCPPIE;

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Out);
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> ParseArray(const FString& Text)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Out);
		return Out;
	}

	const TCHAR* const SampleText = TEXT(
		"clip rate=60 length=600 grid=frame fixed_timestep=true seed=7 hold_timeout=300\n"
		"map /Game/Maps/L_Arena\n"
		"description \"The door opens\"\n"
		"region r1 0 300 \"Walk to the door\"\n"
		"region r2 300 600 \"Open it\"\n"
		"track /Game/Input/IA_Jump bool\n"
		"  note n1 30 10\n"
		"  note - 90 5\n"
		"track /Game/Input/IA_Move.IA_Move axis2d\n"
		"  key k1 0 0,1 constant\n"
		"  key k2 120 0,0\n"
		"  note n3 200 20 1,0\n"
		"event e1 0 {\"console\":\"stat fps\"}\n"
		"check c1 0 120 by_end pos_x increasing 100 name=\"Moves forward\"\n"
		"check c2 300 120 hold Door.OpenAngle gt 80\n"
		"check c3 400 60 throughout speed2d lt 700\n"
		"check c4 500 0 manual \"Did it open smoothly?\" agent_judgeable=false\n"
		"recorded c2 330 5500 81.5\n");

	FClip SampleClip()
	{
		FClip Clip;
		TArray<FClipIssue> Issues;
		ClipFromText(SampleText, Clip, Issues);
		return Clip;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipFormatTest, "UE.MCP.PIE.Clip.Format", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipFormatTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipTests;
	FClip Clip;
	TArray<FClipIssue> Issues;
	TestTrue(TEXT("the text form parses"), ClipFromText(SampleText, Clip, Issues));
	TestEqual(TEXT("no issues"), Issues.Num(), 0);
	TestEqual(TEXT("rate"), Clip.Rate, 60);
	TestEqual(TEXT("seed"), Clip.Seed, static_cast<int64>(7));
	TestEqual(TEXT("two regions"), Clip.Regions.Num(), 2);
	TestEqual(TEXT("two tracks"), Clip.Tracks.Num(), 2);
	TestEqual(TEXT("a package path becomes the object path"), Clip.Tracks[0].Action, FString(TEXT("/Game/Input/IA_Jump.IA_Jump")));
	TestEqual(TEXT("an id is assigned after the highest used"), Clip.Tracks[0].Notes[1].Id, FString(TEXT("n4")));
	TestTrue(TEXT("interp read"), Clip.Tracks[1].Keys[0].Interp == EClipInterp::Constant);
	TestEqual(TEXT("an axis note's value"), Clip.Tracks[1].Notes[0].Value.X, 1.0);
	TestTrue(TEXT("event entry"), Clip.Events[0].Do.IsValid() && Clip.Events[0].Do->HasField(TEXT("console")));
	TestTrue(TEXT("check mode"), Clip.Checks[2].Mode == EClipCheckMode::Throughout);
	TestTrue(TEXT("check expect op"), Clip.Checks[0].Expect.Op == EScriptExpectOp::Increasing && Clip.Checks[0].Expect.Target == 100.0);
	TestEqual(TEXT("check name"), Clip.Checks[0].Name, FString(TEXT("Moves forward")));
	TestTrue(TEXT("manual check is a hold"), Clip.Checks[3].bManual && Clip.Checks[3].Mode == EClipCheckMode::Hold && !Clip.Checks[3].bAgentJudgeable);
	TestTrue(TEXT("recorded hold read"), Clip.Checks[1].Recorded.bValid && Clip.Checks[1].Recorded.GameMs == 5500.0);

	// Text, then JSON, then text again: nothing either form holds is lost.
	const FString Text = ClipToText(Clip);
	FClip Again;
	TArray<FClipIssue> Issues2;
	TestTrue(TEXT("written text parses"), ClipFromText(Text, Again, Issues2));
	TestEqual(TEXT("text round trip"), ClipToText(Again), Text);
	FClip FromJson;
	TArray<FClipIssue> Issues3;
	TestTrue(TEXT("JSON parses"), ClipFromJson(ClipToJson(Clip), FromJson, Issues3));
	TestEqual(TEXT("JSON round trip keeps the text"), ClipToText(FromJson), Text);
	TestEqual(TEXT("effective length is the set length"), Clip.EffectiveLength(), 600.0);

	// An unknown line and an unknown op come back with candidates.
	FClip Bad;
	TArray<FClipIssue> BadIssues;
	TestFalse(TEXT("a bad clip is refused"), ClipFromText(TEXT("clip rate=60\ntrak /Game/X bool\ncheck c1 0 10 by_end pos_x sideways 1\n"), Bad, BadIssues));
	const FClipIssue* Line = BadIssues.FindByPredicate([](const FClipIssue& I) { return I.Message.Contains(TEXT("not a clip line")); });
	TestTrue(TEXT("unknown line reported with candidates"), Line && Line->Candidates.Contains(TEXT("track")));
	const FClipIssue* Op = BadIssues.FindByPredicate([](const FClipIssue& I) { return I.Field == TEXT("expect.op"); });
	TestTrue(TEXT("unknown op reported with candidates"), Op && Op->Candidates.Contains(TEXT("increasing")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipStructureTest, "UE.MCP.PIE.Clip.Structure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipStructureTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipTests;
	TArray<FClipIssue> Issues;
	ValidateClipStructure(SampleClip(), Issues);
	TestFalse(TEXT("the sample clip saves"), ClipIssuesBlockSave(Issues));

	FClip Clip;
	TArray<FClipIssue> ParseIssues;
	ClipFromText(TEXT(
		"clip rate=60\n"
		"region r1 0 100\n"
		"region r2 50 200\n"
		"track /Game/Input/IA_Jump.IA_Jump bool\n"
		"  note n1 0 30\n"
		"  note n2 20 10\n"
		"track /Game/Input/IA_Move.IA_Move axis1d\n"
		"  key k1 10 1\n"
		"  key k2 10 0\n"
		"event e1 5 {\"wait_ms\":100}\n"
		"check c1 0 10 throughout /Game/Input/IA_Jump triggered\n"), Clip, ParseIssues);
	Issues.Reset();
	ValidateClipStructure(Clip, Issues);
	auto Has = [&Issues](const TCHAR* Id, const TCHAR* Needle)
	{
		return Issues.ContainsByPredicate([Id, Needle](const FClipIssue& I) { return I.Id == Id && I.Message.Contains(Needle) && I.bBlocksSave; });
	};
	TestTrue(TEXT("overlapping regions"), Has(TEXT("r2"), TEXT("may not overlap")));
	TestTrue(TEXT("overlapping notes"), Has(TEXT("n2"), TEXT("may not overlap")));
	TestTrue(TEXT("two keys on one frame"), Has(TEXT("k2"), TEXT("both at frame")));
	TestTrue(TEXT("wait_ms has no meaning on a timeline"), Has(TEXT("e1"), TEXT("wait_ms")));
	TestTrue(TEXT("a trigger cannot hold throughout"), Has(TEXT("c1"), TEXT("cannot hold throughout")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipEditTest, "UE.MCP.PIE.Clip.Edit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipEditTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipTests;
	FClip Clip = SampleClip();
	TArray<FString> Changed;
	TArray<FClipIssue> Issues;

	TestTrue(TEXT("add, move, trim and set apply"), ApplyClipEdits(Clip, ParseArray(TEXT(
		"[{\"op\":\"add\",\"kind\":\"note\",\"track\":\"/Game/Input/IA_Jump\",\"start\":150.4,\"length\":9.6},"
		"{\"op\":\"move\",\"id\":\"n1\",\"by\":5},"
		"{\"op\":\"trim\",\"id\":\"n3\",\"end\":230},"
		"{\"op\":\"set\",\"id\":\"c1\",\"expect\":{\"target\":150}},"
		"{\"op\":\"add\",\"kind\":\"check\",\"start\":250,\"length\":0,\"mode\":\"by_end\",\"expect\":{\"value\":\"pos_z\",\"op\":\"gt\",\"target\":0}}]")), Changed, Issues));
	const FClipNote* Added = Clip.Tracks[0].Notes.FindByPredicate([](const FClipNote& N) { return N.Start == 150.0; });
	TestTrue(TEXT("a new note snaps to the frame grid"), Added && Added->Length == 10.0);
	TestEqual(TEXT("moved by five frames"), Clip.Tracks[0].Notes[0].Start, 35.0);
	TestEqual(TEXT("trimmed to end at 230"), Clip.Tracks[1].Notes[0].Length, 30.0);
	TestEqual(TEXT("set changes one expect field and keeps the rest"), Clip.Checks[0].Expect.Target, 150.0);
	TestTrue(TEXT("op kept"), Clip.Checks[0].Expect.Op == EScriptExpectOp::Increasing);
	TestTrue(TEXT("the new ids are reported"), Changed.Num() == 5 && Changed[0].StartsWith(TEXT("n")));

	// All or none: an op that fails leaves the clip as it was, and says what to use instead.
	const FString Before = ClipToText(Clip);
	Issues.Reset();
	TestFalse(TEXT("a failing op refuses the batch"), ApplyClipEdits(Clip, ParseArray(TEXT(
		"[{\"op\":\"delete\",\"id\":\"n1\"},{\"op\":\"move\",\"id\":\"nx1\",\"to\":3}]")), Changed, Issues));
	TestEqual(TEXT("nothing changed"), ClipToText(Clip), Before);
	TestTrue(TEXT("unknown id offers candidates"), Issues.Num() > 0 && Issues[0].Candidates.Num() > 0);
	Issues.Reset();
	TestFalse(TEXT("a note on a missing track is refused"), ApplyClipEdits(Clip, ParseArray(TEXT(
		"[{\"op\":\"add\",\"kind\":\"note\",\"track\":\"/Game/Input/IA_Jupm\",\"start\":0,\"length\":5}]")), Changed, Issues));
	TestTrue(TEXT("with the track it probably meant"), Issues.Num() > 0 && Issues[0].Candidates.Contains(TEXT("/Game/Input/IA_Jump.IA_Jump")));
	Issues.Reset();
	TestFalse(TEXT("an edit that makes notes overlap is refused"), ApplyClipEdits(Clip, ParseArray(TEXT(
		"[{\"op\":\"move\",\"id\":\"n4\",\"to\":36}]")), Changed, Issues));

	// Quantize to quarter seconds (15 frames at 60 fps).
	Clip.Grid = TEXT("free");
	TestTrue(TEXT("free grid keeps a fractional start"), ApplyClipEdits(Clip, ParseArray(TEXT("[{\"op\":\"move\",\"id\":\"n4\",\"to\":91.3}]")), Changed, Issues));
	TestEqual(TEXT("fractional"), Clip.Tracks[0].Notes.FindByPredicate([](const FClipNote& N) { return N.Id == TEXT("n4"); })->Start, 91.3);
	TestTrue(TEXT("quantize applies"), ApplyClipEdits(Clip, ParseArray(TEXT("[{\"op\":\"quantize\",\"grid\":\"1/4\",\"track\":\"/Game/Input/IA_Jump\"}]")), Changed, Issues));
	TestEqual(TEXT("snapped to the quarter second"), Clip.Tracks[0].Notes.FindByPredicate([](const FClipNote& N) { return N.Id == TEXT("n4"); })->Start, 90.0);
	TestTrue(TEXT("a note never quantizes to nothing"), Clip.Tracks[0].Notes.FindByPredicate([](const FClipNote& N) { return N.Id == TEXT("n4"); })->Length >= 15.0);

	TestTrue(TEXT("delete a track by its action"), ApplyClipEdits(Clip, ParseArray(TEXT("[{\"op\":\"delete\",\"id\":\"IA_Jump\"}]")), Changed, Issues));
	TestEqual(TEXT("one track left"), Clip.Tracks.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipEvaluateTest, "UE.MCP.PIE.Clip.Evaluate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipEvaluateTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipTests;
	const FClip Clip = SampleClip();
	bool bActive = false;
	TestEqual(TEXT("a bool note is 1 inside"), EvaluateClipTrack(Clip.Tracks[0], 35.0, bActive).X, 1.0);
	TestTrue(TEXT("active inside"), bActive);
	EvaluateClipTrack(Clip.Tracks[0], 40.0, bActive);
	TestFalse(TEXT("a note's end is exclusive"), bActive);
	TestEqual(TEXT("a constant key holds its value"), EvaluateClipTrack(Clip.Tracks[1], 60.0, bActive).Y, 1.0);
	EvaluateClipTrack(Clip.Tracks[1], 150.0, bActive);
	TestFalse(TEXT("a curve at zero feeds nothing"), bActive);
	const FVector OnNote = EvaluateClipTrack(Clip.Tracks[1], 205.0, bActive);
	TestTrue(TEXT("a note wins over the curve"), bActive && OnNote.X == 1.0 && OnNote.Y == 0.0);

	FClipTrack Linear;
	Linear.Type = EClipValueType::Axis1D;
	Linear.Keys = { { TEXT("a"), 0.0, FVector(0, 0, 0) }, { TEXT("b"), 10.0, FVector(1, 0, 0) } };
	TestTrue(TEXT("linear keys interpolate"), FMath::IsNearlyEqual(EvaluateClipTrack(Linear, 5.0, bActive).X, 0.5, 1e-4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipCompTest, "UE.MCP.PIE.Clip.Comp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipCompTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipTests;
	FClip Clip = SampleClip();
	const FString Jump = TEXT("/Game/Input/IA_Jump.IA_Jump");
	const FString Move = TEXT("/Game/Input/IA_Move.IA_Move");
	// A take over frames 0..300 at a variable frame rate: Jump down from 50 to 70, Move ramping up then released.
	TArray<FClipTakeSample> Samples;
	for (double F = 0.0; F < 300.0; F += 1.3)
	{
		FClipTakeSample S;
		S.Frame = F;
		S.Values.Add(Jump, FVector(F >= 50.0 && F < 70.0 ? 1.0 : 0.0, 0, 0));
		S.Values.Add(Move, F < 100.0 ? FVector(0.0, F / 100.0, 0) : FVector::ZeroVector);
		Samples.Add(S);
	}
	TMap<FString, EClipValueType> Types;
	Types.Add(Jump, EClipValueType::Bool);
	Types.Add(Move, EClipValueType::Axis2D);
	CompClipTake(Clip, { Jump, Move }, Types, 0.0, 300.0, Samples, TEXT("take1"), TEXT("2026-11-25T00:00:00Z"));

	const FClipTrack* J = Clip.FindTrack(Jump);
	TestEqual(TEXT("one recorded jump note replaces the two in range"), J->Notes.Num(), 1);
	TestTrue(TEXT("the note starts near frame 50"), FMath::Abs(J->Notes[0].Start - 51.0) <= 1.0);
	TestTrue(TEXT("and lasts about 20 frames"), FMath::Abs(J->Notes[0].Length - 20.0) <= 2.0);
	const FClipTrack* M = Clip.FindTrack(Move);
	TestTrue(TEXT("the ramp is reduced to a few keys"), M->Keys.Num() >= 2 && M->Keys.Num() <= 8);
	bool bActive = false;
	TestTrue(TEXT("the reduced curve still ramps"), FMath::IsNearlyEqual(EvaluateClipTrack(*M, 50.0, bActive).Y, 0.5, 0.03));
	EvaluateClipTrack(*M, 150.0, bActive);
	TestFalse(TEXT("released after the ramp"), bActive);
	TestEqual(TEXT("the note at 200 (outside the take's values) went to history"), M->Notes.Num(), 0);
	TestEqual(TEXT("one take kept as history"), Clip.Takes.Num(), 1);
	TestEqual(TEXT("every frame the take was read on is a frame time"), Clip.FrameTimes.Num(), Samples.Num());
	TestTrue(TEXT("a replay steps from frame time to frame time"), FMath::IsNearlyEqual(Clip.NextFrameDelta(Samples[3].Frame), 1.3 / 60.0, 1e-6));
	TestTrue(TEXT("past the frame times it steps at the clip's rate"), FMath::IsNearlyEqual(Clip.NextFrameDelta(400.0), 1.0 / 60.0, 1e-9));
	TestTrue(TEXT("the history holds the old jump notes"), Clip.Takes[0].Tracks.ContainsByPredicate([&Jump](const FClipTrack& T) { return T.Action == Jump && T.Notes.Num() == 2; }));
	TArray<FClipIssue> Issues;
	ValidateClipStructure(Clip, Issues);
	TestFalse(TEXT("the comped clip saves"), ClipIssuesBlockSave(Issues));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipChecksTest, "UE.MCP.PIE.Clip.Checks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipChecksTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipTests;
	const FClip Clip = SampleClip();
	auto Frame = [](double F, double V, bool bObserved = true) { FClipCheckFrame X; X.Frame = F; X.GameMs = F * 1000.0 / 60.0; X.bObserved = bObserved; X.Value = V; return X; };

	// by_end, increasing by 100 from the value at the note's start.
	FClipCheckRun ByEnd;
	ByEnd.Def = Clip.Checks[0];
	AdvanceClipCheck(ByEnd, Frame(0, 10));
	TestTrue(TEXT("running from its start"), ByEnd.Status == EClipCheckStatus::Running);
	AdvanceClipCheck(ByEnd, Frame(60, 90));
	TestTrue(TEXT("not yet"), ByEnd.Status == EClipCheckStatus::Running);
	AdvanceClipCheck(ByEnd, Frame(61, 111));
	TestTrue(TEXT("passes once it rose by 100"), ByEnd.Status == EClipCheckStatus::Passed);
	FClipCheckRun Late;
	Late.Def = Clip.Checks[0];
	AdvanceClipCheck(Late, Frame(0, 10));
	AdvanceClipCheck(Late, Frame(120, 50));
	TestTrue(TEXT("fails at the note's end"), Late.Status == EClipCheckStatus::Failed && Late.Reason.Contains(TEXT("by frame")));

	// throughout: one frame out of bounds fails it.
	FClipCheckRun Through;
	Through.Def = Clip.Checks[2];
	AdvanceClipCheck(Through, Frame(399, 800));
	TestTrue(TEXT("pending before its start"), Through.Status == EClipCheckStatus::Pending);
	AdvanceClipCheck(Through, Frame(400, 600));
	AdvanceClipCheck(Through, Frame(430, 701));
	TestTrue(TEXT("fails the frame it stops holding"), Through.Status == EClipCheckStatus::Failed && Through.EndedFrame == 430.0);
	FClipCheckRun Held;
	Held.Def = Clip.Checks[2];
	AdvanceClipCheck(Held, Frame(400, 600));
	AdvanceClipCheck(Held, Frame(460, 650));
	TestTrue(TEXT("passes at its end"), Held.Status == EClipCheckStatus::Passed);

	// hold: passes when true; the run decides the timeout. A manual check awaits its verdict.
	FClipCheckRun Hold;
	Hold.Def = Clip.Checks[1];
	AdvanceClipCheck(Hold, Frame(300, 10));
	AdvanceClipCheck(Hold, Frame(300, 85));
	TestTrue(TEXT("hold passes when true"), Hold.Status == EClipCheckStatus::Passed);
	FClipCheckRun Manual;
	Manual.Def = Clip.Checks[3];
	AdvanceClipCheck(Manual, Frame(500, 0, false));
	TestTrue(TEXT("manual awaits a verdict"), Manual.Status == EClipCheckStatus::AwaitingVerdict);
	SettleClipCheckAtEnd(Manual, TEXT("stopped"));
	TestTrue(TEXT("an unanswered manual check fails when the run ends"), Manual.Status == EClipCheckStatus::Failed);

	// Regions report against their checks and the drift inside them.
	TArray<FClipCheckRun> Runs = { ByEnd, Hold, Through, Manual };
	TArray<FClipDriftSample> Drift = { { 10.0, 1.f, 2.f }, { 320.0, 12.f, 30.f } };
	const TArray<FClipRegionResult> Regions = SummarizeClipRegions(Clip, Runs, Drift, 5.f, 25.f);
	TestEqual(TEXT("two regions"), Regions.Num(), 2);
	TestEqual(TEXT("region 1 passed"), Regions[0].Status, FString(TEXT("passed")));
	TestEqual(TEXT("region 2 failed"), Regions[1].Status, FString(TEXT("failed")));
	TestEqual(TEXT("region 2 drift"), Regions[1].MaxPositionCm, 12.f);
	TestEqual(TEXT("first frame over threshold"), Regions[1].FirstOverFrame, 320.0);

	// A check on the boundary belongs to the region that starts there; the last region keeps its own end.
	TestFalse(TEXT("a check at a region's end is not in it"), ClipCheckStartsInRegion(Clip, 0, 300.0));
	TestTrue(TEXT("a check at a region's end is in the next"), ClipCheckStartsInRegion(Clip, 1, 300.0));
	TestTrue(TEXT("the last region keeps its end"), ClipCheckStartsInRegion(Clip, 1, 600.0));
	TestFalse(TEXT("an unknown region holds nothing"), ClipCheckStartsInRegion(Clip, 5, 10.0));

	// A region whose checks were all skipped, outside the region a run played, did not pass.
	TArray<FClipCheckRun> Skipped = Runs;
	for (FClipCheckRun& C : Skipped) if (C.Def.Start < 300.0) C.Status = EClipCheckStatus::Skipped;
	const TArray<FClipRegionResult> OneRegion = SummarizeClipRegions(Clip, Skipped, {}, 5.f, 25.f);
	TestEqual(TEXT("a region of skipped checks is skipped"), OneRegion[0].Status, FString(TEXT("skipped")));
	return true;
}

#endif
