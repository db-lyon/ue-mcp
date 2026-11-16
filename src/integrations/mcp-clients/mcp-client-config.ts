// @session per-session: Writes the project positionals a server is started with.
import * as fs from "node:fs";
import * as path from "node:path";
import { warn as logWarn } from "../../core/log.js";

export type McpClientConfigFormat = "json" | "toml";

/** The name the server's entry goes by in every client config, and the server name clients show. */
export const MCP_SERVER_ENTRY = "ue-mcp";

const TOML_TABLE = `mcp_servers.${MCP_SERVER_ENTRY}`;
/** The header lines a TOML config can open the entry's table with; the quoted key is the same table. */
const TOML_HEADERS = [`[${TOML_TABLE}]`, `[mcp_servers."${MCP_SERVER_ENTRY}"]`];
const isEntryHeader = (line: string): boolean => TOML_HEADERS.includes(line.trim());

/**
 * Lifts npm's release-age gate for this one launch. npx ignores
 * `min-release-age-exclude` (npm/cli#9765), so without it a release younger
 * than the window is refused even when ue-mcp is excluded (#1510). The `=`
 * form matters: npm before 11.10 does not know the key and ignores it, but
 * would read a separate `0` as the package to run.
 */
export const NPX_AGE_GATE_OVERRIDE = "--min-release-age=0";

/**
 * How every MCP client entry launches the server. `-y` plus `@latest` makes
 * npx fetch the current release on each start, so a stale local copy cannot
 * shadow it. doctor flags any entry that launches it another way.
 */
export const UE_MCP_LAUNCH = `npx -y ${NPX_AGE_GATE_OVERRIDE} ue-mcp@latest`;

/**
 * How a written entry launches the server. `pin` names an exact version, so the
 * server and the bridge it was installed with move together; unset means
 * `@latest`. `command` replaces `npx`, for a GUI client that started before
 * Node was on its PATH.
 */
export interface ServerLaunch {
  pin?: string | null;
  command?: string | null;
}

export function ueMcpPackageSpec(launch: ServerLaunch = {}): string {
  return `ue-mcp@${launch.pin ?? "latest"}`;
}

export function ueMcpServerArgs(uprojectPath: string, launch: ServerLaunch = {}): string[] {
  return ["-y", NPX_AGE_GATE_OVERRIDE, ueMcpPackageSpec(launch), toMcpPath(uprojectPath)];
}

/** Whether npx args lift the release-age gate, in the `=` form or as a separate value. */
export function liftsAgeGate(args: readonly string[]): boolean {
  return args.some((a, i) => a === NPX_AGE_GATE_OVERRIDE || (a === "--min-release-age" && args[i + 1] === "0"));
}

/** Whether a client entry's command is npx, by name or by path. */
export function isNpxCommand(command: string | null): boolean {
  return !!command && /(^|[\\/])npx(\.cmd|\.exe)?$/i.test(command.trim());
}

/** The version a launch spec pins, or null for `@latest`, a tag, or no spec. */
export function pinnedVersionOf(args: readonly string[]): string | null {
  for (const a of args) {
    const m = /^ue-mcp@(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)$/.exec(a);
    if (m) return m[1];
  }
  return null;
}

export interface McpClient {
  name: string;
  configPath: string;
  detected: boolean;
  format: McpClientConfigFormat;
  /** JSON key holding the servers. VS Code uses `servers`; the rest `mcpServers`. */
  serversKey?: string;
  /** Write `type: "stdio"` into the entry, as VS Code documents it. */
  stdioType?: boolean;
}

export type ClientShape = Pick<McpClient, "configPath" | "format" | "serversKey">;

export const serversKeyOf = (client: Pick<McpClient, "serversKey">): string => client.serversKey ?? "mcpServers";

/**
 * Where a client config sits: under the project directory, the user's home, or
 * the roaming app-data folder.
 */
export type ClientConfigBase = "project" | "home" | "appData";

interface ClientPath {
  base: ClientConfigBase;
  parts: readonly string[];
}

/** One MCP client config ue-mcp knows how to read and write. */
export interface ClientLocation extends ClientPath {
  /** The stable id `--clients` takes. */
  id: string;
  /** The name init, doctor and status show. */
  name: string;
  format: McpClientConfigFormat;
  serversKey?: string;
  stdioType?: boolean;
  /**
   * Paths whose existence means the client is on this machine. Absent, it is
   * the config's own folder.
   */
  detectWhen?: readonly ClientPath[];
}

/**
 * "Detected" for both Claude Code scopes means Claude Code has been opened on
 * this machine at all. Gating the project scope on the project's .mcp.json
 * already existing would hide its checkbox from a first-time user in a fresh
 * project and funnel them into the global scope by elimination.
 */
const CLAUDE_CODE_DETECT: readonly ClientPath[] = [
  { base: "project", parts: [".mcp.json"] },
  { base: "home", parts: [".claude"] },
];

/**
 * Every client config, in the order init offers them. A project-based entry is
 * project scoped: enabling it affects only this project, so init may check it
 * by default. A home or app-data entry touches every project the user opens.
 */
export const CLIENT_LOCATIONS: readonly ClientLocation[] = [
  { id: "claude-code", name: "Claude Code (project)", base: "project", parts: [".mcp.json"], format: "json", detectWhen: CLAUDE_CODE_DETECT },
  { id: "claude-code-global", name: "Claude Code (global)", base: "home", parts: [".claude", ".mcp.json"], format: "json", detectWhen: CLAUDE_CODE_DETECT },
  { id: "claude-desktop", name: "Claude Desktop", base: "appData", parts: ["Claude", "claude_desktop_config.json"], format: "json" },
  { id: "cursor", name: "Cursor", base: "project", parts: [".cursor", "mcp.json"], format: "json" },
  { id: "codex", name: "Codex", base: "home", parts: [".codex", "config.toml"], format: "toml" },
  { id: "vscode", name: "VS Code", base: "project", parts: [".vscode", "mcp.json"], format: "json", serversKey: "servers", stdioType: true },
  {
    id: "gemini",
    name: "Gemini CLI",
    base: "project",
    parts: [".gemini", "settings.json"],
    format: "json",
    detectWhen: [{ base: "project", parts: [".gemini"] }, { base: "home", parts: [".gemini"] }],
  },
  {
    // Codex reads a project's .codex/config.toml once the project is trusted.
    id: "codex-project",
    name: "Codex (project)",
    base: "project",
    parts: [".codex", "config.toml"],
    format: "toml",
    detectWhen: [{ base: "project", parts: [".codex"] }, { base: "home", parts: [".codex"] }],
  },
];

/** The location with this id. Throws on an id CLIENT_LOCATIONS does not declare. */
export function clientLocation(id: string): ClientLocation {
  const location = CLIENT_LOCATIONS.find((l) => l.id === id);
  if (!location) throw new Error(`No MCP client location '${id}'.`);
  return location;
}

/** Stable ids for `--clients`, mapped to the names detectMcpClients uses. */
export const CLIENT_IDS: Record<string, string> = Object.fromEntries(CLIENT_LOCATIONS.map((l) => [l.id, l.name]));

export function clientId(name: string): string {
  return CLIENT_LOCATIONS.find((l) => l.name === name)?.id ?? name;
}

/** Whether the named client writes its config beside the project rather than for every project. */
export function isProjectScopedClient(clientName: string): boolean {
  return CLIENT_LOCATIONS.find((l) => l.name === clientName)?.base === "project";
}

type ClientDirs = Record<ClientConfigBase, string>;

function clientDirs(projectDir: string): ClientDirs {
  const home = process.env.HOME || process.env.USERPROFILE || "";
  return { project: projectDir, home, appData: process.env.APPDATA || path.join(home, "AppData", "Roaming") };
}

const resolveAt = (dirs: ClientDirs, p: ClientPath): string => path.join(dirs[p.base], ...p.parts);

function clientAt(location: ClientLocation, configPath: string, detected: boolean): McpClient {
  return {
    name: location.name,
    configPath,
    detected,
    format: location.format,
    ...(location.serversKey ? { serversKey: location.serversKey } : {}),
    ...(location.stdioType ? { stdioType: true } : {}),
  };
}

export function detectMcpClients(projectDir: string): McpClient[] {
  const dirs = clientDirs(projectDir);
  return CLIENT_LOCATIONS.map((location) => {
    const configPath = resolveAt(dirs, location);
    const probes = location.detectWhen?.map((p) => resolveAt(dirs, p)) ?? [path.dirname(configPath)];
    return clientAt(location, configPath, probes.some((p) => fs.existsSync(p)));
  });
}

/**
 * The project-scoped client configs as they would sit under `dir`: the five
 * files UE 5.9's client config writer writes. `detected` is whether the file
 * exists.
 */
export function projectScopedClientsAt(dir: string): McpClient[] {
  return CLIENT_LOCATIONS.filter((l) => l.base === "project").map((location) => projectClientAt(location, dir));
}

/** A project-based client's config as it would sit under `dir`; `detected` is whether the file exists. */
export function projectClientAt(location: ClientLocation, dir: string): McpClient {
  const configPath = path.join(dir, ...location.parts);
  return clientAt(location, configPath, fs.existsSync(configPath));
}

/**
 * Every client config that can hold a ue-mcp entry for `projectDir`, nearest
 * first: the project's own, then the user's home, then app data. A base this
 * machine has no folder for is left out. `detected` is whether the file exists.
 */
export function clientsNearestFirst(projectDir: string): McpClient[] {
  const dirs = clientDirs(projectDir);
  if (!process.env.APPDATA && !dirs.home) dirs.appData = "";
  const order: readonly ClientConfigBase[] = ["project", "home", "appData"];
  return order.flatMap((base) =>
    dirs[base]
      ? CLIENT_LOCATIONS.filter((l) => l.base === base).map((l) => {
          const configPath = resolveAt(dirs, l);
          return clientAt(l, configPath, fs.existsSync(configPath));
        })
      : [],
  );
}

/** Whether a client's config has a ue-mcp entry, and the version it pins. */
export interface UeMcpEntry {
  configured: boolean;
  /** The exact version the entry pins, or null for @latest or no entry. */
  pinned: string | null;
  /** The command the entry launches with, when it has one. */
  command: string | null;
  /** The .uproject the entry serves, when it names one. */
  project: string | null;
  /** The entry's arguments as written. */
  args: string[];
}

const NO_ENTRY: UeMcpEntry = { configured: false, pinned: null, command: null, project: null, args: [] };

function entryFromArgs(command: string | null, args: string[]): UeMcpEntry {
  const project = [...args].reverse().find((a) => /\.uproject$/i.test(a)) ?? null;
  return { configured: true, pinned: pinnedVersionOf(args), command, project, args };
}

/** The ue-mcp entry in a client's config: whether there is one, its pin, command and project. */
export function readUeMcpEntry(client: ClientShape): UeMcpEntry {
  try {
    const raw = fs.readFileSync(client.configPath, "utf-8");
    if (client.format === "json") {
      const entry = JSON.parse(raw)?.[serversKeyOf(client)]?.[MCP_SERVER_ENTRY];
      if (!entry) return NO_ENTRY;
      const args: string[] = Array.isArray(entry.args) ? entry.args.map(String) : [];
      return entryFromArgs(typeof entry.command === "string" ? entry.command : null, args);
    }
    const lines = raw.split(/\r?\n/);
    const start = lines.findIndex(isEntryHeader);
    if (start < 0) return NO_ENTRY;
    const end = lines.findIndex((l, i) => i > start && /^\s*\[/.test(l));
    const table = lines.slice(start, end < 0 ? undefined : end).join("\n");
    const unquote = (a: string) => a.trim().replace(/^"|"$/g, "").replace(/\\\\/g, "\\");
    const args = (/args\s*=\s*\[([^\]]*)\]/.exec(table)?.[1] ?? "").split(",").map(unquote).filter(Boolean);
    const command = /command\s*=\s*"([^"]*)"/.exec(table)?.[1] ?? null;
    return entryFromArgs(command ? unquote(command) : null, args);
  } catch {
    return NO_ENTRY;
  }
}

/** Remove the ue-mcp entry from a client's config, leaving everything else. True when one was removed. */
export function removeUeMcpEntry(client: ClientShape): boolean {
  if (!fs.existsSync(client.configPath)) return false;
  const raw = fs.readFileSync(client.configPath, "utf-8");
  if (client.format === "toml") {
    const next = removeTomlTable(raw, TOML_TABLE).trimEnd();
    if (next === raw.trimEnd()) return false;
    fs.writeFileSync(client.configPath, next ? `${next}\n` : "");
    return true;
  }
  const parsed = JSON.parse(raw) as Record<string, Record<string, unknown> | undefined>;
  const servers = parsed[serversKeyOf(client)];
  if (!servers || !(MCP_SERVER_ENTRY in servers)) return false;
  delete servers[MCP_SERVER_ENTRY];
  fs.writeFileSync(client.configPath, JSON.stringify(parsed, null, 2));
  return true;
}

/**
 * Insert NPX_AGE_GATE_OVERRIDE before the ue-mcp package spec in a client's
 * entry, leaving its command, spec and projects as they are. True when the
 * config changed.
 */
export function liftAgeGateInEntry(client: ClientShape): boolean {
  if (!fs.existsSync(client.configPath)) return false;
  const raw = fs.readFileSync(client.configPath, "utf-8");
  const isSpec = (a: string) => a === "ue-mcp" || a.startsWith("ue-mcp@");
  if (client.format === "json") {
    const parsed = JSON.parse(raw) as Record<string, Record<string, { args?: unknown[] }> | undefined>;
    const args = parsed[serversKeyOf(client)]?.[MCP_SERVER_ENTRY]?.args;
    if (!Array.isArray(args) || liftsAgeGate(args.map(String))) return false;
    const at = args.findIndex((a) => typeof a === "string" && isSpec(a));
    if (at < 0) return false;
    args.splice(at, 0, NPX_AGE_GATE_OVERRIDE);
    fs.writeFileSync(client.configPath, JSON.stringify(parsed, null, 2));
    return true;
  }
  const lines = raw.split(/\r?\n/);
  const start = lines.findIndex(isEntryHeader);
  if (start < 0) return false;
  for (let i = start + 1; i < lines.length && !/^\s*\[/.test(lines[i]); i++) {
    if (!/^\s*args\s*=/.test(lines[i])) continue;
    if (lines[i].includes(tomlString(NPX_AGE_GATE_OVERRIDE))) return false;
    const next = lines[i].replace(/"ue-mcp(@[^"]*)?"/, (spec) => `${tomlString(NPX_AGE_GATE_OVERRIDE)}, ${spec}`);
    if (next === lines[i]) return false;
    lines[i] = next;
    fs.writeFileSync(client.configPath, lines.join(raw.includes("\r\n") ? "\r\n" : "\n"));
    return true;
  }
  return false;
}

export function writeMcpConfig(client: McpClient, uprojectPath: string, launch: ServerLaunch = {}): void {
  if (client.format === "toml") {
    writeCodexMcpConfig(client.configPath, uprojectPath, launch);
  } else {
    writeJsonMcpConfig(client.configPath, uprojectPath, launch, client);
  }
}

export function writeJsonMcpConfig(
  configPath: string,
  uprojectPath: string,
  launch: ServerLaunch = {},
  shape: Pick<McpClient, "serversKey" | "stdioType"> = {},
): void {
  let existing: Record<string, unknown> = {};
  if (fs.existsSync(configPath)) {
    try {
      existing = JSON.parse(fs.readFileSync(configPath, "utf-8"));
    } catch (e) {
      logWarn("init", `MCP client config at ${configPath} was not valid JSON - overwriting with a fresh ue-mcp entry`, e);
    }
  }

  const key = serversKeyOf(shape);
  const servers = (existing[key] ?? {}) as Record<string, unknown>;
  servers[MCP_SERVER_ENTRY] = {
    ...(shape.stdioType ? { type: "stdio" } : {}),
    command: launch.command ?? "npx",
    args: ueMcpServerArgs(uprojectPath, launch),
  };
  existing[key] = servers;

  const dir = path.dirname(configPath);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(configPath, JSON.stringify(existing, null, 2));
}

export function writeCodexMcpConfig(configPath: string, uprojectPath: string, launch: ServerLaunch = {}): void {
  const existing = fs.existsSync(configPath)
    ? fs.readFileSync(configPath, "utf-8")
    : "";
  const next = upsertCodexMcpServer(existing, uprojectPath, launch);

  const dir = path.dirname(configPath);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(configPath, next, "utf-8");
}

export function upsertCodexMcpServer(existingToml: string, uprojectPath: string, launch: ServerLaunch = {}): string {
  const withoutExisting = removeTomlTable(existingToml, TOML_TABLE).trimEnd();
  const block = [
    `[${TOML_TABLE}]`,
    `command = ${tomlString(launch.command ?? "npx")}`,
    `args = [${ueMcpServerArgs(uprojectPath, launch).map(tomlString).join(", ")}]`,
    `cwd = ${tomlString(toMcpPath(getProjectDir(uprojectPath)))}`,
    "enabled = true",
  ].join("\n");

  return `${withoutExisting}${withoutExisting ? "\n\n" : ""}${block}\n`;
}

function removeTomlTable(toml: string, tableName: string): string {
  const lines = toml.split(/\r?\n/);
  const output: string[] = [];
  let removing = false;

  for (const line of lines) {
    const trimmed = line.trim();
    // A quoted key names the same table, so `[mcp_servers."ue-mcp"]` goes too.
    const currentTable = getTomlTableName(trimmed)?.replace(/"/g, "");
    if (currentTable) {
      removing = currentTable === tableName || currentTable.startsWith(`${tableName}.`);
      if (removing) continue;
    }
    if (!removing) output.push(line);
  }

  return output.join("\n");
}

function getTomlTableName(trimmedLine: string): string | undefined {
  const singleTable = trimmedLine.match(/^\[([^\[\]]+)\]$/);
  if (singleTable) return singleTable[1].trim();

  const arrayTable = trimmedLine.match(/^\[\[([^\[\]]+)\]\]$/);
  return arrayTable ? arrayTable[1].trim() : undefined;
}

function toMcpPath(value: string): string {
  return value.replace(/\\/g, "/");
}

function getProjectDir(uprojectPath: string): string {
  return looksLikeWindowsPath(uprojectPath)
    ? path.win32.dirname(uprojectPath)
    : path.posix.dirname(uprojectPath);
}

function looksLikeWindowsPath(value: string): boolean {
  return /^[A-Za-z]:[\\/]/.test(value) || value.includes("\\");
}

function tomlString(value: string): string {
  return `"${value.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
}
