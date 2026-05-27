#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UInputAction;

/**
 * Shared input-injection primitives used by both the manual inject_input_*
 * handlers and the PIE replayer. Routes all values through
 * UEnhancedInputLocalPlayerSubsystem::InjectInputForAction, which is the
 * same code path the engine uses when keys come from the OS.
 *
 * All public methods are game-thread only. The injector hooks
 * FCoreDelegates::OnEndFrame to re-inject continuous holds and step active
 * tapes; the hook self-binds the first time a stateful injection starts and
 * unbinds when nothing is active, so idle servers pay no per-frame cost.
 */
namespace UEMCPPIE
{
	struct FInjectionStatus
	{
		FString Id;
		FString ActionPath;
		FString ActionName;
		bool bIsTape = false;
		FInputActionValue CurrentValue;
		int32 TapeIndex = 0;
		int32 TapeTotal = 0;
	};

	class FPIEInputInjector
	{
	public:
		// One-shot: queues a single-frame injection. Returns false + writes
		// OutError if the EnhancedInput subsystem is not reachable (e.g. PIE
		// has not yet spawned a local player). The injected value lasts one
		// frame; the engine clears it on the next tick. ClientIndex selects
		// which local player (0 = first; 1+ for multi-client PIE).
		static bool InjectOnce(UInputAction* Action, const FInputActionValue& Value, FString& OutError, int32 ClientIndex = 0);

		// Begin a continuous hold. Re-injects `Value` every end-of-frame
		// until StopHold is called or PIE ends. If DesiredId is empty, an
		// id is generated. Returns the id on success; OutError is set and
		// an empty string returned on failure.
		static FString StartHold(UInputAction* Action, const FInputActionValue& Value, const FString& DesiredId, FString& OutError, int32 ClientIndex = 0);

		// Replace the value of a running hold. Returns false if no hold
		// with that id exists.
		static bool UpdateHold(const FString& Id, const FInputActionValue& Value);

		// Stop a running hold. Returns false if no hold existed for that id.
		static bool StopHold(const FString& Id);

		// Begin a tape: one entry per frame at the given Hz (used to pin
		// t.MaxFPS during replay). DesiredId / OutError behave as StartHold.
		// Stores TapeValues by-value so the caller does not need to keep
		// the array alive. Auto-stops after the last frame.
		static FString StartTape(UInputAction* Action, const TArray<FVector>& Values, int32 Hz, const FString& DesiredId, FString& OutError, int32 ClientIndex = 0);

		// Stop a tape. Returns false if no tape existed for that id.
		static bool StopTape(const FString& Id);

		// Stops a hold or tape (id is unique across both maps).
		static bool StopAny(const FString& Id);

		// Snapshot of all active injections (for status queries).
		static TArray<FInjectionStatus> List();

		// Lifecycle. Call once on module startup / shutdown. PIE-end clears
		// all active injections so a new session does not inherit ghosts.
		static void Init();
		static void Shutdown();
		static void OnPIEEnded();
	};
}
