#pragma once

// Synchronous Slate captures for editor(capture_screenshot) target=pie and
// target=window (#1450). In a header so the automation test reaches the same
// functions the handler calls.
//
// FSlateApplication::TakeScreenshot hands the renderer a pointer to the
// caller's pixel array and relies on the next draw of that window to read
// back into it and clear the request. When the window is not drawn, the call
// fails and the request stays queued; the next time the window draws, the
// render thread writes into an array that no longer exists.

#include "CoreMinimal.h"
#include "Widgets/SWindow.h"

/**
 * Why Slate would not draw Window for a capture, or an empty string when it
 * would. A request for a window that is not drawn is never consumed, so the
 * handler refuses before queuing one.
 *
 * @param bHasRenderViewport Whether the renderer holds a viewport for Window
 *        (FSlateRenderer::GetViewportResource is non-null). The renderer looks
 *        the window up there when the capture is queued.
 * @param WidgetSize Draw size of the widget being captured, in pixels.
 */
inline FString MCPWhySlateWindowNotDrawn(const SWindow& Window, bool bHasRenderViewport, const FVector2f WidgetSize)
{
	if (!Window.IsVisible())
	{
		return TEXT("its window is hidden");
	}
	if (Window.IsWindowMinimized())
	{
		return TEXT("its window is minimized");
	}
	if (!bHasRenderViewport)
	{
		return TEXT("its window has no render viewport");
	}
	const FVector2f WindowSize = Window.GetSizeInScreen();
	if (WindowSize.X < 1.0f || WindowSize.Y < 1.0f)
	{
		return TEXT("its window has no area");
	}
	if (WidgetSize.X < 1.0f || WidgetSize.Y < 1.0f)
	{
		return TEXT("the viewport widget has no area");
	}
	return FString();
}

/**
 * The array every bridge TakeScreenshot call reads back into. It lives as long
 * as the module, so a request the renderer never consumed writes into memory
 * the bridge still owns instead of a freed stack array.
 */
inline TArray<FColor>& MCPSlateCaptureSink()
{
	static TArray<FColor> Sink;
	return Sink;
}

/**
 * True while a failed capture may have left a readback queued against
 * MCPSlateCaptureSink. The next capture flushes rendering before it empties
 * the sink, so a late write cannot pass for its pixels.
 */
inline bool& MCPSlateCaptureMayBePending()
{
	static bool bPending = false;
	return bPending;
}
