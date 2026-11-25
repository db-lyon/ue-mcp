#pragma once

#include "CoreMinimal.h"
#include "InputModifiers.h"
#include "UObject/StrongObjectPtr.h"
#include "PIERawInputTap.generated.h"

class UEnhancedPlayerInput;
class UInputAction;

/**
 * Reads the value that reaches an Input Action before the action's own
 * modifiers and triggers run (#2097): the combined value of every key mapped
 * to it, after each mapping's modifiers (such as Negate on S for a move).
 *
 * Enhanced Input computes that value every frame and then overwrites it with
 * the modified one, without handing it out. The tap is a modifier placed first
 * in the action's instance on the player, ahead of the action's own modifiers,
 * so the engine passes it exactly that value. It returns the value unchanged,
 * so gameplay sees no difference.
 *
 * A replay injects the raw value with InjectInputForAction and no modifiers or
 * triggers of its own, so the action's modifiers and triggers run once, as
 * they did for the player. Injected input passes through the tap too, so a
 * recording made during a replay records what was replayed.
 */
UCLASS(Transient, NotBlueprintable, meta=(DisplayName="MCP Raw Value Tap"))
class UMCPRawValueTap : public UInputModifier
{
	GENERATED_BODY()

public:
	/** The value the engine passed on the latest frame it evaluated the action, and clears it. False when it did not evaluate it since the last read. */
	bool Consume(FVector& OutValue);

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;

private:
	mutable FVector Value = FVector::ZeroVector;
	mutable bool bFresh = false;
};

namespace MCPPIE
{
	/** One player's taps, one per watched action. */
	class FPIERawInputTaps
	{
	public:
		/** Places the action's tap first in its instance on PlayerInput, when it is not there already. False when the player has no instance for the action yet. */
		bool Ensure(UEnhancedPlayerInput* PlayerInput, const UInputAction* Action);
		/** The raw value of Action this frame: zero when the engine did not evaluate it (no key down, nothing injected). */
		FVector Read(const UInputAction* Action);
		/** Takes every tap back out of the player's action instances. */
		void Remove();

	private:
		struct FEntry
		{
			TWeakObjectPtr<const UInputAction> Action;
			TStrongObjectPtr<UMCPRawValueTap> Tap;
		};
		TWeakObjectPtr<UEnhancedPlayerInput> Player;
		TArray<FEntry> Entries;
	};
}
