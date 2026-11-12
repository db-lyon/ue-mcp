#include "MCPPIEActionTrigger.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"

namespace MCPPIE
{
	FString ActionValueTypeName(const UInputAction* Action)
	{
		if (!Action) return FString();
		switch (Action->ValueType)
		{
		case EInputActionValueType::Boolean: return TEXT("bool");
		case EInputActionValueType::Axis1D:  return TEXT("axis1d");
		case EInputActionValueType::Axis2D:  return TEXT("axis2d");
		case EInputActionValueType::Axis3D:  return TEXT("axis3d");
		}
		return FString();
	}

	bool ReadActionTriggerState(const ULocalPlayer* LocalPlayer, const UInputAction* Action,
		bool& bOutTriggered, FVector& OutValue, FString& OutError)
	{
		bOutTriggered = false;
		OutValue = FVector::ZeroVector;
		if (!LocalPlayer || !Action)
		{
			OutError = TEXT("no local player or Input Action to watch");
			return false;
		}
		UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		const UEnhancedPlayerInput* PlayerInput = Subsystem ? Subsystem->GetPlayerInput() : nullptr;
		if (!PlayerInput)
		{
			OutError = TEXT("the player has no Enhanced Input (no EnhancedInputLocalPlayerSubsystem or player input yet)");
			return false;
		}
		// No instance data: nothing has mapped or injected the action yet, so it has not triggered.
		if (const FInputActionInstance* Instance = PlayerInput->FindActionInstanceData(Action))
		{
			bOutTriggered = Instance->GetTriggerEvent() == ETriggerEvent::Triggered;
			if (bOutTriggered) OutValue = Instance->GetValue().Get<FVector>();
		}
		return true;
	}
}
