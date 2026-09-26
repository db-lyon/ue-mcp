#pragma once

// Emitter and stack-context addressing shared by every Niagara handler file.
// It lives in a header because the module is a unity build: a file-local copy
// in a second .cpp is a C2084 redefinition, and copies drift apart.

#include "CoreMinimal.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraCommon.h"

namespace MCPNiagara
{
	/** The four emitter stack contexts a module can live in, spelled once. */
	inline const TCHAR* ValidStackContexts()
	{
		return TEXT("ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate");
	}

	/** Index of the emitter handle named EmitterName (case-insensitive), or at
	 *  EmitterIndex when no name is given. INDEX_NONE with OutError naming the
	 *  emitters present when neither addresses one. */
	inline int32 ResolveEmitterHandleIndex(
		UNiagaraSystem* System, const FString& EmitterName, int32 EmitterIndex, FString& OutError)
	{
		if (!System) { OutError = TEXT("System is null"); return INDEX_NONE; }

		const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : Handles) Names.Add(H.GetName().ToString());
		const FString Present = Names.Num() ? FString::Join(Names, TEXT(", ")) : FString(TEXT("none"));

		if (!EmitterName.IsEmpty())
		{
			for (int32 i = 0; i < Names.Num(); ++i)
			{
				if (Names[i].Equals(EmitterName, ESearchCase::IgnoreCase)) return i;
			}
			OutError = FString::Printf(
				TEXT("No emitter named '%s' in '%s'. Emitters present: [%s]. Address one by 'emitterName' or by 'emitterIndex' (0-%d)."),
				*EmitterName, *System->GetPathName(), *Present, FMath::Max(0, Names.Num() - 1));
			return INDEX_NONE;
		}
		if (EmitterIndex >= 0 && EmitterIndex < Handles.Num()) return EmitterIndex;

		OutError = FString::Printf(
			TEXT("emitterIndex %d is out of range for '%s', which has %d emitter(s): [%s]. Pass 'emitterName' instead when you know it."),
			EmitterIndex, *System->GetPathName(), Names.Num(), *Present);
		return INDEX_NONE;
	}

	/** The addressed emitter's version data, with the emitter and version it
	 *  belongs to. nullptr with OutError set when nothing resolves. */
	inline FVersionedNiagaraEmitterData* ResolveEmitter(
		UNiagaraSystem* System, const FString& EmitterName, int32 EmitterIndex,
		UNiagaraEmitter*& OutEmitter, FGuid& OutVersion, FString& OutError)
	{
		OutEmitter = nullptr;
		const int32 TargetIdx = ResolveEmitterHandleIndex(System, EmitterName, EmitterIndex, OutError);
		if (TargetIdx == INDEX_NONE) return nullptr;

		const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[TargetIdx];
		FVersionedNiagaraEmitter VE = Handle.GetInstance();
		OutEmitter = VE.Emitter;
		OutVersion = VE.Version;
		FVersionedNiagaraEmitterData* Data = VE.GetEmitterData();
		if (!Data)
		{
			OutError = FString::Printf(
				TEXT("Emitter '%s' resolved but carries no version data for version %s. The emitter asset may be a stale or unmigrated version."),
				*Handle.GetName().ToString(), *OutVersion.ToString());
		}
		return Data;
	}

	inline UNiagaraGraph* GraphOfScript(UNiagaraScript* Script)
	{
		if (!Script) return nullptr;
		UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		return Src ? Src->NodeGraph : nullptr;
	}

	struct FStackSlot
	{
		FString Context;
		UNiagaraScript* Script = nullptr;
		ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleSpawnScript;
	};

	/** One stack context by name (case-insensitive). False for a name that is
	 *  not one of ValidStackContexts(). */
	inline bool ResolveStackContext(FVersionedNiagaraEmitterData* Data, const FString& Ctx, FStackSlot& Out)
	{
		if (!Data) return false;
		struct FEntry { const TCHAR* Name; ENiagaraScriptUsage Usage; };
		static const FEntry Table[] = {
			{ TEXT("ParticleSpawn"),  ENiagaraScriptUsage::ParticleSpawnScript },
			{ TEXT("ParticleUpdate"), ENiagaraScriptUsage::ParticleUpdateScript },
			{ TEXT("EmitterSpawn"),   ENiagaraScriptUsage::EmitterSpawnScript },
			{ TEXT("EmitterUpdate"),  ENiagaraScriptUsage::EmitterUpdateScript },
		};
		for (const FEntry& Entry : Table)
		{
			if (!Ctx.Equals(Entry.Name, ESearchCase::IgnoreCase)) continue;
			UNiagaraScript* Script = nullptr;
			switch (Entry.Usage)
			{
			case ENiagaraScriptUsage::ParticleSpawnScript:  Script = Data->SpawnScriptProps.Script; break;
			case ENiagaraScriptUsage::ParticleUpdateScript: Script = Data->UpdateScriptProps.Script; break;
			case ENiagaraScriptUsage::EmitterSpawnScript:   Script = Data->EmitterSpawnScriptProps.Script; break;
			default:                                        Script = Data->EmitterUpdateScriptProps.Script; break;
			}
			Out = { Entry.Name, Script, Entry.Usage };
			return true;
		}
		return false;
	}

	/** Every context Filter selects ("all" or empty selects the four), skipping
	 *  a context with no script. False, with OutError, for an unknown Filter. */
	inline bool CollectStackContexts(
		FVersionedNiagaraEmitterData* Data, const FString& Filter, TArray<FStackSlot>& Out, FString& OutError)
	{
		static const TCHAR* All[] = { TEXT("ParticleSpawn"), TEXT("ParticleUpdate"), TEXT("EmitterSpawn"), TEXT("EmitterUpdate") };
		const bool bAll = Filter.IsEmpty() || Filter.Equals(TEXT("all"), ESearchCase::IgnoreCase);
		bool bKnown = bAll;
		for (const TCHAR* Name : All)
		{
			if (!bAll && !Filter.Equals(Name, ESearchCase::IgnoreCase)) continue;
			bKnown = true;
			FStackSlot Slot;
			if (ResolveStackContext(Data, Name, Slot) && Slot.Script) Out.Add(Slot);
		}
		if (!bKnown)
		{
			OutError = FString::Printf(
				TEXT("Unknown stackContext '%s'. Valid values: %s, or 'all'."), *Filter, ValidStackContexts());
		}
		return bKnown;
	}
}
