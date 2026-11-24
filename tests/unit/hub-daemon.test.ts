/**
 * What ties the hub to the daemon: the editor signals worked out from the
 * daemon's events, the hub a client configures through api.hub.configure,
 * ue-mcp's own broker starting a script, and the provenance pie(script_run)
 * records beside a run.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { ActionResult } from "../../src/contract/daemon.js";
import { EditorSignals, createDaemonHub } from "../../src/hub/daemon-hub.js";
import { HubHolder, hubOf, registerHub } from "../../src/hub/hub-client.js";
import { ScriptRunBroker } from "../../src/hub/script-broker.js";
import { ueMcpProvenance } from "../../src/hub/provenance.js";
import { readProvenanceFile } from "../../src/hub/evidence.js";
import { recordRunProvenance } from "../../src/pie/script-provenance.js";

const answer = (body: Record<string, unknown>): ActionResult => ({ isError: false, content: [{ type: "text", text: JSON.stringify({ success: true, ...body }) }] });
let root: string;
beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "hub-daemon-"));
});
afterEach(() => fs.rmSync(root, { recursive: true, force: true }));

describe("editor signals", () => {
  it("follow PIE, other clients' calls and editor operations from the daemon's events, and ignore the hub's own category", () => {
    const s = new EditorSignals(() => 1000);
    const seen: boolean[] = [];
    s.onPie((r) => seen.push(r));
    s.observe({ type: "pie.started", data: {} });
    s.observe({ type: "pie.started", data: {} });
    expect(s.pie()).toBe(true);
    s.observe({ type: "pie.ended", data: {} });
    expect(seen).toEqual([true, false]);

    s.observe({ type: "call.started", data: { client: "claude-code", category: "asset", action: "list" } });
    s.observe({ type: "call.started", data: { client: "claude-code", category: "hub", action: "run_plan" } });
    expect(s.busy().calls).toMatchObject([{ category: "asset", action: "list", source: "client", client: "claude-code" }]);
    s.observe({ type: "call.finished", data: { client: "claude-code", category: "asset", action: "list" } });
    expect(s.busy().calls).toEqual([]);

    s.observe({ type: "editor.operation", data: { op: "rebuild", step: "editor.stop_editor", phase: "started" } });
    expect(s.busy().operation).toMatchObject({ op: "rebuild", step: "editor.stop_editor" });
    s.observe({ type: "editor.operation", data: { op: "rebuild", step: "project.build", phase: "failed", reason: "compile error" } });
    expect(s.busy()).toMatchObject({ failure: { op: "rebuild", step: "project.build", reason: "compile error" } });
    expect(s.busy().operation).toBeUndefined();
    s.observe({ type: "editor.connected", data: {} });
    expect(s.busy().failure).toBeUndefined();
  });
});

describe("the daemon's hub", () => {
  const deps = () => {
    const published: Array<{ type: string; data?: Record<string, unknown> }> = [];
    return {
      published,
      deps: {
        projectDir: path.join(root, "project"),
        dataDir: path.join(root, "data"),
        events: { publish: (type: string, data?: Record<string, unknown>) => void published.push({ type, data }), subscribe: () => () => undefined },
        callAction: async () => answer({}),
        editors: () => [{ connected: false }],
        dialogs: () => 0,
        log: { warn() {} },
        timers: false,
      },
    };
  };

  it("keeps its files under the project's data folder, and is found by project for the hub category", async () => {
    fs.mkdirSync(path.join(root, "project"), { recursive: true });
    const d = deps();
    const holder = new HubHolder((client) => createDaemonHub(d.deps as never, client));
    const off = registerHub(path.join(root, "project"), holder);
    try {
      const hub = hubOf(path.join(root, "project"));
      expect(hub).toBe(holder.get());
      hub!.goals.create({ title: "Door", id: "door" });
      expect(fs.existsSync(path.join(root, "data", "hub", "goals"))).toBe(true);
      expect(hubOf(path.join(root, "elsewhere"))).toBeNull();
    } finally {
      off();
      holder.dispose();
    }
    expect(hubOf(path.join(root, "project"))).toBeNull();
  });

  it("is rebuilt around what a client configures, and goes back when it takes it away", async () => {
    fs.mkdirSync(path.join(root, "project"), { recursive: true });
    const d = deps();
    const holder = new HubHolder((client) => createDaemonHub(d.deps as never, client));
    try {
      const first = holder.get();
      // Without a signer from a client, the daemon's own stands: none here, so a sign-off is refused.
      first.goals.create({ title: "Door", id: "door" });
      await expect(first.signOff("door", {})).rejects.toMatchObject({ status: 409 });
      const off = holder.configure({ signer: () => "ama" });
      const second = holder.get();
      expect(second).not.toBe(first);
      expect(second.goals.get("door")).toBeDefined();
      await expect(second.signOff("door", {})).resolves.toMatchObject({ by: "ama" });
      off();
      await expect(holder.get().signOff("door", {})).rejects.toMatchObject({ status: 409 });
      // A client with no fixer leaves fix loops to answer 409.
      await expect(holder.get().fix({})).rejects.toMatchObject({ status: 409 });
    } finally {
      holder.dispose();
    }
  });
});

describe("ue-mcp's own broker", () => {
  it("starts a script with PIE, tells listeners, reads status, and stops a run", async () => {
    const calls: Array<{ action: string; args: Record<string, unknown> }> = [];
    const broker = new ScriptRunBroker({
      log: { warn() {} },
      callAction: async (_c, action, args = {}) => {
        calls.push({ action, args });
        if (action === "script_run") return answer({ run_id: "r1", script_name: "DoorOpens", state: "armed", steps: [{ index: 0, instruction: "x", judge: "auto", status: "pending" }], last_seq: 1, pie_started: true });
        if (action === "script_status") return answer({ run_id: "r1", state: "finished", steps: [{ index: 0, instruction: "x", judge: "auto", status: "passed" }], last_seq: 4 });
        return answer({});
      },
    });
    const saved: string[] = [];
    broker.onSaved((r) => saved.push(`${r.runId}:${r.state}`));
    const started = await broker.startScript("/Game/Tests/DoorOpens");
    expect("run" in started && started.run).toMatchObject({ runId: "r1", script: "/Game/Tests/DoorOpens", pieStarted: true });
    expect(calls[0]).toEqual({ action: "script_run", args: { script: "/Game/Tests/DoorOpens", start_pie: true, record: true } });
    const read = await broker.status("r1");
    expect("run" in read && read.run?.state).toBe("finished");
    expect(calls[1].args).toEqual({ run_id: "r1", since_seq: 1 });
    expect(saved).toEqual(["r1:armed", "r1:finished"]);
    expect(broker.owns(broker.get("r1")!)).toBe(true);
    // A finished run cannot be stopped again.
    expect(await broker.stopRun("r1", "x")).toMatchObject({ error: "only a run that is going can be stopped" });
  });
});

describe("provenance at script_run", () => {
  it("records the workspace, the script's version, the engine and the players beside the run, in the shape the hub reads", async () => {
    const projectDir = path.join(root, "project");
    fs.mkdirSync(projectDir, { recursive: true });
    const ctx = {
      project: { projectDir, engineAssociation: "5.6" },
      bridge: {
        call: async (method: string) => {
          if (method === "pie_script_read") return { success: true, path: "/Game/Tests/Door", name: "Door", map: "/Game/Maps/L", steps: [{ instruction: "x", judge: "auto" }] };
          if (method === "pie_clients") return { success: true, networked: true, server_net_mode: "listen_server", host_player: true, clients: [{ client: 1 }] };
          throw new Error(`unexpected ${method}`);
        },
      },
    };
    await recordRunProvenance(ctx as never, "script_Door_1", "/Game/Tests/Door");
    const file = readProvenanceFile(projectDir, "script_Door_1");
    expect(file).toMatchObject({ environment: { engine_version: "5.6", net_mode: "listen", clients: 2 } });
    expect(String(file?.script_hash)).toMatch(/^[0-9a-f]{64}$/);
    // The hub takes it as ue-mcp's own record, and results.json's own provenance wins over it.
    expect(ueMcpProvenance(null, file)).toMatchObject({ scriptHash: `ue-mcp:${String(file?.script_hash)}`, engine: "5.6", players: { count: 2, netMode: "listen" } });
    expect(ueMcpProvenance({ provenance: { script_hash: "f00d" } }, file).scriptHash).toBe("ue-mcp:f00d");
  });

  it("is never in the way: a run id that could leave the folder, or a failing editor, records nothing and throws nothing", async () => {
    const projectDir = path.join(root, "project");
    fs.mkdirSync(projectDir, { recursive: true });
    const ctx = { project: { projectDir, engineAssociation: null }, bridge: { call: async () => { throw new Error("offline"); } } };
    await expect(recordRunProvenance(ctx as never, "../escape", "/Game/Tests/Door")).resolves.toBeUndefined();
    expect(fs.existsSync(path.join(root, "escape"))).toBe(false);
    await expect(recordRunProvenance(ctx as never, "script_Door_2", "/Game/Tests/Door")).resolves.toBeUndefined();
    expect(readProvenanceFile(projectDir, "script_Door_2")).toMatchObject({ environment: {} });
  });
});
