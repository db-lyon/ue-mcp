// An editor launched with -MCPNoIde hides the Live Coding console after every
// compile by disabling Live Coding for the session, and keeps new classes and
// hot_reload on Live Coding even though it is then no longer enabled for the
// session. Without the switch, nothing changes.

#if WITH_DEV_AUTOMATION_TESTS

#include "MCPNoIde.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPNoIdeDecisionsTest,
	"UE.MCP.Editor.NoIde.Decisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPNoIdeDecisionsTest::RunTest(const FString& Parameters)
{
	struct FHideCase
	{
		const TCHAR* What;
		bool bNoIde;
		bool bEnabledForSession;
		bool bWasCompiling;
		bool bIsCompiling;
		bool bExpected;
	};
	const FHideCase HideCases[] = {
		{ TEXT("hide: no-IDE, enabled, idle for a whole tick (a waited compile ended inside one call)"), true, true, false, false, true },
		{ TEXT("hide: no-IDE, enabled, compile ended this tick: wait one tick for the patch to load"), true, true, true, false, false },
		{ TEXT("hide: no-IDE, enabled, compiling"), true, true, false, true, false },
		{ TEXT("hide: no-IDE, enabled, still compiling"), true, true, true, true, false },
		{ TEXT("hide: no-IDE, already hidden (not enabled for the session)"), true, false, false, false, false },
		{ TEXT("hide: no-IDE, not enabled, compile ended this tick"), true, false, true, false, false },
		{ TEXT("hide: without the switch, enabled and idle: the console stays"), false, true, false, false, false },
		{ TEXT("hide: without the switch, enabled, compile ended"), false, true, true, false, false },
	};
	for (const FHideCase& Case : HideCases)
	{
		TestEqual(Case.What,
			MCPShouldHideLiveCodingConsole(Case.bNoIde, Case.bEnabledForSession, Case.bWasCompiling, Case.bIsCompiling),
			Case.bExpected);
	}

	struct FLiveCodingStateCase
	{
		const TCHAR* What;
		bool bNoIde;
		bool bHasStarted;
		bool bEnabledForSession;
		bool bEnableBeforeNewClass;
		bool bHotReloadUsesModule;
		bool bReportsEnabledForSession;
	};
	const FLiveCodingStateCase StateCases[] = {
		{ TEXT("no-IDE, started and hidden: enable before a new class, hot_reload uses the module, status reports enabled"), true, true, false, true, true, true },
		{ TEXT("no-IDE, started and enabled: nothing to enable, hot_reload uses the module, status reports enabled"), true, true, true, false, true, true },
		{ TEXT("no-IDE, not started: nothing to enable, hot_reload falls back to the console command, status reports not enabled"), true, false, false, false, false, false },
		{ TEXT("without the switch, started and not enabled: as today, no enable, the fallback and not enabled"), false, true, false, false, false, false },
		{ TEXT("without the switch, started and enabled: as today, the module and enabled"), false, true, true, false, true, true },
		{ TEXT("without the switch, not started: as today, the fallback and not enabled"), false, false, false, false, false, false },
	};
	for (const FLiveCodingStateCase& Case : StateCases)
	{
		TestEqual(FString::Printf(TEXT("enable before new class: %s"), Case.What),
			MCPShouldEnableLiveCodingBeforeNewClass(Case.bNoIde, Case.bHasStarted, Case.bEnabledForSession),
			Case.bEnableBeforeNewClass);
		TestEqual(FString::Printf(TEXT("hot_reload path: %s"), Case.What),
			MCPHotReloadUsesLiveCodingModule(Case.bNoIde, Case.bHasStarted, Case.bEnabledForSession),
			Case.bHotReloadUsesModule);
		TestEqual(FString::Printf(TEXT("live_coding_status enabledForSession: %s"), Case.What),
			MCPReportsLiveCodingEnabledForSession(Case.bNoIde, Case.bHasStarted, Case.bEnabledForSession),
			Case.bReportsEnabledForSession);
	}
	return true;
}

#endif
