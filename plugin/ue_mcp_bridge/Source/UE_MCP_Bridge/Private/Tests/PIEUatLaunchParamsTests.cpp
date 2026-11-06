// #1526: an acceptance run must not grow the launch parameters an earlier run leaked, and must leave
// everything the user wrote in place.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/PIE/PIEUatRunner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEUatStripLaunchParametersTest,
	"UE.MCP.PIE.Uat.StripLaunchParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEUatStripLaunchParametersTest::RunTest(const FString& /*Parameters*/)
{
	using MCPPIE::StripUatLaunchParameters;

	TestEqual(TEXT("empty stays empty"), StripUatLaunchParameters(TEXT("")), FString());

	TestEqual(TEXT("one run's parameters are removed"),
		StripUatLaunchParameters(TEXT("-ExecCmds=\"t.MaxFPS 60,t.IdleWhenNotForeground 0\" -RenderOffscreen")),
		FString());

	TestEqual(TEXT("parameters leaked by three runs are all removed"),
		StripUatLaunchParameters(TEXT("-ExecCmds=\"t.MaxFPS 60,t.IdleWhenNotForeground 0,t.MaxFPS 60,t.IdleWhenNotForeground 0,t.MaxFPS 60,t.IdleWhenNotForeground 0\" -RenderOffscreen -RenderOffscreen -RenderOffscreen")),
		FString());

	TestEqual(TEXT("the user's own commands and flags survive"),
		StripUatLaunchParameters(TEXT("-log -ExecCmds=\"t.MaxFPS 60,t.IdleWhenNotForeground 0,stat fps\" -RenderOffscreen")),
		FString(TEXT("-log -ExecCmds=\"stat fps\"")));

	TestEqual(TEXT("a user's -RenderOffscreen with no run parameters is kept"),
		StripUatLaunchParameters(TEXT("-RenderOffscreen -log")),
		FString(TEXT("-RenderOffscreen -log")));

	TestEqual(TEXT("only as many -RenderOffscreen go as run entries"),
		StripUatLaunchParameters(TEXT("-RenderOffscreen -ExecCmds=\"t.MaxFPS 30,t.IdleWhenNotForeground 0\" -RenderOffscreen")),
		FString(TEXT("-RenderOffscreen")));

	TestEqual(TEXT("a frame cap the user set alone is not the run's"),
		StripUatLaunchParameters(TEXT("-ExecCmds=\"t.MaxFPS 30\"")),
		FString(TEXT("-ExecCmds=\"t.MaxFPS 30\"")));

	return true;
}

#endif
