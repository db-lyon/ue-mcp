import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  findUnrealMcpEntries,
  removeUnrealMcpEntries,
  REPLACED_BACKUP_SUFFIX,
  sourceBuildConfigRoot,
} from "../../src/integrations/claude-code/unreal-mcp-entries.js";
import { readUeMcpEntry, writeMcpConfig, type McpClient } from "../../src/integrations/claude-code/mcp-client-config.js";
import { proxyMcpJson, UNREAL_CLIENT_FIXTURES, UNREAL_URL } from "../helpers/unreal-mcp-fixtures.js";

let dir: string;
beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-editor-entries-"));
});
afterEach(() => fs.rmSync(dir, { recursive: true, force: true }));

function clientFor(fixture: (typeof UNREAL_CLIENT_FIXTURES)[number]): McpClient {
  const configPath = path.join(dir, fixture.file);
  fs.mkdirSync(path.dirname(configPath), { recursive: true });
  fs.writeFileSync(configPath, fixture.content);
  return {
    name: fixture.id,
    configPath,
    detected: true,
    format: fixture.serversKey ? "json" : "toml",
    serversKey: fixture.serversKey ?? undefined,
    stdioType: fixture.id === "vscode",
  };
}

describe("Unreal MCP entries per client format", () => {
  for (const fixture of UNREAL_CLIENT_FIXTURES) {
    describe(fixture.id, () => {
      it("finds the HTTP entry and nothing else", () => {
        expect(findUnrealMcpEntries(clientFor(fixture))).toEqual([{ name: "unreal-mcp", kind: "http", target: UNREAL_URL }]);
      });

      it("removes only that entry, saves the file first, and the ue-mcp entry lands beside the rest", () => {
        const client = clientFor(fixture);
        const r = removeUnrealMcpEntries(client);
        expect(r.removed.map((e) => e.name)).toEqual(["unreal-mcp"]);
        expect(r.backup).toBe(`${client.configPath}${REPLACED_BACKUP_SUFFIX}`);
        expect(fs.readFileSync(r.backup!, "utf-8")).toBe(fixture.content);

        writeMcpConfig(client, path.join(dir, "Game.uproject"));
        expect(findUnrealMcpEntries(client)).toEqual([]);
        expect(readUeMcpEntry(client).configured).toBe(true);

        const after = fs.readFileSync(client.configPath, "utf-8");
        if (fixture.serversKey) {
          const parsed = JSON.parse(after);
          expect(Object.keys(parsed[fixture.serversKey]).sort()).toEqual(["my-existing-server", "ue-mcp"]);
          expect(parsed[fixture.serversKey]["my-existing-server"]).toEqual(JSON.parse(fixture.content)[fixture.serversKey]["my-existing-server"]);
          // Keys outside the servers object are kept.
          for (const key of Object.keys(JSON.parse(fixture.content))) expect(parsed).toHaveProperty(key);
        } else {
          expect(after).toContain('model = "o3"');
          expect(after).toContain('[mcp_servers.my-existing-server]\ncommand = "my-server"');
          expect(after).not.toContain("unreal-mcp");
          expect(after).toContain("[mcp_servers.ue-mcp]");
        }
      });
    });
  }

  it("writes VS Code's entry under servers with type stdio", () => {
    const client = clientFor(UNREAL_CLIENT_FIXTURES.find((f) => f.id === "vscode")!);
    removeUnrealMcpEntries(client);
    writeMcpConfig(client, path.join(dir, "Game.uproject"));
    const parsed = JSON.parse(fs.readFileSync(client.configPath, "utf-8"));
    expect(parsed.mcpServers).toBeUndefined();
    expect(parsed.servers["ue-mcp"]).toMatchObject({ type: "stdio", command: "npx" });
  });

  const proxyBinary = () => path.join(dir, "Engine", "Plugins", "Experimental", "ModelContextProtocol", "Extras", "Proxy", "Bin", "Win64", "unreal_mcp_proxy.exe");

  for (const withFlag of [false, true]) {
    it(`finds and replaces the unreal-mcp-proxy entry install writes ${withFlag ? "with" : "without"} --mcp-json`, () => {
      const configPath = path.join(dir, ".mcp.json");
      const content = proxyMcpJson(proxyBinary(), withFlag ? configPath : undefined);
      fs.writeFileSync(configPath, content);
      const client: McpClient = { name: "Claude Code (project)", configPath, detected: true, format: "json" };

      expect(findUnrealMcpEntries(client)).toEqual([{ name: "unreal-mcp-proxy", kind: "proxy", target: proxyBinary() }]);
      const r = removeUnrealMcpEntries(client);
      expect(fs.readFileSync(r.backup!, "utf-8")).toBe(content);
      expect(Object.keys(JSON.parse(fs.readFileSync(configPath, "utf-8")).mcpServers)).toEqual(["my-existing-server"]);
    });
  }

  it("removes both entries when the editor rewrote unreal-mcp beside an installed proxy", () => {
    const configPath = path.join(dir, ".mcp.json");
    const parsed = JSON.parse(proxyMcpJson(proxyBinary()));
    parsed.mcpServers["unreal-mcp"] = { type: "http", url: "http://127.0.0.1:8001/mcp" };
    fs.writeFileSync(configPath, JSON.stringify(parsed, null, 2));
    const r = removeUnrealMcpEntries({ configPath, format: "json" });
    expect(r.removed.map((e) => e.name).sort()).toEqual(["unreal-mcp", "unreal-mcp-proxy"]);
    expect(Object.keys(JSON.parse(fs.readFileSync(configPath, "utf-8")).mcpServers)).toEqual(["my-existing-server"]);
  });

  it("finds a proxy entry under another name by its command", () => {
    const configPath = path.join(dir, ".mcp.json");
    fs.writeFileSync(configPath, JSON.stringify({ mcpServers: { unreal: { command: "/opt/ue/Proxy/Bin/Linux/unreal_mcp_proxy", args: [] } } }));
    expect(findUnrealMcpEntries({ configPath, format: "json" })).toEqual([{ name: "unreal", kind: "proxy", target: "/opt/ue/Proxy/Bin/Linux/unreal_mcp_proxy" }]);
  });

  it("leaves an unreal-mcp entry that is not on localhost, as the proxy does", () => {
    const configPath = path.join(dir, ".mcp.json");
    fs.writeFileSync(configPath, JSON.stringify({ mcpServers: { "unreal-mcp": { type: "http", url: "https://remote.example.com/mcp" } } }));
    expect(findUnrealMcpEntries({ configPath, format: "json" })).toEqual([]);
  });

  it("leaves other HTTP servers, a ue-mcp entry and unparseable files alone", () => {
    const configPath = path.join(dir, ".mcp.json");
    const content = JSON.stringify({
      mcpServers: {
        "other-http": { type: "http", url: "http://127.0.0.1:8000/mcp" },
        "ue-mcp": { command: "npx", args: ["-y", "ue-mcp@latest", "C:/G/G.uproject"] },
      },
    });
    fs.writeFileSync(configPath, content);
    expect(findUnrealMcpEntries({ configPath, format: "json" })).toEqual([]);
    expect(removeUnrealMcpEntries({ configPath, format: "json" })).toEqual({ removed: [], backup: null });
    expect(fs.readFileSync(configPath, "utf-8")).toBe(content);
    expect(fs.existsSync(`${configPath}${REPLACED_BACKUP_SUFFIX}`)).toBe(false);

    fs.writeFileSync(configPath, "{ not json");
    expect(findUnrealMcpEntries({ configPath, format: "json" })).toEqual([]);
    expect(findUnrealMcpEntries({ configPath: path.join(dir, "missing.json"), format: "json" })).toEqual([]);
  });

  it("removes a TOML server's subtables with it and keeps CRLF line endings", () => {
    const configPath = path.join(dir, "config.toml");
    fs.writeFileSync(
      configPath,
      ["[mcp_servers.unreal-mcp]", `url = "${UNREAL_URL}"`, "", "[mcp_servers.unreal-mcp.env]", 'A = "1"', "", "[mcp_servers.keep]", 'command = "k"', ""].join("\r\n"),
    );
    removeUnrealMcpEntries({ configPath, format: "toml" });
    expect(fs.readFileSync(configPath, "utf-8")).toBe('[mcp_servers.keep]\r\ncommand = "k"\r\n');
  });
});

describe("where a source build's editor writes client configs", () => {
  const engine = (name: string, installed = false) => {
    const root = path.join(dir, name);
    fs.mkdirSync(path.join(root, "Engine", "Build"), { recursive: true });
    if (installed) fs.writeFileSync(path.join(root, "Engine", "Build", "InstalledBuild.txt"), "");
    return root;
  };

  it("is the folder holding Engine/ for a source build", () => {
    const root = engine("UnrealEngine");
    expect(sourceBuildConfigRoot(root, path.join(dir, "Game"))).toBe(path.resolve(root));
  });

  it("is nothing for an installed build, which writes into the project", () => {
    expect(sourceBuildConfigRoot(engine("UE_5.9", true), path.join(dir, "Game"))).toBeNull();
  });

  it("is nothing under Program Files, nothing when unresolved, and nothing when it is the project directory", () => {
    expect(sourceBuildConfigRoot("C:/Program Files/Epic Games/UE_5.9", path.join(dir, "Game"))).toBeNull();
    expect(sourceBuildConfigRoot("C:\\Program Files (x86)\\UE", path.join(dir, "Game"))).toBeNull();
    expect(sourceBuildConfigRoot(null, path.join(dir, "Game"))).toBeNull();
    const root = engine("Same");
    expect(sourceBuildConfigRoot(root, root)).toBeNull();
  });
});
