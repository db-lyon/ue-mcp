import * as fs from "node:fs";
import * as path from "node:path";
import { warn as logWarn } from "./log.js";

export type McpClientConfigFormat = "json" | "toml";

export interface McpClient {
  name: string;
  configPath: string;
  detected: boolean;
  format: McpClientConfigFormat;
}

/**
 * Project-scoped clients write their MCP config alongside the .uproject,
 * so enabling them only affects this project. Global/Desktop configs touch
 * every project the user opens — they should not be opted in by default.
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

export function writeMcpConfig(client: McpClient, uprojectPath: string): void {
  if (client.format === "toml") {
    writeCodexMcpConfig(client.configPath, uprojectPath);
  } else {
    writeJsonMcpConfig(client.configPath, uprojectPath);
  }
}

export function writeJsonMcpConfig(configPath: string, uprojectPath: string): void {
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
    command: "npx",
    args: ["ue-mcp", toMcpPath(uprojectPath)],
  };
  existing.mcpServers = mcpServers;

  const dir = path.dirname(configPath);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(configPath, JSON.stringify(existing, null, 2));
}

export function writeCodexMcpConfig(configPath: string, uprojectPath: string): void {
  const existing = fs.existsSync(configPath)
    ? fs.readFileSync(configPath, "utf-8")
    : "";
  const next = upsertCodexMcpServer(existing, uprojectPath);

  const dir = path.dirname(configPath);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(configPath, next, "utf-8");
}

export function upsertCodexMcpServer(existingToml: string, uprojectPath: string): string {
  const withoutExisting = removeTomlTable(existingToml, "mcp_servers.ue-mcp").trimEnd();
  const block = [
    "[mcp_servers.ue-mcp]",
    'command = "npx"',
    `args = ["ue-mcp", ${tomlString(toMcpPath(uprojectPath))}]`,
    `cwd = ${tomlString(toMcpPath(path.dirname(uprojectPath)))}`,
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

function tomlString(value: string): string {
  return `"${value.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
}
