import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import yaml from "js-yaml";
import { warn } from "./log.js";

/**
 * User-global config layer: `~/.ue-mcp/config.yml` (CumulusCI-style, after
 * cumulusci's `~/.cumulusci/cumulusci.yml`).
 *
 * This is untracked, per-user *config* - a place to set your personal defaults
 * once for every project on this machine (context strategy, native-tool
 * preferences, personal tasks/flows). It mirrors the structure of a project
 * `ue-mcp.yml` (top-level `ue-mcp:` block plus `tasks:` / `flows:`).
 *
 * Precedence, low -> high:
 *
 *     built-in defaults
 *     ~/.ue-mcp/config.yml        <- this file (user-global, untracked)
 *     <project>/ue-mcp.yml        (project, tracked)
 *     <project>/ue-mcp.{env}.yml  (env overlay)
 *     <project>/ue-mcp.local.yml  (per-machine, untracked)
 *     env vars (UE_MCP_*)         (highest)
 *
 * Deliberately distinct from `~/.ue-mcp/state.json`, which holds machine-written
 * *state* (installed-hook paths, feedback-mode preference) - bookkeeping the
 * tool writes, never hand-edited config. Config layers here; state lives there.
 */
export function globalConfigPath(): string {
  return (
    process.env.UE_MCP_GLOBAL_CONFIG ||
    path.join(os.homedir(), ".ue-mcp", "config.yml")
  );
}

/**
 * The full parsed `~/.ue-mcp/config.yml` document (`ue-mcp:` block, `tasks:`,
 * `flows:`, ...). Returns {} when the file is absent, empty, or unparseable.
 */
export function readGlobalConfigDoc(): Record<string, unknown> {
  const file = globalConfigPath();
  if (!fs.existsSync(file)) return {};
  try {
    const raw = yaml.load(fs.readFileSync(file, "utf-8"));
    return raw && typeof raw === "object" && !Array.isArray(raw)
      ? (raw as Record<string, unknown>)
      : {};
  } catch (e) {
    warn("config", `failed to parse ${file} - ignoring the user-global config layer`, e);
    return {};
  }
}

/** Just the `ue-mcp:` block from the user-global config, or {} when absent. */
export function readGlobalUeMcpBlock(): Record<string, unknown> {
  const block = readGlobalConfigDoc()["ue-mcp"];
  return block && typeof block === "object" && !Array.isArray(block)
    ? (block as Record<string, unknown>)
    : {};
}
