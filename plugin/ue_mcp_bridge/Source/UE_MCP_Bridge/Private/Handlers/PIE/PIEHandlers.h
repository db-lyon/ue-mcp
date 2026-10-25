#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FMCPHandlerRegistry;

/**
 * The pie category: record, replay, observe, diff, inject input, and run
 * test scripts during Play-In-Editor sessions. Bridge methods are named
 * pie_<action>; the TS tool exposes them unprefixed as pie(<action>).
 */
class FPIEHandlers
{
public:
	static void RegisterHandlers(FMCPHandlerRegistry& Registry);

	/** Recorder, replayer, observer, session log, script runner, panel and toolbar. Called once from the module. */
	static void StartupSystems();
	static void ShutdownSystems();

	// PIE inspection
	static TSharedPtr<FJsonValue> GetPieAnimState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetPieAnimProperties(const TSharedPtr<FJsonObject>& Params);

	// Session log / errors
	static TSharedPtr<FJsonValue> PieSessionErrors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieSessionLog(const TSharedPtr<FJsonObject>& Params);

	// Profiling
	static TSharedPtr<FJsonValue> PiePerfSummary(const TSharedPtr<FJsonObject>& Params);

	// Reproduction tests
	static TSharedPtr<FJsonValue> PieTestScaffold(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieTestRun(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieTestList(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieAssertEval(const TSharedPtr<FJsonObject>& Params);

	// Declarative scenario
	static TSharedPtr<FJsonValue> PieScenarioScaffold(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScenarioValidate(const TSharedPtr<FJsonObject>& Params);

	// Input injection
	static TSharedPtr<FJsonValue> InjectInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputStart(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputUpdate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputTape(const TSharedPtr<FJsonObject>& Params);

	// Multiplayer players
	static TSharedPtr<FJsonValue> PieClients(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieConsole(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRemoteResult(const TSharedPtr<FJsonObject>& Params);

	// Multiplayer acceptance runs
	static TSharedPtr<FJsonValue> PieUatRun(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieUatStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieUatAbort(const TSharedPtr<FJsonObject>& Params);

	// Shared selector parsing for handlers that target a PIE instance.
	static bool ParsePIEInstance(const TSharedPtr<FJsonObject>& Params,
		int32& OutPIEInstance, FString& OutError);

	// Recording
	static TSharedPtr<FJsonValue> PieRecordArm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordDisarm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordList(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordRead(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordDelete(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieMark(const TSharedPtr<FJsonObject>& Params);

	// Replay
	static TSharedPtr<FJsonValue> PieReplayArm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayRun(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayDisarm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayAnalyze(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayState(const TSharedPtr<FJsonObject>& Params);

	// Diff
	static TSharedPtr<FJsonValue> PieRecordDiff(const TSharedPtr<FJsonObject>& Params);

	// Observation profiles
	static TSharedPtr<FJsonValue> PieProfileCreate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieProfileRead(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieProfileUpdate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieProfileDelete(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieProfileList(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveArm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveDisarm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveList(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveRead(const TSharedPtr<FJsonObject>& Params);

	// Test scripts
	static void RegisterScriptHandlers(FMCPHandlerRegistry& Registry);
	static TSharedPtr<FJsonValue> PieScriptCreate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptRead(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptUpdate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptDelete(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptValidate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptValues(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptList(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptRun(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptVerdict(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptResults(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptRuns(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieScriptReplay(const TSharedPtr<FJsonObject>& Params);
};
