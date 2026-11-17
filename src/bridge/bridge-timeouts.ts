// @session independent: The call budget table and its resolution; the same answer for every editor.
/**
 * How long the client waits for a bridge call to answer (#989).
 *
 * The client waited a flat 30 seconds for every method. Two things were wrong
 * with that, and both produce the same bad outcome: the editor completes the
 * operation, the client reports a failure, and a naive retry applies the
 * mutation a second time.
 *
 *  1. Some calls are simply long. A 190-item batch on a machine that is also
 *     compiling shaders does not finish inside 30 seconds. Callers can pass a
 *     `timeoutMs` and be believed.
 *
 *  2. The two ends disagreed. The editor waits as long as an action declares
 *     at its C++ registration, and the client cut it off at 30. The client now
 *     waits at least as long as the editor will.
 *
 * Each action's timeout is declared once, at its C++ registration
 * (FMCPActionFacts), published in get_bridge_capabilities.actionFacts and
 * recorded into ACTION_FACTS, so this file keeps no copy of any of them. The
 * default and the ceiling come from the wire contract both sides read.
 */
import { ENV_VARS, readEnv } from "../core/env.js";
import { ACTION_FACTS } from "./action-facts.generated.js";
import { DEFAULT_HANDLER_TIMEOUT_SECONDS, MAX_REQUEST_TIMEOUT_SECONDS } from "./wire-contract.js";

/** What a method with no declared timeout gets: the bridge's own default. */
export const DEFAULT_BRIDGE_TIMEOUT_MS = DEFAULT_HANDLER_TIMEOUT_SECONDS * 1000;

/**
 * Slack added on top of a server-declared timeout, so the client outlives the
 * server's own deadline and can report the server's error instead of its own
 * guess. Without it the two expire together and the race decides which message
 * the user sees.
 */
export const SERVER_TIMEOUT_MARGIN_MS = 5_000;

/**
 * Upper bound on any single call, caller-supplied included. A larger request is
 * capped here, never refused; work that needs longer is split into several calls,
 * or run through the action's job form and polled where it has one.
 */
export const MAX_BRIDGE_TIMEOUT_MS = MAX_REQUEST_TIMEOUT_SECONDS * 1000;

/** Environment override for the floor under every call, in milliseconds. */
export const TIMEOUT_ENV_VAR = ENV_VARS.bridgeTimeoutMs;

/**
 * Every declared game-thread timeout, in seconds, keyed by bridge method.
 *
 * External handlers registered at runtime through
 * RegisterExternalHandlerWithTimeout are not here and cannot be, since they come
 * from plugins this repo does not build.
 */
export const REGISTERED_HANDLER_TIMEOUT_SECONDS: Readonly<Record<string, number>> = Object.fromEntries(
  Object.entries(ACTION_FACTS)
    .filter(([, facts]) => facts.timeoutSeconds !== undefined)
    .map(([method, facts]) => [method, facts.timeoutSeconds as number]),
);

/** What the editor will wait for this method, in ms, or undefined for the default. */
export function registeredTimeoutMs(method: string): number | undefined {
  const seconds = REGISTERED_HANDLER_TIMEOUT_SECONDS[method];
  return seconds === undefined ? undefined : seconds * 1000;
}

/** The floor under every call: the env override when it is a usable number. */
export function environmentTimeoutMs(env: NodeJS.ProcessEnv = process.env): number | undefined {
  const raw = readEnv("bridgeTimeoutMs", env);
  if (raw === undefined || raw === "") return undefined;
  const parsed = Number(raw);
  if (!Number.isFinite(parsed) || parsed <= 0) return undefined;
  return Math.min(parsed, MAX_BRIDGE_TIMEOUT_MS);
}

/**
 * How long to wait for `method`.
 *
 * An explicit per-call value wins outright, including a shorter one: a caller
 * that asks for less has said so on purpose. Otherwise the answer is the
 * longest of the default, the environment floor, and the server's own
 * declared limit plus its margin, so the client never gives up on a call the
 * editor is still allowed to be working on.
 */
export function resolveBridgeTimeout(
  method: string,
  explicitMs?: number,
  env: NodeJS.ProcessEnv = process.env,
): number {
  if (typeof explicitMs === "number" && Number.isFinite(explicitMs) && explicitMs > 0) {
    return Math.min(explicitMs, MAX_BRIDGE_TIMEOUT_MS);
  }
  const registered = registeredTimeoutMs(method);
  const candidates = [
    DEFAULT_BRIDGE_TIMEOUT_MS,
    environmentTimeoutMs(env) ?? 0,
    registered === undefined ? 0 : registered + SERVER_TIMEOUT_MARGIN_MS,
  ];
  return Math.min(Math.max(...candidates), MAX_BRIDGE_TIMEOUT_MS);
}
