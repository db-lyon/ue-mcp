#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FFoliageHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> ListFoliageTypes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SampleFoliage(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetFoliageSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetFoliageTypeSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateFoliageType(const TSharedPtr<FJsonObject>& Params);
};
