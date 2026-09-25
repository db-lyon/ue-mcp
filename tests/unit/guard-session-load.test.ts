/** Exercise failed add_editor guard construction through the real MCP server. */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { expect, it } from "vitest";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { FakeBridge } from "../fake-bridge.js";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");

it("retries failed guard construction before publishing a runtime editor", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-guard-session-"));
  const bridges: FakeBridge[] = [];
  const client = new Client({ name: "guard-session-test", version: "1" }, { capabilities: {} });
  const log = fs.openSync(path.join(dir, "server.log"), "a");
  try {
    const projects = ["Alpha", "Beta"].map((name) => {
      const projectDir = path.join(dir, name);
      fs.mkdirSync(path.join(projectDir, "Content"), { recursive: true });
      const uproject = path.join(projectDir, `${name}.uproject`);
      fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6" }));
      return { projectDir, uproject };
    });
    for (const project of projects) {
      bridges.push(await FakeBridge.start({
        projectDir: project.projectDir,
        handlers: { list_dialogs: () => ({ success: true, dialogs: [] }) },
      }));
    }
    const config = path.join(projects[1].projectDir, "ue-mcp.yml");
    fs.writeFileSync(config, "guards:\n  policy:\n    before:\n      class_path: missing_guard_for_session_test\n");
    const env: Record<string, string> = Object.fromEntries(
      Object.entries(process.env).filter(([key, value]) => !key.startsWith("UE_MCP_") && value !== undefined),
    ) as Record<string, string>;
    env.UE_MCP_HOST = "127.0.0.1";
    env.UE_MCP_STATE_DIR = path.join(dir, "state");
    env.UE_MCP_CONFIG_DIR = path.join(dir, "config");
    env.UE_MCP_GLOBAL_CONFIG = path.join(dir, "global.yml");
    fs.writeFileSync(env.UE_MCP_GLOBAL_CONFIG, "guards: [broken");
    await client.connect(new StdioClientTransport({
      command: process.execPath,
      args: ["--import", "tsx", path.join(repo, "src/index.ts"), projects[0].uproject],
      cwd: repo,
      env,
      stderr: log,
    }));
    const add = () => client.callTool({ name: "project", arguments: {
      action: "add_editor", projectPath: projects[1].uproject, editor: "Alpha",
    } });
    for (let attempt = 0; attempt < 2; attempt++) {
      const result = await add();
      expect(result.isError).toBe(true);
      expect(JSON.stringify(result.content)).toContain("missing_guard_for_session_test");
      expect(JSON.stringify(result.content)).toContain("ue-mcp.yml");
    }
    // A failed preparation also prevents direct dispatch to the registered name.
    const blocked = await client.callTool({ name: "level", arguments: { action: "get_outliner", editor: "Beta" } });
    expect(blocked.isError).toBe(true);
    expect(bridges[1].methods).not.toContain("get_outliner");
    fs.writeFileSync(config, "{}\n");
    expect((await add()).isError).not.toBe(true);
  } finally {
    await client.close();
    for (const bridge of bridges) await bridge.stop();
    fs.closeSync(log);
    fs.rmSync(dir, { recursive: true, force: true });
  }
}, 60_000);
