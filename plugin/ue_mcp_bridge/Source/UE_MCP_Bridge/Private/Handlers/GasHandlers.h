#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"

class FGasHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Shared flow used by every GAS "create blueprint by parent class" handler.
	// Requires `name`, reads `packagePath` / `onConflict`, runs the existence
	// check + asset-tools create + compile + save + rollback record, and
	// invokes ExtraResultFields (if provided) to stamp handler-specific fields
	// onto the result before returning.
	static TSharedPtr<FJsonValue> CreateGasBlueprint(
		const TSharedPtr<FJsonObject>& Params,
		const FString& DefaultPackagePath,
		class UClass* ParentClass,
		const FString& FriendlyType,
		TFunction<void(TSharedPtr<FJsonObject>&)> ExtraResultFields = nullptr);

	static TSharedPtr<FJsonValue> CreateGameplayEffect(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetGasInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameplayAbility(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateAttributeSet(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameplayCue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddAbilitySystemComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddAttribute(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetAbilityTags(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetEffectModifier(const TSharedPtr<FJsonObject>& Params);

	// Wire an AttributeSet (with optional init DataTable) onto a Blueprint's ASC
	// component template via DefaultStartingData. Authoring; in GasHandlers.cpp.
	static TSharedPtr<FJsonValue> SetAscDefaults(const TSharedPtr<FJsonObject>& Params);

	// Runtime GAS control (operates on a live actor's AbilitySystemComponent,
	// PIE by default). Implemented in GasHandlers_Runtime.cpp.
	static TSharedPtr<FJsonValue> ApplyEffect(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetAttribute(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetAttribute(const TSharedPtr<FJsonObject>& Params);
	// InitAbilityActorInfo + optionally GetOrCreateAttributeSubobject on a live
	// actor, so a bridge-authored GAS actor has live attributes to test against.
	static TSharedPtr<FJsonValue> InitAsc(const TSharedPtr<FJsonObject>& Params);
	// #587: introspect a live ASC - granted ability specs (class, level, input,
	// active, dynamic tags) + owned gameplay tags.
	static TSharedPtr<FJsonValue> GetAscState(const TSharedPtr<FJsonObject>& Params);
};
