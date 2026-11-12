/**
 * Attaching ue-mcp to Epic's Unreal Assistant (the AIAssistant plugin, UE 5.9).
 *
 * What the engine supports, read from AIAssistantConfig.{h,cpp} and
 * AIAssistantAgentProcessHost.cpp in UE 5.9:
 *
 * - The assistant reads one JSON file, AIAssistant.json. FConfig::FindConfigFile
 *   takes the FIRST that exists, in this order: the Restricted
 *   (NotForLicensees, NoRedist, LimitedAccess) variants of the search
 *   directories, then Engine/Config/, FPaths::EngineUserDir() and
 *   FPaths::EngineVersionAgnosticUserDir(). Files are not merged.
 * - On an installed engine (Engine/Build/InstalledBuild.txt) the two user
 *   directories are <UserSettingsDir>/UnrealEngine/<major.minor>/ and
 *   <UserSettingsDir>/UnrealEngine/Common/, where UserSettingsDir is
 *   %LOCALAPPDATA% on Windows. On a source build both are Engine/ itself.
 * - `mcp_servers` is a JSON array of ACP MCP server descriptors that the host
 *   hands to the page in the agent "environment" event, and so to the ACP agent
 *   the assistant runs (the `agent` in this file, or the external agent picked
 *   in Editor Preferences). When the key is absent the host sends the editor's
 *   own MCP server instead (FindEditorMcpServersRawJson: name "unreal-mcp",
 *   type "http", the server's URL, empty headers). A present list REPLACES that
 *   default, and "[]" turns it off.
 *
 * So ue-mcp attaches by adding a stdio descriptor to `mcp_servers` in the user
 * file the engine would read, keeping every other key and server. When the
 * file had no list, the editor's own server is written beside ue-mcp's, so
 * adding ue-mcp does not take Epic's tools away. Detaching removes only
 * ue-mcp's entry, and drops a list that is left holding nothing but that
 * default, so the engine goes back to choosing it itself.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { ueMcpServerArgs, type ServerLaunch } from "../claude-code/mcp-client-config.js";
import { REPLACED_BACKUP_SUFFIX, UNREAL_MCP_SERVER_NAME } from "../claude-code/unreal-mcp-entries.js";
import { engineAtLeast, enginePluginPresent, uprojectPluginState } from "./unreal-mcp-server.js";

export const UNREAL_ASSISTANT_PLUGIN = "AIAssistant";
export const ASSISTANT_CONFIG_FILENAME = "AIAssistant.json";
export const UE_MCP_ASSISTANT_SERVER_NAME = "ue-mcp";
const RESTRICTED = ["NotForLicensees", "NoRedist", "LimitedAccess"];

export interface UnrealAssistantInputs {
  uprojectPath: string;
  engineVersion: string | null;
  engineRoot: string | null;
  env?: NodeJS.ProcessEnv;
  platform?: NodeJS.Platform;
}

export interface UnrealAssistantStatus {
  /** Whether the project's engine has the assistant and the project does not turn it off. */
  available: boolean;
  /** Why it is not available, when it is not. */
  reason: string | null;
  /** The file ue-mcp reads and writes: the user file the engine would load. */
  configPath: string | null;
  /** An engine-level AIAssistant.json that wins over every user file, so the user file is ignored. */
  shadowedBy: string | null;
  /** Whether configPath's mcp_servers holds a ue-mcp entry. */
  attached: boolean;
  /** The project that entry serves, when it names one. */
  attachedProject: string | null;
  /** True when configPath holds JSON the engine would refuse, which ue-mcp then leaves alone. */
  invalid: boolean;
}

export interface McpDescriptor {
  name: string;
  [key: string]: unknown;
}

/** FPlatformProcess::UserSettingsDir: %LOCALAPPDATA% on Windows. */
export function userSettingsDir(env: NodeJS.ProcessEnv = process.env, platform: NodeJS.Platform = process.platform): string {
  const home = (platform === "win32" ? env.USERPROFILE || env.HOME : env.HOME) || os.homedir();
  if (platform === "win32") return env.LOCALAPPDATA || path.join(home, "AppData", "Local");
  if (platform === "darwin") return path.join(home, "Library", "Application Support");
  return env.XDG_CONFIG_HOME || path.join(home, ".config");
}

function isInstalledEngine(engineRoot: string | null): boolean {
  return !engineRoot || fs.existsSync(path.join(engineRoot, "Engine", "Build", "InstalledBuild.txt"));
}

/**
 * The places the assistant looks for its config, in its own order, split into
 * the engine's files (which ue-mcp never writes) and the user's.
 */
export function assistantSearchPaths(inputs: UnrealAssistantInputs): { engine: string[]; user: string[] } {
  const env = inputs.env ?? process.env;
  const platform = inputs.platform ?? process.platform;
  const engineDir = inputs.engineRoot ? path.join(inputs.engineRoot, "Engine") : null;
  const engine: string[] = [];
  if (engineDir) {
    for (const r of RESTRICTED) engine.push(path.join(engineDir, "Restricted", r, "Config", ASSISTANT_CONFIG_FILENAME));
    if (!isInstalledEngine(inputs.engineRoot)) {
      for (const r of RESTRICTED) engine.push(path.join(engineDir, "Restricted", r, ASSISTANT_CONFIG_FILENAME));
    }
    engine.push(path.join(engineDir, "Config", ASSISTANT_CONFIG_FILENAME));
  }
  let user: string[];
  if (isInstalledEngine(inputs.engineRoot)) {
    const base = path.join(userSettingsDir(env, platform), "UnrealEngine");
    const version = /^\d+\.\d+/.exec(inputs.engineVersion ?? "")?.[0];
    user = [...(version ? [path.join(base, version, ASSISTANT_CONFIG_FILENAME)] : []), path.join(base, "Common", ASSISTANT_CONFIG_FILENAME)];
  } else {
    user = [path.join(engineDir!, ASSISTANT_CONFIG_FILENAME)];
  }
  return { engine, user };
}

function readJsonObject(file: string): { value: Record<string, unknown> | null; invalid: boolean } {
  let raw: string;
  try {
    raw = fs.readFileSync(file, "utf-8");
  } catch {
    return { value: null, invalid: false };
  }
  try {
    const parsed = JSON.parse(raw.replace(/^﻿/, ""));
    if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) return { value: parsed as Record<string, unknown>, invalid: false };
  } catch {
    /* fall through */
  }
  return { value: null, invalid: true };
}

function isDescriptor(v: unknown): v is McpDescriptor {
  return !!v && typeof v === "object" && typeof (v as { name?: unknown }).name === "string";
}

/** The project a ue-mcp stdio descriptor serves: its last .uproject argument. */
function projectOf(entry: McpDescriptor): string | null {
  const args = Array.isArray(entry.args) ? entry.args.filter((a): a is string => typeof a === "string") : [];
  return [...args].reverse().find((a) => /\.uproject$/i.test(a)) ?? null;
}

export function readUnrealAssistant(inputs: UnrealAssistantInputs): UnrealAssistantStatus {
  const none = (reason: string): UnrealAssistantStatus => ({
    available: false, reason, configPath: null, shadowedBy: null, attached: false, attachedProject: null, invalid: false,
  });
  const present = enginePluginPresent(inputs.engineRoot, UNREAL_ASSISTANT_PLUGIN);
  const recent = engineAtLeast(inputs.engineVersion, 5, 9);
  if (recent === false) return none(`Unreal Assistant takes MCP servers from UE 5.9; this project is on ${inputs.engineVersion}`);
  if (present === false || (present === null && recent !== true)) return none("the project's engine has no Unreal Assistant plugin");
  if (uprojectPluginState(inputs.uprojectPath, UNREAL_ASSISTANT_PLUGIN) === false) return none("the .uproject disables the AIAssistant plugin");

  const { engine, user } = assistantSearchPaths(inputs);
  const shadowedBy = engine.find((f) => fs.existsSync(f)) ?? null;
  const configPath = user.find((f) => fs.existsSync(f)) ?? user[user.length - 1];
  const { value, invalid } = readJsonObject(configPath);
  const servers = Array.isArray(value?.mcp_servers) ? (value!.mcp_servers as unknown[]) : [];
  const entry = servers.find((s): s is McpDescriptor => isDescriptor(s) && s.name === UE_MCP_ASSISTANT_SERVER_NAME) ?? null;
  return {
    available: true,
    reason: null,
    configPath,
    shadowedBy,
    attached: !!entry,
    attachedProject: entry ? projectOf(entry) : null,
    invalid,
  };
}

/** The ACP stdio descriptor that launches ue-mcp for a project, as the client configs do. */
export function ueMcpAssistantDescriptor(uprojectPath: string, launch: ServerLaunch = {}): McpDescriptor {
  return { name: UE_MCP_ASSISTANT_SERVER_NAME, command: launch.command ?? "npx", args: ueMcpServerArgs(uprojectPath, launch), env: [] };
}

/** The descriptor the engine sends for its own server when the list is absent. */
export function editorServerDescriptor(url: string): McpDescriptor {
  return { name: UNREAL_MCP_SERVER_NAME, type: "http", url, headers: [] };
}

export interface AssistantWrite {
  changed: boolean;
  /** The copy saved before an existing file was changed, or null. */
  backup: string | null;
}

function writeJson(file: string, before: string | null, next: Record<string, unknown> | null): AssistantWrite {
  const text = next ? JSON.stringify(next, null, 2) + "\n" : null;
  if (text === before) return { changed: false, backup: null };
  let backup: string | null = null;
  if (before !== null) {
    backup = `${file}${REPLACED_BACKUP_SUFFIX}`;
    fs.writeFileSync(backup, before);
  }
  if (text === null) fs.rmSync(file, { force: true });
  else {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text);
  }
  return { changed: true, backup };
}

/**
 * Put ue-mcp's descriptor in the file's mcp_servers, replacing an earlier
 * ue-mcp entry and keeping every other key and server. `editorServer` is
 * written beside it only when the file had no list, standing in for the
 * default the list would otherwise turn off; null leaves it out.
 */
export function attachUnrealAssistant(configPath: string, descriptor: McpDescriptor, editorServer: McpDescriptor | null): AssistantWrite {
  const before = fs.existsSync(configPath) ? fs.readFileSync(configPath, "utf-8") : null;
  const { value, invalid } = readJsonObject(configPath);
  if (invalid) throw new Error(`${configPath} is not a JSON object, so the Unreal Assistant ignores it too; fix or remove it first`);
  const root = value ?? {};
  let servers: unknown[];
  if (Array.isArray(root.mcp_servers)) {
    servers = (root.mcp_servers as unknown[]).filter((s) => !(isDescriptor(s) && s.name === descriptor.name));
  } else {
    if (root.mcp_servers !== undefined) throw new Error(`"mcp_servers" in ${configPath} is not an array, which the Unreal Assistant ignores; fix it first`);
    servers = editorServer ? [editorServer] : [];
  }
  servers.push(descriptor);
  return writeJson(configPath, before, { ...root, mcp_servers: servers });
}

/**
 * Remove ue-mcp's descriptor. A list left empty, or holding only the editor's
 * own server, is dropped so the engine picks its default again; a file left
 * empty is removed.
 */
export function detachUnrealAssistant(configPath: string): AssistantWrite {
  if (!fs.existsSync(configPath)) return { changed: false, backup: null };
  const before = fs.readFileSync(configPath, "utf-8");
  const { value } = readJsonObject(configPath);
  if (!value || !Array.isArray(value.mcp_servers)) return { changed: false, backup: null };
  const all = value.mcp_servers as unknown[];
  const rest = all.filter((s) => !(isDescriptor(s) && s.name === UE_MCP_ASSISTANT_SERVER_NAME));
  if (rest.length === all.length) return { changed: false, backup: null };
  const onlyDefault = rest.length === 1 && isDescriptor(rest[0]) && rest[0].name === UNREAL_MCP_SERVER_NAME && rest[0].type === "http";
  const next: Record<string, unknown> = { ...value };
  if (rest.length === 0 || onlyDefault) delete next.mcp_servers;
  else next.mcp_servers = rest;
  return writeJson(configPath, before, Object.keys(next).length > 0 ? next : null);
}
