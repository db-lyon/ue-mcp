// launch_standalone_game from an offscreen editor once started a windowed
// game beside it. The game now inherits -RenderOffScreen from the editor,
// unless the caller's extraArgs already hold the switch.

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Editor/HandlerStandaloneArgs.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPStandaloneGameInheritsOffScreenTest,
	"UE.MCP.Editor.LaunchStandaloneGame.InheritsOffScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPStandaloneGameInheritsOffScreenTest::RunTest(const FString& Parameters)
{
	const FString Base = TEXT("\"C:/Projects/Game.uproject\" -game -windowed -ResX=1280 -ResY=720");

	TestEqual(TEXT("an offscreen editor adds the switch"),
		MCPStandaloneGameArgs(Base, FString(), true),
		Base + TEXT(" -RenderOffScreen"));
	TestEqual(TEXT("an offscreen editor adds the switch before the caller's extra arguments"),
		MCPStandaloneGameArgs(Base, TEXT("-log"), true),
		Base + TEXT(" -RenderOffScreen -log"));

	TestEqual(TEXT("a normal editor adds nothing"),
		MCPStandaloneGameArgs(Base, FString(), false),
		Base);
	TestEqual(TEXT("a normal editor appends the caller's extra arguments only"),
		MCPStandaloneGameArgs(Base, TEXT("-log"), false),
		Base + TEXT(" -log"));

	TestEqual(TEXT("extraArgs holding -RenderOffScreen get no duplicate"),
		MCPStandaloneGameArgs(Base, TEXT("-log -RenderOffScreen"), true),
		Base + TEXT(" -log -RenderOffScreen"));
	TestEqual(TEXT("extraArgs holding /renderoffscreen get no duplicate"),
		MCPStandaloneGameArgs(Base, TEXT("/renderoffscreen"), true),
		Base + TEXT(" /renderoffscreen"));

	TestEqual(TEXT("-RenderOffScreenX is a different switch, so the switch is added"),
		MCPStandaloneGameArgs(Base, TEXT("-RenderOffScreenX"), true),
		Base + TEXT(" -RenderOffScreen -RenderOffScreenX"));
	TestEqual(TEXT("the text inside a quoted path is not the switch, so the switch is added"),
		MCPStandaloneGameArgs(Base, TEXT("-abslog=\"C:/My Logs -RenderOffScreen here/game.log\""), true),
		Base + TEXT(" -RenderOffScreen -abslog=\"C:/My Logs -RenderOffScreen here/game.log\""));

	return true;
}

#endif
