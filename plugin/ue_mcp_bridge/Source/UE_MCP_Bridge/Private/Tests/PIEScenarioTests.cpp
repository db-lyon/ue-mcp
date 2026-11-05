// Automation coverage for the declarative scenario (Roadmap v2, F2):
// parse + validate a well-formed scenario, and reject a malformed one with a
// message per problem. Pure (no PIE); live ApplyArrange is covered once F3 can
// drive it in a real game.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEScenario.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEScenarioTest,
	"UE.MCP.PIE.Scenario.ParseValidate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	TSharedPtr<FJsonObject> FromString(const FString& S)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(S);
		FJsonSerializer::Deserialize(R, Obj);
		return Obj;
	}
}

bool FPIEScenarioTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;

	// Well-formed: spawn/set/call arrange, a replay act, one valid predicate.
	{
		const FString Json = TEXT("{")
			TEXT("\"name\":\"boss-fight\",")
			TEXT("\"arrange\":[")
			TEXT("{\"kind\":\"spawn\",\"class\":\"/Game/BP_Boss.BP_Boss_C\",\"at\":[1200,0,90]},")
			TEXT("{\"kind\":\"set\",\"target\":\"boss\",\"path\":\"Health\",\"value\":500},")
			TEXT("{\"kind\":\"call\",\"target\":\"boss\",\"func\":\"StartPhase\",\"args\":[2]}")
			TEXT("],")
			TEXT("\"act\":[{\"kind\":\"replay\",\"recording_dir\":\"/x\"}],")
			TEXT("\"assert\":[{\"name\":\"floor\",\"channel\":\"pos_z\",\"op\":\"gte\",\"value\":-50,\"hold\":\"always\"}]")
			TEXT("}");
		FScenario S; FString Err;
		TestTrue(TEXT("parse ok"), FPIEScenario::Parse(FromString(Json), S, Err));
		TestEqual(TEXT("arrange count"), S.Arrange.Num(), 3);
		TestEqual(TEXT("act count"), S.Act.Num(), 1);
		TestEqual(TEXT("assert count"), S.Assert.Num(), 1);
		TArray<FString> Errors;
		const bool bValid = FPIEScenario::Validate(S, Errors);
		TestTrue(FString::Printf(TEXT("valid (%d errors)"), Errors.Num()), bValid);

		// Round-trip: ToJson preserves the three legs.
		TSharedRef<FJsonObject> Back = FPIEScenario::ToJson(S);
		FScenario S2; FString Err2;
		TestTrue(TEXT("re-parse ok"), FPIEScenario::Parse(Back, S2, Err2));
		TestEqual(TEXT("round-trip arrange"), S2.Arrange.Num(), 3);
		TestEqual(TEXT("round-trip assert"), S2.Assert.Num(), 1);
	}

	// Malformed: missing name, set missing path/value, unknown act kind, bad predicate.
	{
		const FString Json = TEXT("{")
			TEXT("\"name\":\"\",")
			TEXT("\"arrange\":[{\"kind\":\"set\",\"target\":\"boss\"}],")
			TEXT("\"act\":[{\"kind\":\"teleport\"}],")
			TEXT("\"assert\":[{\"channel\":\"pos_z\"}]")
			TEXT("}");
		FScenario S; FString Err;
		TestTrue(TEXT("parse ok (bad doc still parses)"), FPIEScenario::Parse(FromString(Json), S, Err));
		TArray<FString> Errors;
		const bool bValid = FPIEScenario::Validate(S, Errors);
		TestFalse(TEXT("invalid"), bValid);
		// Expect at least: name, set.path, set.value, act.kind, assert(op).
		TestTrue(FString::Printf(TEXT("multiple problems reported (%d)"), Errors.Num()), Errors.Num() >= 4);
	}

	// #3: a timed gameplay call as an ACT step. arrange's 'call' fires once
	// during setup; this is the stimulus half of a validation run, so it needs
	// the same per-kind checks - a mistyped target in a timed step otherwise
	// fails by silently never firing.
	{
		const FString Json = TEXT("{")
			TEXT("\"name\":\"climb-traversal\",")
			TEXT("\"arrange\":[{\"kind\":\"spawn\",\"class\":\"/Game/BP_Char.BP_Char_C\"}],")
			TEXT("\"act\":[")
			TEXT("{\"kind\":\"call\",\"at_seconds\":0.0,\"target\":\"char\",\"component\":\"AC_Climb\",\"func\":\"SetClimbMoveInput\",\"args\":[-1]},")
			TEXT("{\"kind\":\"call\",\"at_seconds\":0.8,\"target\":\"char\",\"component\":\"AC_Climb\",\"func\":\"StopClimb\"}")
			TEXT("],")
			TEXT("\"assert\":[{\"name\":\"floor\",\"channel\":\"pos_z\",\"op\":\"gte\",\"value\":-50,\"hold\":\"always\"}]")
			TEXT("}");
		FScenario S; FString Err;
		TestTrue(TEXT("timed-call scenario parses"), FPIEScenario::Parse(FromString(Json), S, Err));
		TestEqual(TEXT("act count"), S.Act.Num(), 2);
		TArray<FString> Errors;
		TestTrue(FString::Printf(TEXT("timed-call scenario valid (%d errors)"), Errors.Num()),
			FPIEScenario::Validate(S, Errors));
	}

	// A call act step missing target/func must be rejected, not accepted as a
	// merely "known kind" the way act steps used to be.
	{
		const FString Json = TEXT("{")
			TEXT("\"name\":\"bad-call\",")
			TEXT("\"arrange\":[],")
			TEXT("\"act\":[{\"kind\":\"call\",\"at_seconds\":-1}],")
			TEXT("\"assert\":[{\"name\":\"floor\",\"channel\":\"pos_z\",\"op\":\"gte\",\"value\":-50,\"hold\":\"always\"}]")
			TEXT("}");
		FScenario S; FString Err;
		TestTrue(TEXT("parse ok"), FPIEScenario::Parse(FromString(Json), S, Err));
		TArray<FString> Errors;
		TestFalse(TEXT("invalid"), FPIEScenario::Validate(S, Errors));
		// target, func and the negative at_seconds.
		TestTrue(FString::Printf(TEXT("call problems reported (%d)"), Errors.Num()), Errors.Num() >= 3);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
