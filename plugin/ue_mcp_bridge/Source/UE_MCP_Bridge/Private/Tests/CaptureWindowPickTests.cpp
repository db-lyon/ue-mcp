// capture_screenshot target=window, without pieInstance or worldPath, means
// "the active window". In an editor with no OS focus (-RenderOffScreen) Slate's
// active top-level window can be any popup, and a capture of the editor once
// came back as a tooltip reading "Open the edit menu". Only a regular window
// that is not a menu or notification is ever chosen, and the root window only
// when it is shown.

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Editor/HandlerWindowPick.h"

#include "Misc/AutomationTest.h"
#include "Widgets/SWindow.h"

namespace
{
	/** Constructed, never shown or added to the application: the pick reads
	 *  each window's type and popup flag, and an unshown window is hidden. */
	TSharedRef<SWindow> MakeUnshownWindow(EWindowType Type, const TCHAR* Title)
	{
		return SNew(SWindow)
			.Type(Type)
			.Title(FText::FromString(FString(Title)))
			.ClientSize(FVector2D(320.0f, 200.0f))
			.CreateTitleBar(false);
	}

	/** A Normal-typed window flagged as a popup, which is not a regular window. */
	TSharedRef<SWindow> MakeUnshownPopupWindow(const TCHAR* Title)
	{
		return SNew(SWindow)
			.Type(EWindowType::Normal)
			.IsPopupWindow(true)
			.Title(FText::FromString(FString(Title)))
			.ClientSize(FVector2D(320.0f, 200.0f))
			.CreateTitleBar(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPCaptureWindowPickTest,
	"UE.MCP.Editor.CaptureScreenshot.WindowPickSkipsPopups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPCaptureWindowPickTest::RunTest(const FString& Parameters)
{
	const TSharedRef<SWindow> Root = MakeUnshownWindow(EWindowType::Normal, TEXT("Root"));

	// An active tooltip above a Normal window: the Normal window is the one.
	{
		const TSharedRef<SWindow> Below = MakeUnshownWindow(EWindowType::Normal, TEXT("Below"));
		const TSharedRef<SWindow> Tooltip = MakeUnshownWindow(EWindowType::ToolTip, TEXT("Open the edit menu"));
		const TArray<TSharedRef<SWindow>> Ordered = { Root, Below, Tooltip };

		const TSharedPtr<SWindow> Picked = MCPPickCaptureWindow(Tooltip, Ordered, Root);
		TestTrue(TEXT("an active tooltip gives the topmost Normal window below it"), Picked == Below);
	}

	// No active window: the topmost regular window, not a popup drawn above it.
	{
		const TSharedRef<SWindow> Below = MakeUnshownWindow(EWindowType::Normal, TEXT("Below"));
		const TSharedRef<SWindow> Top = MakeUnshownWindow(EWindowType::Normal, TEXT("Top"));
		const TSharedRef<SWindow> Tooltip = MakeUnshownWindow(EWindowType::ToolTip, TEXT("Tooltip"));
		const TArray<TSharedRef<SWindow>> Ordered = { Below, Top, Tooltip };

		TestTrue(TEXT("no active window gives the topmost regular window"),
			MCPPickCaptureWindow(nullptr, Ordered, Root) == Top);
	}

	// A Normal-typed window created as a popup is not a regular window.
	{
		const TSharedRef<SWindow> Below = MakeUnshownWindow(EWindowType::Normal, TEXT("Below"));
		const TSharedRef<SWindow> Popup = MakeUnshownPopupWindow(TEXT("Popup"));
		const TArray<TSharedRef<SWindow>> Ordered = { Below, Popup };

		TestTrue(TEXT("an active Normal-typed popup is skipped"),
			MCPPickCaptureWindow(Popup, Ordered, Root) == Below);
		TestTrue(TEXT("a topmost Normal-typed popup is skipped"),
			MCPPickCaptureWindow(nullptr, Ordered, Root) == Below);
	}

	// An active notification is skipped.
	{
		const TSharedRef<SWindow> Below = MakeUnshownWindow(EWindowType::Normal, TEXT("Below"));
		const TSharedRef<SWindow> Toast = MakeUnshownWindow(EWindowType::Notification, TEXT("Toast"));
		const TArray<TSharedRef<SWindow>> Ordered = { Below, Toast };

		TestTrue(TEXT("an active notification gives the regular window below it"),
			MCPPickCaptureWindow(Toast, Ordered, Root) == Below);
	}

	// Nothing capturable is active or visible, and the root is hidden (never
	// shown): no window, so the handler reports "No visible Slate window".
	{
		const TSharedRef<SWindow> Menu = MakeUnshownWindow(EWindowType::Menu, TEXT("Menu"));
		const TSharedRef<SWindow> Toast = MakeUnshownWindow(EWindowType::Notification, TEXT("Toast"));
		const TSharedRef<SWindow> Decorator = MakeUnshownWindow(EWindowType::CursorDecorator, TEXT("Decorator"));
		const TArray<TSharedRef<SWindow>> Ordered = { Menu, Toast, Decorator };

		TestFalse(TEXT("only popups visible, one active, and a hidden root gives no window"),
			MCPPickCaptureWindow(Menu, Ordered, Root).IsValid());
		TestFalse(TEXT("only popups visible, none active, and a hidden root gives no window"),
			MCPPickCaptureWindow(nullptr, Ordered, Root).IsValid());
		TestFalse(TEXT("no capturable window and no root gives no window"),
			MCPPickCaptureWindow(nullptr, Ordered, nullptr).IsValid());
	}

	// An active Normal window wins over a Normal window drawn above it.
	{
		const TSharedRef<SWindow> Active = MakeUnshownWindow(EWindowType::Normal, TEXT("Active"));
		const TSharedRef<SWindow> Above = MakeUnshownWindow(EWindowType::Normal, TEXT("Above"));
		const TArray<TSharedRef<SWindow>> Ordered = { Root, Active, Above };

		TestTrue(TEXT("an active Normal window is picked"),
			MCPPickCaptureWindow(Active, Ordered, Root) == Active);
	}

	return true;
}

#endif
