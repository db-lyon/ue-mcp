// Automation coverage for a clip's actor lanes (#2137): performers as a tagged union, move, action and value lanes in
// the JSON and text forms, their structural checks against the performer they drive, their edits, a value lane's
// evaluation, comping what an actor did into a take, and the drivers a run hands lanes to. The drivers run against
// an actor in a disposable world; no PIE.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

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

namespace MCPPIEClipLaneTests
{
	using namespace MCPPIE;

	TArray<TSharedPtr<FJsonValue>> LaneOps(const FString& Text)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Out);
		return Out;
	}

	const TCHAR* const GuardText = TEXT(
		"clip rate=60 length=300\n"
		"performer actor:\"Guard 2\"\n"
		"lane l1 move\n"
		"  point p1 0 100,0,0\n"
		"  point p2 120 400,0,0 radius=30\n"
		"lane l2 action watch=Alert\n"
		"  call a1 200 Attack [2,\"left\"]\n"
		"lane l3 value bb:Alert axis1d\n"
		"  key k1 0 0\n"
		"  key k2 180 1 constant\n"
		"lane l4 value Health axis1d performer=player:0\n"
		"  key - 0 100\n"
		"check c1 0 300 by_end pos_x increasing 100\n");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipLaneFormatTest, "UE.MCP.PIE.Clip.Lanes.Format", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipLaneFormatTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipLaneTests;
	FClip Clip;
	TArray<FClipIssue> Issues;
	TestTrue(TEXT("a clip with lanes parses"), ClipFromText(GuardText, Clip, Issues));
	TestEqual(TEXT("no issue"), Issues.Num(), 0);
	TestTrue(TEXT("the clip names its performer"), Clip.bHasPerformer && Clip.Performer.Kind == EClipPerformerKind::Actor && Clip.Performer.Actor == TEXT("Guard 2"));
	if (!TestEqual(TEXT("four lanes"), Clip.Lanes.Num(), 4)) return false;
	TestTrue(TEXT("a move lane"), Clip.Lanes[0].Capability == EClipLaneCapability::Move);
	TestEqual(TEXT("two points"), Clip.Lanes[0].Points.Num(), 2);
	TestEqual(TEXT("a point's radius"), Clip.Lanes[0].Points[1].Radius, 30.0);
	TestEqual(TEXT("a call's arguments"), Clip.Lanes[1].Calls.Num() == 1 ? Clip.Lanes[1].Calls[0].Args.Num() : -1, 2);
	TestEqual(TEXT("what the action lane watches"), Clip.Lanes[1].Watch, TArray<FString>{ TEXT("Alert") });
	TestEqual(TEXT("a blackboard target"), Clip.Lanes[2].Target, FString(TEXT("bb:Alert")));
	TestEqual(TEXT("an assigned key id"), Clip.Lanes[3].Keys.Num() == 1 ? Clip.Lanes[3].Keys[0].Id : FString(), FString(TEXT("k3")));
	TestEqual(TEXT("a lane's own performer wins"), Clip.LanePerformer(Clip.Lanes[3]).Key(), FString(TEXT("player:0")));
	TestEqual(TEXT("the others take the clip's"), Clip.LanePerformer(Clip.Lanes[0]).Key(), FString(TEXT("actor:guard 2")));
	TestEqual(TEXT("the lanes count toward the length"), FClip(Clip).EffectiveLength(), 300.0);

	// Both forms read back to the same clip.
	FClip FromText;
	TArray<FClipIssue> TextIssues;
	TestTrue(TEXT("the text form reads back"), ClipFromText(ClipToText(Clip), FromText, TextIssues));
	TestEqual(TEXT("the text form is stable"), ClipToText(FromText), ClipToText(Clip));
	TestTrue(TEXT("a quoted actor stays quoted"), ClipToText(Clip).Contains(TEXT("performer actor:\"Guard 2\"")));
	FClip FromJson;
	TArray<FClipIssue> JsonIssues;
	TestTrue(TEXT("the JSON form reads back"), ClipFromJson(ClipToJson(Clip), FromJson, JsonIssues));
	TestEqual(TEXT("the JSON form is stable"), ClipToText(FromJson), ClipToText(Clip));
	const TSharedRef<FJsonObject> Json = ClipToJson(Clip);
	const TArray<TSharedPtr<FJsonValue>>& Lanes = Json->GetArrayField(TEXT("lanes"));
	TestEqual(TEXT("a lane's kind in JSON"), Lanes[0]->AsObject()->GetStringField(TEXT("kind")), FString(TEXT("move")));
	TestEqual(TEXT("a performer in JSON is a tagged union"), Json->GetObjectField(TEXT("performer"))->GetStringField(TEXT("kind")), FString(TEXT("actor")));

	// Performers and capabilities.
	FClipPerformer P;
	TestTrue(TEXT("player:3 reads"), ClipPerformerFromText(TEXT("player:3"), P) && P.Kind == EClipPerformerKind::Player && P.Client == 3);
	TestFalse(TEXT("an unknown kind is refused"), ClipPerformerFromText(TEXT("group:a"), P));
	TestFalse(TEXT("a client past 15 is refused"), ClipPerformerFromText(TEXT("player:16"), P));
	TestTrue(TEXT("a player takes input"), ClipPerformerAccepts(EClipPerformerKind::Player, EClipLaneCapability::Input));
	TestFalse(TEXT("a player takes no move"), ClipPerformerAccepts(EClipPerformerKind::Player, EClipLaneCapability::Move));
	TestTrue(TEXT("an actor takes move"), ClipPerformerAccepts(EClipPerformerKind::Actor, EClipLaneCapability::Move));
	TestFalse(TEXT("an actor takes no input"), ClipPerformerAccepts(EClipPerformerKind::Actor, EClipLaneCapability::Input));

	FClip Bad;
	TArray<FClipIssue> BadIssues;
	TestFalse(TEXT("an input lane among lanes is refused"), ClipFromText(TEXT("clip rate=60\nlane l1 input\n"), Bad, BadIssues));
	TestTrue(TEXT("with the kinds it takes as candidates"), BadIssues.Num() > 0 && BadIssues[0].Candidates.Contains(TEXT("move")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipLaneStructureTest, "UE.MCP.PIE.Clip.Lanes.Structure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipLaneStructureTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipLaneTests;
	FClip Clip;
	TArray<FClipIssue> Issues;
	ClipFromText(GuardText, Clip, Issues);
	Issues.Reset();
	ValidateClipStructure(Clip, Issues);
	TestFalse(TEXT("the guard clip saves"), ClipIssuesBlockSave(Issues));

	// A move lane on a player is said, not refused: the run skips it.
	FClip OnPlayer;
	TArray<FClipIssue> P;
	ClipFromText(TEXT("clip rate=60\nlane l1 move\n  point p1 0 0,0,0\n"), OnPlayer, P);
	P.Reset();
	ValidateClipStructure(OnPlayer, P);
	const FClipIssue* Mismatch = P.FindByPredicate([](const FClipIssue& I) { return I.Field == TEXT("performer"); });
	TestTrue(TEXT("a move lane on a player is a finding"), Mismatch && !Mismatch->bBlocksSave && Mismatch->Candidates.Contains(TEXT("input")));

	// Two move lanes for one actor over the same frames fight; one after the other is how instances compose.
	FClip Overlap;
	TArray<FClipIssue> O;
	ClipFromText(TEXT("clip rate=60\nperformer actor:Guard\nlane l1 move\n  point - 0 0,0,0\n  point - 100 1,0,0\nlane l2 move\n  point - 50 2,0,0\n"), Overlap, O);
	O.Reset();
	ValidateClipStructure(Overlap, O);
	TestTrue(TEXT("overlapping move lanes are refused"), O.ContainsByPredicate([](const FClipIssue& I) { return I.bBlocksSave && I.Id == TEXT("l2"); }));
	FClip After;
	TArray<FClipIssue> A;
	ClipFromText(TEXT("clip rate=60\nperformer actor:Guard\nlane l1 move\n  point - 0 0,0,0\n  point - 100 1,0,0\nlane l2 move\n  point - 150 2,0,0\nlane l3 move performer=actor:Other\n  point - 50 3,0,0\n"), After, A);
	A.Reset();
	ValidateClipStructure(After, A);
	TestFalse(TEXT("move lanes one after another, or on another actor, save"), ClipIssuesBlockSave(A));

	FClip NoTarget;
	TArray<FClipIssue> N;
	ClipFromText(TEXT("clip rate=60\nlane l1 value \"\" axis1d\n"), NoTarget, N);
	N.Reset();
	ValidateClipStructure(NoTarget, N);
	TestTrue(TEXT("an empty target is refused"), N.ContainsByPredicate([](const FClipIssue& I) { return I.bBlocksSave && I.Field == TEXT("target"); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipLaneEditTest, "UE.MCP.PIE.Clip.Lanes.Edit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipLaneEditTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipLaneTests;
	FClip Clip;
	TArray<FString> Changed;
	TArray<FClipIssue> Issues;
	const bool bOk = ApplyClipEdits(Clip, LaneOps(TEXT(
		"[{\"op\":\"set_clip\",\"performer\":{\"kind\":\"actor\",\"actor\":\"Guard\"}},"
		"{\"op\":\"add\",\"kind\":\"lane\",\"capability\":\"move\"},"
		"{\"op\":\"add\",\"kind\":\"point\",\"lane\":\"l1\",\"time\":10.4,\"at\":[100,0,0]},"
		"{\"op\":\"add\",\"kind\":\"lane\",\"capability\":\"action\",\"id\":\"swing\"},"
		"{\"op\":\"add\",\"kind\":\"call\",\"lane\":\"swing\",\"time\":30,\"fn\":\"Attack\",\"args\":[1]},"
		"{\"op\":\"add\",\"kind\":\"lane\",\"capability\":\"value\",\"target\":\"bb:Alert\"},"
		"{\"op\":\"add\",\"kind\":\"key\",\"lane\":\"l2\",\"time\":0,\"value\":0},"
		"{\"op\":\"add\",\"kind\":\"key\",\"lane\":\"l2\",\"time\":60,\"value\":1}]")), Changed, Issues);
	TestTrue(TEXT("the edits apply"), bOk);
	if (!bOk) { for (const FClipIssue& I : Issues) AddError(I.Message); return false; }
	TestEqual(TEXT("three lanes"), Clip.Lanes.Num(), 3);
	TestEqual(TEXT("the point snaps to the grid"), Clip.Lanes[0].Points[0].Time, 10.0);
	TestEqual(TEXT("ids changed, in order"), Changed, TArray<FString>{ TEXT("clip"), TEXT("l1"), TEXT("p1"), TEXT("swing"), TEXT("a1"), TEXT("l2"), TEXT("k1"), TEXT("k2") });

	Changed.Reset();
	TestTrue(TEXT("points, calls and keys move, change and go"), ApplyClipEdits(Clip, LaneOps(TEXT(
		"[{\"op\":\"move\",\"id\":\"p1\",\"by\":20},"
		"{\"op\":\"set\",\"id\":\"a1\",\"fn\":\"Block\"},"
		"{\"op\":\"set\",\"id\":\"l2\",\"performer\":\"actor:Door\",\"type\":\"bool\"},"
		"{\"op\":\"set\",\"id\":\"k2\",\"value\":0.5},"
		"{\"op\":\"delete\",\"id\":\"k1\"}]")), Changed, Issues));
	TestEqual(TEXT("the point moved"), Clip.Lanes[0].Points[0].Time, 30.0);
	TestEqual(TEXT("the call changed"), Clip.Lanes[1].Calls[0].Function, FString(TEXT("Block")));
	TestTrue(TEXT("the lane took its own performer"), Clip.Lanes[2].bHasPerformer && Clip.Lanes[2].Performer.Actor == TEXT("Door"));
	TestTrue(TEXT("the lane is bool now"), Clip.Lanes[2].Type == EClipValueType::Bool);
	TestEqual(TEXT("one key left"), Clip.Lanes[2].Keys.Num(), 1);

	FClip Before = Clip;
	Issues.Reset();
	TestFalse(TEXT("a call on a move lane is refused"), ApplyClipEdits(Clip, LaneOps(TEXT("[{\"op\":\"add\",\"kind\":\"call\",\"lane\":\"l1\",\"time\":0,\"fn\":\"X\"}]")), Changed, Issues));
	TestFalse(TEXT("a lane that is not there names candidates"), ApplyClipEdits(Clip, LaneOps(TEXT("[{\"op\":\"add\",\"kind\":\"point\",\"lane\":\"l9\",\"time\":0,\"at\":[0,0,0]}]")), Changed, Issues) || !Issues.Last().Candidates.Contains(TEXT("l1")));
	TestFalse(TEXT("an input lane is not a lane"), ApplyClipEdits(Clip, LaneOps(TEXT("[{\"op\":\"add\",\"kind\":\"lane\",\"capability\":\"input\"}]")), Changed, Issues));
	TestEqual(TEXT("a refused edit changes nothing"), ClipToText(Clip), ClipToText(Before));

	Changed.Reset();
	TestTrue(TEXT("a whole lane goes"), ApplyClipEdits(Clip, LaneOps(TEXT("[{\"op\":\"delete\",\"id\":\"swing\"},{\"op\":\"set_clip\",\"performer\":null}]")), Changed, Issues));
	TestEqual(TEXT("two lanes left"), Clip.Lanes.Num(), 2);
	TestFalse(TEXT("the clip's performer cleared"), Clip.bHasPerformer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipLaneEvaluateTest, "UE.MCP.PIE.Clip.Lanes.Evaluate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipLaneEvaluateTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipLaneTests;
	FClip Clip;
	TArray<FClipIssue> Issues;
	ClipFromText(TEXT("clip rate=60\nlane l1 value Speed axis1d performer=actor:A\n  key k1 10 0\n  key k2 30 100\nlane l2 value bOpen bool performer=actor:A\n  key k3 0 0.7\n"), Clip, Issues);
	bool bActive = true;
	EvaluateClipValueLane(Clip.Lanes[0], 5.0, bActive);
	TestFalse(TEXT("before the first key the lane writes nothing"), bActive);
	TestEqual(TEXT("halfway along the curve"), EvaluateClipValueLane(Clip.Lanes[0], 20.0, bActive), 50.0, 1e-3);
	TestTrue(TEXT("inside, it writes"), bActive);
	TestEqual(TEXT("on the last key"), EvaluateClipValueLane(Clip.Lanes[0], 30.0, bActive), 100.0, 1e-3);
	EvaluateClipValueLane(Clip.Lanes[0], 40.0, bActive);
	TestFalse(TEXT("past the last key it lets go"), bActive);
	TestEqual(TEXT("a bool lane rounds"), EvaluateClipValueLane(Clip.Lanes[1], 0.0, bActive), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipLaneRecordTest, "UE.MCP.PIE.Clip.Lanes.Record", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipLaneRecordTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipLaneTests;
	// The actor stood at the origin until frame 20, walked to (300,0,0) by frame 50, turned, walked to (300,300,0)
	// by frame 80 and stood there.
	TArray<FClipLaneSample> Walk;
	for (int32 F = 0; F <= 100; ++F)
	{
		FClipLaneSample S;
		S.Frame = F;
		S.bHasLocation = true;
		if (F <= 20) S.Location = FVector::ZeroVector;
		else if (F <= 50) S.Location = FVector((F - 20) * 10.0, 0, 0);
		else if (F <= 80) S.Location = FVector(300, (F - 50) * 10.0, 0);
		else S.Location = FVector(300, 300, 0);
		Walk.Add(S);
	}
	const TArray<FClipMovePoint> Points = ReduceClipMoveSamples(Walk);
	if (!TestEqual(TEXT("the walk reduces to its two legs"), Points.Num(), 2)) return false;
	TestEqual(TEXT("it sets off when it left the origin"), Points[0].Time, 20.0);
	TestTrue(TEXT("toward the corner"), Points[0].Location.Equals(FVector(300, 0, 0), 1.0));
	TestEqual(TEXT("then when it left the corner"), Points[1].Time, 50.0);
	TestTrue(TEXT("toward where it stopped"), Points[1].Location.Equals(FVector(300, 300, 0), 1.0));

	FClip Clip;
	TArray<FClipIssue> Issues;
	ClipFromText(TEXT("clip rate=60 length=120\nperformer actor:Guard\nlane l1 move\n  point p1 0 9,9,9\nlane l2 action\n  call a1 10 Old\nlane l3 value Health axis1d\n  key k1 0 100\n"), Clip, Issues);
	TArray<FClipLaneRecording> Recs;
	FClipLaneRecording Move;
	Move.LaneId = TEXT("l1");
	Move.Samples = Walk;
	Recs.Add(Move);
	FClipLaneRecording Calls;
	Calls.LaneId = TEXT("l2");
	FClipActionCall Swing;
	Swing.Time = 42.0;
	Swing.Function = TEXT("Attack");
	Calls.Calls.Add(Swing);
	Recs.Add(Calls);
	FClipLaneRecording Value;
	Value.LaneId = TEXT("l3");
	for (int32 F = 0; F <= 100; ++F)
	{
		FClipLaneSample S;
		S.Frame = F;
		S.bHasValue = true;
		S.Value = F < 60 ? 100.0 : 100.0 - (F - 60);
		Value.Samples.Add(S);
	}
	Recs.Add(Value);
	CompClipLaneTakes(Clip, Recs, 0.0, 110.0, TEXT("take1"), TEXT("2026-11-26T00:00:00Z"));
	TestEqual(TEXT("the move lane holds the walk"), Clip.Lanes[0].Points.Num(), 2);
	TestEqual(TEXT("the action lane holds the call seen"), Clip.Lanes[1].Calls.Num() == 1 ? Clip.Lanes[1].Calls[0].Function : FString(), FString(TEXT("Attack")));
	TestTrue(TEXT("the value lane is reduced to a few keys"), Clip.Lanes[2].Keys.Num() >= 2 && Clip.Lanes[2].Keys.Num() <= 4);
	TestTrue(TEXT("the value lane keeps the fall"), Clip.Lanes[2].Keys.ContainsByPredicate([](const FClipKey& K) { return FMath::IsNearlyEqual(K.Value.X, 60.0, 0.01); }));
	const FClipTake* Take = Clip.Takes.FindByPredicate([](const FClipTake& T) { return T.Id == TEXT("take1"); });
	TestTrue(TEXT("what the range held is kept as a take"), Take && Take->Lanes.Num() == 3);
	FClip Back;
	TArray<FClipIssue> BackIssues;
	TestTrue(TEXT("the take reads back from JSON"), ClipFromJson(ClipToJson(Clip), Back, BackIssues) && Back.Takes.Num() == 1 && Back.Takes[0].Lanes.Num() == 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPIEClipLaneDriversTest, "UE.MCP.PIE.Clip.Lanes.Drivers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEClipLaneDriversTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEClipLaneTests;
	// One driver per kind, each declaring what it takes.
	FClipDriverContext Ctx;
	TUniquePtr<FClipPerformerDriver> Player = MakeClipPerformerDriver(FClipPerformer::Player(0), Ctx);
	TUniquePtr<FClipPerformerDriver> Actor = MakeClipPerformerDriver(FClipPerformer::ForActor(TEXT("UEMCP_LaneActor")), Ctx);
	if (!TestTrue(TEXT("a driver for each kind"), Player.IsValid() && Actor.IsValid())) return false;
	TestTrue(TEXT("the player driver takes input"), Player->Accepts(EClipLaneCapability::Input));
	TestFalse(TEXT("the player driver takes no move"), Player->Accepts(EClipLaneCapability::Move));
	TestTrue(TEXT("the actor driver takes move"), Actor->Accepts(EClipLaneCapability::Move));
	FString Err;
	FClipMovePoint Pt;
	TestFalse(TEXT("a player refuses a move"), Player->MoveTo(nullptr, Pt, Err));

	if (!GEditor) return true;
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("UEMCP_ClipLaneTest")), GetTransientPackage());
	if (!TestNotNull(TEXT("a disposable world"), World)) return false;
	// An actor with a root component, so it has a place to stand.
	AActor* Target = World->SpawnActor<AStaticMeshActor>(FVector(10, 20, 30), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an actor to drive"), Target)) { MCPDestroyDisposableWorld(World); return false; }
	Target->SetActorLabel(TEXT("UEMCP_LaneActor"));

	TestTrue(TEXT("the actor driver finds its actor by label"), Actor->ResolveActor(World, Err) == Target);
	FVector Where;
	TestTrue(TEXT("it reads where the actor stands"), Actor->ReadLocation(World, Where, Err) && Where.Equals(FVector(10, 20, 30), 0.01));

	// A value lane writes and reads back a property.
	FClipLane Dilation;
	Dilation.Capability = EClipLaneCapability::Value;
	Dilation.Target = TEXT("CustomTimeDilation");
	TestTrue(TEXT("a value lane writes the property"), Actor->SetValue(World, Dilation, 0.25, Err));
	double Read = 0.0;
	TestTrue(TEXT("and reads it back"), Actor->ReadValue(World, Dilation, Read, Err) && FMath::IsNearlyEqual(Read, 0.25, 1e-4));
	FClipLane Board = Dilation;
	Board.Target = TEXT("bb:Alert");
	TestFalse(TEXT("a blackboard key needs an AI controller"), Actor->SetValue(World, Board, 1.0, Err));

	// An action lane calls a function, with its arguments.
	FClipActionCall Hide;
	Hide.Function = TEXT("SetActorHiddenInGame");
	Hide.Args.Add(MakeShared<FJsonValueBoolean>(true));
	TestTrue(TEXT("an action lane calls the function"), Actor->Call(World, Hide, Err));
	TestTrue(TEXT("and the call took effect"), Target->IsHidden());
	TestFalse(TEXT("a move lane needs a pawn"), Actor->MoveTo(World, Pt, Err));

	// Recording an action lane: a call through ProcessEvent is seen with its arguments, and the function is as it
	// was once the take ends.
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
		// An editor world's actors take script calls only under the guard, as a PIE world's take them always.
		FEditorScriptExecutionGuard Guard;
		Target->ProcessEvent(Fn, Parms.GetData());
	}
	const TMap<FString, TArray<FClipActionCall>> Seen = Recorder.Finish();
	const TArray<FClipActionCall>* Calls = Seen.Find(TEXT("l1"));
	TestTrue(TEXT("the call was seen on its frame"), Calls && Calls->Num() == 1 && FMath::IsNearlyEqual((*Calls)[0].Time, 12.0));
	TestTrue(TEXT("with its argument"), Calls && Calls->Num() == 1 && (*Calls)[0].Args.Num() == 1 && (*Calls)[0].Args[0]->Type == EJson::Boolean && !(*Calls)[0].Args[0]->AsBool());
	TestFalse(TEXT("the call still ran"), Target->IsHidden());
	TestTrue(TEXT("the function is as it was"), Fn && Fn->GetNativeFunc() == Original);
	TestFalse(TEXT("nothing is watched after the take"), Recorder.IsWatching());

	MCPDestroyDisposableWorld(World);
	return true;
}

#endif
