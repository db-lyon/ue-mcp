#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FNiagaraHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> ListNiagaraSystems(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListNiagaraModules(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateNiagaraSystem(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetNiagaraInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListEmittersInSystem(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateNiagaraEmitter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnNiagaraAtLocation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetNiagaraParameter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateNiagaraSystemFromEmitter(const TSharedPtr<FJsonObject>& Params);
};
