#include "Handlers/PIE/PIEUIPointer.h"
#include "Handlers/PIE/PIELog.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedPlayerInput.h"
#include "HandlerEngineVersion.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "Layout/WidgetPath.h"
#include "UObject/UnrealType.h"
#include "Widgets/SViewport.h"

namespace MCPPIE
{
	namespace
	{
		// Depth counter rather than a bool: Slate can re-enter while one
		// replayed event is processed (a click that opens a widget which
		// synthesises a move).
		int32 GUIPointerInjectDepth = 0;

		struct FScopedUIPointerInject
		{
			FScopedUIPointerInject() { ++GUIPointerInjectDepth; }
			~FScopedUIPointerInject() { --GUIPointerInjectDepth; }
		};

		uint8 ModifierBitsFrom(const FModifierKeysState& M)
		{
			uint8 Bits = 0;
			if (M.IsShiftDown())   Bits |= UIModifier::Shift;
			if (M.IsControlDown()) Bits |= UIModifier::Control;
			if (M.IsAltDown())     Bits |= UIModifier::Alt;
			if (M.IsCommandDown()) Bits |= UIModifier::Command;
			return Bits;
		}

		FModifierKeysState ModifierStateFrom(uint8 Bits)
		{
			// Left-hand keys stand in for either side; a widget asks whether
			// shift is down, not which one.
			return FModifierKeysState(
				(Bits & UIModifier::Shift) != 0, false,
				(Bits & UIModifier::Control) != 0, false,
				(Bits & UIModifier::Alt) != 0, false,
				(Bits & UIModifier::Command) != 0, false,
				false);
		}

		double QuantizeRelative(double V)
		{
			// Four decimals is a tenth of a pixel on a 4K viewport.
			return FMath::RoundToDouble(V * 10000.0) / 10000.0;
		}
	}

	FString UIPointerEventToString(EUIPointerEvent Event)
	{
		switch (Event)
		{
		case EUIPointerEvent::Move:        return TEXT("mouse_move");
		case EUIPointerEvent::Down:        return TEXT("mouse_down");
		case EUIPointerEvent::Up:          return TEXT("mouse_up");
		case EUIPointerEvent::DoubleClick: return TEXT("mouse_double_click");
		case EUIPointerEvent::Wheel:       return TEXT("mouse_wheel");
		}
		return TEXT("mouse_move");
	}

	bool ParseUIPointerEvent(const FString& Str, EUIPointerEvent& Out)
	{
		if (Str == TEXT("mouse_move"))         { Out = EUIPointerEvent::Move;        return true; }
		if (Str == TEXT("mouse_down"))         { Out = EUIPointerEvent::Down;        return true; }
		if (Str == TEXT("mouse_up"))           { Out = EUIPointerEvent::Up;          return true; }
		if (Str == TEXT("mouse_double_click")) { Out = EUIPointerEvent::DoubleClick; return true; }
		if (Str == TEXT("mouse_wheel"))        { Out = EUIPointerEvent::Wheel;       return true; }
		return false;
	}

	TArray<FString> UIModifiersToStrings(uint8 Bits)
	{
		TArray<FString> Out;
		if (Bits & UIModifier::Shift)   Out.Add(TEXT("shift"));
		if (Bits & UIModifier::Control) Out.Add(TEXT("control"));
		if (Bits & UIModifier::Alt)     Out.Add(TEXT("alt"));
		if (Bits & UIModifier::Command) Out.Add(TEXT("command"));
		return Out;
	}

	uint8 UIModifiersFromStrings(const TArray<FString>& Names)
	{
		uint8 Bits = 0;
		for (const FString& N : Names)
		{
			if (N.Equals(TEXT("shift"), ESearchCase::IgnoreCase))        Bits |= UIModifier::Shift;
			else if (N.Equals(TEXT("control"), ESearchCase::IgnoreCase)) Bits |= UIModifier::Control;
			else if (N.Equals(TEXT("ctrl"), ESearchCase::IgnoreCase))    Bits |= UIModifier::Control;
			else if (N.Equals(TEXT("alt"), ESearchCase::IgnoreCase))     Bits |= UIModifier::Alt;
			else if (N.Equals(TEXT("command"), ESearchCase::IgnoreCase)) Bits |= UIModifier::Command;
		}
		return Bits;
	}

	bool ScreenToViewportRelative(const FVector2D& Screen, const FVector2D& ViewOrigin,
		const FVector2D& ViewSize, FVector2D& OutRelative)
	{
		if (ViewSize.X <= 0.0 || ViewSize.Y <= 0.0)
		{
			OutRelative = FVector2D::ZeroVector;
			return false;
		}
		OutRelative = FVector2D(
			QuantizeRelative((Screen.X - ViewOrigin.X) / ViewSize.X),
			QuantizeRelative((Screen.Y - ViewOrigin.Y) / ViewSize.Y));
		return OutRelative.X >= 0.0 && OutRelative.X <= 1.0
			&& OutRelative.Y >= 0.0 && OutRelative.Y <= 1.0;
	}

	FVector2D ViewportRelativeToScreen(const FVector2D& Relative, const FVector2D& ViewOrigin,
		const FVector2D& ViewSize)
	{
		return FVector2D(ViewOrigin.X + Relative.X * ViewSize.X, ViewOrigin.Y + Relative.Y * ViewSize.Y);
	}

	void AppendUIPointerSample(TArray<FUIPointerSample>& Samples, const FUIPointerSample& Sample)
	{
		if (Sample.Event == EUIPointerEvent::Move && Samples.Num() > 0)
		{
			FUIPointerSample& Last = Samples.Last();
			if (Last.Event == EUIPointerEvent::Move && Last.FrameNumber == Sample.FrameNumber)
			{
				Last = Sample;
				return;
			}
		}
		Samples.Add(Sample);
	}

	void BuildUIPointerSteps(const TArray<FUIPointerSample>& Samples, double BaseRealTime,
		TArray<FStep>& OutSteps)
	{
		for (const FUIPointerSample& P : Samples)
		{
			if (P.RealTime < BaseRealTime) continue;
			FStep S;
			S.Type = EStepType::UIInput;
			S.DelayMs = static_cast<int32>(FMath::RoundToInt((P.RealTime - BaseRealTime) * 1000.0));
			S.UIEvent = UIPointerEventToString(P.Event);
			S.PointerX = P.Position.X;
			S.PointerY = P.Position.Y;
			if (P.Event == EUIPointerEvent::Down || P.Event == EUIPointerEvent::Up
				|| P.Event == EUIPointerEvent::DoubleClick)
			{
				S.UIKey = P.Button;
			}
			if (P.Event == EUIPointerEvent::Wheel)
			{
				S.WheelDelta = P.WheelDelta;
			}
			S.UIModifiers = P.Modifiers;
			OutSteps.Add(S);
		}
	}

	bool GetPIEViewportRect(UWorld* World, FVector2D& OutOrigin, FVector2D& OutSize)
	{
		UGameViewportClient* GVC = World ? World->GetGameViewport() : nullptr;
		if (!GVC) return false;
		TSharedPtr<SViewport> Widget = GVC->GetGameViewportWidget();
		if (!Widget.IsValid()) return false;
		const FGeometry& G = Widget->GetTickSpaceGeometry();
		OutOrigin = FVector2D(G.GetAbsolutePosition());
		OutSize = FVector2D(G.GetAbsoluteSize());
		return OutSize.X > 0.0 && OutSize.Y > 0.0;
	}

	// ── Enhanced Input overlap filter ────────────────────────────────────

	bool IsUIPointerInputMapped(const TSet<FKey>& MappedKeys, EUIPointerEvent Event, const FKey& Button)
	{
		switch (Event)
		{
		case EUIPointerEvent::Down:
		case EUIPointerEvent::Up:
		case EUIPointerEvent::DoubleClick:
			return Button.IsValid() && MappedKeys.Contains(Button);
		case EUIPointerEvent::Wheel:
			return MappedKeys.Contains(EKeys::MouseWheelAxis) || MappedKeys.Contains(EKeys::MouseScrollUp)
				|| MappedKeys.Contains(EKeys::MouseScrollDown);
		case EUIPointerEvent::Move:
			return false;
		}
		return false;
	}

	bool ShouldSkipUIPointerEvent(EUIPointerEvent Event, bool bReachesGameViewport, bool bMappedInEnhancedInput)
	{
		// Moves drive hover and never fire an Enhanced Input action, and a
		// release follows its press (a skipped press is never held).
		const bool bFilterable = Event == EUIPointerEvent::Down || Event == EUIPointerEvent::DoubleClick
			|| Event == EUIPointerEvent::Wheel;
		return bFilterable && bReachesGameViewport && bMappedInEnhancedInput;
	}

	namespace
	{
		// Every key the player's applied mapping contexts bind to an action.
		TSet<FKey> GetEnhancedInputMappedKeys(APlayerController* PC)
		{
			TSet<FKey> Keys;
			UEnhancedPlayerInput* PlayerInput = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
			if (!PlayerInput) return Keys;
			TConstArrayView<FEnhancedActionKeyMapping> Mappings;
#if UE_MCP_HAS_5_8_API
			Mappings = PlayerInput->GetEnhancedActionMappingsView();
#else
			// GetEnhancedActionMappings() is protected before 5.8; the backing
			// UPROPERTY is readable through reflection (as gameplay input reads do).
			if (FArrayProperty* Prop = CastField<FArrayProperty>(
					PlayerInput->GetClass()->FindPropertyByName(TEXT("EnhancedActionMappings"))))
			{
				Mappings = *Prop->ContainerPtrToValuePtr<TArray<FEnhancedActionKeyMapping>>(PlayerInput);
			}
#endif
			for (const FEnhancedActionKeyMapping& Mapping : Mappings)
			{
				if (Mapping.Action) Keys.Add(Mapping.Key);
			}
			return Keys;
		}

		// True when the topmost hit-testable widget under the pointer is the
		// game viewport itself, so the event goes to the game, not to UMG.
		// OutLeaf names that widget for the log.
		bool ReachesGameViewport(UWorld* W, const FPointerEvent& E, FString& OutLeaf)
		{
			UGameViewportClient* GVC = W ? W->GetGameViewport() : nullptr;
			const TSharedPtr<SViewport> Viewport = GVC ? GVC->GetGameViewportWidget() : nullptr;
			if (!Viewport.IsValid() || !FSlateApplication::IsInitialized()) return false;
			FSlateApplication& App = FSlateApplication::Get();
			const FWidgetPath Path = App.LocateWindowUnderMouse(E.GetScreenSpacePosition(),
				App.GetInteractiveTopLevelWindows(), false, E.GetUserIndex());
			if (!Path.IsValid()) return false;
			const TSharedRef<SWidget> Leaf = Path.GetLastWidget();
			OutLeaf = Leaf->GetTypeAsString();
			return &Leaf.Get() == Viewport.Get();
		}
	}

	// ── Capture ──────────────────────────────────────────────────────────

	void FPIEUIPointerCapture::SetTarget(UWorld* InWorld, APlayerController* PC)
	{
		World = InWorld;
		PlayerController = PC;
	}

	void FPIEUIPointerCapture::Reset()
	{
		Samples.Reset();
		HeldButtons.Reset();
		SkippedForEnhancedInput = 0;
	}

	TSharedPtr<FPIEUIPointerCapture> FPIEUIPointerCapture::Start(UWorld* InWorld, APlayerController* PC)
	{
		if (!FSlateApplication::IsInitialized()) return nullptr;
		TSharedPtr<FPIEUIPointerCapture> Capture = MakeShared<FPIEUIPointerCapture>();
		Capture->SetTarget(InWorld, PC);
		// Index 0 so the capture sees the event before another pre-processor
		// can consume it.
		FSlateApplication::Get().RegisterInputPreProcessor(Capture, 0);
		return Capture;
	}

	void FPIEUIPointerCapture::Stop(const TSharedPtr<FPIEUIPointerCapture>& Capture)
	{
		if (Capture.IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(Capture);
		}
	}

	void FPIEUIPointerCapture::Record(EUIPointerEvent Event, const FPointerEvent& MouseEvent)
	{
		if (FPIEUIPointerPlayer::IsInjecting()) return;
		UWorld* W = World.Get();
		APlayerController* PC = PlayerController.Get();
		if (!W || !PC) return;

		FVector2D Origin, Size;
		if (!GetPIEViewportRect(W, Origin, Size)) return;

		FUIPointerSample S;
		S.Event = Event;
		S.RealTime = W->GetRealTimeSeconds();
		S.FrameNumber = GFrameCounter;
		S.Modifiers = ModifierBitsFrom(MouseEvent.GetModifierKeys());
		const bool bInside = ScreenToViewportRelative(FVector2D(MouseEvent.GetScreenSpacePosition()), Origin, Size, S.Position);
		const FKey Button = MouseEvent.GetEffectingButton();

		switch (Event)
		{
		case EUIPointerEvent::Up:
			// A release is recorded when its press was, wherever the cursor
			// is now and whether or not the cursor is still shown, so no
			// replayed button stays down.
			if (!HeldButtons.Contains(Button)) return;
			HeldButtons.Remove(Button);
			S.Button = Button.GetFName().ToString();
			break;
		case EUIPointerEvent::Move:
			if (!(PC->ShouldShowMouseCursor() && (bInside || HeldButtons.Num() > 0))) return;
			break;
		case EUIPointerEvent::Down:
		case EUIPointerEvent::DoubleClick:
			if (!PC->ShouldShowMouseCursor() || !bInside || !Button.IsValid()) return;
			if (SkipForEnhancedInput(Event, Button, MouseEvent)) return;
			HeldButtons.Add(Button);
			S.Button = Button.GetFName().ToString();
			break;
		case EUIPointerEvent::Wheel:
			if (!PC->ShouldShowMouseCursor() || !bInside) return;
			if (SkipForEnhancedInput(Event, EKeys::Invalid, MouseEvent)) return;
			S.WheelDelta = MouseEvent.GetWheelDelta();
			break;
		}
		AppendUIPointerSample(Samples, S);
	}

	bool FPIEUIPointerCapture::SkipForEnhancedInput(EUIPointerEvent Event, const FKey& Button, const FPointerEvent& MouseEvent)
	{
		FString Leaf;
		const bool bReachesGame = ReachesGameViewport(World.Get(), MouseEvent, Leaf);
		const bool bMapped = bReachesGame && IsUIPointerInputMapped(GetEnhancedInputMappedKeys(PlayerController.Get()), Event, Button);
		const FString What = Event == EUIPointerEvent::Wheel ? TEXT("wheel") : Button.GetFName().ToString();
		if (ShouldSkipUIPointerEvent(Event, bReachesGame, bMapped))
		{
			++SkippedForEnhancedInput;
			UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REC] ui_input: %s %s left to Enhanced Input (reaches the game viewport and is mapped)"),
				*UIPointerEventToString(Event), *What);
			return true;
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REC] ui_input: %s %s recorded (under the cursor: %s)"),
			*UIPointerEventToString(Event), *What, Leaf.IsEmpty() ? TEXT("nothing") : *Leaf);
		return false;
	}

	bool FPIEUIPointerCapture::HandleMouseMoveEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& MouseEvent)
	{
		Record(EUIPointerEvent::Move, MouseEvent);
		return false;
	}

	bool FPIEUIPointerCapture::HandleMouseButtonDownEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& MouseEvent)
	{
		Record(EUIPointerEvent::Down, MouseEvent);
		return false;
	}

	bool FPIEUIPointerCapture::HandleMouseButtonUpEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& MouseEvent)
	{
		Record(EUIPointerEvent::Up, MouseEvent);
		return false;
	}

	bool FPIEUIPointerCapture::HandleMouseButtonDoubleClickEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& MouseEvent)
	{
		Record(EUIPointerEvent::DoubleClick, MouseEvent);
		return false;
	}

	bool FPIEUIPointerCapture::HandleMouseWheelOrGestureEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& InWheelEvent, const FPointerEvent* /*InGestureEvent*/)
	{
		Record(EUIPointerEvent::Wheel, InWheelEvent);
		return false;
	}

	// ── Replay ───────────────────────────────────────────────────────────

	namespace
	{
		/**
		 * Swallows the physical mouse inside the PIE viewport while ui_input
		 * steps replay, so the user's own mouse cannot pull the cursor off the
		 * recorded path. Replayed events pass, so do releases (an unmatched up
		 * is harmless, a swallowed one could leave a button down) and anything
		 * outside the viewport, so the editor's own controls stay usable.
		 */
		class FUIPointerReplayBlocker : public IInputProcessor
		{
		public:
			explicit FUIPointerReplayBlocker(UWorld* InWorld) : World(InWorld) {}

			virtual void Tick(const float /*DeltaTime*/, FSlateApplication& /*SlateApp*/, TSharedRef<ICursor> /*Cursor*/) override {}
			virtual bool HandleMouseMoveEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& E) override { return ShouldBlock(E); }
			virtual bool HandleMouseButtonDownEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& E) override { return ShouldBlock(E); }
			virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& E) override { return ShouldBlock(E); }
			virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& E, const FPointerEvent* /*Gesture*/) override { return ShouldBlock(E); }
			virtual const TCHAR* GetDebugName() const override { return TEXT("UE_MCP_PIEUIPointerReplayBlocker"); }

		private:
			bool ShouldBlock(const FPointerEvent& E) const
			{
				if (FPIEUIPointerPlayer::IsInjecting()) return false;
				FVector2D Origin, Size, Relative;
				return GetPIEViewportRect(World.Get(), Origin, Size)
					&& ScreenToViewportRelative(FVector2D(E.GetScreenSpacePosition()), Origin, Size, Relative);
			}

			TWeakObjectPtr<UWorld> World;
		};

		TSharedPtr<FUIPointerReplayBlocker> GUIPointerReplayBlocker;

		void StartUIPointerReplayBlocker(UWorld* World)
		{
			if (GUIPointerReplayBlocker.IsValid() || !FSlateApplication::IsInitialized()) return;
			GUIPointerReplayBlocker = MakeShared<FUIPointerReplayBlocker>(World);
			FSlateApplication::Get().RegisterInputPreProcessor(GUIPointerReplayBlocker, 0);
			UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] ui_input: physical mouse blocked inside the PIE viewport until the replay ends"));
		}

		void StopUIPointerReplayBlocker()
		{
			if (!GUIPointerReplayBlocker.IsValid()) return;
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().UnregisterInputPreProcessor(GUIPointerReplayBlocker);
			}
			GUIPointerReplayBlocker.Reset();
			UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] ui_input: physical mouse restored"));
		}
	}

	bool FPIEUIPointerPlayer::IsInjecting()
	{
		return GUIPointerInjectDepth > 0;
	}

	void FPIEUIPointerPlayer::Reset()
	{
		StopUIPointerReplayBlocker();
		Pressed.Reset();
		LastScreen = FVector2D::ZeroVector;
		bHasLast = false;
		Played = 0;
	}

	bool FPIEUIPointerPlayer::Play(const FStep& Step, UWorld* World, FString& OutError)
	{
		if (!FSlateApplication::IsInitialized())
		{
			OutError = TEXT("ui_input: Slate is not initialised");
			return false;
		}
		EUIPointerEvent Event;
		if (!ParseUIPointerEvent(Step.UIEvent, Event))
		{
			OutError = FString::Printf(TEXT("ui_input: unknown event '%s'"), *Step.UIEvent);
			return false;
		}
		FVector2D Origin, Size;
		if (!GetPIEViewportRect(World, Origin, Size))
		{
			OutError = TEXT("ui_input: the PIE world has no game viewport");
			return false;
		}

		FSlateApplication& App = FSlateApplication::Get();
		const FVector2D Screen = ViewportRelativeToScreen(FVector2D(Step.PointerX, Step.PointerY), Origin, Size);
		const FVector2D Last = bHasLast ? LastScreen : Screen;
		const FModifierKeysState Mods = ModifierStateFrom(Step.UIModifiers);
		const uint32 PointerIndex = FSlateApplicationBase::CursorPointerIndex;

		FKey Button = EKeys::Invalid;
		if (Event == EUIPointerEvent::Down || Event == EUIPointerEvent::Up || Event == EUIPointerEvent::DoubleClick)
		{
			Button = FKey(FName(*Step.UIKey));
			if (!Button.IsValid() || !Button.IsMouseButton())
			{
				OutError = FString::Printf(TEXT("ui_input: '%s' is not a mouse button"), *Step.UIKey);
				return false;
			}
		}

		StartUIPointerReplayBlocker(World);

		FScopedUIPointerInject Guard;
		// Move the platform cursor too: hover state and the next real-time
		// cursor query read it, not the event.
		App.SetCursorPos(Screen);

		switch (Event)
		{
		case EUIPointerEvent::Move:
		{
			const FPointerEvent E(PointerIndex, Screen, Last, Pressed, EKeys::Invalid, 0.f, Mods);
			App.ProcessMouseMoveEvent(E);
			break;
		}
		case EUIPointerEvent::Down:
		{
			Pressed.Add(Button);
			const FPointerEvent E(PointerIndex, Screen, Last, Pressed, Button, 0.f, Mods);
			App.ProcessMouseButtonDownEvent(nullptr, E);
			break;
		}
		case EUIPointerEvent::DoubleClick:
		{
			Pressed.Add(Button);
			const FPointerEvent E(PointerIndex, Screen, Last, Pressed, Button, 0.f, Mods);
			App.ProcessMouseButtonDoubleClickEvent(nullptr, E);
			break;
		}
		case EUIPointerEvent::Up:
		{
			Pressed.Remove(Button);
			const FPointerEvent E(PointerIndex, Screen, Last, Pressed, Button, 0.f, Mods);
			App.ProcessMouseButtonUpEvent(E);
			break;
		}
		case EUIPointerEvent::Wheel:
		{
			const FPointerEvent E(PointerIndex, Screen, Last, Pressed, EKeys::Invalid, static_cast<float>(Step.WheelDelta), Mods);
			App.ProcessMouseWheelOrGestureEvent(E, nullptr);
			break;
		}
		}

		LastScreen = Screen;
		bHasLast = true;
		++Played;
		return true;
	}

	void FPIEUIPointerPlayer::ReleaseAll(UWorld* World)
	{
		// ReleaseAll runs whenever a replay ends or is stopped, so the mouse comes back here.
		StopUIPointerReplayBlocker();
		if (Pressed.Num() == 0) return;
		if (!FSlateApplication::IsInitialized())
		{
			Pressed.Reset();
			return;
		}
		FSlateApplication& App = FSlateApplication::Get();
		FVector2D Screen = LastScreen;
		if (!bHasLast)
		{
			FVector2D Origin, Size;
			Screen = GetPIEViewportRect(World, Origin, Size) ? Origin + Size * 0.5 : FVector2D(App.GetCursorPos());
		}
		const TArray<FKey> Held = Pressed.Array();
		FScopedUIPointerInject Guard;
		for (const FKey& Button : Held)
		{
			Pressed.Remove(Button);
			const FPointerEvent E(FSlateApplicationBase::CursorPointerIndex, Screen, Screen, Pressed, Button, 0.f, FModifierKeysState());
			App.ProcessMouseButtonUpEvent(E);
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] ui_input: released %d held mouse button(s)"), Held.Num());
	}
}
