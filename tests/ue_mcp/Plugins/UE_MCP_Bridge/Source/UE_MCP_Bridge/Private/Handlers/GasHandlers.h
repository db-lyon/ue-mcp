#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FGasHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> CreateGameplayEffect(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetGasInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameplayAbility(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateAttributeSet(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameplayCue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddAbilityTag(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameplayCueNotify(const TSharedPtr<FJsonObject>& Params);
};
