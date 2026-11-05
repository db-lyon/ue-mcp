// Nothing the bridge presses may start an IDE on the user's desktop.

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Dialog/DialogHandlers.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPDialogIdeButtonTest,
	"UE.MCP.Dialog.IdeButtonsAreNeverPressed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPDialogIdeButtonTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("a button naming Visual Studio"), FDialogHandlers::OpensAnIde(TEXT("Open with Visual Studio"), TEXT("Compile failed")));
	TestTrue(TEXT("a button naming the IDE"), FDialogHandlers::OpensAnIde(TEXT("Open IDE"), TEXT("")));
	TestTrue(TEXT("Rider"), FDialogHandlers::OpensAnIde(TEXT("Open in Rider"), TEXT("")));
	TestTrue(TEXT("Yes to a prompt about the IDE"), FDialogHandlers::OpensAnIde(TEXT("Yes"), TEXT("Would you like to open Visual Studio now?")));
	TestFalse(TEXT("No to a prompt about the IDE"), FDialogHandlers::OpensAnIde(TEXT("No"), TEXT("Would you like to open Visual Studio now?")));
	TestFalse(TEXT("an ordinary Save"), FDialogHandlers::OpensAnIde(TEXT("Save Selected"), TEXT("Select Content to Save")));
	TestFalse(TEXT("a word that only contains ide"), FDialogHandlers::OpensAnIde(TEXT("Yes"), TEXT("Hide the side panel and override it?")));
	return true;
}

#endif
