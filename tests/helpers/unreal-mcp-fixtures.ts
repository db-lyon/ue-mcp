/**
 * Client configs as the editor's built-in MCP server (UE 5.9) leaves them.
 *
 * The HTTP entries follow ModelContextProtocolClientConfig.cpp: the server is
 * always named `unreal-mcp`, Claude Code and VS Code get `type: "http"`, Gemini
 * CLI gets `httpUrl` instead of `url`, and Codex gets a one-key TOML table. The
 * JSON is pretty printed with tabs, as UE's TPrettyJsonPrintPolicy does.
 *
 * The proxy entry follows Extras/Proxy/cmd/unreal_mcp_proxy (config.go
 * proxyEntry, install.go): `install` deletes `unreal-mcp` and writes
 * `unreal-mcp-proxy` with the binary's absolute path as `command`, `args` of
 * `[]` or `["--mcp-json", <abs path>]`, and `_upstreamEntry` holding the
 * removed entry under its name. Go's json.MarshalIndent sorts keys, indents
 * two spaces, and saveConfig appends a newline.
 */
import * as path from "node:path";

export const UNREAL_URL = "http://127.0.0.1:8000/mcp";

const tabs = (value: unknown) => JSON.stringify(value, null, "\t");

const httpEntry = { type: "http", url: UNREAL_URL };

export interface UnrealClientFixture {
  /** Client id, as `init --clients` takes it. */
  id: string;
  /** Path relative to the project directory. */
  file: string;
  /** Key the servers live under, null for TOML. */
  serversKey: string | null;
  /** The file as the editor writes it, beside one unrelated server. */
  content: string;
}

/** One fixture per client the editor writes for, each with an unrelated server that must survive. */
export const UNREAL_CLIENT_FIXTURES: UnrealClientFixture[] = [
  {
    id: "claude-code",
    file: ".mcp.json",
    serversKey: "mcpServers",
    content: tabs({ mcpServers: { "my-existing-server": { command: "my-server" }, "unreal-mcp": httpEntry } }),
  },
  {
    id: "cursor",
    file: path.join(".cursor", "mcp.json"),
    serversKey: "mcpServers",
    content: tabs({ mcpServers: { "my-existing-server": { command: "my-server" }, "unreal-mcp": { url: UNREAL_URL } } }),
  },
  {
    id: "vscode",
    file: path.join(".vscode", "mcp.json"),
    serversKey: "servers",
    content: tabs({ servers: { "my-existing-server": { type: "stdio", command: "my-server" }, "unreal-mcp": httpEntry } }),
  },
  {
    id: "gemini",
    file: path.join(".gemini", "settings.json"),
    serversKey: "mcpServers",
    content: tabs({ theme: "Default", mcpServers: { "my-existing-server": { command: "my-server" }, "unreal-mcp": { httpUrl: UNREAL_URL } } }),
  },
  {
    id: "codex-project",
    file: path.join(".codex", "config.toml"),
    serversKey: null,
    content: `model = "o3"\n\n[mcp_servers.my-existing-server]\ncommand = "my-server"\n\n[mcp_servers.unreal-mcp]\nurl = "${UNREAL_URL}"\n`,
  },
];

/**
 * A .mcp.json after `unreal_mcp_proxy install` rewrote the editor's entry.
 * `mcpJsonPath` is the `--mcp-json` the proxy was installed with, if any.
 */
export function proxyMcpJson(proxyBinary: string, mcpJsonPath?: string): string {
  // Key order is Go's sorted order.
  const value = {
    mcpServers: {
      "my-existing-server": { command: "my-server" },
      "unreal-mcp-proxy": {
        _upstreamEntry: { "unreal-mcp": httpEntry },
        args: mcpJsonPath ? ["--mcp-json", mcpJsonPath] : [],
        command: proxyBinary,
      },
    },
  };
  return `${JSON.stringify(value, null, 2)}\n`;
}
