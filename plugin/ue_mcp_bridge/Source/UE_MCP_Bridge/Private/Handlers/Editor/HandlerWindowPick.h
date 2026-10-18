#pragma once

// Which Slate window editor(capture_screenshot target=window) captures, in a
// header so the automation test reaches the same pure function the handler
// calls.

#include "CoreMinimal.h"
#include "Widgets/SWindow.h"

/**
 * A window a capture can mean: a regular window (not a popup, tooltip or
 * cursor decorator, as SWindow::IsRegularWindow says) that is not a menu or a
 * notification either. The dialog handlers use the same regular-window test.
 */
inline bool MCPIsCapturableWindow(const SWindow& Window)
{
	const EWindowType Type = Window.GetType();
	return Window.IsRegularWindow() && Type != EWindowType::Menu && Type != EWindowType::Notification;
}

/**
 * Picks the window a "capture the active window" request means.
 *
 * Slate's active top-level window is whatever last took activation, and in an
 * editor with no OS focus (-RenderOffScreen) that is as often a tooltip or a
 * menu as a real window. Only a capturable window (MCPIsCapturableWindow) is
 * ever chosen from the application.
 *
 * @param ActiveWindow The application's active top-level window, may be null.
 * @param VisibleWindowsOrdered Every visible window, back to front, as
 *        FSlateApplication::GetAllVisibleWindowsOrdered returns them.
 * @param RootWindow The main editor window, the answer when no capturable
 *        window is active or visible, provided it is shown and not minimized.
 *        May be null.
 * @return The active window if it is capturable, else the topmost visible
 *         capturable window, else RootWindow when it is visible and not
 *         minimized, else null.
 */
inline TSharedPtr<SWindow> MCPPickCaptureWindow(
	const TSharedPtr<SWindow>& ActiveWindow,
	const TArray<TSharedRef<SWindow>>& VisibleWindowsOrdered,
	const TSharedPtr<SWindow>& RootWindow)
{
	if (ActiveWindow.IsValid() && MCPIsCapturableWindow(*ActiveWindow))
	{
		return ActiveWindow;
	}
	for (int32 Index = VisibleWindowsOrdered.Num() - 1; Index >= 0; --Index)
	{
		if (MCPIsCapturableWindow(*VisibleWindowsOrdered[Index]))
		{
			return VisibleWindowsOrdered[Index];
		}
	}
	if (RootWindow.IsValid() && RootWindow->IsVisible() && !RootWindow->IsWindowMinimized())
	{
		return RootWindow;
	}
	return nullptr;
}
