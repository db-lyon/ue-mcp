import * as fs from "node:fs";
import * as path from "node:path";
import { warn as logWarn } from "../../core/log.js";

export type McpClientConfigFormat = "json" | "toml";

/**
 * How every MCP client entry launches the server. `-y` plus `@latest` makes
 * npx fetch the current release on each start, so a stale local copy cannot
 * shadow it. doctor flags any entry that launches it another way.
 */
export const UE_MCP_LAUNCH = "npx -y ue-mcp@latest";

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
  return ["-y", ueMcpPackageSpec(launch), toMcpPath(uprojectPath)];
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
export function readUeMcpEntry(client: Pick<McpClient, "configPath" | "format">): { configured: boolean; pinned: string | null } {
  try {
    const raw = fs.readFileSync(client.configPath, "utf-8");
    if (client.format === "json") {
      const entry = JSON.parse(raw)?.mcpServers?.["ue-mcp"];
      const args: string[] = Array.isArray(entry?.args) ? entry.args.map(String) : [];
      return { configured: !!entry, pinned: pinnedVersionOf(args) };
    }
    const lines = raw.split(/\r?\n/);
    const start = lines.findIndex((l) => l.trim() === "[mcp_servers.ue-mcp]");
    if (start < 0) return { configured: false, pinned: null };
    const end = lines.findIndex((l, i) => i > start && /^\s*\[/.test(l));
    const table = lines.slice(start, end < 0 ? undefined : end).join("\n");
    const args = /args\s*=\s*\[([^\]]*)\]/.exec(table)?.[1] ?? "";
    return { configured: true, pinned: pinnedVersionOf(args.split(",").map((a) => a.trim().replace(/^"|"$/g, ""))) };
  } catch {
    return { configured: false, pinned: null };
  }
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
