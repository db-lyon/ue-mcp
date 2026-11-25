#include "Handlers/PIE/PIERawInputTap.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "UObject/UnrealType.h"

bool UMCPRawValueTap::Consume(FVector& OutValue)
{
	if (!bFresh)
	{
		OutValue = FVector::ZeroVector;
		return false;
	}
	OutValue = Value;
	bFresh = false;
	return true;
}

FInputActionValue UMCPRawValueTap::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	Value = CurrentValue.Get<FVector>();
	bFresh = true;
	return CurrentValue;
}

namespace MCPPIE
{
	namespace
	{
		// FInputActionInstance keeps its modifiers protected, but they are a UPROPERTY, so reflection reaches them
		// without touching the action asset.
		TArray<TObjectPtr<UInputModifier>>* InstanceModifiers(const FInputActionInstance* Instance)
		{
			// Found by name: the struct's StaticStruct is not exported from Enhanced Input on every engine.
			static const UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/EnhancedInput.InputActionInstance"));
			static FArrayProperty* Prop = Struct ? CastField<FArrayProperty>(Struct->FindPropertyByName(TEXT("Modifiers"))) : nullptr;
			if (!Instance || !Prop)
			{
				return nullptr;
			}
			return Prop->ContainerPtrToValuePtr<TArray<TObjectPtr<UInputModifier>>>(const_cast<FInputActionInstance*>(Instance));
		}
	}

	bool FPIERawInputTaps::Ensure(UEnhancedPlayerInput* PlayerInput, const UInputAction* Action)
	{
		if (!PlayerInput || !Action)
		{
			return false;
		}
		if (Player.Get() != PlayerInput)
		{
			Remove();
			Player = PlayerInput;
		}
		TArray<TObjectPtr<UInputModifier>>* Mods = InstanceModifiers(PlayerInput->FindActionInstanceData(Action));
		if (!Mods)
		{
			return false;
		}
		FEntry* Entry = Entries.FindByPredicate([Action](const FEntry& E) { return E.Action.Get() == Action; });
		if (!Entry)
		{
			Entry = &Entries.AddDefaulted_GetRef();
			Entry->Action = Action;
			// Outered to the transient package, never the player: the tap outlives the PIE world it reads from, and an
			// object of that world held here would keep the whole world alive when PIE ends.
			Entry->Tap.Reset(NewObject<UMCPRawValueTap>(GetTransientPackage()));
		}
		// The engine rebuilds an instance when the player's mappings change, so the tap is put back whenever it is gone.
		if (Mods->Num() == 0 || (*Mods)[0] != Entry->Tap.Get())
		{
			Mods->Remove(Entry->Tap.Get());
			Mods->Insert(Entry->Tap.Get(), 0);
		}
		return true;
	}

	FVector FPIERawInputTaps::Read(const UInputAction* Action)
	{
		FVector V = FVector::ZeroVector;
		if (FEntry* Entry = Entries.FindByPredicate([Action](const FEntry& E) { return E.Action.Get() == Action; }))
		{
			Entry->Tap->Consume(V);
		}
		return V;
	}

	void FPIERawInputTaps::Remove()
	{
		if (UEnhancedPlayerInput* PlayerInput = Player.Get())
		{
			for (const FEntry& E : Entries)
			{
				if (TArray<TObjectPtr<UInputModifier>>* Mods = InstanceModifiers(PlayerInput->FindActionInstanceData(E.Action.Get())))
				{
					Mods->Remove(E.Tap.Get());
				}
			}
		}
		Entries.Reset();
		Player.Reset();
	}
}
