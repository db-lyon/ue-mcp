// capture_screenshot target=pie and target=window refuse a window Slate will
// not draw, because the readback FSlateApplication::TakeScreenshot queues for
// it is never consumed and later wrote into a freed array (#1450). The pixels
// land in one array that outlives every call.

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Editor/HandlerSlateCapture.h"

#include "Misc/AutomationTest.h"
#include "Widgets/SWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPSlateCaptureGuardTest,
	"UE.MCP.Editor.CaptureScreenshot.RefusesUndrawnWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSlateCaptureGuardTest::RunTest(const FString& Parameters)
{
	// Constructed, never shown or added to the application, so it is hidden.
	const TSharedRef<SWindow> Unshown = SNew(SWindow)
		.Title(FText::FromString(TEXT("Unshown")))
		.ClientSize(FVector2D(320.0f, 200.0f))
		.CreateTitleBar(false);

	const FString Hidden = MCPWhySlateWindowNotDrawn(*Unshown, true, FVector2f(320.0f, 200.0f));
	TestEqual(TEXT("a window that was never shown is refused as hidden"), Hidden, FString(TEXT("its window is hidden")));

	// Visibility is checked first, so the other reasons never mask it.
	TestEqual(TEXT("a hidden window without a render viewport still reports hidden"),
		MCPWhySlateWindowNotDrawn(*Unshown, false, FVector2f::ZeroVector), FString(TEXT("its window is hidden")));

	// The readback target is the same array on every call, so a request the
	// renderer never consumed still points at live memory.
	TArray<FColor>& First = MCPSlateCaptureSink();
	TArray<FColor>& Second = MCPSlateCaptureSink();
	TestTrue(TEXT("every capture reads back into the same long-lived array"), &First == &Second);
	TestTrue(TEXT("the pending flag is one module-wide value"), &MCPSlateCaptureMayBePending() == &MCPSlateCaptureMayBePending());

	return true;
}

#endif
