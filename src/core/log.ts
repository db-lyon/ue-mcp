/**
 * Structured logging for the ue-mcp server.
 *
 * All logs go to stderr (stdout is reserved for MCP stdio protocol). Each
 * line is prefixed with `[ue-mcp]` so it can be filtered in agent transcripts.
 *
 * Levels are filtered by UE_MCP_LOG_LEVEL (debug | info | warn | error).
 * Default is info.
 */
import { readEnv } from "./env.js";

type Level = "debug" | "info" | "warn" | "error";

const LEVELS: Record<Level, number> = { debug: 10, info: 20, warn: 30, error: 40 };

function threshold(): number {
  const envLevel = (readEnv("logLevel") ?? "info").toLowerCase();
  return LEVELS[envLevel as Level] ?? LEVELS.info;
}

function fmt(level: Level, component: string, msg: string, err?: unknown): string {
  const detail = err === undefined
    ? ""
    : ` :: ${err instanceof Error ? `${err.name}: ${err.message}` : String(err)}`;
  return `[ue-mcp] ${level} ${component}: ${msg}${detail}`;
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
