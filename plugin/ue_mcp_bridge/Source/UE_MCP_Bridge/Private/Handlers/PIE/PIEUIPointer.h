#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "InputCoreTypes.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;
class APlayerController;
class FSlateApplication;
struct FPointerEvent;

/**
 * Cursor and UI (Slate/UMG) pointer input for the PIE recorder and replayer
 * (#1822). Enhanced Input never sees the cursor, so a UMG hover, click or
 * drag-and-drop is invisible to the action sampler. This records the mouse
 * at the Slate layer and plays it back through FSlateApplication, the same
 * path a real mouse takes, so hover, press and drag all happen.
 *
 * Positions are stored relative to the PIE viewport (0..1 on each axis), so a
 * recording stays valid when the viewport is a different size on replay.
 *
 * Timing uses the world's real time (undilated, and still running while the
 * game is paused), because menus are often opened with the game paused and
 * world time would collapse every event in the menu onto one instant.
 */
namespace MCPPIE
{
	enum class EUIPointerEvent : uint8
	{
		Move,
		Down,
		Up,
		DoubleClick,
		Wheel
	};

	FString UIPointerEventToString(EUIPointerEvent Event);
	bool ParseUIPointerEvent(const FString& Str, EUIPointerEvent& Out);

	// Modifier bits stored on a ui_input step.
	namespace UIModifier
	{
		constexpr uint8 Shift   = 1 << 0;
		constexpr uint8 Control = 1 << 1;
		constexpr uint8 Alt     = 1 << 2;
		constexpr uint8 Command = 1 << 3;
	}

	TArray<FString> UIModifiersToStrings(uint8 Bits);
	uint8 UIModifiersFromStrings(const TArray<FString>& Names);

	// One captured pointer event, before it is turned into a sequence step.
	struct FUIPointerSample
	{
		double RealTime = 0.0;        // UWorld::GetRealTimeSeconds() when captured
		uint64 FrameNumber = 0;       // GFrameCounter, used to coalesce moves
		EUIPointerEvent Event = EUIPointerEvent::Move;
		FVector2D Position = FVector2D::ZeroVector; // viewport-relative, 0..1
		FString Button;               // FKey name for down/up/double_click
		float WheelDelta = 0.f;
		uint8 Modifiers = 0;
	};

	// ── Pure helpers (no Slate, unit tested) ─────────────────────────────

	// Screen-space position to viewport-relative. Returns true when the point
	// is inside the viewport. A zero-sized viewport yields (0,0) and false.
	bool ScreenToViewportRelative(const FVector2D& Screen, const FVector2D& ViewOrigin,
		const FVector2D& ViewSize, FVector2D& OutRelative);
	FVector2D ViewportRelativeToScreen(const FVector2D& Relative, const FVector2D& ViewOrigin,
		const FVector2D& ViewSize);

	// Append a sample. Consecutive moves in the same frame collapse into the
	// latest one, so a high-rate mouse does not write a step per OS event.
	void AppendUIPointerSample(TArray<FUIPointerSample>& Samples, const FUIPointerSample& Sample);

	// Turn captured samples into ui_input steps. Delays are measured from
	// BaseRealTime (the real time of the recording's first row); samples
	// before it are dropped.
	void BuildUIPointerSteps(const TArray<FUIPointerSample>& Samples, double BaseRealTime,
		TArray<FStep>& OutSteps);

	// ── Capture ──────────────────────────────────────────────────────────

	/**
	 * Slate input pre-processor that watches the mouse during a recording.
	 * Never consumes input. Records only while the target player shows its
	 * mouse cursor (with the cursor hidden, the mouse is driving gameplay,
	 * which Enhanced Input already records) and only inside the PIE
	 * viewport, except that a button pressed inside keeps reporting moves
	 * and its release until it comes up, so a drag that leaves the viewport
	 * is still whole.
	 */
	class FPIEUIPointerCapture : public IInputProcessor
	{
	public:
		void SetTarget(UWorld* World, APlayerController* PC);
		void Reset();
		const TArray<FUIPointerSample>& GetSamples() const { return Samples; }

		// Register with / unregister from FSlateApplication. Safe to call
		// when Slate is not initialised (no-op).
		static TSharedPtr<FPIEUIPointerCapture> Start(UWorld* World, APlayerController* PC);
		static void Stop(const TSharedPtr<FPIEUIPointerCapture>& Capture);

		// IInputProcessor
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
		virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
		virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
		virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
		virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
		virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override;
		virtual const TCHAR* GetDebugName() const override { return TEXT("UE_MCP_PIEUIPointerCapture"); }

	private:
		void Record(EUIPointerEvent Event, const FPointerEvent& MouseEvent);

		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<APlayerController> PlayerController;
		TArray<FUIPointerSample> Samples;
		TSet<FKey> HeldButtons;       // pressed inside the viewport, not yet released
	};

	// ── Replay ───────────────────────────────────────────────────────────

	/**
	 * Plays ui_input steps through FSlateApplication. Keeps the buttons it
	 * has pressed so the pointer events carry the right pressed set, and so
	 * a replay that stops mid-drag releases them.
	 */
	class FPIEUIPointerPlayer
	{
	public:
		bool Play(const FStep& Step, UWorld* World, FString& OutError);
		// Release every button this player pressed. Call when a replay ends.
		void ReleaseAll(UWorld* World);
		void Reset();
		int32 GetPlayedCount() const { return Played; }

		// True while a replayed event is being processed, so the capture does
		// not record it again when a recording runs during a replay.
		static bool IsInjecting();

	private:
		TSet<FKey> Pressed;
		FVector2D LastScreen = FVector2D::ZeroVector;
		bool bHasLast = false;
		int32 Played = 0;
	};

	// Absolute (desktop) rectangle of the world's game viewport widget.
	bool GetPIEViewportRect(UWorld* World, FVector2D& OutOrigin, FVector2D& OutSize);
}
