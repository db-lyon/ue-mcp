/**
 * Reading and writing ue-mcp.yml and its layers. Every file in the cascade
 * shares one shape (a `ue-mcp:` block plus `tasks:`, `flows:`, `plugins:`),
 * and is parsed and written here. Precedence, low -> high:
 *
 *     built-in defaults
 *     ~/.ue-mcp/config.yml        user-global, untracked
 *     <project>/ue-mcp.yml        project, tracked
 *     <project>/ue-mcp.{env}.yml  env overlay
 *     <project>/ue-mcp.local.yml  per-machine, untracked
 *     env vars (UE_MCP_*)         applied where each setting is consumed
 *
 * Machine state (installed hooks, feedback and dialog modes) is not config and
 * lives in ~/.ue-mcp/state.json. Kept free of ProjectContext so a hook process
 * can read a project's config without loading one.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import { warn } from "./log.js";
import { userDir } from "./user-dir.js";
import { readEnv } from "./env.js";
import { dumpYaml } from "./yaml-dump.js";

/** A parsed config file: its top-level keys. */
export type ConfigDoc = Record<string, unknown>;

export const CONFIG_FILE = "ue-mcp.yml";
export const LOCAL_CONFIG_FILE = "ue-mcp.local.yml";

/** A project's tracked ue-mcp.yml. */
export function projectConfigPath(projectDir: string): string {
  return path.join(projectDir, CONFIG_FILE);
}

/** A project's untracked per-machine layer. */
export function localConfigPath(projectDir: string): string {
  return path.join(projectDir, LOCAL_CONFIG_FILE);
}

/** A project's `ue-mcp.<name>.yml` overlay. */
export function overlayConfigPath(projectDir: string, name: string): string {
  return path.join(projectDir, `ue-mcp.${name}.yml`);
}

/** The user-global layer: UE_MCP_GLOBAL_CONFIG, else ~/.ue-mcp/config.yml. */
export function globalConfigPath(): string {
  return readEnv("globalConfig") || path.join(userDir(), "config.yml");
}

/**
 * Parse one config file. Absent, empty, or not a mapping reads as {}. A file
 * that does not parse throws, unless `onError` is given, which is told and
 * the file reads as {}.
 */
export function readConfigDoc(file: string, onError?: (error: unknown) => void): ConfigDoc {
  if (!fs.existsSync(file)) return {};
  try {
    const raw = yaml.load(fs.readFileSync(file, "utf-8"), { filename: file });
    return raw && typeof raw === "object" && !Array.isArray(raw) ? (raw as ConfigDoc) : {};
  } catch (e) {
    if (!onError) throw e;
    onError(e);
    return {};
  }
}

/** The `ue-mcp:` block of a parsed file, or {} when it has none. */
export function ueMcpBlockOf(doc: ConfigDoc): Record<string, unknown> {
  const block = doc["ue-mcp"];
  return block && typeof block === "object" && !Array.isArray(block)
    ? (block as Record<string, unknown>)
    : {};
}

/** A file's `ue-mcp:` block; a file that does not parse is named and skipped. */
export function readUeMcpBlock(file: string): Record<string, unknown> {
  return ueMcpBlockOf(
    readConfigDoc(file, (e) => warn("project", `failed to parse ${file} - skipping ue-mcp: block from this file`, e)),
  );
}

/** Write a whole config file, creating its directory when needed. */
export function writeConfigDoc(file: string, doc: ConfigDoc): void {
  const dir = path.dirname(file);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(file, dumpYaml(doc), "utf-8");
}

/**
 * The full user-global document. A file that does not parse is reported
 * through `onError` and ignored.
 */
export function readGlobalConfigDoc(
  onError: (file: string, error: unknown) => void = (file, error) =>
    warn("config", `failed to parse ${file} - ignoring the user-global config layer`, error),
): ConfigDoc {
  const file = globalConfigPath();
  return readConfigDoc(file, (e) => onError(file, e));
}

/** Just the `ue-mcp:` block from the user-global config, or {} when absent. */
export function readGlobalUeMcpBlock(): Record<string, unknown> {
  return ueMcpBlockOf(readGlobalConfigDoc());
}

export type ConfigLayerTarget = "global" | "project" | "env" | "local";

export interface ConfigLayerFile {
  target: ConfigLayerTarget;
  file: string;
}

/**
 * The files of a project's cascade in precedence order, low -> high. The
 * caller names the overlay, because the server and the editor plugin choose
 * it by different rules.
 */
export function configLayerFiles(projectDir: string, overlay?: string): ConfigLayerFile[] {
  const layers: ConfigLayerFile[] = [
    { target: "global", file: globalConfigPath() },
    { target: "project", file: projectConfigPath(projectDir) },
  ];
  if (overlay) layers.push({ target: "env", file: overlayConfigPath(projectDir, overlay) });
  layers.push({ target: "local", file: localConfigPath(projectDir) });
  return layers;
}
