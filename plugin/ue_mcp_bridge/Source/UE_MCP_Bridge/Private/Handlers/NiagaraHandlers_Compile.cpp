// Niagara compile: force a real compile of a system and report what the
// compiler said, per script.
//
// Every other Niagara action here answers "did the call succeed". That is a
// different question from "is the graph this call built well formed", and the
// gap between them is where the depth authoring in NiagaraHandlers_Advanced.cpp
// lives: a simulation stage, an event handler or a module removal each rewires
// a UNiagaraScript's node graph, returns success, and only fails when the
// compiler is asked to translate it. The installed engine ships no NiagaraEditor
// sources, so those graph shapes were derived from header signatures and have to
// be proved against a compile rather than against a return value.
//
// get_compiled_hlsl cannot serve as that proof. A CPU-sim emitter has no
// compiled HLSL, so it reports "Emitter is CPU-sim; no compiled HLSL available"
// and returns success without compiling anything at all.
//
// This action instead calls the system's own compile path and reads the result
// out of each script's cached VM executable data: the last compile status, the
// error message, and the compile events with their severity and the node and pin
// guids that produced them. Nothing here is reachable by a property write - the
// status lives on FNiagaraVMExecutableData, and the compile itself is a function
// call with a wait attached.
//
// Translation-unit partition of FNiagaraHandlers; registration lives in
// NiagaraHandlers.cpp.

#include "NiagaraHandlers.h"

#include "HandlerUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "NiagaraCommon.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraShared.h"
#include "NiagaraSystem.h"

namespace
{
	// Every helper here carries the NiagaraCompileLocal prefix on purpose. The
	// module is a unity build: two translation units sharing a blob merge their
	// anonymous namespaces, so a helper whose name collides with one in a
	// neighbouring handler file is a redefinition on whichever machine's
	// adaptive-unity working set groups them together.

	/** An enumerator's name without its scope, so "NCS_UpToDate" and
	 *  "ParticleSpawnScript" read the way the engine's own headers spell them. */
	template <typename TEnum>
	FString NiagaraCompileLocalEnumName(TEnum Value)
	{
		if (const UEnum* Enum = StaticEnum<TEnum>())
		{
			const FString Name = Enum->GetNameStringByValue(static_cast<int64>(Value));
			if (!Name.IsEmpty()) return Name;
		}
		return FString::Printf(TEXT("%d"), static_cast<int32>(Value));
	}

	/** True when a status means the script translated. Warnings still count:
	 *  a warning is a note about a graph the compiler accepted, and treating it
	 *  as a failure would make every ordinary system report as broken. */
	bool NiagaraCompileLocalStatusIsGood(ENiagaraScriptCompileStatus Status)
	{
		return Status == ENiagaraScriptCompileStatus::NCS_UpToDate
			|| Status == ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings
			|| Status == ENiagaraScriptCompileStatus::NCS_ComputeUpToDateWithWarnings;
	}

#if WITH_EDITORONLY_DATA
	/** One compile event, with the guids that address the node and pin it came
	 *  from. Those are what turn a message into somewhere to look. */
	TSharedPtr<FJsonObject> NiagaraCompileLocalEventToJson(const FNiagaraCompileEvent& Event)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("severity"), NiagaraCompileLocalEnumName(Event.Severity));
		Row->SetStringField(TEXT("message"), Event.Message);
		if (!Event.ShortDescription.IsEmpty())
		{
			Row->SetStringField(TEXT("shortDescription"), Event.ShortDescription);
		}
		if (Event.NodeGuid.IsValid()) Row->SetStringField(TEXT("nodeGuid"), Event.NodeGuid.ToString());
		if (Event.PinGuid.IsValid()) Row->SetStringField(TEXT("pinGuid"), Event.PinGuid.ToString());
		return Row;
	}

	/**
	 * One script's compile outcome.
	 *
	 * OutHadError is set when the status is not a good one or when the script
	 * carries an Error-severity event, because those two disagree: a script can
	 * report NCS_UpToDate and still have logged errors from a dependency.
	 */
	TSharedPtr<FJsonObject> NiagaraCompileLocalScriptToJson(
		const UNiagaraScript* Script,
		const FString& EmitterName,
		bool& OutHadError,
		TArray<TSharedPtr<FJsonValue>>& OutErrors)
	{
		OutHadError = false;
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		if (!Script) return Row;

		const FString ScriptName = Script->GetName();
		Row->SetStringField(TEXT("scriptName"), ScriptName);
		Row->SetStringField(TEXT("scriptPath"), Script->GetPathName());
		if (!EmitterName.IsEmpty()) Row->SetStringField(TEXT("emitterName"), EmitterName);
		Row->SetStringField(TEXT("usage"), NiagaraCompileLocalEnumName(Script->GetUsage()));

		const ENiagaraScriptCompileStatus Status = Script->GetLastCompileStatus();
		Row->SetStringField(TEXT("status"), NiagaraCompileLocalEnumName(Status));
		Row->SetBoolField(TEXT("upToDate"), NiagaraCompileLocalStatusIsGood(Status));

		const FNiagaraVMExecutableData& VM = Script->GetVMExecutableData();
		Row->SetStringField(TEXT("errorMsg"), VM.ErrorMsg);

		TArray<TSharedPtr<FJsonValue>> Events;
		int32 ErrorEvents = 0;
		int32 WarningEvents = 0;
		for (const FNiagaraCompileEvent& Event : VM.LastCompileEvents)
		{
			if (Event.Severity == FNiagaraCompileEventSeverity::Error) ++ErrorEvents;
			else if (Event.Severity == FNiagaraCompileEventSeverity::Warning) ++WarningEvents;

			TSharedPtr<FJsonObject> EventRow = NiagaraCompileLocalEventToJson(Event);
			Events.Add(MakeShared<FJsonValueObject>(EventRow));

			if (Event.Severity != FNiagaraCompileEventSeverity::Error) continue;
			// The flat error list carries the script identity with it, because a
			// caller reading `errors` is not holding the row it came from.
			TSharedPtr<FJsonObject> Flat = MakeShared<FJsonObject>();
			Flat->SetStringField(TEXT("scriptName"), ScriptName);
			if (!EmitterName.IsEmpty()) Flat->SetStringField(TEXT("emitterName"), EmitterName);
			Flat->SetStringField(TEXT("message"), Event.Message);
			if (Event.NodeGuid.IsValid()) Flat->SetStringField(TEXT("nodeGuid"), Event.NodeGuid.ToString());
			if (Event.PinGuid.IsValid()) Flat->SetStringField(TEXT("pinGuid"), Event.PinGuid.ToString());
			OutErrors.Add(MakeShared<FJsonValueObject>(Flat));
		}
		Row->SetArrayField(TEXT("events"), Events);
		Row->SetNumberField(TEXT("errorEventCount"), ErrorEvents);
		Row->SetNumberField(TEXT("warningEventCount"), WarningEvents);

		OutHadError = !NiagaraCompileLocalStatusIsGood(Status) || ErrorEvents > 0;
		if (OutHadError && ErrorEvents == 0)
		{
			// A bad status with no event of its own would otherwise leave the
			// `errors` array empty on a system that plainly did not compile.
			TSharedPtr<FJsonObject> Flat = MakeShared<FJsonObject>();
			Flat->SetStringField(TEXT("scriptName"), ScriptName);
			if (!EmitterName.IsEmpty()) Flat->SetStringField(TEXT("emitterName"), EmitterName);
			Flat->SetStringField(TEXT("message"), VM.ErrorMsg.IsEmpty()
				? FString::Printf(
					TEXT("Compile status is %s and the script logged no compile event explaining it."),
					*NiagaraCompileLocalEnumName(Status))
				: VM.ErrorMsg);
			OutErrors.Add(MakeShared<FJsonValueObject>(Flat));
		}
		return Row;
	}
#endif // WITH_EDITORONLY_DATA
}

TSharedPtr<FJsonValue> FNiagaraHandlers::CompileSystem(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

#if !WITH_EDITORONLY_DATA
	return MCPError(TEXT(
		"niagara(compile) needs editor-only data: compile status and compile events are stripped from a "
		"cooked build, so there is nothing to report."));
#else
	FString SystemPath;
	if (auto Err = RequireString(Params, TEXT("systemPath"), SystemPath)) return Err;

	TSharedPtr<FJsonValue> Error;
	UObject* Asset = MCPRequireAssetObject(SystemPath, Error, TEXT("NiagaraSystem"));
	if (!Asset) return Error;
	UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset);
	if (!System) return MCPAssetWrongTypeError(SystemPath, Asset, TEXT("NiagaraSystem"));

	// force=true recompiles even when nothing looks dirty, which is the point:
	// a graph edit that left the change tracking untouched is exactly the bug
	// this action exists to catch.
	const bool bForce = OptionalBool(Params, TEXT("force"), true);
	const bool bIncludeGpuShaders = OptionalBool(Params, TEXT("includeGpuShaders"), false);

	const bool bQueued = System->RequestCompile(bForce);
	// Blocking is deliberate. An asynchronous compile would hand back the
	// PREVIOUS compile's status, which reads as a pass for a graph that has not
	// been translated yet.
	System->WaitForCompilationComplete(bIncludeGpuShaders, /*bShowProgress*/ false);
	const bool bPollComplete = System->PollForCompilationComplete(/*bFlushRequestCompile*/ true);

	TArray<TSharedPtr<FJsonValue>> ScriptRows;
	TArray<TSharedPtr<FJsonValue>> Errors;
	int32 ScriptCount = 0;
	int32 FailedScriptCount = 0;

	const auto AppendScript = [&ScriptRows, &Errors, &ScriptCount, &FailedScriptCount]
		(const UNiagaraScript* Script, const FString& EmitterName)
	{
		if (!Script) return;
		bool bHadError = false;
		TSharedPtr<FJsonObject> Row = NiagaraCompileLocalScriptToJson(Script, EmitterName, bHadError, Errors);
		ScriptRows.Add(MakeShared<FJsonValueObject>(Row));
		++ScriptCount;
		if (bHadError) ++FailedScriptCount;
	};

	AppendScript(System->GetSystemSpawnScript(), FString());
	AppendScript(System->GetSystemUpdateScript(), FString());

	TArray<TSharedPtr<FJsonValue>> EmitterRows;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const FString EmitterName = Handle.GetName().ToString();
		TSharedPtr<FJsonObject> EmitterRow = MakeShared<FJsonObject>();
		EmitterRow->SetStringField(TEXT("emitterName"), EmitterName);
		EmitterRow->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());

		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data)
		{
			// A handle with no data is a broken emitter, and a compile report
			// that silently skipped it would read as a clean system.
			EmitterRow->SetBoolField(TEXT("resolved"), false);
			EmitterRow->SetStringField(TEXT("note"), TEXT(
				"This emitter handle resolves to no emitter data, so none of its scripts could be "
				"compiled or reported."));
			EmitterRows.Add(MakeShared<FJsonValueObject>(EmitterRow));
			TSharedPtr<FJsonObject> Flat = MakeShared<FJsonObject>();
			Flat->SetStringField(TEXT("emitterName"), EmitterName);
			Flat->SetStringField(TEXT("message"), TEXT("Emitter handle resolves to no emitter data."));
			Errors.Add(MakeShared<FJsonValueObject>(Flat));
			++FailedScriptCount;
			continue;
		}

		EmitterRow->SetBoolField(TEXT("resolved"), true);
		EmitterRow->SetStringField(TEXT("simTarget"), NiagaraCompileLocalEnumName(Data->SimTarget));

		TArray<UNiagaraScript*> Scripts;
		// bCompilableOnly: the emitter spawn and update scripts are not
		// translated at all, so their status is meaningless and reporting it
		// would put a permanent NCS_Unknown in every result.
		Data->GetScripts(Scripts, /*bCompilableOnly*/ true, /*bEnabledOnly*/ false);
		EmitterRow->SetNumberField(TEXT("scriptCount"), Scripts.Num());
		EmitterRows.Add(MakeShared<FJsonValueObject>(EmitterRow));

		for (const UNiagaraScript* Script : Scripts)
		{
			AppendScript(Script, EmitterName);
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("systemPath"), System->GetPathName());
	Result->SetBoolField(TEXT("forced"), bForce);
	Result->SetBoolField(TEXT("compileRequested"), bQueued);
	Result->SetBoolField(TEXT("includedGpuShaders"), bIncludeGpuShaders);
	Result->SetBoolField(TEXT("compilationSettled"), bPollComplete);
	Result->SetBoolField(TEXT("outstandingRequests"), System->HasOutstandingCompilationRequests(bIncludeGpuShaders));
	Result->SetArrayField(TEXT("emitters"), EmitterRows);
	Result->SetArrayField(TEXT("scripts"), ScriptRows);
	Result->SetNumberField(TEXT("scriptCount"), ScriptCount);
	Result->SetNumberField(TEXT("failedScriptCount"), FailedScriptCount);
	Result->SetArrayField(TEXT("errors"), Errors);

	const bool bCompiled = FailedScriptCount == 0 && Errors.Num() == 0;
	Result->SetBoolField(TEXT("compiled"), bCompiled);
	Result->SetStringField(TEXT("note"), bCompiled
		? TEXT("Every compilable script in this system translated. This is the assertion a graph edit has "
			   "to survive: niagara(validate) answers whether the system emits, which a malformed script "
			   "can still pass.")
		: TEXT("At least one script did not translate. Each entry in 'errors' names the script and, where "
			   "the compiler reported one, the node and pin guid that produced it. Read the whole graph "
			   "back with niagara(get_emitter_info) or niagara(list_dynamic_inputs)."));

	// Idempotency: RequestCompile answers whether this call actually put a
	// compile on the queue. With force=false a system whose scripts are already
	// translated and unchanged returns false, so a replayed flow step is a
	// no-op rather than a second translation. force=true is the default, and it
	// requests one every time by design, which the field then reports honestly.
	Result->SetBoolField(TEXT("unchanged"), !bQueued);
	if (bQueued) MCPSetUpdated(Result);

	// Compiling changes no authored state. It writes the translated VM bytecode
	// and shader maps onto the system's scripts, which is exactly what an
	// uncompiled system is missing, and leaves the package dirty. There is no
	// previous graph to put back and no bridge call that un-translates a
	// script; the only route to the old build is to edit the graph back.
	MCPSetNoRollback(Result, TEXT(
		"Compiling replaces the translated bytecode and shader maps cached on this system's scripts and leaves the "
		"package dirty. No authored state changed, so there is nothing to restore, and no bridge call un-translates "
		"a script back to its previous build."));
	return MCPResult(Result);
#endif // WITH_EDITORONLY_DATA
}
