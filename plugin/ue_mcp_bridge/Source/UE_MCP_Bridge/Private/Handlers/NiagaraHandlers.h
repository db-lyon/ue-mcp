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

	// v0.7.10 - Niagara depth
	static TSharedPtr<FJsonValue> ListEmitterRenderers(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddEmitterRenderer(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveEmitterRenderer(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetRendererProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InspectDataInterface(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateNiagaraSystemFromSpec(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCompiledHLSL(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListSystemParameters(const TSharedPtr<FJsonObject>& Params);

	// v0.7.14 - module inputs, static switches, HLSL modules
	static TSharedPtr<FJsonValue> ListModuleInputs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetModuleInput(const TSharedPtr<FJsonObject>& Params);
	// Add a stock /Niagara/Modules script to an emitter's stack context.
	static TSharedPtr<FJsonValue> AddModule(const TSharedPtr<FJsonObject>& Params);

	// Depth authoring, in NiagaraHandlers_Advanced.cpp. Dynamic inputs are
	// graph nodes rather than properties; simulation stages and event handlers
	// each need a backing UNiagaraScript with an output node before their
	// struct fields mean anything; a CustomHlsl write must reconstruct the node
	// or the pins go stale. None of it is reachable by a property write.
	static TSharedPtr<FJsonValue> ListDynamicInputs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetDynamicInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveDynamicInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddSimulationStage(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveSimulationStage(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddEventHandler(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveEventHandler(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCustomHlsl(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetCustomHlsl(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveModule(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetModuleEnabled(const TSharedPtr<FJsonObject>& Params);
	// Remove an emitter handle from a system (CRUD delete).
	static TSharedPtr<FJsonValue> RemoveEmitterFromSystem(const TSharedPtr<FJsonObject>& Params);
	// Structural verify gate: does the system have emitters that will emit?
	static TSharedPtr<FJsonValue> ValidateSystem(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListStaticSwitches(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetStaticSwitch(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateModuleFromHlsl(const TSharedPtr<FJsonObject>& Params);
	// #185: Create an empty scratch-pad-style Niagara module
	static TSharedPtr<FJsonValue> CreateScratchModule(const TSharedPtr<FJsonObject>& Params);

	// Force a real compile and report it per script, in
	// NiagaraHandlers_Compile.cpp. The only way to prove a graph edit produced
	// a script the translator accepts: get_niagara_compiled_hlsl returns
	// success without compiling anything on a CPU-sim emitter.
	static TSharedPtr<FJsonValue> CompileSystem(const TSharedPtr<FJsonObject>& Params);
};
