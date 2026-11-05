/**
 * `ue-mcp playtest mode`, spawned: what it stores, and that an MCP session
 * started afterwards has its pie(script_run) and pie(script_replay) refused
 * under manual, directly and through the micro gateway, without the editor
 * being called, and run under automatic.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, plain, runCli, Sandbox, type CliResult } from "./harness.js";

let sb: Sandbox;
let uproject: string;

const statePath = () => path.join(sb.home, ".ue-mcp", "state.json");
const state = () => (fs.existsSync(statePath()) ? JSON.parse(fs.readFileSync(statePath(), "utf8")) : {}) as {
  preferences?: { playtest?: { mode?: string } };
  projects?: Record<string, { playtest?: { mode?: string } }>;
};

async function playtest(args: string[]): Promise<CliResult> {
  const r = await runCli(sb, ["playtest", ...args]);
  return { ...r, stdout: plain(r.stdout), stderr: plain(r.stderr) };
}

beforeAll(() => {
  sb = new Sandbox();
  uproject = sb.makeProject("Play");
});
afterAll(() => sb.cleanup());

describe("ue-mcp playtest mode", () => {
  it("prints unset, stores user-wide and per-editor modes, and clears them", async () => {
    covers("cli.playtest.mode");
    const initial = await playtest(["mode"]);
    expect(initial.code).toBe(0);
    expect(initial.stdout).toContain("effective: not set (each client decides)");

    expect((await playtest(["mode", "automatic"])).code).toBe(0);
    expect((await playtest(["mode", "MANUAL", "--editor", uproject])).code).toBe(0);
    expect(state().preferences?.playtest?.mode).toBe("automatic");
    expect(Object.values(state().projects ?? {}).map((p) => p.playtest?.mode)).toEqual(["manual"]);

    const [user, editor] = await Promise.all([playtest(["mode"]), playtest(["mode", "--editor", uproject])]);
    expect(user.stdout).toMatch(/effective:\s+automatic\n/);
    expect(editor.stdout).toMatch(/effective:\s+manual\n/);
    expect(editor.stdout).toContain("editor 'Play': manual");

    const before = fs.readFileSync(statePath(), "utf8");
    const bad = await playtest(["mode", "sometimes"]);
    expect(bad.code).toBe(1);
    expect(bad.stdout + bad.stderr).toContain('Unknown mode "sometimes"');
    expect(fs.readFileSync(statePath(), "utf8")).toBe(before);

    expect((await playtest(["mode", "clear", "--editor", uproject])).code).toBe(0);
    expect(state().projects).toBeUndefined();
    expect((await playtest(["mode", "clear"])).code).toBe(0);
    expect(fs.existsSync(statePath())).toBe(false);
  });

  it("decides whether an MCP session may start a playtest run", async () => {
    covers("cli.playtest.mode");
    covers("server.playtest-mode");
    const projectDir = path.dirname(uproject);
    const bridge = await FakeBridge.start({
      projectDir,
      handlers: {
        pie_script_run: () => ({ success: true, run_id: "run-1", status: "running" }),
        pie_script_replay: () => ({ success: true, run_id: "run-1", status: "running" }),
      },
    });
    const ran = () => bridge.calls.filter((c) => c.method === "pie_script_run" || c.method === "pie_script_replay").length;
    try {
      expect((await playtest(["mode", "manual", "--editor", uproject])).code).toBe(0);
      const full = await McpServer.start(sb, { projects: [uproject], strategy: "full", port: null });
      try {
        const refused = await full.call("pie", { action: "script_run", args: { script: "Scripts/smoke.yaml" } });
        expect(refused.isError).toBe(true);
        const body = json(refused);
        expect(body).toMatchObject({ success: false, refusedMethod: "pie.script_run", playtestMode: "manual" });
        expect(String(body.reason)).toContain("ue-mcp playtest mode automatic");
        const replay = json(await full.call("pie", { action: "script_replay", args: { run_id: "run-1" } }));
        expect(replay).toMatchObject({ success: false, refusedMethod: "pie.script_replay" });
        // Anything else in pie is untouched.
        expect(json(await full.call("pie", { action: "script_status", args: { run_id: "run-1" } }))).not.toHaveProperty("playtestMode");
      } finally {
        await full.close();
      }

      const micro = await McpServer.start(sb, { projects: [uproject], strategy: "micro", port: null });
      try {
        const viaGateway = json(await micro.call("tools", { action: "call", category: "pie", method: "script_run", args: { script: "Scripts/smoke.yaml" } }));
        expect(viaGateway).toMatchObject({ success: false, refusedMethod: "pie.script_run" });
      } finally {
        await micro.close();
      }
      expect(ran()).toBe(0);

      expect((await playtest(["mode", "automatic", "--editor", uproject])).code).toBe(0);
      const allowed = await McpServer.start(sb, { projects: [uproject], strategy: "full", port: null });
      try {
        const r = await allowed.call("pie", { action: "script_run", args: { script: "Scripts/smoke.yaml" } });
        expect(r.text).not.toContain("playtestMode");
        expect(ran()).toBe(1);
      } finally {
        await allowed.close();
      }
    } finally {
      await playtest(["mode", "clear", "--editor", uproject]);
      await bridge.stop();
    }
  });
});
