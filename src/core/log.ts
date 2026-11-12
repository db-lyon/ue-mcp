/**
 * Structured logging for the ue-mcp server.
 *
 * All logs go to stderr (stdout is reserved for MCP stdio protocol). Each
 * line is prefixed with `[ue-mcp]` so it can be filtered in agent transcripts.
 *
 * Levels are filtered by UE_MCP_LOG_LEVEL (debug | info | warn | error).
 * Default is info; a process can lower its own default with setDefaultLogLevel.
 */
import { readEnv } from "./env.js";

type Level = "debug" | "info" | "warn" | "error";

const LEVELS: Record<Level, number> = { debug: 10, info: 20, warn: 30, error: 40 };

let defaultLevel: Level = "info";

/** The level used when UE_MCP_LOG_LEVEL is unset. A terminal command wants warnings only. */
export function setDefaultLogLevel(level: Level): void {
  defaultLevel = level;
}

function threshold(): number {
  const envLevel = (readEnv("logLevel") ?? defaultLevel).toLowerCase();
  return LEVELS[envLevel as Level] ?? LEVELS.info;
}

let stamped = false;

/** Put the time on every line: the daemon's log is read after the fact, when "when" is the question. */
export function stampLogs(on = true): void {
  stamped = on;
}

function fmt(level: Level, component: string, msg: string, err?: unknown): string {
  const detail = err === undefined
    ? ""
    : ` :: ${err instanceof Error ? `${err.name}: ${err.message}` : String(err)}`;
  return `[ue-mcp] ${stamped ? `${new Date().toISOString()} ` : ""}${level} ${component}: ${msg}${detail}`;
}

export function debug(component: string, msg: string, err?: unknown): void {
  if (threshold() <= LEVELS.debug) console.error(fmt("debug", component, msg, err));
}

export function info(component: string, msg: string, err?: unknown): void {
  if (threshold() <= LEVELS.info) console.error(fmt("info", component, msg, err));
}

export function warn(component: string, msg: string, err?: unknown): void {
  if (threshold() <= LEVELS.warn) console.error(fmt("warn", component, msg, err));
}

export function error(component: string, msg: string, err?: unknown): void {
  if (threshold() <= LEVELS.error) console.error(fmt("error", component, msg, err));
}
