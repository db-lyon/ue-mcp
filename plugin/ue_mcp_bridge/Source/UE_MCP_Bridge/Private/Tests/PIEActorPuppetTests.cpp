// Automation coverage for the Arrange substrate (Roadmap v2, F1):
// SetPropertyByPath writes float/bool UProperties by reflection on a transient
// object, and reports a clear error for an unresolved path. Spawn/call need a
// live PIE world and are exercised via the scenario tests once F2 lands.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEActorPuppet.h"
#include "Handlers/PIE/MCPPIEObservationProfile.h"
#include "Dom/JsonValue.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEActorPuppetTest,
	"UE.MCP.PIE.Arrange.SetPropertyByPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEActorPuppetTest::RunTest(const FString& /*Parameters*/)
{
	using namespace MCPPIE;

	UMCPPIEObservationProfile* Obj = NewObject<UMCPPIEObservationProfile>(GetTransientPackage());
	TestNotNull(TEXT("transient profile"), Obj);
	if (!Obj) return false;

	Obj->PositionThresholdCm = 1.0f;
	Obj->bCapturePawnState = false;

	FString Err;

	// Float write.
	const bool bFloat = FPIEActorPuppet::SetPropertyByPath(
		Obj, TEXT("PositionThresholdCm"), MakeShared<FJsonValueNumber>(42.5), Err);
	TestTrue(FString::Printf(TEXT("float write ok (%s)"), *Err), bFloat);
	TestTrue(TEXT("float applied"), FMath::IsNearlyEqual(Obj->PositionThresholdCm, 42.5f));

	// Bool write.
	const bool bBool = FPIEActorPuppet::SetPropertyByPath(
		Obj, TEXT("bCapturePawnState"), MakeShared<FJsonValueBoolean>(true), Err);
	TestTrue(FString::Printf(TEXT("bool write ok (%s)"), *Err), bBool);
	TestTrue(TEXT("bool applied"), Obj->bCapturePawnState);

	// Unresolved path fails with a message.
	Err.Reset();
	const bool bBad = FPIEActorPuppet::SetPropertyByPath(
		Obj, TEXT("NoSuchProperty"), MakeShared<FJsonValueNumber>(1), Err);
	TestFalse(TEXT("unknown path fails"), bBad);
	TestTrue(TEXT("error message set"), !Err.IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
