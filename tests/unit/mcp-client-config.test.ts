import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  detectMcpClients,
  isProjectScopedClient,
  upsertCodexMcpServer,
  writeCodexMcpConfig,
  writeJsonMcpConfig,
} from "../../src/mcp-client-config.js";

let tmpRoot: string;
let originalHome: string | undefined;
let originalUserProfile: string | undefined;
let originalAppData: string | undefined;

beforeEach(() => {
  tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-client-config-test-"));
  originalHome = process.env.HOME;
  originalUserProfile = process.env.USERPROFILE;
  originalAppData = process.env.APPDATA;
  process.env.HOME = tmpRoot;
  process.env.USERPROFILE = tmpRoot;
  delete process.env.APPDATA;
});

afterEach(() => {
  if (originalHome === undefined) delete process.env.HOME;
  else process.env.HOME = originalHome;
  if (originalUserProfile === undefined) delete process.env.USERPROFILE;
  else process.env.USERPROFILE = originalUserProfile;
  if (originalAppData === undefined) delete process.env.APPDATA;
  else process.env.APPDATA = originalAppData;
  fs.rmSync(tmpRoot, { recursive: true, force: true });
});

describe("MCP client config detection", () => {
  it("detects Codex when ~/.codex exists", () => {
    const projectDir = path.join(tmpRoot, "project");
    fs.mkdirSync(projectDir);
    fs.mkdirSync(path.join(tmpRoot, ".codex"));

    const codex = detectMcpClients(projectDir).find((client) => client.name === "Codex");

    expect(codex).toMatchObject({
      configPath: path.join(tmpRoot, ".codex", "config.toml"),
      detected: true,
      format: "toml",
    });
    expect(isProjectScopedClient("Codex")).toBe(false);
  });
});

describe("JSON MCP client config", () => {
  it("writes the existing mcpServers shape for JSON clients", () => {
    const configPath = path.join(tmpRoot, "project", ".mcp.json");

    writeJsonMcpConfig(configPath, "C:\\Projects\\MyGame\\MyGame.uproject");

    const config = JSON.parse(fs.readFileSync(configPath, "utf-8")) as {
      mcpServers: Record<string, { command: string; args: string[] }>;
    };
    expect(config.mcpServers["ue-mcp"]).toEqual({
      command: "npx",
      args: ["ue-mcp", "C:/Projects/MyGame/MyGame.uproject"],
    });
  });
});

describe("Codex MCP client config", () => {
  it("appends a Codex TOML server block and preserves unrelated settings", () => {
    const input = [
      'model = "gpt-5"',
      "",
      "[profiles.default]",
      'approval_policy = "on-request"',
      "",
    ].join("\n");

    const result = upsertCodexMcpServer(input, "C:\\Projects\\MyGame\\MyGame.uproject");

    expect(result).toContain('model = "gpt-5"');
    expect(result).toContain("[profiles.default]");
    expect(result).toContain("[mcp_servers.ue-mcp]");
    expect(result).toContain('command = "npx"');
    expect(result).toContain('args = ["ue-mcp", "C:/Projects/MyGame/MyGame.uproject"]');
    expect(result).toContain('cwd = "C:/Projects/MyGame"');
    expect(result).toContain("enabled = true");
  });

  it("replaces only an existing ue-mcp Codex TOML server block", () => {
    const input = [
      "[mcp_servers.other]",
      'command = "node"',
      "",
      "[mcp_servers.ue-mcp]",
      'command = "old"',
      'args = ["old"]',
      "",
      "[mcp_servers.ue-mcp.env]",
      'UE_MCP_BRIDGE_PORT = "9999"',
      "",
      "[profiles.default]",
      'sandbox_mode = "workspace-write"',
      "",
    ].join("\n");

    const result = upsertCodexMcpServer(input, "/home/alex/Game/Game.uproject");

    expect(result).toContain("[mcp_servers.other]");
    expect(result).toContain('command = "node"');
    expect(result).toContain("[profiles.default]");
    expect(result).toContain('sandbox_mode = "workspace-write"');
    expect(result).not.toContain('command = "old"');
    expect(result).not.toContain('UE_MCP_BRIDGE_PORT = "9999"');
    expect(result).toContain('args = ["ue-mcp", "/home/alex/Game/Game.uproject"]');
    expect(result).toContain('cwd = "/home/alex/Game"');
  });

  it("writes Codex config.toml to disk", () => {
    const configPath = path.join(tmpRoot, ".codex", "config.toml");

    writeCodexMcpConfig(configPath, "M:\\Game\\Game.uproject");

    const result = fs.readFileSync(configPath, "utf-8");
    expect(result).toContain("[mcp_servers.ue-mcp]");
    expect(result).toContain('args = ["ue-mcp", "M:/Game/Game.uproject"]');
  });
});
