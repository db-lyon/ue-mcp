/**
 * `ue-mcp flow list|plan|run`, spawned against a project whose bridge is a
 * fake that answers like an editor (#1477). No server runs: the command builds
 * the flow engine in its own process.
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { plain, runCli, Sandbox, snapshotTree } from "./harness.js";

const CONFIG = `ue-mcp:
  version: 1
flows:
  inspect:
    description: Read the project, then list its textures in the editor.
    steps:
      "1":
        task: project.get_info
      "2":
        task: asset.list_textures
        options:
          directory: /Game/\${steps.1.projectName}
  stops_on_failure:
    description: A step the editor fails, and one that must not run after it.
    steps:
      "1":
        task: asset.list_textures
        options:
          directory: /Game
      "2":
        task: asset.delete
        options:
          assetPath: /Game/Refused
      "3":
        task: asset.list_textures
        options:
          directory: /Game/After
`;

let sb: Sandbox;
let project: string;
let cwd: string;
let bridge: FakeBridge;

const made = () => bridge.calls.filter((c) => c.method !== "list_dialogs");

beforeAll(async () => {
  sb = new Sandbox("ue-mcp-flow-cli-");
  project = sb.makeProject("Flows", { config: CONFIG });
  cwd = path.dirname(project);
  bridge = await FakeBridge.start({
    projectDir: cwd,
    answerAnything: false,
    handlers: { list_textures: (p) => ({ success: true, directory: p.directory, textures: [] }) },
  });
}, 60_000);

afterAll(async () => {
  await bridge?.stop();
  sb.cleanup();
});

describe("flow list", () => {
  it("prints the project's flows and the built-ins, without touching the editor", async () => {
    covers("cli.flow.list");
    bridge.calls.length = 0;
    const before = snapshotTree(cwd);
    const [text, json] = await Promise.all([
      runCli(sb, ["flow", "list"], { cwd }),
      runCli(sb, ["flow", "list", "--json"], { cwd }),
    ]);
    expect(text.code, text.stderr).toBe(0);
    const out = plain(text.stdout);
    expect(out).toMatch(/inspect\s+2 steps\s+Read the project, then list its textures in the editor\./);
    expect(out).toContain("niagara_fire");

    expect(json.code, json.stderr).toBe(0);
    const listed = JSON.parse(json.stdout) as { flowCount: number; flows: Array<{ name: string; stepCount: number }> };
    expect(listed.flowCount).toBe(listed.flows.length);
    expect(listed.flows.find((f) => f.name === "stops_on_failure")?.stepCount).toBe(3);
    expect(made()).toEqual([]);
    expect(snapshotTree(cwd)).toEqual(before);
  });
});

describe("flow plan", () => {
  it("prints the resolved steps without running any of them", async () => {
    covers("cli.flow.plan");
    bridge.calls.length = 0;
    const [text, json, missing] = await Promise.all([
      runCli(sb, ["flow", "plan", "inspect"], { cwd }),
      runCli(sb, ["flow", "plan", "inspect", "--json"], { cwd }),
      runCli(sb, ["flow", "plan", "no_such_flow"], { cwd }),
    ]);
    expect(text.code, text.stderr).toBe(0);
    const out = plain(text.stdout);
    expect(out).toContain("1. project.get_info");
    expect(out).toContain("2. asset.list_textures");

    expect(json.code, json.stderr).toBe(0);
    const plan = JSON.parse(json.stdout) as { steps: Array<{ stepNumber: number; name: string }>; definition: { steps: Record<string, { options?: unknown }> } };
    expect(plan.steps.map((s) => [s.stepNumber, s.name])).toEqual([[1, "project.get_info"], [2, "asset.list_textures"]]);
    expect(plan.definition.steps["2"].options).toEqual({ directory: "/Game/${steps.1.projectName}" });

    expect(missing.code).toBe(1);
    expect(missing.stderr).toContain("no_such_flow");
    expect(made()).toEqual([]);
  });
});

describe("flow run", () => {
  it("runs the flow against the editor, streams each step and exits 0", async () => {
    covers("cli.flow.run");
    bridge.calls.length = 0;
    const r = await runCli(sb, ["flow", "run", "inspect"], { cwd });
    expect(r.code, r.stdout + r.stderr).toBe(0);
    const out = plain(r.stdout);
    expect(out).toMatch(/1\. project\.get_info ok/);
    expect(out).toMatch(/2\. asset\.list_textures ok/);
    expect(out).toContain("Flow completed");
    expect(made()).toEqual([{ method: "list_textures", params: { directory: "/Game/Flows" } }]);
  });

  it("exits 1 on a failed step, runs nothing after it, and --json prints the run result", async () => {
    covers("cli.flow.run", "cli.flow.json");
    bridge.calls.length = 0;
    const r = await runCli(sb, ["flow", "run", "stops_on_failure", "--json"], { cwd });
    expect(r.code, r.stdout + r.stderr).toBe(1);
    const run = JSON.parse(r.stdout) as { success: boolean; failedStep: string; steps: Array<{ stepNumber: number; success: boolean }> };
    expect(run.success).toBe(false);
    expect(run.failedStep).toBe("asset.delete");
    expect(run.steps.find((s) => s.stepNumber === 3)?.success).not.toBe(true);
    expect(made().map((c) => c.method)).toEqual(["list_textures", "delete_asset"]);
  });

  it("passes --param to every step and skips what --skip names", async () => {
    covers("cli.flow.run");
    bridge.calls.length = 0;
    const r = await runCli(sb, ["flow", "run", "stops_on_failure", "--skip", "2", "--param", "directory=/Game/FromCli", "--json"], { cwd });
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(made()).toEqual([
      { method: "list_textures", params: { directory: "/Game/FromCli" } },
      { method: "list_textures", params: { directory: "/Game/FromCli" } },
    ]);
  });

  it("refuses a run with no flow name, or outside a project, with exit 2", async () => {
    covers("cli.flow.run");
    const [noName, noProject] = await Promise.all([
      runCli(sb, ["flow", "run"], { cwd }),
      runCli(sb, ["flow", "run", "inspect", "--json"], { cwd: sb.home }),
    ]);
    expect(noName.code).toBe(2);
    expect(noName.stderr).toContain("needs a flow name");
    expect(noProject.code).toBe(2);
    expect(JSON.parse(noProject.stdout)).toEqual({ ok: false, error: expect.stringContaining("No .uproject found") });
  });
});
