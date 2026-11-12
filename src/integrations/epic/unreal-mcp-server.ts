/**
 * Epic's Unreal MCP server (the ModelContextProtocol plugin, UE 5.8 and later)
 * as it is set up for one project, read from files alone.
 *
 * The plugin is EnabledByDefault, so it runs in every project that does not
 * turn it off in the .uproject. Its settings are per user per project
 * (UModelContextProtocolSettings, config=EditorPerProjectUserSettings):
 * Engine/Config/BaseEditorPerProjectUserSettings.ini, then the project's
 * Config/DefaultEditorPerProjectUserSettings.ini, then
 * Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini, each later
 * file winning. UE 5.9's Base ini turns bAutoStartServer on; the class
 * default, and UE 5.8, leave it off. The port defaults to 8000 and the URL
 * path to /mcp (ModelContextProtocol.h), and `-ModelContextProtocolPort=` on
 * the command line overrides the port for one launch. In 5.8 the class lived
 * in the ModelContextProtocolEngine module, so its section had that name.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { parseIni } from "../../codeintel/ue-ini.js";

export const UNREAL_MCP_PLUGIN = "ModelContextProtocol";
export const UNREAL_MCP_SECTION = "/Script/ModelContextProtocol.ModelContextProtocolSettings";
/** The section name before UE 5.9 moved the class out of ModelContextProtocolEngine. */
export const UNREAL_MCP_SECTION_58 = "/Script/ModelContextProtocolEngine.ModelContextProtocolSettings";
export const UNREAL_MCP_DEFAULT_PORT = 8000;
export const UNREAL_MCP_DEFAULT_URL_PATH = "/mcp";

export type SettingSource = "user" | "project" | "engine" | "default";

export interface UnrealMcpServerInputs {
  uprojectPath: string;
  projectDir: string;
  /** Major.minor, or null when unknown. */
  engineVersion: string | null;
  /** The folder holding Engine/, or null when it could not be resolved. */
  engineRoot: string | null;
  platform?: NodeJS.Platform;
}

export interface UnrealMcpServerStatus {
  engineVersion: string | null;
  /** False when the .uproject turns the plugin off. */
  pluginEnabled: boolean;
  /** "uproject" when the .uproject lists the plugin, "default" when EnabledByDefault decides. */
  enabledBy: "uproject" | "default";
  /** Whether the editor starts the server on launch, from the settings files. */
  autoStart: boolean;
  autoStartSource: SettingSource;
  port: number;
  portSource: SettingSource;
  urlPath: string;
  url: string;
  /** The per-user per-project ini that init writes. */
  userIni: string;
  /** The section the running engine reads its settings from. */
  section: string;
  /** True when the server will be listening beside ue-mcp's bridge whenever this project's editor runs. */
  runsBeside: boolean;
  /** Ports ue-mcp uses that this server's port collides with, as sentences. */
  portConflicts: string[];
}

/** Major.minor as a comparable number pair, or null. */
export function parseEngineVersion(v: string | null | undefined): [number, number] | null {
  const m = v ? /^(\d+)\.(\d+)/.exec(v) : null;
  return m ? [Number(m[1]), Number(m[2])] : null;
}

/** Whether `v` is at least `major.minor`. Null when the version is unknown. */
export function engineAtLeast(v: string | null | undefined, major: number, minor: number): boolean | null {
  const p = parseEngineVersion(v);
  if (!p) return null;
  return p[0] > major || (p[0] === major && p[1] >= minor);
}

/** The editor's saved-config folder for a platform, as UE names it. */
export function editorPlatformDir(platform: NodeJS.Platform = process.platform): string {
  if (platform === "win32") return "WindowsEditor";
  if (platform === "darwin") return "MacEditor";
  return "LinuxEditor";
}

export function userIniPath(projectDir: string, platform: NodeJS.Platform = process.platform): string {
  return path.join(projectDir, "Saved", "Config", editorPlatformDir(platform), "EditorPerProjectUserSettings.ini");
}

/** The .uproject's own entry for a plugin: true, false, or null when it does not list it. */
export function uprojectPluginState(uprojectPath: string, name: string): boolean | null {
  try {
    const plugins = JSON.parse(fs.readFileSync(uprojectPath, "utf-8"))?.Plugins;
    if (!Array.isArray(plugins)) return null;
    const entry = plugins.find((p: { Name?: unknown }) => typeof p?.Name === "string" && p.Name.toLowerCase() === name.toLowerCase());
    if (!entry) return null;
    return entry.Enabled !== false;
  } catch {
    return null;
  }
}

/** Whether an experimental plugin ships with the engine. Null when the engine root is unknown. */
export function enginePluginPresent(engineRoot: string | null, name: string): boolean | null {
  if (!engineRoot) return null;
  return fs.existsSync(path.join(engineRoot, "Engine", "Plugins", "Experimental", name, `${name}.uplugin`));
}

/** UE's bool parsing for config values: true, yes, on and any non-zero number. */
export function iniBool(raw: string | undefined): boolean | undefined {
  if (raw === undefined) return undefined;
  const v = raw.trim().toLowerCase();
  if (v === "true" || v === "yes" || v === "on") return true;
  if (v === "false" || v === "no" || v === "off" || v === "") return false;
  const n = Number(v);
  return Number.isFinite(n) ? n !== 0 : false;
}

function readSection(file: string): Record<string, string> | null {
  let raw: string;
  try {
    raw = fs.readFileSync(file, "utf-8");
  } catch {
    return null;
  }
  const ini = parseIni(raw.replace(/^﻿/, ""));
  // The 5.8 section is redirected to the 5.9 one, so both count; the newer name wins.
  const merged = { ...(ini[UNREAL_MCP_SECTION_58] ?? {}), ...(ini[UNREAL_MCP_SECTION] ?? {}) };
  return Object.keys(merged).length > 0 ? merged : null;
}

/**
 * The server's state for a project, or null when its engine has no such
 * server (before 5.8, or an engine root without the plugin).
 */
export function readUnrealMcpServer(inputs: UnrealMcpServerInputs, uePorts: { bridge?: number; flow?: number } = {}): UnrealMcpServerStatus | null {
  const { uprojectPath, projectDir, engineVersion, engineRoot } = inputs;
  const platform = inputs.platform ?? process.platform;
  const present = enginePluginPresent(engineRoot, UNREAL_MCP_PLUGIN);
  const recent = engineAtLeast(engineVersion, 5, 8);
  // The project's own version wins: an engine root resolved by fallback can be a newer install.
  if (recent === false || present === false || (present === null && recent !== true)) return null;

  const listed = uprojectPluginState(uprojectPath, UNREAL_MCP_PLUGIN);
  const pluginEnabled = listed ?? true;

  const userIni = userIniPath(projectDir, platform);
  const layers: Array<[Exclude<SettingSource, "default">, Record<string, string> | null]> = [
    ["user", readSection(userIni)],
    ["project", readSection(path.join(projectDir, "Config", "DefaultEditorPerProjectUserSettings.ini"))],
    ["engine", engineRoot ? readSection(path.join(engineRoot, "Engine", "Config", "BaseEditorPerProjectUserSettings.ini")) : null],
  ];
  const pick = (key: string): { value: string; source: SettingSource } | null => {
    for (const [source, values] of layers) {
      const hit = values ? Object.entries(values).find(([k]) => k.toLowerCase() === key.toLowerCase()) : undefined;
      if (hit) return { value: hit[1], source };
    }
    return null;
  };

  const auto = pick("bAutoStartServer");
  let autoStart: boolean;
  let autoStartSource: SettingSource;
  if (auto) {
    autoStart = iniBool(auto.value) ?? false;
    autoStartSource = auto.source;
  } else if (!engineRoot && engineAtLeast(engineVersion, 5, 9)) {
    // No engine tree to read: UE 5.9's Base ini is known to turn it on.
    autoStart = true;
    autoStartSource = "engine";
  } else {
    autoStart = false;
    autoStartSource = "default";
  }

  const portSetting = pick("ServerPortNumber");
  const parsedPort = portSetting ? Number.parseInt(portSetting.value, 10) : NaN;
  const port = parsedPort >= 1 && parsedPort <= 65535 ? parsedPort : UNREAL_MCP_DEFAULT_PORT;
  const portSource: SettingSource = parsedPort >= 1 && parsedPort <= 65535 ? portSetting!.source : "default";
  const pathSetting = pick("ServerUrlPath")?.value.trim();
  const urlPath = pathSetting ? (pathSetting.startsWith("/") ? pathSetting : `/${pathSetting}`) : UNREAL_MCP_DEFAULT_URL_PATH;

  const runsBeside = pluginEnabled && autoStart;
  const portConflicts: string[] = [];
  if (pluginEnabled && uePorts.bridge === port) portConflicts.push(`ue-mcp's bridge port for this project is also ${port}`);
  if (pluginEnabled && uePorts.flow === port) portConflicts.push(`ue-mcp's flow HTTP port is also ${port}`);

  return {
    engineVersion,
    pluginEnabled,
    enabledBy: listed === null ? "default" : "uproject",
    autoStart,
    autoStartSource,
    port,
    portSource,
    urlPath,
    url: `http://127.0.0.1:${port}${urlPath}`,
    userIni,
    section: engineAtLeast(engineVersion, 5, 9) === false ? UNREAL_MCP_SECTION_58 : UNREAL_MCP_SECTION,
    runsBeside,
    portConflicts,
  };
}

/**
 * Set keys in one section of an ini file, keeping every other line, the line
 * endings and a byte order mark. A key already in the section is rewritten in
 * place, a missing one goes after the section's last line, and a missing
 * section is appended. Writes only when something changes. True when it did.
 */
export function setIniValues(file: string, section: string, values: Record<string, string>): boolean {
  const before = fs.existsSync(file) ? fs.readFileSync(file, "utf-8") : "";
  const bom = before.startsWith("﻿") ? "﻿" : "";
  const body = bom ? before.slice(1) : before;
  const eol = body.includes("\r\n") ? "\r\n" : "\n";
  const lines = body.length > 0 ? body.split(/\r?\n/) : [];
  if (lines.length > 0 && lines[lines.length - 1] === "") lines.pop();

  const isHeader = (l: string) => /^\s*\[.*\]\s*$/.test(l);
  let start = lines.findIndex((l) => l.trim().toLowerCase() === `[${section.toLowerCase()}]`);
  if (start < 0) {
    if (lines.length > 0 && lines[lines.length - 1].trim() !== "") lines.push("");
    lines.push(`[${section}]`);
    start = lines.length - 1;
  }
  for (const [key, value] of Object.entries(values)) {
    let end = lines.findIndex((l, i) => i > start && isHeader(l));
    if (end < 0) end = lines.length;
    const keyRe = new RegExp(`^\\s*${key.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\\s*=`, "i");
    const at = lines.findIndex((l, i) => i > start && i < end && keyRe.test(l));
    if (at >= 0) {
      lines[at] = `${key}=${value}`;
      continue;
    }
    let insert = end;
    while (insert - 1 > start && lines[insert - 1].trim() === "") insert--;
    lines.splice(insert, 0, `${key}=${value}`);
  }
  const after = bom + lines.join(eol) + eol;
  if (after === before) return false;
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, after);
  return true;
}

export interface UnrealMcpServerChange {
  autoStart?: boolean;
  port?: number;
}

/** Write the requested settings to the project's per-user ini. True when the file changed. */
export function writeUnrealMcpServerSettings(status: Pick<UnrealMcpServerStatus, "userIni" | "section">, change: UnrealMcpServerChange): boolean {
  const values: Record<string, string> = {};
  if (change.autoStart !== undefined) values.bAutoStartServer = change.autoStart ? "True" : "False";
  if (change.port !== undefined) values.ServerPortNumber = String(change.port);
  if (Object.keys(values).length === 0) return false;
  return setIniValues(status.userIni, status.section, values);
}

/** One line on what the server does for this project, for doctor and init. */
export function describeUnrealMcpServer(s: UnrealMcpServerStatus): string {
  if (!s.pluginEnabled) return "plugin disabled in the .uproject";
  const from = (src: SettingSource) => (src === "user" ? "your project user settings" : src === "project" ? "the project's Config" : src === "engine" ? "the engine's Base ini" : "the plugin default");
  return s.autoStart
    ? `starts with the editor on ${s.url} (auto-start from ${from(s.autoStartSource)}, port from ${from(s.portSource)})`
    : `enabled, does not auto-start (${from(s.autoStartSource)}); its URL would be ${s.url}`;
}
