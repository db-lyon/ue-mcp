#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Runs a multiplayer acceptance test unattended: starts PIE with the scenario's players, drives every player's
 * input and console through PIERemotePlayers, and decides each step from the logs.
 *
 * Scenario JSON:
 *   { name, map?, pie: { players, net_mode: "listen"|"dedicated", one_process, windowless?, client_fps? }, join_timeout_s, settle_ms,
 *     steps: [ { name,
 *                do: [ { client, tape: {action, values} } | { client, press: {action, frames?} }
 *                    | { client, hold: {action, value, ms} } | { client, inject: {action, value} }
 *                    | { client, console: "cmd" }
 *                    | { client, place: {at, offset?, face?} | {location, yaw?} } | { client, capture: "label" | true }
 *                    | { wait_ms } ],
 *                expect: [ { log: "host"|"client:N", pattern, min?, max? } ],
 *                forbid: [ { log, pattern } ],
 *                window_ms } ] }
 *
 * Evidence: the editor process's log (every line, captured while the run is active) and, for separate-process
 * clients, the log file each client reports when it joins, read from where it stood when the step began. In
 * one-process PIE every player logs to the editor, so `client:N` reads the host log.
 *
 * A step passes when every expectation matched its count and no forbidden pattern matched inside its window. It
 * ends early once every expectation is met, unless it forbids anything, in which case it watches the whole window.
 */
namespace MCPPIE
{
	class FPIEUatRunner
	{
	public:
		static FPIEUatRunner& Get();

		/** Validates and starts a run. False with an error if the scenario is invalid or a run is active. */
		bool Start(const TSharedPtr<FJsonObject>& Scenario, FString& OutError);

		/** Ends an active run early; it reports what it reached. */
		void Abort(const FString& Reason);

		bool IsRunning() const;

		/** Live state, then the final verdict once finished. */
		TSharedPtr<FJsonObject> Status() const;

		void Init();
		void Shutdown();

	private:
		FPIEUatRunner();
		~FPIEUatRunner();

		struct FImpl;
		TUniquePtr<FImpl> Impl;
	};
}
