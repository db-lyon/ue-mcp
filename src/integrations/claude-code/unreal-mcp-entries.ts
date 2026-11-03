/**
 * Entries for the editor's built-in MCP server (UE 5.9) in a client config,
 * which init replaces with the ue-mcp entry. Two shapes are recognised:
 *
 * - the HTTP entry the editor's client config writer adds, always named
 *   `unreal-mcp`, with `url` (or `httpUrl` for Gemini CLI) set to its server;
 * - the stdio proxy entry its `unreal_mcp_proxy install` writes in place of
 *   the HTTP one, which keeps the original under `_upstreamEntry`.
 */
import * as fs from "node:fs";
import { serversKeyOf, type ClientShape } from "./mcp-client-config.js";

export const UNREAL_MCP_SERVER_NAME = "unreal-mcp";

/** Suffix of the copy init saves before it removes an entry. */
export const REPLACED_BACKUP_SUFFIX = ".ue-mcp-backup";

export interface UnrealMcpEntry {
  /** The server's key in the config. */
  name: string;
  kind: "http" | "proxy";
  /** The URL for an HTTP entry, the command for a proxy entry. */
  target: string;
}

const PROXY_COMMAND = /(^|[\\/])unreal_mcp_proxy(\.exe)?$/i;

function classify(name: string, entry: unknown): UnrealMcpEntry | null {
  if (!entry || typeof entry !== "object") return null;
  const e = entry as Record<string, unknown>;
  const command = typeof e.command === "string" ? e.command : "";
  if ("_upstreamEntry" in e || PROXY_COMMAND.test(command.trim())) {
    return { name, kind: "proxy", target: command || "(no command)" };
  }
  const url = typeof e.url === "string" ? e.url : typeof e.httpUrl === "string" ? e.httpUrl : null;
  if (name === UNREAL_MCP_SERVER_NAME && url) return { name, kind: "http", target: url };
  return null;
}

interface TomlTable {
  name: string;
  start: number;
  end: number;
  body: string;
}

function mcpServerTables(lines: string[]): TomlTable[] {
  const tables: TomlTable[] = [];
  let current: TomlTable | null = null;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const header = /^\s*\[([^\[\]]+)\]\s*$/.exec(line);
    if (!header && !/^\s*\[\[/.test(line)) continue;
    const m = header && /^mcp_servers\.("?)([^".]+)\1(\..+)?$/.exec(header[1].trim());
    // A subtable such as [mcp_servers.x.env] belongs to the server above it.
    if (m && m[3] && current?.name === m[2]) continue;
    if (current) {
      current.end = i;
      tables.push(current);
      current = null;
    }
    if (m && !m[3]) current = { name: m[2], start: i, end: lines.length, body: "" };
  }
  if (current) tables.push(current);
  // The body is the table's own keys, up to its first subtable.
  for (const t of tables) {
    const own = lines.slice(t.start + 1, t.end);
    const sub = own.findIndex((l) => /^\s*\[/.test(l));
    t.body = (sub < 0 ? own : own.slice(0, sub)).join("\n");
  }
  return tables;
}

function tomlEntry(body: string): Record<string, unknown> {
  const str = (key: string) => new RegExp(`^\\s*${key}\\s*=\\s*"([^"]*)"`, "m").exec(body)?.[1];
  return { url: str("url"), command: str("command") };
}

/** The editor's MCP entries in a client's config. Empty when there are none or the file does not parse. */
export function findUnrealMcpEntries(client: ClientShape): UnrealMcpEntry[] {
  let raw: string;
  try {
    raw = fs.readFileSync(client.configPath, "utf-8");
  } catch {
    return [];
  }
  if (client.format === "toml") {
    return mcpServerTables(raw.split(/\r?\n/))
      .map((t) => classify(t.name, tomlEntry(t.body)))
      .filter((e): e is UnrealMcpEntry => e !== null);
  }
  try {
    const servers = JSON.parse(raw)?.[serversKeyOf(client)];
    if (!servers || typeof servers !== "object") return [];
    return Object.entries(servers as Record<string, unknown>)
      .map(([name, entry]) => classify(name, entry))
      .filter((e): e is UnrealMcpEntry => e !== null);
  } catch {
    return [];
  }
}

export interface ReplacedEntries {
  removed: UnrealMcpEntry[];
  /** Copy of the config as it was before the removal, or null when nothing was removed. */
  backup: string | null;
}

/**
 * Save the config beside itself, then remove the editor's MCP entries from it.
 * Every other server and key is left as it was. Restoring the backup undoes it.
 */
export function removeUnrealMcpEntries(client: ClientShape): ReplacedEntries {
  const found = findUnrealMcpEntries(client);
  if (found.length === 0) return { removed: [], backup: null };
  const raw = fs.readFileSync(client.configPath, "utf-8");
  const backup = `${client.configPath}${REPLACED_BACKUP_SUFFIX}`;
  fs.writeFileSync(backup, raw);
  const names = new Set(found.map((e) => e.name));

  if (client.format === "toml") {
    const lines = raw.split(/\r?\n/);
    const drop = new Set<number>();
    for (const t of mcpServerTables(lines)) {
      if (!names.has(t.name)) continue;
      for (let i = t.start; i < t.end; i++) drop.add(i);
    }
    const eol = raw.includes("\r\n") ? "\r\n" : "\n";
    const next = lines.filter((_, i) => !drop.has(i)).join(eol).trimEnd();
    fs.writeFileSync(client.configPath, next ? `${next}${eol}` : "");
  } else {
    const parsed = JSON.parse(raw) as Record<string, Record<string, unknown>>;
    const servers = parsed[serversKeyOf(client)];
    for (const name of names) delete servers[name];
    fs.writeFileSync(client.configPath, JSON.stringify(parsed, null, 2));
  }
  return { removed: found, backup };
}

export function describeUnrealMcpEntry(e: UnrealMcpEntry): string {
  return e.kind === "proxy" ? `'${e.name}' (stdio proxy ${e.target})` : `'${e.name}' (${e.target})`;
}
