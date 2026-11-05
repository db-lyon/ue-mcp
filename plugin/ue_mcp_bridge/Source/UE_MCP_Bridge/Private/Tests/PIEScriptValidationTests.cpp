// Automation coverage for the draft-time checks pie_script_create and
// pie_script_update run: step parse issues, value resolution against a class,
// op and target typing, the auto-step rule and the structured refusal.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEScriptValidation.h"
#include "Dom/JsonObject.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MCPPIEScriptDraftTests
{
	using namespace MCPPIE;

	TArray<TSharedPtr<FJsonValue>> ParseJsonArray(const FString& Text)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Out);
		return Out;
	}

	FScriptLookup Found(EScriptValueKind Kind, const TCHAR* Type, double Min = 0.0, double Max = 0.0)
	{
		FScriptLookup L;
		L.bFound = true;
		L.Info.Kind = Kind;
		L.Info.TypeName = Type;
		L.Info.Min = Min;
		L.Info.Max = Max;
		return L;
	}

	FScriptLookup Missing(const TCHAR* Message, const TArray<FString>& Candidates)
	{
		FScriptLookup L;
		L.Message = Message;
		L.Candidates = Candidates;
		return L;
	}

	// A level with a pawn that has Health (float), bAlive (bool), Mode (enum 0..2) and Ammo (int32),
	// an actor Door, and a widget WBP_Hud. Records every path it was asked to resolve.
	FScriptValidationContext MakeContext(TArray<FString>* Asked = nullptr)
	{
		FScriptValidationContext Ctx;
		Ctx.ResolveValue = [Asked](const FString& Path) -> FScriptLookup
		{
			if (Asked) Asked->Add(Path);
			if (Path == TEXT("Health") || Path == TEXT("AbilitySystem.Health")) return Found(EScriptValueKind::Number, TEXT("float"));
			if (Path == TEXT("bAlive")) return Found(EScriptValueKind::Boolean, TEXT("bool"));
			if (Path == TEXT("Ammo")) return Found(EScriptValueKind::Integer, TEXT("int32"), MIN_int32, MAX_int32);
			if (Path == TEXT("Mode"))
			{
				FScriptLookup L = Found(EScriptValueKind::Enum, TEXT("EMode"));
				L.Info.EnumValues = { TPair<FString, int64>(TEXT("Idle"), 0), TPair<FString, int64>(TEXT("Run"), 1), TPair<FString, int64>(TEXT("Jump"), 2) };
				return L;
			}
			return Missing(TEXT("not on the pawn"), { TEXT("Health"), TEXT("Ammo") });
		};
		Ctx.ResolveActor = [](const FString& Id) -> FScriptLookup
		{
			return Id == TEXT("Door") ? Found(EScriptValueKind::Boolean, TEXT("actor")) : Missing(TEXT("no such actor"), { TEXT("Door") });
		};
		Ctx.ResolveWidget = [](const FString& W) -> FScriptLookup
		{
			return W == TEXT("WBP_Hud") ? Found(EScriptValueKind::Boolean, TEXT("widget")) : Missing(TEXT("no such widget"), { TEXT("WBP_Hud") });
		};
		return Ctx;
	}

	FScriptStepDef Auto(EScriptExpectOp Op, const FString& Value, bool bHasTarget = false, double Target = 0.0, bool bTargetIsBool = false)
	{
		FScriptStepDef S;
		S.Instruction = TEXT("auto");
		S.bHasExpect = true;
		S.Expect.Op = Op;
		S.Expect.Value = Value;
		S.Expect.bHasTarget = bHasTarget;
		S.Expect.Target = Target;
		S.Expect.bTargetIsBool = bTargetIsBool;
		return S;
	}

	FScriptStepDef Manual()
	{
		FScriptStepDef S;
		S.Instruction = TEXT("manual");
		S.Judge = EScriptJudge::Manual;
		return S;
	}

	TArray<FScriptStepIssue> Check(const TArray<FScriptStepDef>& Steps, bool bManualOnly = false, const FScriptValidationContext* Ctx = nullptr)
	{
		TArray<FScriptStepIssue> Issues;
		ValidateScriptSteps(Steps, bManualOnly, Ctx ? *Ctx : MakeContext(), Issues);
		return Issues;
	}

	bool HasIssue(const TArray<FScriptStepIssue>& Issues, int32 Step, const FString& Field)
	{
		return Issues.ContainsByPredicate([&](const FScriptStepIssue& I) { return I.Step == Step && I.Field == Field; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptDraftParseTest,
	"UE.MCP.PIE.Script.DraftChecks.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptDraftParseTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptDraftTests;

	TArray<FScriptStepDef> Steps;
	TArray<FScriptStepIssue> Issues;
	const bool bOk = ParseScriptStepsJsonIssues(ParseJsonArray(TEXT(
		"[{\"instruction\":\"ok\",\"expect\":{\"value\":\"Health\",\"op\":\"decreasing\"}},"
		"{\"instruction\":\"x\",\"expect\":{\"value\":\"Health\",\"op\":\"gtt\",\"target\":1}},"
		"{\"instruction\":\"x\",\"judge\":\"robot\"},"
		"{\"instruction\":\"x\",\"resolve_in_pie\":true},"
		"{\"instruction\":\"x\",\"resolve_in_pie\":\"yes\",\"expect\":{\"value\":\"A\",\"op\":\"exists\"}}]")), Steps, Issues);
	TestFalse(TEXT("bad steps refused"), bOk);
	TestTrue(TEXT("a step that does not parse blocks a save"),
		Issues.Num() > 0 && Issues.FilterByPredicate([](const FScriptStepIssue& I) { return !I.bBlocksSave; }).Num() == 0);
	TestEqual(TEXT("indices line up with the input"), Steps.Num(), 5);
	TestEqual(TEXT("one issue per bad step"), Issues.Num(), 4);
	TestTrue(TEXT("unknown op names its field"), HasIssue(Issues, 1, TEXT("expect.op")));
	const FScriptStepIssue* OpIssue = Issues.FindByPredicate([](const FScriptStepIssue& I) { return I.Step == 1; });
	TestTrue(TEXT("unknown op offers the closest ops"), OpIssue && OpIssue->Candidates.Num() > 0 && OpIssue->Candidates[0] == TEXT("gt"));
	TestTrue(TEXT("bad judge names its field"), HasIssue(Issues, 2, TEXT("judge")));
	TestTrue(TEXT("resolve_in_pie without expect refused"), HasIssue(Issues, 3, TEXT("resolve_in_pie")));
	TestTrue(TEXT("resolve_in_pie must be a boolean"), HasIssue(Issues, 4, TEXT("resolve_in_pie")));

	Issues.Reset();
	TestFalse(TEXT("empty steps refused"), ParseScriptStepsJsonIssues(TArray<TSharedPtr<FJsonValue>>(), Steps, Issues));
	TestTrue(TEXT("empty steps is a script-level issue"), HasIssue(Issues, INDEX_NONE, TEXT("steps")));
	TestTrue(TEXT("empty steps blocks a save"), Issues.Num() > 0 && Issues.Last().bBlocksSave);

	// resolve_in_pie and a boolean target survive a round trip through JSON.
	Issues.Reset();
	TestTrue(TEXT("flagged step parses"), ParseScriptStepsJsonIssues(ParseJsonArray(TEXT(
		"[{\"instruction\":\"spawned\",\"resolve_in_pie\":true,\"expect\":{\"value\":\"BP_Spawned\",\"op\":\"exists\",\"target\":true}}]")), Steps, Issues));
	TestTrue(TEXT("resolve_in_pie read"), Steps.Num() == 1 && Steps[0].bResolveInPIE);
	TestTrue(TEXT("boolean target marked"), Steps.Num() == 1 && Steps[0].Expect.bTargetIsBool);
	if (Steps.Num() == 1)
	{
		bool bFlag = false;
		TestTrue(TEXT("resolve_in_pie written back"), ScriptStepDefToJson(Steps[0])->TryGetBoolField(TEXT("resolve_in_pie"), bFlag) && bFlag);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptDraftTypingTest,
	"UE.MCP.PIE.Script.DraftChecks.Typing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptDraftTypingTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptDraftTests;

	TestEqual(TEXT("valid steps pass"), Check({
		Auto(EScriptExpectOp::Decreasing, TEXT("Health")),
		Auto(EScriptExpectOp::Gt, TEXT("Ammo"), true, 0.0),
		Auto(EScriptExpectOp::Eq, TEXT("bAlive"), true, 1.0, true),
		Auto(EScriptExpectOp::Eq, TEXT("Mode"), true, 2.0),
		Auto(EScriptExpectOp::Exists, TEXT("Door")),
		Auto(EScriptExpectOp::Visible, TEXT("WBP_Hud"), true, 0.0, true),
		Manual(),
	}).Num(), 0);

	// An unknown value names the step, the field and the resolver's candidates.
	TArray<FScriptStepIssue> Issues = Check({ Manual(), Auto(EScriptExpectOp::Gt, TEXT("Helth"), true, 0.0) });
	TestTrue(TEXT("unknown value refused at its index"), HasIssue(Issues, 1, TEXT("expect.value")));
	TestTrue(TEXT("message names the value"), Issues.Num() > 0 && Issues[0].Message.Contains(TEXT("Helth")));
	TestTrue(TEXT("message points at resolve_in_pie"), Issues.Num() > 0 && Issues[0].Message.Contains(TEXT("resolve_in_pie")));
	TestTrue(TEXT("candidates carried"), Issues.Num() > 0 && Issues[0].Candidates.Contains(TEXT("Health")));

	// resolve_in_pie skips resolution, for values and for actors.
	FScriptStepDef Deferred = Auto(EScriptExpectOp::Gt, TEXT("SpawnedThing.Count"), true, 0.0);
	Deferred.bResolveInPIE = true;
	FScriptStepDef DeferredActor = Auto(EScriptExpectOp::Exists, TEXT("BP_Spawned"));
	DeferredActor.bResolveInPIE = true;
	TestEqual(TEXT("resolve_in_pie is not resolved"), Check({ Deferred, DeferredActor }).Num(), 0);
	TestTrue(TEXT("unknown actor refused"), HasIssue(Check({ Auto(EScriptExpectOp::Exists, TEXT("BP_Spawned")) }), 0, TEXT("expect.value")));
	TestTrue(TEXT("unknown widget refused"), HasIssue(Check({ Auto(EScriptExpectOp::Visible, TEXT("WBP_Missing")) }), 0, TEXT("expect.value")));

	// A profile tracked value is read by its last segment, the way the observer reads it.
	TArray<FString> Asked;
	FScriptValidationContext Profiled = MakeContext(&Asked);
	Profiled.ProfileValuePaths = { TEXT("AbilitySystem.Health") };
	TestEqual(TEXT("profile value passes"), Check({ Auto(EScriptExpectOp::Decreasing, TEXT("health")) }, false, &Profiled).Num(), 0);
	TestTrue(TEXT("profile path resolved"), Asked.Contains(TEXT("AbilitySystem.Health")));

	// Ops and targets against the value's type.
	TestTrue(TEXT("gt on a bool refused"), HasIssue(Check({ Auto(EScriptExpectOp::Gt, TEXT("bAlive"), true, 0.0) }), 0, TEXT("expect.op")));
	TestTrue(TEXT("decreasing on a bool refused"), HasIssue(Check({ Auto(EScriptExpectOp::Decreasing, TEXT("bAlive")) }), 0, TEXT("expect.op")));
	TestTrue(TEXT("eq 2 on a bool refused"), HasIssue(Check({ Auto(EScriptExpectOp::Eq, TEXT("bAlive"), true, 2.0) }), 0, TEXT("expect.target")));
	TestEqual(TEXT("eq 0 on a bool passes"), Check({ Auto(EScriptExpectOp::Eq, TEXT("bAlive"), true, 0.0) }).Num(), 0);
	TestTrue(TEXT("lt on an enum refused"), HasIssue(Check({ Auto(EScriptExpectOp::Lt, TEXT("Mode"), true, 1.0) }), 0, TEXT("expect.op")));
	Issues = Check({ Auto(EScriptExpectOp::Eq, TEXT("Mode"), true, 5.0) });
	TestTrue(TEXT("eq outside an enum refused"), HasIssue(Issues, 0, TEXT("expect.target")));
	TestTrue(TEXT("enum values offered"), Issues.Num() > 0 && Issues[0].Candidates.Contains(TEXT("2 (Jump)")));
	TestTrue(TEXT("boolean target on a float refused"), HasIssue(Check({ Auto(EScriptExpectOp::Gt, TEXT("Health"), true, 1.0, true) }), 0, TEXT("expect.target")));
	TestTrue(TEXT("fractional eq on an int refused"), HasIssue(Check({ Auto(EScriptExpectOp::Eq, TEXT("Ammo"), true, 2.5) }), 0, TEXT("expect.target")));
	TestTrue(TEXT("gt past an int's range refused"), HasIssue(Check({ Auto(EScriptExpectOp::Gt, TEXT("Ammo"), true, static_cast<double>(MAX_int32)) }), 0, TEXT("expect.target")));
	TestTrue(TEXT("exists target 2 refused"), HasIssue(Check({ Auto(EScriptExpectOp::Exists, TEXT("Door"), true, 2.0) }), 0, TEXT("expect.target")));

	// A script needs an auto step with an expect, unless it is manual only.
	Issues = Check({ Manual(), Manual() });
	TestTrue(TEXT("no auto step refused"), HasIssue(Issues, INDEX_NONE, TEXT("steps")));
	TestEqual(TEXT("manual_only allows it"), Check({ Manual() }, true).Num(), 0);
	FScriptStepDef Bare;
	Bare.Instruction = TEXT("walk somewhere");
	TestTrue(TEXT("an auto step without expect checks nothing"), HasIssue(Check({ Bare, Manual() }), INDEX_NONE, TEXT("steps")));

	// Steps that failed to parse are skipped, and the auto-step rule waits for them.
	TArray<FScriptStepIssue> Skipped;
	ValidateScriptSteps({ Auto(EScriptExpectOp::Gt, TEXT("Nope"), true, 1.0), Manual() }, false, MakeContext(), Skipped, { 0 });
	TestEqual(TEXT("skipped steps are not checked"), Skipped.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptDraftResolveTest,
	"UE.MCP.PIE.Script.DraftChecks.ResolveOnClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptDraftResolveTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptDraftTests;

	UClass* Pawn = APawn::StaticClass();
	const UObject* PawnCDO = Pawn->GetDefaultObject();
	FScriptLookup L = ResolveScriptPathOnClass(Pawn, PawnCDO, TEXT("BaseEyeHeight"));
	TestTrue(TEXT("float property found"), L.bFound && L.Info.Kind == EScriptValueKind::Number);
	TestTrue(TEXT("inherited actor property found"), ResolveScriptPathOnClass(Pawn, PawnCDO, TEXT("CustomTimeDilation")).bFound);
	L = ResolveScriptPathOnClass(Pawn, PawnCDO, TEXT("BaseEyeHieght"));
	TestFalse(TEXT("misspelt property refused"), L.bFound);
	TestTrue(TEXT("closest property offered first"), L.Candidates.Num() > 0 && L.Candidates[0] == TEXT("BaseEyeHeight"));
	L = ResolveScriptPathOnClass(Pawn, PawnCDO, TEXT("bCanBeDamaged"));
	TestTrue(TEXT("bool property typed"), L.bFound && L.Info.Kind == EScriptValueKind::Boolean);

	UClass* Character = ACharacter::StaticClass();
	const UObject* CharacterCDO = Character->GetDefaultObject();
	TestTrue(TEXT("object property followed through the default subobject"),
		ResolveScriptPathOnClass(Character, CharacterCDO, TEXT("CharacterMovement.MaxWalkSpeed")).bFound);
	L = ResolveScriptPathOnClass(Character, CharacterCDO, TEXT("CharacterMovement.MovementMode"));
	TestTrue(TEXT("enum byte property typed"), L.bFound && L.Info.Kind == EScriptValueKind::Enum && L.Info.EnumValues.Num() > 0);
	L = ResolveScriptPathOnClass(Character, CharacterCDO, TEXT("CharacterMovement.Velocity"));
	TestFalse(TEXT("struct leaf refused"), L.bFound);
	TestTrue(TEXT("struct leaf offers its fields"), L.Candidates.Contains(TEXT("CharacterMovement.Velocity.X")));
	TestTrue(TEXT("struct field found"), ResolveScriptPathOnClass(Character, CharacterCDO, TEXT("CharacterMovement.Velocity.X")).bFound);
	TestTrue(TEXT("component name at the head"),
		ResolveScriptPathOnClass(Character, CharacterCDO, TEXT("CollisionCylinder.CapsuleHalfHeight")).bFound);
	L = ResolveScriptPathOnClass(Character, CharacterCDO, TEXT("CharacterMovement.MaxWalkSped"));
	TestFalse(TEXT("misspelt nested property refused"), L.bFound);
	TestTrue(TEXT("nested candidate keeps its prefix"), L.Candidates.Contains(TEXT("CharacterMovement.MaxWalkSpeed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptDraftRefusalTest,
	"UE.MCP.PIE.Script.DraftChecks.Refusal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptDraftRefusalTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptDraftTests;

	TArray<FScriptStepIssue> Issues = Check({ Auto(EScriptExpectOp::Gt, TEXT("Helth"), true, 0.0) });
	Issues.Append(Check({ Manual() }));
	TArray<FScriptStepIssue> Blocking, Warnings;
	SplitScriptIssues(Issues, Blocking, Warnings);
	TestEqual(TEXT("an unresolved value and no auto step do not block a save"), Blocking.Num(), 0);
	TestEqual(TEXT("both are warnings"), Warnings.Num(), 2);
	const TSharedPtr<FJsonObject> Obj = ScriptIssuesErrorObject(Issues);
	bool bSuccess = true;
	TestTrue(TEXT("success false"), Obj->TryGetBoolField(TEXT("success"), bSuccess) && !bSuccess);
	TestEqual(TEXT("errorCode"), Obj->GetStringField(TEXT("errorCode")), FString(TEXT("INVALID_SCRIPT_STEPS")));
	TestTrue(TEXT("error summarises"), Obj->GetStringField(TEXT("error")).Contains(TEXT("Helth")));
	const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
	TestTrue(TEXT("errors array"), Obj->TryGetArrayField(TEXT("errors"), Errors) && Errors && Errors->Num() == 2);
	if (Errors && Errors->Num() == 2)
	{
		const TSharedPtr<FJsonObject> First = (*Errors)[0]->AsObject();
		TestEqual(TEXT("step index"), First->GetNumberField(TEXT("step")), 0.0);
		TestEqual(TEXT("field"), First->GetStringField(TEXT("field")), FString(TEXT("expect.value")));
		TestTrue(TEXT("candidates"), First->HasTypedField<EJson::Array>(TEXT("candidates")));
		const TSharedPtr<FJsonObject> Second = (*Errors)[1]->AsObject();
		TestTrue(TEXT("script-level step is null"), Second->HasTypedField<EJson::Null>(TEXT("step")));
		TestEqual(TEXT("script-level field"), Second->GetStringField(TEXT("field")), FString(TEXT("steps")));
	}
	TestEqual(TEXT("ranking ignores case and prefers containment"),
		RankScriptCandidates(TEXT("health"), { TEXT("Stamina"), TEXT("MaxHealth"), TEXT("Wealth") }, 1)[0], FString(TEXT("MaxHealth")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScriptDraftArrangeActTest,
	"UE.MCP.PIE.Script.DraftChecks.ArrangeAct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEScriptDraftArrangeActTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIEScriptDraftTests;

	FScriptValidationContext Ctx = MakeContext();
	Ctx.ResolveInputAction = [](const FString& Path) -> FScriptLookup
	{
		return Path == TEXT("/Game/Input/IA_Interact.IA_Interact") ? Found(EScriptValueKind::Boolean, TEXT("InputAction"))
			: Missing(TEXT("no Input Action loads from that path"), { TEXT("/Game/Input/IA_Interact.IA_Interact") });
	};
	Ctx.ResolveActorClass = [](const FString& Path) -> FScriptLookup
	{
		return Path == TEXT("/Script/Engine.StaticMeshActor") ? Found(EScriptValueKind::Boolean, TEXT("class")) : Missing(TEXT("class not found"), {});
	};

	auto Parse = [this](const TCHAR* Json) -> TArray<FScriptStepDef>
	{
		TArray<FScriptStepDef> Steps;
		TArray<FScriptStepIssue> ParseIssues;
		TestTrue(TEXT("steps parse"), ParseScriptStepsJsonIssues(ParseJsonArray(Json), Steps, ParseIssues));
		return Steps;
	};

	// The door script names only what the level and the project hold.
	TArray<FScriptStepDef> Door = Parse(TEXT(
		"[{\"instruction\":\"Open the door\",\"arrange\":[{\"kind\":\"set\",\"target\":\"Door\",\"path\":\"OpenAngle\",\"value\":0}],"
		"\"act\":[{\"place\":{\"at\":\"Door\"}},{\"press\":{\"action\":\"/Game/Input/IA_Interact.IA_Interact\"}}],"
		"\"expect\":{\"value\":\"Health\",\"op\":\"gt\",\"target\":1}}]"));
	TestEqual(TEXT("a resolvable door script has no issues"), Check(Door, false, &Ctx).Num(), 0);

	// An unknown actor, Input Action or class is refused at draft time, each with its field and candidates.
	TArray<FScriptStepDef> Bad = Parse(TEXT(
		"[{\"instruction\":\"x\",\"arrange\":[{\"kind\":\"set\",\"target\":\"Dor\",\"path\":\"OpenAngle\",\"value\":0},{\"kind\":\"spawn\",\"class\":\"/Game/Nope.Nope_C\"}],"
		"\"act\":[{\"place\":{\"at\":\"Dor\"}},{\"press\":{\"action\":\"/Game/Input/IA_Interac\"}}],"
		"\"expect\":{\"value\":\"Health\",\"op\":\"gt\",\"target\":1}},"
		"{\"instruction\":\"y\",\"judge\":\"manual\",\"act\":[{\"inject\":{\"action\":\"/Game/Input/IA_Missing\",\"value\":1}}]}]"));
	const TArray<FScriptStepIssue> Issues = Check(Bad, false, &Ctx);
	TestTrue(TEXT("unknown arrange target"), HasIssue(Issues, 0, TEXT("arrange[0].target")));
	TestTrue(TEXT("unknown spawn class"), HasIssue(Issues, 0, TEXT("arrange[1].class")));
	TestTrue(TEXT("unknown place actor"), HasIssue(Issues, 0, TEXT("act[0].place.at")));
	TestTrue(TEXT("unknown pressed action"), HasIssue(Issues, 0, TEXT("act[1].press.action")));
	TestTrue(TEXT("a manual step's act is checked too"), HasIssue(Issues, 1, TEXT("act[0].inject.action")));
	const FScriptStepIssue* PlaceIssue = Issues.FindByPredicate([](const FScriptStepIssue& I) { return I.Field == TEXT("act[0].place.at"); });
	TestTrue(TEXT("the actor issue offers the level's actors"), PlaceIssue && PlaceIssue->Candidates.Contains(TEXT("Door")));
	TestTrue(TEXT("the message starts with the step and field"), PlaceIssue && PlaceIssue->Message.StartsWith(TEXT("steps[0].act[0].place.at:")));

	// resolve_in_pie defers the actors, never the assets.
	TArray<FScriptStepDef> Deferred = Parse(TEXT(
		"[{\"instruction\":\"x\",\"resolve_in_pie\":true,\"act\":[{\"place\":{\"at\":\"BP_Spawned_C_0\"}},{\"press\":{\"action\":\"/Game/Input/IA_Missing\"}}],"
		"\"expect\":{\"value\":\"Health\",\"op\":\"gt\",\"target\":1}}]"));
	const TArray<FScriptStepIssue> DeferredIssues = Check(Deferred, false, &Ctx);
	TestFalse(TEXT("a deferred actor is not resolved"), HasIssue(DeferredIssues, 0, TEXT("act[0].place.at")));
	TestTrue(TEXT("a missing Input Action is still refused"), HasIssue(DeferredIssues, 0, TEXT("act[1].press.action")));
	return true;
}

#endif
