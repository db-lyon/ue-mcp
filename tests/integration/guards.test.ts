/**
 * Guards declared in ue-mcp.yml, on a real server with a bridge behind it.
 * The guard is a shell command that exits non-zero, so a refusal comes from
 * the configured hook and nothing else. What is asserted is what the bridge
 * received: a refused call must never reach the editor.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { pathToFileURL } from "node:url";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, REPO_ROOT, runCli, Sandbox, stopSandboxDaemons } from "./harness.js";
import { call as daemonCall, daemonEnv, readDiscovery } from "./daemon-fakes.js";

const guarded = (scope: string) => `ue-mcp:
  version: 1
guards:
  policy:
    scope: ${scope}
    before:
      class_path: shell
      options:
        command: exit 3
flows:
  spawn_light:
    description: One mutating step.
    steps:
      "1":
        task: level.place_actor
        options:
          actorClass: PointLight
`;

let sb: Sandbox;
let mutationsBridge: FakeBridge;
let writesBridge: FakeBridge;
let mutations: McpServer;
let writes: McpServer;
let writesProjectDir: string;

const made = (b: FakeBridge) => b.methods.filter((m) => m !== "list_dialogs");
const call = (s: McpServer, category: string, method: string, args: Record<string, unknown> = {}) =>
  s.call("tools", { action: "call", category, method, args });

beforeAll(async () => {
  sb = new Sandbox();
  const mutationsProject = sb.makeProject("Frozen", { config: guarded("mutations") });
  const writesProject = sb.makeProject("Tracked", { config: guarded("writes"), content: true });
  writesProjectDir = path.dirname(writesProject);
  fs.writeFileSync(path.join(writesProjectDir, "Content", "Existing.uasset"), "not a real package");
  [mutationsBridge, writesBridge] = await Promise.all([
    FakeBridge.start({ projectDir: path.dirname(mutationsProject) }),
    FakeBridge.start({ projectDir: writesProjectDir }),
  ]);
  [mutations, writes] = await Promise.all([
    McpServer.start(sb, { projects: [mutationsProject], port: null }),
    McpServer.start(sb, { projects: [writesProject], port: null }),
  ]);
}, 60_000);

afterAll(async () => {
  await mutations?.close();
  await writes?.close();
  await Promise.all([mutationsBridge?.stop(), writesBridge?.stop()]);
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("a guard from ue-mcp.yml", () => {
  it("refuses the calls it covers before they reach the editor and lets the rest through", async () => {
    covers("guards.config");
    mutationsBridge.calls.length = 0;

    const spawn = await call(mutations, "level", "place_actor", { actorClass: "PointLight" });
    expect(spawn.isError).toBe(true);
    expect(spawn.text).toContain("blocked (place_actor)");
    const del = await call(mutations, "asset", "delete", { assetPath: "/Game/Anything" });
    expect(del.isError).toBe(true);
    expect(del.text).toContain("blocked (delete_asset)");
    expect(made(mutationsBridge)).toEqual([]);

    const read = await call(mutations, "asset", "list_textures", { directory: "/Game" });
    expect(read.isError).toBe(false);
    expect(json(read).method).toBe("list_textures");
    expect(made(mutationsBridge)).toEqual(["list_textures"]);
  });

  it("guards a flow's steps the same way", async () => {
    covers("guards.config");
    mutationsBridge.calls.length = 0;
    const run = json<{ success: boolean; steps: Array<{ success: boolean; error?: string }> }>(
      await mutations.call("flow", { action: "run", flowName: "spawn_light" }),
    );
    expect(run.success).toBe(false);
    expect(JSON.stringify(run.steps[0])).toContain("blocked (place_actor)");
    expect(made(mutationsBridge)).toEqual([]);
  });
});

describe("write detection", () => {
  it("scopes a writes guard to calls that modify existing content, and names the file", async () => {
    covers("guards.write-detect");
    writesBridge.calls.length = 0;

    const existing = await call(writes, "asset", "delete", { assetPath: "/Game/Existing" });
    expect(existing.isError).toBe(true);
    expect(existing.text).toContain("blocked (delete_asset)");
    expect(existing.text).toContain(path.join(writesProjectDir, "Content", "Existing.uasset"));
    expect(made(writesBridge)).toEqual([]);

    // Nothing on disk to modify, and a mutation that names no content: both pass a writes guard.
    const absent = await call(writes, "asset", "delete", { assetPath: "/Game/NotOnDisk" });
    expect(absent.isError).toBe(false);
    const spawn = await call(writes, "level", "place_actor", { actorClass: "PointLight" });
    expect(spawn.isError).toBe(false);
    const read = await call(writes, "asset", "list_textures", { directory: "/Game" });
    expect(read.isError).toBe(false);
    expect(made(writesBridge)).toEqual(["delete_asset", "place_actor", "list_textures"]);
  });
});

describe("a guard class_path in ue-mcp.yml", () => {
  it("resolves from the project folder when the daemon starts from another folder (#1590)", async () => {
    covers("guards.config", "cli.daemon.start");
    const project = sb.makeProject("FarProject", {
      config: `ue-mcp:
  version: 1
guards:
  freeze:
    scope: mutations
    before:
      class_path: guards.BlockMutations
`,
    });
    const projectDir = path.dirname(project);
    const guardSource = pathToFileURL(path.join(REPO_ROOT, "src", "guard-task.ts")).href;
    fs.mkdirSync(path.join(projectDir, "guards"));
    fs.writeFileSync(path.join(projectDir, "guards", "package.json"), JSON.stringify({ type: "module" }));
    fs.writeFileSync(
      path.join(projectDir, "guards", "BlockMutations.js"),
      `import { UeMcpGuard } from ${JSON.stringify(guardSource)};
export default class BlockMutations extends UeMcpGuard {
  get taskName() { return "block-mutations"; }
  before(call) { return this.deny("frozen by guards/BlockMutations: " + call.method); }
}
`,
    );
    // Neither the CLI nor the daemon it spawns starts in the project folder.
    const elsewhere = sb.path("launch-dir");
    fs.mkdirSync(elsewhere, { recursive: true });
    expect(path.relative(elsewhere, projectDir).startsWith("..")).toBe(true);
    const bridge = await FakeBridge.start({ projectDir });
    try {
      const started = await runCli(sb, ["daemon", "start", project, "--json"], {
        cwd: elsewhere,
        env: daemonEnv({ UE_MCP_PORT: undefined }),
      });
      expect(started.code, started.stderr + started.stdout).toBe(0);
      const d = readDiscovery(sb, project)!;
      expect(d).not.toBeNull();

      const server = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null, cwd: elsewhere });
      try {
        expect((await daemonCall(d, "GET", "/v1/health")).body.clients).toBe(1);
        const spawn = await call(server, "level", "place_actor", { actorClass: "PointLight" });
        expect(spawn.isError).toBe(true);
        expect(spawn.text).toContain("frozen by guards/BlockMutations: place_actor");
        expect(made(bridge)).not.toContain("place_actor");
      } finally {
        await server.close();
      }
    } finally {
      await bridge.stop();
    }
  }, 90_000);
});
