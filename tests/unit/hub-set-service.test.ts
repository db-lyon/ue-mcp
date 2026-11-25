/**
 * The hub's set end to end (#2113) over the daemon's real routes, with the
 * editor's actions faked: goals becoming tracks once, set ops with their
 * findings, make unique, and track and scene runs played as one composed run
 * whose instances report at their places, judged into status.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { ActionResult } from "../../src/contract/daemon.js";
import { StatusError } from "../../src/core/status-error.js";
import { hubRoutes } from "../../src/daemon/hub-routes.js";
import { findRoute, type RouteHandler, type RouteRequest } from "../../src/daemon/router.js";
import { GoalFiles } from "../../src/hub/goal-files.js";
import { Hub } from "../../src/hub/hub.js";
import { ScriptIndex } from "../../src/hub/script-index.js";
import { ScriptRunBroker } from "../../src/hub/script-broker.js";
import { runHubAction } from "../../src/hub/hub-tools.js";

const ok = (body: Record<string, unknown>): ActionResult => ({ isError: false, content: [{ type: "text", text: JSON.stringify({ success: true, ...body }) }] });
const fail = (error: string): ActionResult => ({ isError: false, content: [{ type: "text", text: JSON.stringify({ success: false, error }) }] });

const WALK = "/Game/Clips/CL_Walk";
const JUMP = "/Game/Clips/CL_Jump";
const DOOR = "/Game/Tests/DoorOpens";
const clips: Record<string, Record<string, unknown>> = {
  [WALK]: { rate: 60, length: 120, map: "/Game/Maps/Arena", tracks: [{ action: "/Game/Input/IA_Move.IA_Move", type: "axis2d", keys: [{ id: "k1", t: 0, v: [0, 1], interp: "constant" }] }], events: [], checks: [{ id: "c1", start: 0, length: 100, mode: "by_end", expect: { value: "pos_x", op: "increasing", target: 100 } }], regions: [] },
  [JUMP]: { rate: 60, length: 60, tracks: [{ action: "/Game/Input/IA_Jump.IA_Jump", type: "bool", notes: [{ id: "n1", start: 0, length: 6 }] }], events: [], checks: [{ id: "c1", start: 0, length: 60, mode: "by_end", expect: { value: "pos_z", op: "increasing", target: 50 } }], regions: [] },
};

let root: string;
let calls: Array<{ action: string; args: Record<string, unknown> }>;
let clipRuns: Map<string, Record<string, unknown>>;
let scriptRuns: Map<string, Record<string, unknown>>;
let pie: boolean;
let hub: Hub;
let k: number;

const host = () => ({
  projectDir: path.join(root, "project"),
  dataDir: path.join(root, "data"),
  callAction: async (_category: string, action: string, args: Record<string, unknown> = {}) => {
    calls.push({ action, args });
    switch (action) {
      case "script_list":
        return ok({ scripts: [{ path: DOOR, name: "DoorOpens" }], count: 1 });
      case "script_read":
        return args.path === DOOR ? ok({ path: DOOR, name: "DoorOpens", steps: [{ instruction: "Open it", judge: "auto" }] }) : fail("no script");
      case "clip_list":
        return ok({ clips: Object.keys(clips).map((p) => ({ path: p, name: p.split("/").pop() })) });
      case "clip_read":
        return clips[String(args.path)] ? ok({ clip: clips[String(args.path)] }) : fail("no clip");
      case "clip_run": {
        const id = `clip_${k++}`;
        const started = args.start_pie === true && !pie;
        pie = true;
        const checks = ((args.clip as { checks: Array<{ id: string }> }).checks ?? []).map((c) => ({ id: c.id, status: "pending" }));
        clipRuns.set(id, { run_id: id, state: "running", position: 0, checks });
        return ok({ run_id: id, pie_started: started });
      }
      case "clip_status":
        return ok(clipRuns.get(String(args.run_id)) ?? { state: "idle" });
      case "clip_stop":
        clipRuns.set(String(args.run_id), { ...clipRuns.get(String(args.run_id)), state: "stopped" });
        return ok({});
      case "script_run": {
        const id = `script_${k++}`;
        const started = args.start_pie === true && !pie;
        pie = true;
        scriptRuns.set(id, { run_id: id, script: DOOR, script_name: "DoorOpens", state: "running", steps: [{ index: 0, instruction: "Open it", judge: "auto", status: "running" }], awaiting: [] });
        return ok({ run_id: id, pie_started: started });
      }
      case "script_status":
      case "script_results":
        return ok(scriptRuns.get(String(args.run_id)) ?? { state: "idle", steps: [] });
      case "duplicate":
        return ok({ path: `${String(args.destinationPath)}.${String(args.destinationPath).split("/").pop()}` });
      case "play_in_editor":
        if (args.pieAction === "stop") pie = false;
        return ok({});
      default:
        return ok({});
    }
  },
  editors: () => [{ connected: true }],
  events: { publish: () => undefined, subscribe: () => () => undefined },
  log: { info() {}, warn() {} },
});

function setup(): Hub {
  const h = host();
  return new Hub({ host: h, broker: new ScriptRunBroker(h), index: new ScriptIndex(h), timers: false, workspace: async () => ({ vcs: "none", revision: null, dirty: 0 }), editor: { pie: () => pie, dialogs: () => 0, busy: () => ({ now: Date.now(), calls: [] }), inputAgeMs: () => null }, pieWaits: { startMs: 50, inUseMs: 50 } });
}

async function call(method: string, url: string, body?: unknown): Promise<{ status: number; body: any }> {
  const [p, qs] = url.split("?");
  const route = `/v1${p}`;
  const hit = findRoute(method, route);
  if (!hit) throw new Error(`no route ${method} ${url}`);
  const handler = (hubRoutes(() => hub, path.join(root, "project")) as unknown as Record<string, RouteHandler>)[hit.name];
  const request = { method, path: route, url: new URL(`http://127.0.0.1${route}?${qs ?? ""}`), params: hit.params, query: new URLSearchParams(qs ?? ""), body: async () => body, req: { headers: {} }, res: {} } as unknown as RouteRequest;
  try {
    const r = (await handler(request)) as { status: number; body?: unknown };
    return { status: r.status, body: r.body };
  } catch (e) {
    if (e instanceof StatusError) return { status: e.status, body: { error: e.message } };
    throw e;
  }
}

const settle = () => new Promise((r) => setTimeout(r, 10));

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "hub-set-"));
  fs.mkdirSync(path.join(root, "project"), { recursive: true });
  calls = [];
  clipRuns = new Map();
  scriptRuns = new Map();
  pie = false;
  k = 0;
});

afterEach(() => {
  hub?.dispose();
  fs.rmSync(root, { recursive: true, force: true });
});

describe("the hub's set", () => {
  it("lays goals out as tracks once, and milestones play their goals", async () => {
    const files = new GoalFiles(path.join(root, "data"), () => new Date().toISOString());
    files.write([
      { type: "goal", id: "door-aaaa", data: { schema: 1, id: "door-aaaa", kind: "goal", title: "Door", acceptance: { stableRuns: 1, signOff: false }, checks: [{ kind: "script", target: DOOR }] } },
      { type: "goal", id: "ms-bbbb", data: { schema: 1, id: "ms-bbbb", kind: "milestone", title: "Alpha", acceptance: { stableRuns: 1, signOff: false }, checks: [], requires: [{ goal: "door-aaaa" }] } },
    ]);
    hub = setup();
    const door = hub.goals.get("door-aaaa")!;
    expect(door.arrangement?.instances).toEqual([{ id: "i1", kind: "script", target: DOOR, start: 0, length: 600 }]);
    expect(door.checks).toEqual([{ kind: "script", target: DOOR }]);
    const set = (await call("GET", "/hub/set")).body;
    expect(set.scenes[0]).toMatchObject({ id: "ms-bbbb", tracks: ["door-aaaa"] });
    expect(set.text).toContain("scene ms-bbbb Alpha tracks=door-aaaa");
    // Once: an emptied track stays empty.
    hub.goals.setArrangement("door-aaaa", { rate: 60, instances: [] });
    hub.dispose();
    hub = setup();
    expect(hub.goals.get("door-aaaa")!.arrangement?.instances).toEqual([]);
  });

  it("edits the set with op lines and answers a bad name with candidates", async () => {
    hub = setup();
    const r = await call("POST", "/hub/set/edit", { ops: `track add Walker color=green
track add Guard player=1
add Walker clip ${WALK}
add Walker clip ${JUMP} at 200
duplicate Walker/i2
track mute Guard
scene add "Boss fight"
scene track "Boss fight" add Walker
scene track "Boss fight" add Guard` });
    expect(r.status).toBe(200);
    const walker = r.body.tracks.find((t: { title: string }) => t.title === "Walker");
    expect(walker.layout.map((l: { id: string; start: number; end: number }) => [l.id, l.start, l.end])).toEqual([["i1", 0, 120], ["i2", 200, 260], ["i3", 260, 320]]);
    expect(r.body.scenes[0].tracks).toHaveLength(2);
    expect(hub.goals.get(walker.id)!.kind).toBe("goal");
    const bad = await call("POST", "/hub/set/edit", { ops: "track rename Walkr Runner" });
    expect(bad.status).toBe(400);
    expect(bad.body.finding.candidates).toContain("Walker");
    const unknown = await call("POST", "/hub/set/edit", { ops: `add Walker clip /Game/Clips/CL_Jmp` });
    expect(unknown.body.finding.candidates[0]).toBe(JUMP);
  });

  it("deletes tracks and scenes through ops, refusing one a queued or running run was made for", async () => {
    hub = setup();
    const e = await call("POST", "/hub/set/edit", { ops: `track add A\ntrack add B\nadd A clip ${WALK}\nscene add S\nscene track S add A\nscene add T` });
    const [a, b] = e.body.tracks.map((t: { id: string }) => t.id);
    expect((await call("POST", "/hub/run", { scope: { kind: "track", goalId: a } })).status).toBe(200);
    await settle();
    const refused = await call("POST", "/hub/set/edit", { ops: "track delete A" });
    expect(refused.status).toBe(400);
    expect(refused.body.finding).toMatchObject({ id: a, field: "delete", blocks: true });
    expect(refused.body.finding.message).toContain(`"A" can't be deleted while a run made for it is queued or running`);
    expect(hub.goals.get(a)).toBeTruthy();
    const r = await call("POST", "/hub/set/edit", { ops: "track delete B\nscene delete S" });
    expect(r.status).toBe(200);
    expect(r.body.tracks.map((t: { id: string }) => t.id)).toEqual([a]);
    expect(r.body.scenes.map((s: { title: string }) => s.title)).toEqual(["T"]);
    expect(hub.goals.get(b)).toBeUndefined();
    const agent = (await runHubAction(hub, "edit_set", { ops: "scene delete T" })) as { text: string };
    expect(agent.text).not.toContain("scene ");
  });

  it("makes an instance unique by copying its clip", async () => {
    hub = setup();
    await call("POST", "/hub/set/edit", { ops: `track add A\nadd A clip ${WALK}` });
    const r = await call("POST", "/hub/set/edit", { ops: "unique A/i1" });
    expect(r.status).toBe(200);
    expect(calls.find((c) => c.action === "duplicate")?.args).toMatchObject({ sourcePath: WALK, destinationPath: `${WALK}_2`, onConflict: "error" });
    expect(r.body.tracks[0].arrangement.instances[0].target).toBe(`${WALK}_2`);
  });

  it("runs a scene as one composed run, each track on its player, and judges it", async () => {
    hub = setup();
    const e = await call("POST", "/hub/set/edit", { ops: `track add A\ntrack add B player=1\ntrack add C\nadd A clip ${WALK}\nadd B clip ${JUMP} at 30\nadd C clip ${JUMP}\ntrack mute C\nscene add S\nscene track S add A\nscene track S add B\nscene track S add C` });
    const scene = e.body.scenes[0].id;
    const queued = await call("POST", "/hub/run", { scope: { kind: "scene", goalId: scene } });
    expect(queued.status).toBe(200);
    await settle();
    const run = calls.find((c) => c.action === "clip_run")!;
    const clip = run.args.clip as { tracks: Array<{ action: string; client?: number }>; checks: Array<{ id: string; start: number }> };
    // C is muted; B's lanes drive remote client 1.
    expect(clip.tracks.map((t) => [t.action.split(".").pop(), t.client ?? 0])).toEqual([["IA_Move", 0], ["IA_Jump", 1]]);
    expect(clip.checks.map((c) => [c.id, c.start])).toEqual([["1-i1.c1", 0], ["2-i1.c1", 30]]);
    const runId = hub.queue.state().current!.items[0].runId!;
    clipRuns.set("clip_0", { run_id: "clip_0", state: "finished", position: 120, checks: [{ id: "1-i1.c1", status: "passed" }, { id: "2-i1.c1", status: "passed" }] });
    await hub.arrangements.refresh(runId);
    await settle();
    const detail = (await call("GET", `/hub/runs/${runId}`)).body;
    expect(detail.record).toMatchObject({ kind: "scene", target: scene, outcome: "passed" });
    expect(detail.arrangement.instances.map((i: { key: string; status: string }) => [i.key, i.status])).toEqual([["1-i1", "passed"], ["2-i1", "passed"]]);
    expect(detail.arrangement.tracks.map((t: { player: number; status: string }) => [t.player, t.status])).toEqual([[0, "passed"], [1, "passed"]]);
    const set = (await call("GET", "/hub/set")).body;
    expect(set.scenes[0].status).toBe("met");
  });

  it("plays a step-list instance between clip segments in the same PIE session", async () => {
    hub = setup();
    await call("POST", "/hub/set/edit", { ops: `track add T\nadd T clip ${JUMP}\nadd T script ${DOOR} at 60\nadd T clip ${JUMP} at 700` });
    const track = hub.goals.setModel().tracks[0].id;
    await call("POST", "/hub/run", { scope: { kind: "track", goalId: track } });
    await settle();
    const runId = hub.queue.state().current!.items[0].runId!;
    clipRuns.set("clip_0", { run_id: "clip_0", state: "finished", position: 60, checks: [{ id: "i1.c1", status: "passed" }] });
    await hub.arrangements.refresh(runId);
    const script = calls.find((c) => c.action === "script_run")!;
    expect(script.args).toMatchObject({ script: DOOR, start_pie: true, keep_pie: true });
    scriptRuns.set("script_1", { run_id: "script_1", script: DOOR, script_name: "DoorOpens", state: "finished", steps: [{ index: 0, instruction: "Open it", judge: "auto", status: "failed" }], awaiting: [] });
    await hub.arrangements.refresh(runId);
    const second = calls.filter((c) => c.action === "clip_run")[1];
    expect((second.args.clip as { checks: Array<{ id: string; start: number }> }).checks[0]).toMatchObject({ id: "i3.c1", start: 40 });
    clipRuns.set("clip_2", { run_id: "clip_2", state: "finished", position: 100, checks: [{ id: "i3.c1", status: "passed" }] });
    await hub.arrangements.refresh(runId);
    await settle();
    const rec = hub.runs.get(runId)!;
    expect(rec.outcome).toBe("failed");
    expect(rec.arrangement!.instances.map((i) => [i.id, i.status])).toEqual([["i1", "passed"], ["i2", "failed"], ["i3", "passed"]]);
  });

  it("gives agents the set, its ops and runs in text", async () => {
    hub = setup();
    const edited = (await runHubAction(hub, "edit_set", { ops: `track add Walk\nadd Walk clip ${WALK}` })) as { text: string; changed: string[] };
    expect(edited.text).toContain(`clip i1 ${WALK} 0`);
    const queued = (await runHubAction(hub, "run", { scope: "track Walk" })) as { plan: { items: Array<{ playtest: string }> } };
    expect(queued.plan.items[0].playtest).toMatch(/^track:walk-/);
    await expect(runHubAction(hub, "edit_set", { ops: "track solo Wlk" })).rejects.toThrow(/did you mean: Walk/);
  });
});
