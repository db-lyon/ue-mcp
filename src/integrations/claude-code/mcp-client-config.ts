import * as fs from "node:fs";
import * as path from "node:path";
import { warn as logWarn } from "../../core/log.js";

export type McpClientConfigFormat = "json" | "toml";

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
}

/**
 * Project-scoped clients write their MCP config alongside the .uproject,
 * so enabling them only affects this project. Global/Desktop configs touch
 * every project the user opens - they should not be opted in by default.
 */
export function isProjectScopedClient(clientName: string): boolean {
  return clientName.includes("(project)") || clientName === "Cursor";
}

export function detectMcpClients(projectDir: string): McpClient[] {
  const home = process.env.HOME || process.env.USERPROFILE || "";
  const clients: McpClient[] = [];

  const claudeProjectMcp = path.join(projectDir, ".mcp.json");
  const claudeGlobalMcp = path.join(home, ".claude", ".mcp.json");
  // "Detected" for both Claude Code scopes means "Claude Code is installed
  // anywhere on this machine." If we gated project-scope detection on the
  // project's .mcp.json already existing, first-time users in a fresh
  // project would never see the project-scope checkbox and would be
  // funneled into global scope by elimination. Show both scopes whenever
  // Claude Code has been opened at least once; let the user pick.
  const claudeInstalled =
    fs.existsSync(claudeProjectMcp) ||
    fs.existsSync(path.dirname(claudeGlobalMcp));
  clients.push({
    name: "Claude Code (project)",
    configPath: claudeProjectMcp,
    detected: claudeInstalled,
    format: "json",
  });
  clients.push({
    name: "Claude Code (global)",
    configPath: claudeGlobalMcp,
    detected: claudeInstalled,
    format: "json",
  });

  const appData =
    process.env.APPDATA || path.join(home, "AppData", "Roaming");
  const claudeDesktop = path.join(
    appData,
    "Claude",
    "claude_desktop_config.json",
  );
  clients.push({
    name: "Claude Desktop",
    configPath: claudeDesktop,
    detected: fs.existsSync(path.dirname(claudeDesktop)),
    format: "json",
  });

  const cursorMcp = path.join(projectDir, ".cursor", "mcp.json");
  clients.push({
    name: "Cursor",
    configPath: cursorMcp,
    detected: fs.existsSync(path.join(projectDir, ".cursor")),
    format: "json",
  });

  const codexConfig = path.join(home, ".codex", "config.toml");
  clients.push({
    name: "Codex",
    configPath: codexConfig,
    detected: fs.existsSync(path.dirname(codexConfig)),
    format: "toml",
  });

  return clients;
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
export function readUeMcpEntry(client: Pick<McpClient, "configPath" | "format">): UeMcpEntry {
  try {
    const raw = fs.readFileSync(client.configPath, "utf-8");
    if (client.format === "json") {
      const entry = JSON.parse(raw)?.mcpServers?.["ue-mcp"];
      if (!entry) return NO_ENTRY;
      const args: string[] = Array.isArray(entry.args) ? entry.args.map(String) : [];
      return entryFromArgs(typeof entry.command === "string" ? entry.command : null, args);
    }
    const lines = raw.split(/\r?\n/);
    const start = lines.findIndex((l) => l.trim() === "[mcp_servers.ue-mcp]");
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
export function removeUeMcpEntry(client: Pick<McpClient, "configPath" | "format">): boolean {
  if (!fs.existsSync(client.configPath)) return false;
  const raw = fs.readFileSync(client.configPath, "utf-8");
  if (client.format === "toml") {
    const next = removeTomlTable(raw, "mcp_servers.ue-mcp").trimEnd();
    if (next === raw.trimEnd()) return false;
    fs.writeFileSync(client.configPath, next ? `${next}\n` : "");
    return true;
  }
  const parsed = JSON.parse(raw) as { mcpServers?: Record<string, unknown> };
  if (!parsed.mcpServers || !("ue-mcp" in parsed.mcpServers)) return false;
  delete parsed.mcpServers["ue-mcp"];
  fs.writeFileSync(client.configPath, JSON.stringify(parsed, null, 2));
  return true;
}

/**
 * Insert NPX_AGE_GATE_OVERRIDE before the ue-mcp package spec in a client's
 * entry, leaving its command, spec and projects as they are. True when the
 * config changed.
 */
export function liftAgeGateInEntry(client: Pick<McpClient, "configPath" | "format">): boolean {
  if (!fs.existsSync(client.configPath)) return false;
  const raw = fs.readFileSync(client.configPath, "utf-8");
  const isSpec = (a: string) => a === "ue-mcp" || a.startsWith("ue-mcp@");
  if (client.format === "json") {
    const parsed = JSON.parse(raw) as { mcpServers?: Record<string, { args?: unknown[] }> };
    const args = parsed.mcpServers?.["ue-mcp"]?.args;
    if (!Array.isArray(args) || liftsAgeGate(args.map(String))) return false;
    const at = args.findIndex((a) => typeof a === "string" && isSpec(a));
    if (at < 0) return false;
    args.splice(at, 0, NPX_AGE_GATE_OVERRIDE);
    fs.writeFileSync(client.configPath, JSON.stringify(parsed, null, 2));
    return true;
  }
  const lines = raw.split(/\r?\n/);
  const start = lines.findIndex((l) => l.trim() === "[mcp_servers.ue-mcp]");
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
    writeJsonMcpConfig(client.configPath, uprojectPath, launch);
  }
}

export function writeJsonMcpConfig(configPath: string, uprojectPath: string, launch: ServerLaunch = {}): void {
  let existing: Record<string, unknown> = {};
  if (fs.existsSync(configPath)) {
    try {
      existing = JSON.parse(fs.readFileSync(configPath, "utf-8"));
    } catch (e) {
      logWarn("init", `MCP client config at ${configPath} was not valid JSON - overwriting with a fresh ue-mcp entry`, e);
    }
  }

  const mcpServers = (existing.mcpServers ?? {}) as Record<string, unknown>;
  mcpServers["ue-mcp"] = {
    command: launch.command ?? "npx",
    args: ueMcpServerArgs(uprojectPath, launch),
  };
  existing.mcpServers = mcpServers;

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
  const withoutExisting = removeTomlTable(existingToml, "mcp_servers.ue-mcp").trimEnd();
  const block = [
    "[mcp_servers.ue-mcp]",
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
    const currentTable = getTomlTableName(trimmed);
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
