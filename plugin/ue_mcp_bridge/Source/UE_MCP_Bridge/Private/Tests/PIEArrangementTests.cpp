// The Playtest Hub's arrangement player as plain data (#2146): placing clips on tracks for each performer kind, the
// Checks track's four modes, a region, a punch-in take, and the per-kind drivers against a disposable world.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Handlers/PIE/PIEArrangement.h"
#include "Handlers/PIE/PIEClip.h"
#include "Handlers/PIE/PIEClipDrivers.h"
#include "HandlerWorld.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace MCPPIEArrangementTests
{
	using namespace MCPPIE;

	TSharedPtr<FJsonObject> Json(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Out);
		return Out;
	}

	// A player clip at 60 fps: jump held from frame 0 for 30 frames, a stick pushed from frame 0 to 60.
	const TCHAR* const Clips = TEXT(R"({
		"jump": {"performer": "player", "rate": 60, "length": 60, "input": [
			{"action": "/Game/Input/IA_Jump", "type": "bool", "notes": [{"id": "n1", "start": 0, "length": 30}], "keys": []},
			{"action": "/Game/Input/IA_Move", "type": "axis2d", "notes": [], "keys": [{"id": "k1", "t": 0, "v": [0, 1]}, {"id": "k2", "t": 60, "v": [0, 0]}]}],
			"lanes": [{"id": "hp", "kind": "value", "target": "Health", "type": "axis1d", "keys": [{"id": "k1", "t": 0, "v": 100}]}]},
		"patrol": {"performer": "actor", "rate": 30, "length": 30, "input": [], "lanes": [
			{"id": "walk", "kind": "move", "points": [{"id": "p1", "t": 0, "at": [100, 0, 0]}, {"id": "p2", "t": 15, "at": [400, 0, 0], "radius": 30}]},
			{"id": "hit", "kind": "action", "calls": [{"id": "a1", "t": 20, "fn": "Attack", "args": [2]}]},
			{"id": "alert", "kind": "value", "target": "bb:Alert", "type": "bool", "keys": [{"id": "k1", "t": 0, "v": false}, {"id": "k2", "t": 10, "v": true}]}]}
	})");

	FString Arrangement(const FString& Checks, const FString& Regions = TEXT("[]"), const FString& ExtraTracks = FString())
	{
		return FString::Printf(TEXT(R"({"id": "door", "level": null, "rate": 60, "tracks": [
			{"id": "p", "performer": {"kind": "player", "client": 0}, "mute": false, "solo": false, "instances": [{"id": "i1", "clip": "jump", "at": 30}]},
			{"id": "g", "performer": {"kind": "actor", "actor": "Gate Guard"}, "mute": false, "solo": false, "instances": [{"id": "i1", "clip": "patrol", "at": 0}, {"id": "i2", "clip": "patrol", "at": 120}]}%s],
			"checks": %s, "regions": %s})"), *ExtraTracks, *Checks, *Regions);
	}

	FArrangementTimeline Timeline(FAutomationTestBase& Test, const FString& Checks, const FString& Region = FString(), const FString& Regions = TEXT("[]"))
	{
		FArrangementTimeline T;
		FString Err;
		Test.TestTrue(TEXT("the arrangement compiles: ") + Err, CompileArrangement(Json(Arrangement(Checks, Regions)), Json(Clips), Region, T.Plan, Err));
		return T;
	}

	const FHubCheckRun& CheckOf(const FArrangementTimeline& T, const FString& Id)
	{
		return *T.Plan.Checks.FindByPredicate([&Id](const FHubCheckRun& C) { return C.Def.Id == Id; });
	}

	// Plays frame by frame, one frame of game time per step, reading Value(frame) for every check.
	void Play(FArrangementTimeline& T, int32 Frames, TFunctionRef<double(double)> Value)
	{
		for (int32 i = 0; i < Frames && !T.Done(); ++i)
		{
			T.Judge([&](const FHubCheckRun&, double& V) { V = Value(T.Position); return true; });
			if (T.AwaitingVerdict()) return;
			T.Advance(1.0);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEArrangementCompileTest, "UE.MCP.PIE.Arrangement.Compile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEArrangementCompileTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEArrangementTests;
	FArrangementPlan Plan;
	FString Err;
	if (!TestTrue(TEXT("compiles"), CompileArrangement(Json(Arrangement(TEXT("[]"))), Json(Clips), FString(), Plan, Err))) return false;
	TestEqual(TEXT("two performers, one of each kind"), Plan.Performers.Num(), 2);

	// The player track: input lanes at the instance's frame, driven as client 0.
	if (!TestEqual(TEXT("the player clip's two input lanes"), Plan.Input.Num(), 2)) return false;
	const FPlacedInput& Jump = Plan.Input[0];
	TestEqual(TEXT("the action path is normalized"), Jump.Track.Action, FString(TEXT("/Game/Input/IA_Jump.IA_Jump")));
	TestEqual(TEXT("the note starts at the instance"), Jump.Track.Notes[0].Start, 30.0);
	TestEqual(TEXT("the instance spans its clip"), Jump.To, 90.0);
	bool bActive = false;
	EvaluateClipTrack(Jump.Track, 45.0, bActive);
	TestTrue(TEXT("jump is held inside the note"), bActive);
	EvaluateClipTrack(Jump.Track, 70.0, bActive);
	TestFalse(TEXT("and released after it"), bActive);

	// The actor track: a 30 fps clip placed twice on a 60 fps arrangement, each lane taking the track's performer.
	const FClipLane* Walk2 = Plan.Lanes.FindByPredicate([](const FClipLane& L) { return L.Id == TEXT("g/i2/walk"); });
	if (!TestNotNull(TEXT("the second instance's move lane"), Walk2)) return false;
	TestTrue(TEXT("it drives the actor"), Walk2->Performer.Kind == EClipPerformerKind::Actor && Walk2->Performer.Actor == TEXT("Gate Guard"));
	TestEqual(TEXT("scaled to the arrangement's rate and moved to its instance"), Walk2->Points[1].Time, 150.0);
	const FClipLane* Hit = Plan.Lanes.FindByPredicate([](const FClipLane& L) { return L.Id == TEXT("g/i1/hit"); });
	TestTrue(TEXT("an action lane's call lands on its frame"), Hit && FMath::IsNearlyEqual(Hit->Calls[0].Time, 40.0));
	const FClipLane* Hp = Plan.Lanes.FindByPredicate([](const FClipLane& L) { return L.Id == TEXT("p/i1/hp"); });
	TestTrue(TEXT("a player's value lane acts on its pawn"), Hp && Hp->Performer.Kind == EClipPerformerKind::Player);
	TestEqual(TEXT("the play ends with the last instance"), Plan.End, 180.0);

	// A clip for the wrong kind of performer, a missing clip, and mute and solo.
	const FString Wrong = TEXT(R"(, {"id": "w", "performer": {"kind": "actor", "actor": "Door"}, "mute": false, "solo": false, "instances": [{"id": "i1", "clip": "jump", "at": 0}]})");
	TestFalse(TEXT("a player clip on an actor track is refused"), CompileArrangement(Json(Arrangement(TEXT("[]"), TEXT("[]"), Wrong)), Json(Clips), FString(), Plan, Err));
	TestTrue(TEXT("saying which"), Err.Contains(TEXT("clip jump is for a player")));
	const FString Missing = TEXT(R"(, {"id": "m", "performer": {"kind": "player", "client": 1}, "mute": false, "solo": false, "instances": [{"id": "i1", "clip": "nope", "at": 0}]})");
	TestFalse(TEXT("a clip clips does not hold is refused"), CompileArrangement(Json(Arrangement(TEXT("[]"), TEXT("[]"), Missing)), Json(Clips), FString(), Plan, Err));
	const FString Soloed = TEXT(R"(, {"id": "s", "performer": {"kind": "player", "client": 2}, "mute": false, "solo": true, "instances": []})");
	TestTrue(TEXT("compiles with a soloed track"), CompileArrangement(Json(Arrangement(TEXT("[]"), TEXT("[]"), Soloed)), Json(Clips), FString(), Plan, Err));
	TestTrue(TEXT("only the soloed track plays"), Plan.Input.Num() == 0 && Plan.Lanes.Num() == 0 && Plan.Performers.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEArrangementChecksTest, "UE.MCP.PIE.Arrangement.Checks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEArrangementChecksTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEArrangementTests;
	const FString Checks = TEXT(R"([
		{"id": "by", "at": 10, "length": 20, "mode": "by_end", "expect": {"value": "pos_x", "op": "gt", "target": 25}},
		{"id": "late", "at": 10, "length": 5, "mode": "by_end", "expect": {"value": "pos_x", "op": "gt", "target": 25}},
		{"id": "thru", "at": 0, "length": 40, "mode": "throughout", "expect": {"value": "pos_x", "op": "lt", "target": 1000}},
		{"id": "broke", "at": 0, "length": 100, "mode": "throughout", "expect": {"value": "pos_x", "op": "lt", "target": 50}},
		{"id": "inc", "at": 0, "length": 30, "mode": "by_end", "expect": {"value": "pos_x", "op": "increasing", "target": 10, "subject": "Door"}}])");
	FArrangementTimeline T = Timeline(*this, Checks);
	Play(T, 1000, [](double F) { return F; });
	TestEqual(TEXT("by_end passes once true before its end"), CheckOf(T, TEXT("by")).Status, EHubCheckStatus::Passed);
	TestEqual(TEXT("on the frame it came true"), CheckOf(T, TEXT("by")).Frame, 26.0);
	TestEqual(TEXT("by_end fails at its end when never true"), CheckOf(T, TEXT("late")).Status, EHubCheckStatus::Failed);
	TestTrue(TEXT("saying what it read last"), CheckOf(T, TEXT("late")).Message.Contains(TEXT("last value")));
	TestEqual(TEXT("throughout passes when it holds to its end"), CheckOf(T, TEXT("thru")).Status, EHubCheckStatus::Passed);
	TestEqual(TEXT("throughout fails on the first frame it does not hold"), CheckOf(T, TEXT("broke")).Frame, 50.0);
	TestEqual(TEXT("a subject names the actor read"), CheckOf(T, TEXT("inc")).Def.Expect.Path(), FString(TEXT("Door.pos_x")));
	TestEqual(TEXT("increasing compares with the value at its start"), CheckOf(T, TEXT("inc")).Status, EHubCheckStatus::Passed);
	TestTrue(TEXT("the play reached its end"), T.Done());

	// hold: the position stands at its start until the value comes true, then plays on.
	FArrangementTimeline H = Timeline(*this, TEXT(R"([{"id": "door", "at": 20, "length": 0, "timeout": 50, "mode": "hold", "expect": {"value": "Door.Open", "op": "eq", "target": true}}])"));
	int32 Steps = 0;
	for (; Steps < 40; ++Steps)
	{
		H.Judge([&](const FHubCheckRun&, double& V) { V = Steps >= 30 ? 1.0 : 0.0; return true; });
		H.Advance(1.0);
	}
	TestEqual(TEXT("hold passes once true"), CheckOf(H, TEXT("door")).Status, EHubCheckStatus::Passed);
	TestEqual(TEXT("having held the position at its start"), CheckOf(H, TEXT("door")).Frame, 20.0);
	TestEqual(TEXT("and then played on"), H.Position, 30.0);

	// hold times out after its timeout of game time held.
	FArrangementTimeline Out = Timeline(*this, TEXT(R"([{"id": "door", "at": 5, "length": 0, "timeout": 30, "mode": "hold", "expect": {"value": "Door.Open", "op": "eq", "target": true}}])"));
	Play(Out, 400, [](double) { return 0.0; });
	TestEqual(TEXT("a hold that never comes true fails"), CheckOf(Out, TEXT("door")).Status, EHubCheckStatus::Failed);
	TestTrue(TEXT("after its timeout"), CheckOf(Out, TEXT("door")).HeldFrames >= 30.0 && CheckOf(Out, TEXT("door")).Message.Contains(TEXT("within 30 frames")));
	TestTrue(TEXT("and the play goes on to its end"), Out.Done());

	// manual: playback stands and waits for the verdict.
	FArrangementTimeline M = Timeline(*this, TEXT(R"([{"id": "look", "at": 12, "length": 0, "mode": "manual", "instruction": "Does the door swing smoothly?"}])"));
	Play(M, 100, [](double) { return 0.0; });
	TestTrue(TEXT("a manual check waits for its verdict"), M.AwaitingVerdict() && CheckOf(M, TEXT("look")).Status == EHubCheckStatus::AwaitingVerdict);
	TestEqual(TEXT("at its frame"), M.Position, 12.0);
	M.Advance(10.0);
	TestEqual(TEXT("the position does not move while it waits"), M.Position, 12.0);
	FString Err;
	TestFalse(TEXT("a verdict is pass or fail"), M.Verdict(TEXT("look"), TEXT("skip"), FString(), Err));
	TestFalse(TEXT("an unknown check is named"), M.Verdict(TEXT("nope"), TEXT("pass"), FString(), Err) || !Err.Contains(TEXT("look")));
	TestTrue(TEXT("a verdict settles it"), M.Verdict(TEXT("look"), TEXT("fail"), TEXT("it stutters"), Err));
	TestEqual(TEXT("as given"), CheckOf(M, TEXT("look")).Status, EHubCheckStatus::Failed);
	TestTrue(TEXT("with the note in its message"), HubCheckRunToJson(CheckOf(M, TEXT("look")))->GetStringField(TEXT("message")).Contains(TEXT("it stutters")));
	M.Advance(5.0);
	TestEqual(TEXT("and playback moves on"), M.Position, 17.0);

	// What does not read is refused before anything plays.
	FArrangementPlan Plan;
	TestFalse(TEXT("a check with no expect is refused"), CompileArrangement(Json(Arrangement(TEXT(R"([{"id": "x", "at": 0, "length": 1, "mode": "by_end"}])"))), Json(Clips), FString(), Plan, Err));
	TestFalse(TEXT("an unknown mode is refused"), CompileArrangement(Json(Arrangement(TEXT(R"([{"id": "x", "at": 0, "length": 1, "mode": "sometimes", "expect": {"value": "a", "op": "gt", "target": 1}}])"))), Json(Clips), FString(), Plan, Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEArrangementRegionTest, "UE.MCP.PIE.Arrangement.Region", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEArrangementRegionTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEArrangementTests;
	const FString Regions = TEXT(R"([{"id": "walk", "name": "Walk", "start": 0, "end": 60}, {"id": "fight", "name": "Fight", "start": 60, "end": 120}])");
	const FString Checks = TEXT(R"([
		{"id": "a", "at": 10, "length": 10, "mode": "by_end", "expect": {"value": "pos_x", "op": "gt", "target": 5}},
		{"id": "b", "at": 70, "length": 10, "mode": "by_end", "expect": {"value": "pos_x", "op": "gt", "target": 1000}},
		{"id": "c", "at": 100, "length": 0, "mode": "manual", "instruction": "Look"}])");
	FArrangementTimeline T = Timeline(*this, Checks, TEXT("walk"), Regions);
	TestEqual(TEXT("a region plays to its end"), T.Plan.End, 60.0);
	TestEqual(TEXT("checks outside it are skipped"), CheckOf(T, TEXT("b")).Status, EHubCheckStatus::Skipped);
	TestEqual(TEXT("manual ones too"), CheckOf(T, TEXT("c")).Status, EHubCheckStatus::Skipped);
	Play(T, 1000, [](double F) { return F; });
	TestEqual(TEXT("the check inside it is judged"), CheckOf(T, TEXT("a")).Status, EHubCheckStatus::Passed);
	TestTrue(TEXT("and the skipped manual check did not stop the play"), T.Done() && FMath::IsNearlyEqual(T.Position, 60.0));

	FArrangementTimeline F = Timeline(*this, Checks, TEXT("fight"), Regions);
	TestEqual(TEXT("a later region plays from frame 0 to its end"), F.Plan.End, 120.0);
	TestEqual(TEXT("and skips the checks before it"), CheckOf(F, TEXT("a")).Status, EHubCheckStatus::Skipped);
	FArrangementPlan Plan;
	FString Err;
	TestFalse(TEXT("an unknown region is refused"), CompileArrangement(Json(Arrangement(Checks, Regions)), Json(Clips), TEXT("boss"), Plan, Err));
	TestTrue(TEXT("naming the ones there are"), Err.Contains(TEXT("walk, fight")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEArrangementRecordTest, "UE.MCP.PIE.Arrangement.Record", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEArrangementRecordTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEArrangementTests;
	// A punch-in over frames 20..40 of a clip with jump held over 10..50 and a stick curve.
	FClip Clip;
	FString Err;
	if (!TestTrue(TEXT("the clip reads"), ClipFromJson(Json(TEXT(R"({"rate": 60, "length": 60, "input": [
		{"action": "/Game/Input/IA_Jump.IA_Jump", "type": "bool", "notes": [{"id": "n1", "start": 10, "length": 40}], "keys": []},
		{"action": "/Game/Input/IA_Move.IA_Move", "type": "axis1d", "notes": [], "keys": [{"id": "k1", "t": 0, "v": 0.5}, {"id": "k2", "t": 30, "v": 0.5}, {"id": "k3", "t": 59, "v": 0.5}]}],
		"lanes": [{"id": "hp", "kind": "value", "target": "Health", "type": "axis1d", "keys": [{"id": "k1", "t": 0, "v": 100}, {"id": "k2", "t": 30, "v": 100}]}]})")), Clip, Err))) return false;
	TArray<FClipTakeSample> Samples;
	for (int32 F = 20; F < 40; ++F)
	{
		FClipTakeSample S;
		S.Frame = F;
		// Jump pressed on 25..30 only; the stick pulled back to -1.
		S.Values.Add(TEXT("/Game/Input/IA_Jump.IA_Jump"), FVector(F >= 25 && F < 30 ? 1.0 : 0.0, 0, 0));
		S.Values.Add(TEXT("/Game/Input/IA_Move.IA_Move"), FVector(-1.0, 0, 0));
		Samples.Add(S);
	}
	TMap<FString, EClipValueType> Types;
	Types.Add(TEXT("/Game/Input/IA_Jump.IA_Jump"), EClipValueType::Bool);
	Types.Add(TEXT("/Game/Input/IA_Move.IA_Move"), EClipValueType::Axis1D);
	PunchClipInput(Clip, { TEXT("/Game/Input/IA_Jump.IA_Jump"), TEXT("/Game/Input/IA_Move.IA_Move") }, Types, 20.0, 40.0, Samples);
	const FClipTrack* Jump = Clip.FindInput(TEXT("/Game/Input/IA_Jump"));
	if (!TestNotNull(TEXT("the jump lane"), Jump)) return false;
	bool bActive = false;
	EvaluateClipTrack(*Jump, 15.0, bActive);
	TestTrue(TEXT("before the punch-in the old note holds"), bActive);
	EvaluateClipTrack(*Jump, 22.0, bActive);
	TestFalse(TEXT("inside it the take's silence replaced it"), bActive);
	EvaluateClipTrack(*Jump, 27.0, bActive);
	TestTrue(TEXT("and the take's press is there"), bActive);
	EvaluateClipTrack(*Jump, 45.0, bActive);
	TestTrue(TEXT("after the punch-out the old note holds again"), bActive);
	const FClipTrack* Move = Clip.FindInput(TEXT("/Game/Input/IA_Move"));
	TestTrue(TEXT("the stick reads the take inside the range"), Move && FMath::IsNearlyEqual(EvaluateClipTrack(*Move, 30.0, bActive).X, -1.0, 1e-3));
	TestTrue(TEXT("and its old curve outside it"), Move && FMath::IsNearlyEqual(EvaluateClipTrack(*Move, 10.0, bActive).X, 0.5, 1e-3));

	// A value lane punched by what its performer held.
	FClipLaneRecording Hp;
	Hp.LaneId = TEXT("hp");
	for (int32 F = 20; F < 40; ++F) { FClipLaneSample S; S.Frame = F; S.bHasValue = true; S.Value = 100.0 - (F - 20) * 2.0; Hp.Samples.Add(S); }
	PunchClipLanes(Clip, { Hp }, 20.0, 40.0);
	TestTrue(TEXT("the value lane holds the fall"), Clip.FindLane(TEXT("hp"))->Keys.ContainsByPredicate([](const FClipKey& K) { return FMath::IsNearlyEqual(K.Value.X, 62.0, 0.01); }));
	TestFalse(TEXT("and not the old key inside the range"), Clip.FindLane(TEXT("hp"))->Keys.ContainsByPredicate([](const FClipKey& K) { return FMath::IsNearlyEqual(K.Time, 30.0); }));

	// A move lane recording reduces to the route walked.
	TArray<FClipLaneSample> Walk;
	for (int32 F = 0; F <= 100; ++F)
	{
		FClipLaneSample S;
		S.Frame = F;
		S.bHasLocation = true;
		S.Location = F <= 20 ? FVector::ZeroVector : F <= 50 ? FVector((F - 20) * 10.0, 0, 0) : F <= 80 ? FVector(300, (F - 50) * 10.0, 0) : FVector(300, 300, 0);
		Walk.Add(S);
	}
	const TArray<FClipMovePoint> Points = ReduceClipMoveSamples(Walk);
	TestTrue(TEXT("the walk reduces to its two legs, set off when it left each"), Points.Num() == 2 && Points[0].Time == 20.0 && Points[1].Time == 50.0);

	// The answer clip_record_stop gives: the hub's shape, which reads back as it was.
	const TSharedRef<FJsonObject> Out = ClipToJson(Clip);
	TestTrue(TEXT("rate, length, input and lanes"), Out->HasField(TEXT("rate")) && Out->HasField(TEXT("length")) && Out->HasField(TEXT("input")) && Out->HasField(TEXT("lanes")));
	TestFalse(TEXT("and no checks"), Out->HasField(TEXT("checks")));
	FClip Back;
	TestTrue(TEXT("reads back"), ClipFromJson(Out, Back, Err) && Back.Input.Num() == 2 && Back.Lanes.Num() == 1);
	TestTrue(TEXT("with every note given an id"), Back.Input[0].Notes.Num() > 0 && !Back.Input[0].Notes.ContainsByPredicate([](const FClipNote& N) { return N.Id.IsEmpty(); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEArrangementDriversTest, "UE.MCP.PIE.Arrangement.Drivers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEArrangementDriversTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEArrangementTests;
	// One driver per kind, each declaring what it takes.
	FClipDriverContext Ctx;
	TUniquePtr<FClipPerformerDriver> Player = MakeClipPerformerDriver(FClipPerformer::Player(0), Ctx);
	TUniquePtr<FClipPerformerDriver> Actor = MakeClipPerformerDriver(FClipPerformer::ForActor(TEXT("UEMCP_HubActor")), Ctx);
	if (!TestTrue(TEXT("a driver for each kind"), Player.IsValid() && Actor.IsValid())) return false;
	TestTrue(TEXT("the player driver takes input"), Player->Accepts(EClipLaneCapability::Input));
	TestFalse(TEXT("the player driver takes no move"), Player->Accepts(EClipLaneCapability::Move));
	TestTrue(TEXT("the actor driver takes move"), Actor->Accepts(EClipLaneCapability::Move));
	TestFalse(TEXT("the actor driver takes no input"), Actor->Accepts(EClipLaneCapability::Input));
	FString Err;
	FClipMovePoint Pt;
	TestFalse(TEXT("a player refuses a move"), Player->MoveTo(nullptr, Pt, Err));

	if (!GEditor) return true;
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("UEMCP_HubDriverTest")), GetTransientPackage());
	if (!TestNotNull(TEXT("a disposable world"), World)) return false;
	AActor* Target = World->SpawnActor<AStaticMeshActor>(FVector(10, 20, 30), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an actor to drive"), Target)) { MCPDestroyDisposableWorld(World); return false; }
	Target->SetActorLabel(TEXT("UEMCP_HubActor"));

	TestTrue(TEXT("the actor driver finds its actor by label"), Actor->ResolveActor(World, Err) == Target);
	FVector Where;
	TestTrue(TEXT("it reads where the actor stands"), Actor->ReadLocation(World, Where, Err) && Where.Equals(FVector(10, 20, 30), 0.01));
	FClipLane Dilation;
	Dilation.Capability = EClipLaneCapability::Value;
	Dilation.Target = TEXT("CustomTimeDilation");
	TestTrue(TEXT("a value lane writes the property"), Actor->SetValue(World, Dilation, 0.25, Err));
	double Read = 0.0;
	TestTrue(TEXT("and reads it back"), Actor->ReadValue(World, Dilation, Read, Err) && FMath::IsNearlyEqual(Read, 0.25, 1e-4));
	FClipActionCall Hide;
	Hide.Function = TEXT("SetActorHiddenInGame");
	Hide.Args.Add(MakeShared<FJsonValueBoolean>(true));
	TestTrue(TEXT("an action lane calls the function"), Actor->Call(World, Hide, Err));
	TestTrue(TEXT("and the call took effect"), Target->IsHidden());
	TestFalse(TEXT("a move lane needs a pawn"), Actor->MoveTo(World, Pt, Err));

	// Recording an action lane: a call through ProcessEvent is seen on its frame, and the function is as it was after.
	UFunction* Fn = Target->FindFunction(TEXT("SetActorHiddenInGame"));
	const FNativeFuncPtr Original = Fn ? Fn->GetNativeFunc() : nullptr;
	FClipCallRecorder& Recorder = FClipCallRecorder::Get();
	TestEqual(TEXT("a missing function is named"), Recorder.Watch(TEXT("l1"), Target, { TEXT("SetActorHiddenInGame"), TEXT("NoSuchFunction") }), TArray<FString>{ TEXT("NoSuchFunction") });
	Recorder.SetFrame(12.0);
	if (Fn)
	{
		TArray<uint8> Parms;
		Parms.SetNumZeroed(Fn->ParmsSize);
		if (FBoolProperty* B = CastField<FBoolProperty>(Fn->FindPropertyByName(TEXT("bNewHidden")))) B->SetPropertyValue_InContainer(Parms.GetData(), false);
		FEditorScriptExecutionGuard Guard;
		Target->ProcessEvent(Fn, Parms.GetData());
	}
	const TMap<FString, TArray<FClipActionCall>> Seen = Recorder.Finish();
	const TArray<FClipActionCall>* Calls = Seen.Find(TEXT("l1"));
	TestTrue(TEXT("the call was seen on its frame"), Calls && Calls->Num() == 1 && FMath::IsNearlyEqual((*Calls)[0].Time, 12.0));
	TestTrue(TEXT("the function is as it was"), Fn && Fn->GetNativeFunc() == Original);
	TestFalse(TEXT("nothing is watched after the take"), Recorder.IsWatching());
	MCPDestroyDisposableWorld(World);
	return true;
}

#endif
