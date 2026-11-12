#pragma once

#include "CoreMinimal.h"

class UInputAction;
class ULocalPlayer;

/**
 * Watching for a person to fire an Enhanced Input Action, for a test script's triggered expectation (#1897).
 *
 * The editor watches its own player and a remote player's process watches its own, so the frame read and the
 * rule for what counts as a fresh trigger live here, in the runtime module both of them load.
 */
namespace MCPPIE
{
	/**
	 * Decides which frame counts as a fresh trigger. Fed once per frame with whether the action was in its
	 * Triggered event that frame. A trigger counts only after a frame without one, so input that was already
	 * down when the watch began (a test step's own act still playing out, a key held from before) does not.
	 * Fires once.
	 */
	struct FActionTriggerGate
	{
		bool bReleased = false;
		bool bFired = false;

		/** True on the one frame the gate fires. */
		bool Feed(bool bTriggeredThisFrame)
		{
			if (bFired) return false;
			if (!bTriggeredThisFrame)
			{
				bReleased = true;
				return false;
			}
			if (!bReleased) return false;
			bFired = true;
			return true;
		}
	};

	/** The Input Action's value type as a script reports it: bool, axis1d, axis2d or axis3d. */
	UE_MCP_BRIDGERUNTIME_API FString ActionValueTypeName(const UInputAction* Action);

	/**
	 * This frame's state of Action on LocalPlayer's Enhanced Input: whether it is in its Triggered event, and its
	 * value (x, y, z; a boolean is 0 or 1 in x). An action the player has no instance data for has not triggered.
	 * False with OutError when the player has no Enhanced Input to read.
	 */
	UE_MCP_BRIDGERUNTIME_API bool ReadActionTriggerState(const ULocalPlayer* LocalPlayer, const UInputAction* Action,
		bool& bOutTriggered, FVector& OutValue, FString& OutError);
}
