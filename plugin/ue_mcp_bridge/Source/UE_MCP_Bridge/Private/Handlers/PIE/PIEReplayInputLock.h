#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class UWorld;

/**
 * Replay input lock (replay_arm / replay_run lock_input): while a replay runs,
 * the user's own keyboard, mouse and gamepad stop reaching the PIE game, so
 * only the recording drives the player. Recorded gameplay input is injected
 * straight into Enhanced Input and recorded ui_input steps are marked as
 * replayed, so neither is affected.
 */
namespace MCPPIE
{
	enum class EReplayLockInput : uint8
	{
		Key,          // keyboard key or mouse button, down or up
		Analog,       // gamepad stick or trigger
		MouseMove,
		MouseWheel
	};

	// One physical input event, reduced to what the lock decides on.
	struct FReplayLockInputEvent
	{
		EReplayLockInput Kind = EReplayLockInput::Key;
		FKey Key;                     // Key and Analog only
		bool bRelease = false;        // a key or button coming up
		bool bShift = false;
		bool bInjected = false;       // a replayed event, not the user's
		bool bInsideViewport = false; // pointer position is inside the PIE viewport
		bool bViewportFocused = false; // the PIE viewport has keyboard focus
	};

	// Keys that always get through, so the user can still leave a locked
	// replay: Esc (stop PIE), Shift+F1 (release the mouse), F8 (eject).
	bool IsReplayLockEscapeKey(const FKey& Key, bool bShift);

	// Pure pass/block rule (unit tested). Releases always pass, so a key or
	// button held when the lock starts is never left down.
	bool ShouldReplayLockBlock(const FReplayLockInputEvent& Event);

	namespace ReplayInputLock
	{
		// Start locking input aimed at World's game viewport. No-op when
		// already active or when Slate is not initialised.
		void Start(UWorld* World);
		void Stop();
		bool IsActive();
	}
}
