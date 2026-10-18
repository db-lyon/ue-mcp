// An offscreen editor turns the background CPU throttle off in memory only.
// If something else in the editor saves the performance settings, the false
// lands in the per-user ini every project on that engine version shares, and
// the bridge puts the user's value back at shutdown. Only that case writes.

#if WITH_DEV_AUTOMATION_TESTS

#include "MCPBackgroundThrottle.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPThrottleRestoreDecisionTest,
	"UE.MCP.Editor.Throttle.RestoreDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPThrottleRestoreDecisionTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* What;
		bool bTurnedOff;
		bool bUserValue;
		bool bConfigHoldsFalse;
		bool bExpected;
	};
	const FCase Cases[] = {
		{ TEXT("turned off, user had it on, config now off: restore"), true, true, true, true },
		{ TEXT("turned off, user had it on, config still on: nothing was saved"), true, true, false, false },
		{ TEXT("turned off, user had it off, config off: the user's own value"), true, false, true, false },
		{ TEXT("turned off, user had it off, config on: not ours to change"), true, false, false, false },
		{ TEXT("not turned off, user on, config off: not this session's doing"), false, true, true, false },
		{ TEXT("not turned off, user on, config on"), false, true, false, false },
		{ TEXT("not turned off, user off, config off"), false, false, true, false },
		{ TEXT("not turned off, user off, config on"), false, false, false, false },
	};

	for (const FCase& Case : Cases)
	{
		TestEqual(Case.What, MCPShouldRestoreThrottle(Case.bTurnedOff, Case.bUserValue, Case.bConfigHoldsFalse), Case.bExpected);
	}
	return true;
}

#endif
