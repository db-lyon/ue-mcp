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
	static TSharedPtr<FJsonValue> SpawnNiagaraActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReactivateNiagara(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetNiagaraParameter(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddEmitterToSystem(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetEmitterProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetEmitterInfo(const TSharedPtr<FJsonObject>& Params);

	// v0.7.10 — Niagara depth
	static TSharedPtr<FJsonValue> ListEmitterRenderers(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddEmitterRenderer(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveEmitterRenderer(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetRendererProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InspectDataInterface(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateNiagaraSystemFromSpec(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCompiledHLSL(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSystemParameters(const TSharedPtr<FJsonObject>& Params);

	// v0.7.14 — module inputs, static switches, HLSL modules
	static TSharedPtr<FJsonValue> ListModuleInputs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetModuleInput(const TSharedPtr<FJsonObject>& Params);
	// Add a stock /Niagara/Modules script to an emitter's stack context.
	static TSharedPtr<FJsonValue> AddModule(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListStaticSwitches(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetStaticSwitch(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateModuleFromHlsl(const TSharedPtr<FJsonObject>& Params);
	// #185: Create an empty scratch-pad-style Niagara module
	static TSharedPtr<FJsonValue> CreateScratchModule(const TSharedPtr<FJsonObject>& Params);
};
