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
	static TSharedPtr<FJsonValue> AddAbilityTag(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameplayCueNotify(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddAbilitySystemComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddAttribute(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetAbilityTags(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetEffectModifier(const TSharedPtr<FJsonObject>& Params);
};
