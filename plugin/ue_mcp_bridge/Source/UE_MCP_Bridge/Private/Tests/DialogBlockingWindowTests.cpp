// Regression coverage for #1078 and #1118.
//
// #1078: a window hosting docked tabs is not a prompt, and a parented window
// without them still is - that being the case GetActiveModalWindow misses.
//
// #1118: the two prompt kinds are not interchangeable. Only a modal parks the
// game thread, and that fact has to survive the whole way to the value the
// dialog gate reads, because the gate refusing on the other one turned an
// ordinary editor window into a session-long outage.

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Dialog/DialogHandlers.h"

#include "EngineStatusHooks.h"
#include "Framework/Application/SlateApplication.h"
#include "MCPEngineStatus.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
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

		// #1118: it blocks in the sense of being a question, and in no other
		// sense. The game thread is not parked behind it, and the gate that
		// refuses every call reads exactly this distinction.
		TestEqual(TEXT("a parented non-modal prompt is not classified as modal"),
			(int32)FDialogHandlers::ClassifyWindow(Window), (int32)EMCPPromptKind::Parented);
	}

	// A window on the modal stack is the one kind the engine is parked behind.
	{
		const TSharedRef<SWindow> Window = MakeOffscreenWindow(
			SNew(SButton)[SNew(STextBlock).Text(FText::FromString(TEXT("OK")))],
			TEXT("Really?"));
		// SetAsModalWindow is what AddModalWindow sets, and the only thing that
		// sets it. Calling it directly classifies the window without pushing a
		// real modal loop the suite would then be stuck inside.
		Window->SetAsModalWindow();

		TestEqual(TEXT("a modal window is classified as modal"),
			(int32)FDialogHandlers::ClassifyWindow(Window), (int32)EMCPPromptKind::Modal);
		TestEqual(TEXT("the wire name says which kind it was"),
			FString(FDialogHandlers::PromptKindName(EMCPPromptKind::Modal)), FString(TEXT("modalWindow")));
		TestEqual(TEXT("and which kind the other one is"),
			FString(FDialogHandlers::PromptKindName(EMCPPromptKind::Parented)), FString(TEXT("parentedWindow")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPDialogGateBlocksOnlyForModalTest,
	"UE.MCP.Dialog.Gate.OnlyAModalParksTheGameThread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPDialogGateBlocksOnlyForModalTest::RunTest(const FString& Parameters)
{
	// The value the dialog gate in BridgeServer reads before refusing a call.
	// The classification above is only worth having if it survives the trip
	// through the status snapshot, which is where the gate looks (#1118).
	FMCPEngineStatus& Status = FMCPEngineStatus::Get();

	// Restored on every exit path below, so a failure cannot leave the editor
	// describing a prompt that was never on screen.
	ON_SCOPE_EXIT
	{
		Status.SetModalProvider(FMCPEngineStatusHooks::ModalProvider());
		Status.CaptureNow();
	};

	const auto Report = [&Status](bool bBlocks)
	{
		Status.SetModalProvider([bBlocks](FString& OutTitle, FString& OutMessage, TArray<FString>& OutButtons, bool& OutBlocksGameThread)
		{
			OutTitle = TEXT("Find Results");
			OutMessage = TEXT("3 results");
			OutButtons.Reset();
			OutBlocksGameThread = bBlocks;
			return true;
		});
		Status.CaptureNow();
	};

	{
		Report(false);
		FString Title, Message;
		TArray<FString> Buttons;
		bool bBlocks = true;
		const bool bPresent = Status.GetActiveModal(Title, Message, Buttons, &bBlocks);
		TestTrue(TEXT("a non-blocking prompt is still reported"), bPresent);
		TestEqual(TEXT("and named"), Title, FString(TEXT("Find Results")));
		TestFalse(TEXT("but it does not park the game thread"), bBlocks);

		const TSharedPtr<FJsonObject> Snapshot = Status.Snapshot();
		const TSharedPtr<FJsonObject>* Modal = nullptr;
		if (Snapshot.IsValid() && Snapshot->TryGetObjectField(TEXT("modal"), Modal) && Modal)
		{
			TestFalse(TEXT("the snapshot says so too, which is what the CLI gate reads"),
				(*Modal)->GetBoolField(TEXT("blocksGameThread")));
		}
		else
		{
			AddError(TEXT("the snapshot dropped a prompt the status was reporting"));
		}
	}

	{
		Report(true);
		FString Title, Message;
		TArray<FString> Buttons;
		bool bBlocks = false;
		TestTrue(TEXT("a modal is reported"), Status.GetActiveModal(Title, Message, Buttons, &bBlocks));
		TestTrue(TEXT("and it does park the game thread"), bBlocks);
	}

	return true;
}

#endif
