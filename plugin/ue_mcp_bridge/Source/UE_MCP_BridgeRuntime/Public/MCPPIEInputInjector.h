#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UInputAction;
class UEnhancedInputLocalPlayerSubsystem;
class ULocalPlayer;

/**
 * Shared input-injection primitives used by both the manual inject_input_*
 * handlers and the PIE replayer. One-shot values use
 * UEnhancedInputLocalPlayerSubsystem::InjectInputForAction; stateful values
 * use the subsystem's continuous injection lifecycle.
 *
 * All public methods are game-thread only. The injector hooks
 * FCoreDelegates::OnEndFrame to advance active tapes; the hook self-binds when
 * stateful injection starts and unbinds when nothing is active, so idle
 * servers pay no per-frame cost.
 */
namespace MCPPIE
{
	// Resolves a (PIE instance, local-player index) pair to a local player. The editor module installs one backed
	// by the PIE world resolver; a separate-process client has no PIE world contexts and always passes the exact
	// local player instead.
	using FPlayerResolver = TFunction<ULocalPlayer*(int32 PIEInstance, int32 ClientIndex, FString& OutError)>;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnInjectionFinished, const FString& /*Id*/);

	struct UE_MCP_BRIDGERUNTIME_API FInjectionStatus
	{
		FString Id;
		FString ActionPath;
		FString ActionName;
		bool bIsTape = false;
		FInputActionValue CurrentValue;
		int32 TapeIndex = 0;
		int32 TapeTotal = 0;
	};

	class UE_MCP_BRIDGERUNTIME_API FPIEInputInjector
	{
	public:
		// Installed by the editor module at startup. Without one, only exact-local-player calls resolve.
		static void SetPlayerResolver(FPlayerResolver Resolver);

		// Raised when a tape plays its last frame, so a remote player can report completion.
		static FOnInjectionFinished& OnTapeFinished();

		// One-shot: queues a single-frame injection. Returns false + writes
		// OutError if the EnhancedInput subsystem is not reachable (e.g. PIE
		// has not yet spawned a local player). The injected value lasts one
		// frame; the engine clears it on the next tick. ClientIndex selects
		// the local-player index inside selected PIEInstance.
		static bool InjectOnce(UInputAction* Action, const FInputActionValue& Value,
			FString& OutError, int32 ClientIndex = 0, int32 PIEInstance = INDEX_NONE,
			ULocalPlayer* ExactLocalPlayer = nullptr);

		// Begin a continuous hold using the subsystem's continuous injection
		// lifecycle until StopHold is called or PIE ends. If DesiredId is empty,
		// an id is generated. Returns the id on success; OutError is set and an
		// empty string returned on failure.

		static FString StartHold(UInputAction* Action, const FInputActionValue& Value,
			const FString& DesiredId, FString& OutError, int32 ClientIndex = 0,
			int32 PIEInstance = INDEX_NONE, ULocalPlayer* ExactLocalPlayer = nullptr);

		// Replace the value of a running hold. Returns false if no hold
		// with that id exists.
		static bool UpdateHold(const FString& Id, const FInputActionValue& Value);

		// Stop a running hold. Returns false if no hold existed for that id.
		static bool StopHold(const FString& Id);

		// Begin a tape: the first entry starts a continuous injection and each
		// later entry updates it once per end-of-frame at the given Hz (used to
		// pin t.MaxFPS during replay). DesiredId / OutError behave as StartHold.
		// Stores TapeValues by-value and auto-stops after the last frame.
		static FString StartTape(UInputAction* Action, const TArray<FVector>& Values,
			int32 Hz, const FString& DesiredId, FString& OutError, int32 ClientIndex = 0,
			int32 PIEInstance = INDEX_NONE, ULocalPlayer* ExactLocalPlayer = nullptr);

		// Stop a tape. Returns false if no tape existed for that id.
		static bool StopTape(const FString& Id);

		// Stops a hold or tape (id is unique across both maps).
		static bool StopAny(const FString& Id);

		// Returns whether the hold or tape is still registered.
		static bool IsActive(const FString& Id);

		// Snapshot of all active injections (for status queries).
		static TArray<FInjectionStatus> List();

		// Lifecycle. Call once on module startup / shutdown. PIE-end clears
		// all active injections so a new session does not inherit ghosts.
		static void Init();
		static void Shutdown();
		static void OnPIEEnded();
	};
}
