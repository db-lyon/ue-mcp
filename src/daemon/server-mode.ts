/**
 * How `ue-mcp <uproject>` serves: `daemon` relays to the project's daemon,
 * which owns the editor connection and outlives editor restarts; `in-process`
 * runs the whole server inside the process the MCP client launched.
 *
 * UE_MCP_SERVER_MODE wins, then `server.mode` in the project's ue-mcp.yml, then
 * the default, daemon. A daemon serves one project, so several projects or
 * none run in process whatever is configured.
 */
import * as path from "node:path";
import { readEnv } from "../core/env.js";
import { readUeMcpConfig } from "../config/project.js";

export type ServerMode = "daemon" | "in-process";
export const DEFAULT_SERVER_MODE: ServerMode = "daemon";

export interface ResolvedServerMode {
  mode: ServerMode;
  /** Why, for the startup log. */
  reason: string;
}

function parse(raw: string | undefined): ServerMode | null {
  const v = raw?.trim().toLowerCase();
  return v === "daemon" || v === "in-process" ? v : null;
}

export function resolveServerMode(
  projectArgs: readonly string[],
  readConfig: (projectDir: string) => { server?: { mode?: string } } = readUeMcpConfig,
): ResolvedServerMode {
  if (projectArgs.length !== 1) {
    return { mode: "in-process", reason: projectArgs.length === 0 ? "no project given" : "a daemon serves one project; several were given" };
  }
  const env = readEnv("serverMode");
  const fromEnv = parse(env);
  if (fromEnv) return { mode: fromEnv, reason: "UE_MCP_SERVER_MODE" };
  let fromConfig: ServerMode | null = null;
  try {
    fromConfig = parse(readConfig(path.dirname(path.resolve(projectArgs[0]))).server?.mode);
  } catch {
    // An unreadable config is reported by the server itself; the mode falls back to the default.
  }
  if (fromConfig) return { mode: fromConfig, reason: "server.mode in ue-mcp.yml" };
  return { mode: DEFAULT_SERVER_MODE, reason: env ? `default (UE_MCP_SERVER_MODE '${env}' is not daemon or in-process)` : "default" };
}
