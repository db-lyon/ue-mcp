#include "Handlers/PIE/PIEReplayInputLock.h"
#include "Handlers/PIE/PIELog.h"
#include "Handlers/PIE/PIEUIPointer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Widgets/SViewport.h"

namespace MCPPIE
{
	bool IsReplayLockEscapeKey(const FKey& Key, bool bShift)
	{
		return Key == EKeys::Escape || Key == EKeys::F8 || (Key == EKeys::F1 && bShift);
	}

	bool ShouldReplayLockBlock(const FReplayLockInputEvent& Event)
	{
		if (Event.bInjected) return false;
		switch (Event.Kind)
		{
		case EReplayLockInput::Key:
			if (Event.bRelease || IsReplayLockEscapeKey(Event.Key, Event.bShift)) return false;
			if (Event.Key.IsGamepadKey()) return true;
			// Mouse buttons follow the pointer; keyboard keys follow focus, so
			// typing elsewhere in the editor still works.
			return Event.Key.IsMouseButton() ? Event.bInsideViewport : Event.bViewportFocused;
		case EReplayLockInput::Analog:
			return Event.Key.IsGamepadKey() || Event.bViewportFocused;
		case EReplayLockInput::MouseMove:
		case EReplayLockInput::MouseWheel:
			return Event.bInsideViewport;
		}
		return false;
	}

	namespace
	{
		// Slate pre-processor that applies ShouldReplayLockBlock to every
		// physical event. Registered first so nothing downstream sees a
		// blocked event, the game viewport included.
		class FReplayInputLockProcessor : public IInputProcessor
		{
		public:
			explicit FReplayInputLockProcessor(UWorld* InWorld) : World(InWorld) {}

			virtual void Tick(const float /*DeltaTime*/, FSlateApplication& /*SlateApp*/, TSharedRef<ICursor> /*Cursor*/) override {}

			virtual bool HandleKeyDownEvent(FSlateApplication& /*SlateApp*/, const FKeyEvent& E) override { return BlockKey(E, false); }
			virtual bool HandleKeyUpEvent(FSlateApplication& /*SlateApp*/, const FKeyEvent& E) override { return BlockKey(E, true); }

			virtual bool HandleAnalogInputEvent(FSlateApplication& /*SlateApp*/, const FAnalogInputEvent& E) override
			{
				FReplayLockInputEvent Q;
				Q.Kind = EReplayLockInput::Analog;
				Q.Key = E.GetKey();
				Q.bViewportFocused = IsViewportFocused();
				return ShouldReplayLockBlock(Q);
			}

			virtual bool HandleMouseMoveEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& E) override { return BlockPointer(E, EReplayLockInput::MouseMove, false); }
			virtual bool HandleMouseButtonDownEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& E) override { return BlockPointer(E, EReplayLockInput::Key, false); }
			virtual bool HandleMouseButtonUpEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& E) override { return BlockPointer(E, EReplayLockInput::Key, true); }
			virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& E) override { return BlockPointer(E, EReplayLockInput::Key, false); }
			virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& E, const FPointerEvent* /*Gesture*/) override { return BlockPointer(E, EReplayLockInput::MouseWheel, false); }

			virtual const TCHAR* GetDebugName() const override { return TEXT("UE_MCP_PIEReplayInputLock"); }

		private:
			TSharedPtr<SViewport> GetViewportWidget() const
			{
				UWorld* W = World.Get();
				UGameViewportClient* GVC = W ? W->GetGameViewport() : nullptr;
				return GVC ? GVC->GetGameViewportWidget() : nullptr;
			}

			bool IsViewportFocused() const
			{
				const TSharedPtr<SViewport> Widget = GetViewportWidget();
				return Widget.IsValid() && (Widget->HasKeyboardFocus() || Widget->HasFocusedDescendants());
			}

			bool IsInsideViewport(const FPointerEvent& E) const
			{
				FVector2D Origin, Size, Relative;
				return GetPIEViewportRect(World.Get(), Origin, Size)
					&& ScreenToViewportRelative(FVector2D(E.GetScreenSpacePosition()), Origin, Size, Relative);
			}

			bool BlockKey(const FKeyEvent& E, bool bRelease) const
			{
				FReplayLockInputEvent Q;
				Q.Kind = EReplayLockInput::Key;
				Q.Key = E.GetKey();
				Q.bRelease = bRelease;
				Q.bShift = E.IsShiftDown();
				Q.bViewportFocused = IsViewportFocused();
				return ShouldReplayLockBlock(Q);
			}

			bool BlockPointer(const FPointerEvent& E, EReplayLockInput Kind, bool bRelease) const
			{
				FReplayLockInputEvent Q;
				Q.Kind = Kind;
				Q.Key = E.GetEffectingButton();
				Q.bRelease = bRelease;
				Q.bShift = E.IsShiftDown();
				Q.bInjected = FPIEUIPointerPlayer::IsInjecting();
				Q.bInsideViewport = IsInsideViewport(E);
				return ShouldReplayLockBlock(Q);
			}

			TWeakObjectPtr<UWorld> World;
		};

		TSharedPtr<FReplayInputLockProcessor> GReplayInputLock;
	}

	namespace ReplayInputLock
	{
		void Start(UWorld* World)
		{
			if (GReplayInputLock.IsValid() || !FSlateApplication::IsInitialized()) return;
			GReplayInputLock = MakeShared<FReplayInputLockProcessor>(World);
			FSlateApplication::Get().RegisterInputPreProcessor(GReplayInputLock, 0);
			UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Input locked: only the recording drives the player (Esc, Shift+F1 and F8 still work)"));
		}

		void Stop()
		{
			if (!GReplayInputLock.IsValid()) return;
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().UnregisterInputPreProcessor(GReplayInputLock);
			}
			GReplayInputLock.Reset();
			UE_LOG(LogMCPPIE, Log, TEXT("[PIE-REP] Input unlocked"));
		}

		bool IsActive()
		{
			return GReplayInputLock.IsValid();
		}
	}
}
