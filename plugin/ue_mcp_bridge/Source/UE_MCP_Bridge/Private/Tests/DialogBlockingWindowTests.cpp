// Regression coverage for #1078: the two directions that cost something. A
// window hosting docked tabs is not a prompt, and a parented window without
// them still is - that being the case GetActiveModalWindow misses.

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/DialogHandlers.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** A window that is never shown: constructing one is enough to ask the
	 *  predicate about it, and showing it would steal focus from whatever is
	 *  running the suite. */
	TSharedRef<SWindow> MakeOffscreenWindow(const TSharedRef<SWidget>& Content, const TCHAR* Title)
	{
		return SNew(SWindow)
			.Title(FText::FromString(FString(Title)))
			.ClientSize(FVector2D(320.0f, 200.0f))
			.CreateTitleBar(false)
			[
				Content
			];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPDialogBlockingWindowTest,
	"UE.MCP.Dialog.Gate.ADockedTabHostIsNotAPrompt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPDialogBlockingWindowTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slate is not initialized in this run, so there are no windows to classify."));
		return true;
	}

	// A window hosting a dock tab is a workspace. This is the Message Log
	// shape: regular, parented to the main editor window, and answering no
	// question. The real one carries an SDockingArea, which is private to
	// Slate; SDockTab is the public member of the same family and the walk
	// matches every one of them by type name.
	{
		const TSharedRef<SWindow> Window = MakeOffscreenWindow(
			SNew(SDockTab).TabRole(ETabRole::NomadTab), TEXT("Message Log"));

		// Parented, because that is the whole point: an unparented window is
		// rejected one step earlier and would pass this for the wrong reason.
		// The restored Message Log this reproduces is a child of the main
		// editor window, which is exactly what made it look like a prompt.
		const TSharedRef<SWindow> Editor = MakeOffscreenWindow(SNullWidget::NullWidget, TEXT("Editor"));
		Editor->AddChildWindow(Window);
		TestTrue(TEXT("the parent link was established"), Window->GetParentWindow().IsValid());

		FString Reason;
		TestFalse(TEXT("a window hosting a dock tab does not block"),
			FDialogHandlers::IsBlockingWindow(Window, &Reason));
		TestTrue(TEXT("the skip is reported with a reason a caller can read"),
			Reason.Contains(TEXT("docked tabs")));
	}

	// A parented window with no docked tabs is still a prompt. Losing this is
	// the failure in the other direction: a quit sent at an editor that is
	// genuinely sitting on a dialog Slate's own modal stack does not know
	// about, which is what the parented test was added for.
	{
		const TSharedRef<SWindow> Window = MakeOffscreenWindow(
			SNew(SButton)[SNew(STextBlock).Text(FText::FromString(TEXT("Save Selected")))],
			TEXT("Save Content"));

		// Not parented yet: a top-level window of its own is not a question.
		FString Reason;
		TestFalse(TEXT("an unparented non-modal window is not a prompt"),
			FDialogHandlers::IsBlockingWindow(Window, &Reason));
		TestTrue(TEXT("a window that was never a candidate reports no skip reason"),
			Reason.IsEmpty());

		// The parent is what makes it something the editor raised.
		// AddChildWindow sets the link without either window being shown, so
		// this never puts anything on screen.
		const TSharedRef<SWindow> Parent = MakeOffscreenWindow(SNullWidget::NullWidget, TEXT("Editor"));
		Parent->AddChildWindow(Window);
		TestTrue(TEXT("the parent link was established"), Window->GetParentWindow().IsValid());

		TestTrue(TEXT("a parented window with no docked tabs still blocks"),
			FDialogHandlers::IsBlockingWindow(Window));
	}

	return true;
}

#endif
