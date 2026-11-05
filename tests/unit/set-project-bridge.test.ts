// #1615: project(set_project) deployed the server's own bridge source over the
// target project's installed bridge. A switch only detects and enables; the
// target's Plugins/UE_MCP_Bridge stays byte-identical.
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import yaml from "js-yaml";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { EditorBridge } from "../../src/bridge/bridge.js";
import { ProjectContext } from "../../src/config/project.js";
import { sessionActions } from "../../src/tools/project/sessions.js";
import type { ToolContext } from "../../src/core/types.js";

const dirs: string[] = [];
let savedEnvPort: string | undefined;

function makeProject(name: string): { uproject: string; dir: string } {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `ue-mcp-setproj-${name}-`));
  dirs.push(dir);
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, Plugins: [{ Name: "UE_MCP_Bridge", Enabled: true }] }));
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
  // Port 1: nothing listens, so no real editor is reached.
  fs.writeFileSync(path.join(dir, "ue-mcp.yml"), yaml.dump({ "ue-mcp": { version: 1, bridge: { port: 1 } } }));
  return { uproject, dir };
}

/** Every file under a folder, by relative path, with its bytes. */
function snapshot(root: string): Record<string, string> {
  const out: Record<string, string> = {};
  const walk = (d: string) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) walk(p);
      else out[path.relative(root, p)] = fs.readFileSync(p).toString("base64");
    }
  };
  walk(root);
  return out;
}

beforeEach(() => {
  savedEnvPort = process.env.UE_MCP_PORT;
  delete process.env.UE_MCP_PORT;
  // Keep the developer's ~/.ue-mcp/config.yml out of this test.
  const global = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-global-"));
  dirs.push(global);
  process.env.UE_MCP_GLOBAL_CONFIG = path.join(global, "config.yml");
});

afterEach(() => {
  for (const d of dirs.splice(0)) fs.rmSync(d, { recursive: true, force: true });
  delete process.env.UE_MCP_GLOBAL_CONFIG;
  if (savedEnvPort === undefined) delete process.env.UE_MCP_PORT;
  else process.env.UE_MCP_PORT = savedEnvPort;
});

describe("project(set_project) and the target's installed bridge (#1615)", () => {
  it("leaves a bridge that init or update installed byte-identical, and reports the mismatch", async () => {
    const a = makeProject("A");
    const b = makeProject("B");
    const plugin = path.join(b.dir, "Plugins", "UE_MCP_Bridge");
    fs.mkdirSync(path.join(plugin, "Source", "UE_MCP_Bridge", "Private"), { recursive: true });
    fs.mkdirSync(path.join(plugin, "Content", "Python"), { recursive: true });
    fs.writeFileSync(path.join(plugin, "UE_MCP_Bridge.uplugin"), JSON.stringify({ FileVersion: 3, VersionName: "0.0.1-installed", FriendlyName: "UE MCP Bridge" }));
    fs.writeFileSync(path.join(plugin, "Source", "UE_MCP_Bridge", "Private", "Installed.cpp"), "// the installed bridge\n");
    fs.writeFileSync(path.join(plugin, "Content", "Python", "legacy.py"), "# kept\n");
    fs.writeFileSync(path.join(plugin, ".ue-mcp-install.json"), JSON.stringify({ kind: "source", engine: "5.7", platform: "Win64", version: "0.0.1-installed" }));
    const before = snapshot(plugin);

    const project = new ProjectContext();
    const bridge = new EditorBridge();
    project.setProject(a.uproject);
    bridge.setProjectContext(project.projectPath);
    try {
      const ctx = { project, bridge } as unknown as ToolContext;
      const handler = (sessionActions.set_project as { handler: (c: ToolContext, p: Record<string, unknown>) => Promise<Record<string, unknown>> }).handler;
      const result = await handler(ctx, { projectPath: b.uproject });

      expect(snapshot(plugin)).toEqual(before);
      expect(project.projectPath).toBe(path.resolve(b.uproject));
      expect(String(result.bridgeSetup)).toMatch(/version mismatch - installed v0\.0\.1-installed/);
      expect(String(result.bridgeSetup)).toMatch(/Plugin left untouched/);
    } finally {
      bridge.disconnect();
    }
  });
});
