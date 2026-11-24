/**
 * The Playtest Hub end to end over ue-mcp's own ScriptRunBroker and
 * ScriptIndex and the daemon's real hub routes, with the editor's actions
 * faked: the overview and missing checks, goal routes and their errors, a plan
 * that holds at manual steps for their verdicts, unattended plans, Write
 * playtests linking what a client's flow drafts, fix loops through a client's
 * fixer, the run screen's evidence, moved scripts, the goal store's export and
 * import, and hub.changed.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { ActionResult } from "../../src/contract/daemon.js";
import { DAEMON_HEADERS, DAEMON_ROUTES } from "../../src/contract/daemon.js";
import { HUB_ROUTES, qaDraftFromScript, qaRunFromAnswer, type CurrentProvenance, type HubOverview, type PlanRun, type QueueState } from "../../src/hub/contract.js";
import { StatusError } from "../../src/core/status-error.js";
import { hubRoutes } from "../../src/daemon/hub-routes.js";
import { findRoute, type RouteHandler, type RouteRequest } from "../../src/daemon/router.js";
import { Hub } from "../../src/hub/hub.js";
import { sample, seriesOf } from "../../src/hub/evidence.js";
import type { HubFixer, HubWritePlaytests } from "../../src/hub/ports.js";
import type { BusyCall } from "../../src/contract/hub.js";
import { ScriptIndex } from "../../src/hub/script-index.js";
import { nextRunAt } from "../../src/hub/schedules.js";
import { ScriptRunBroker } from "../../src/hub/script-broker.js";
import { VerdictHold } from "../../src/hub/verdict-hold.js";

const ok = (body: Record<string, unknown>): ActionResult => ({ isError: false, content: [{ type: "text", text: JSON.stringify({ success: true, ...body }) }] });
const fail = (error: string): ActionResult => ({ isError: false, content: [{ type: "text", text: JSON.stringify({ success: false, error }) }] });

const DOOR = "/Game/Tests/DoorOpens";
const JUMP = "/Game/Tests/JumpCheck";
const STEPS = [{ instruction: "Walk to the door", judge: "auto", expect: { value: "Door.Distance", op: "decreasing" } }, { instruction: "It looks right", judge: "manual" }];

let root: string;
let connected: boolean;
let listed: string[];
let calls: Array<{ action: string; args: Record<string, unknown> }>;
let published: Array<{ type: string; data?: Record<string, unknown> }>;
let listeners: Array<(e: { type: string; data: Record<string, unknown> }) => void>;
/** Each run's status as the fake editor reports it. */
let status: Map<string, Record<string, unknown>>;
/** The run the fake editor holds: verdicts for any other are refused. */
let held: string | null;
let k: number;
let hub: Hub;
let broker: ScriptRunBroker;
let hold: VerdictHold;
/** The project's source control state, as the fake workspace reports it. */
let workspace: CurrentProvenance;
/** Play In Editor runs in the fake editor, whoever started it. */
let pie: boolean;
/** Modal dialogs open in the fake editor. */
let dialogs: number;
/** script_run's PIE start is still queued when it answers: PIE comes up only when a test says so. */
let pieLate: boolean;
/** script_run waits for this before it answers. */
let gate: Promise<void> | null;

const steps = (statuses: string[]) => statuses.map((s, index) => ({ index, instruction: STEPS[index].instruction, judge: STEPS[index].judge, status: s }));

const host = () => ({
  projectDir: path.join(root, "project"),
  dataDir: path.join(root, "data"),
  callAction: async (_category: string, action: string, args: Record<string, unknown> = {}) => {
    calls.push({ action, args });
    switch (action) {
      case "script_list":
        return ok({ scripts: listed.map((p) => ({ path: p, name: p.split("/").pop(), map: "/Game/Maps/Test" })), count: listed.length });
      case "script_read":
        return listed.includes(String(args.path)) ? ok({ path: args.path, name: String(args.path).split("/").pop(), steps: STEPS }) : fail(`no script at ${String(args.path)}`);
      case "script_run": {
        if (gate) await gate;
        const runId = `script_${String(args.script).split("/").pop()}_${k++}`;
        // As ue-mcp does: start_pie starts PIE only when none runs, and says whether it did. A late PIE is still queued.
        const started = args.start_pie === true && !pie;
        if (started && !pieLate) pie = true;
        const s = { run_id: runId, script: args.script, script_name: String(args.script).split("/").pop(), state: "armed", mode: "full", steps: steps(["pending", "pending"]), awaiting: [], last_seq: 1, pie_started: started };
        status.set(runId, s);
        held = runId;
        return ok(s);
      }
      case "script_replay": {
        // As ue-mcp does: a replay always starts PIE.
        const runId = `replay_${k++}`;
        pie = true;
        status.set(runId, { run_id: runId, script: DOOR, script_name: "DoorOpens", state: "armed", steps: steps(["pending", "pending"]), awaiting: [], last_seq: 1 });
        return ok({ run_id: runId, replay_of: args.run_id, pie_started: true, auto_stop_pie: true });
      }
      case "play_in_editor":
        if (args.pieAction === "stop") pie = false;
        return ok({});
      case "script_status":
      case "script_results":
        return ok(status.get(String(args.run_id)) ?? { run_id: args.run_id, state: "idle", steps: [] });
      case "script_stop": {
        const s = status.get(String(args.run_id));
        if (s) status.set(String(args.run_id), { ...s, state: "stopped", ...(args.reason ? { stop_reason: args.reason } : {}) });
        return ok({ stopped: true });
      }
      case "verdict": {
        // As the bridge words it: a verdict is taken only for the run the editor holds, ended or not.
        if (args.run_id !== held) return fail(`Run ${String(args.run_id)} is not the current run (${held})`);
        const s = status.get(String(args.run_id))!;
        return ok({ run_id: args.run_id, step: args.step, status: args.verdict === "pass" ? "passed" : "failed", verdict: args.verdict, verdict_source: "action", state: s.state, awaiting: [] });
      }
      case "script_runs":
        return ok({ runs: [] });
      case "fixup_redirectors": {
        // A dry run reports the redirector at the path and where it points; OldDoor moved to DoorOpens.
        const from = (args.paths as string[])[0];
        return ok({ dryRun: true, redirectors: from === "/Game/Tests/OldDoor" ? [{ packageName: from, destinationPath: `${DOOR}.DoorOpens` }] : [] });
      }
      default:
        return ok({});
    }
  },
  editors: () => [{ connected }],
  events: {
    publish: (type: string, data?: Record<string, unknown>) => void published.push({ type, data }),
    subscribe: (l: (e: { type: string; data: Record<string, unknown> }) => void) => {
      listeners.push(l);
      return () => void (listeners = listeners.filter((x) => x !== l));
    },
  },
  log: { info() {}, warn() {} },
});

/** Call a hub route as the daemon would: the real route table and the real handlers, a thrown StatusError answered as { error }. */
async function call(method: string, url: string, body?: unknown, headers: Record<string, string> = {}): Promise<{ status: number; body: any }> {
  const [p, qs] = url.split("?");
  const route = `/v1${p}`;
  const hit = findRoute(method, route);
  if (!hit) throw new Error(`no route ${method} ${url}`);
  const handler = (hubRoutes(() => hub, path.join(root, "project")) as unknown as Record<string, RouteHandler>)[hit.name];
  const request = {
    method,
    path: route,
    url: new URL(`http://127.0.0.1${route}?${qs ?? ""}`),
    params: hit.params,
    query: new URLSearchParams(qs ?? ""),
    body: async () => body,
    req: { headers },
    res: {},
  } as unknown as RouteRequest;
  try {
    const r = (await handler(request)) as { status: number; body?: unknown };
    return { status: r.status, body: r.body };
  } catch (e) {
    if (e instanceof StatusError) return { status: e.status, body: { error: e.message } };
    throw e;
  }
}

const settle = () => new Promise((r) => setTimeout(r, 0));
const enc = encodeURIComponent;

function setup(o: { writePlaytests?: HubWritePlaytests; fixer?: HubFixer; enabled?: boolean | (() => boolean); signer?: () => string | null; busy?: () => BusyCall[] } = {}) {
  const h = host();
  broker = new ScriptRunBroker(h);
  // As the daemon wires it (src/hub/daemon-hub.ts): a hold on the runs the broker started.
  hold = new VerdictHold({
    callAction: h.callAction,
    publish: (type, data) => void published.push({ type, data }),
    log: h.log,
    stop: (runId, reason) => broker.stopRun(runId, reason),
    waitMinutes: (run) => broker.waitMinutesOf(run) ?? 10,
    owns: (run) => broker.owns(run),
  });
  broker.attachHold(hold);
  hub = new Hub({
    host: { ...h, projectDir: h.projectDir, dataDir: h.dataDir },
    broker,
    index: new ScriptIndex(h),
    signer: o.signer ?? (() => "octocat"),
    playtestRuns: () => "manual",
    enabled: () => (typeof o.enabled === "function" ? o.enabled() : o.enabled !== false),
    ...(o.writePlaytests ? { writePlaytests: o.writePlaytests } : {}),
    ...(o.fixer ? { fixer: o.fixer } : {}),
    timers: false,
    engine: () => "5.5",
    workspace: async () => workspace,
    editor: { pie: () => pie, dialogs: () => dialogs, busy: () => ({ now: Date.now(), calls: o.busy?.() ?? [] }), inputAgeMs: () => null },
    pieWaits: { startMs: 200, inUseMs: 200 },
  });
  return h;
}

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "hub-"));
  fs.mkdirSync(path.join(root, "project"), { recursive: true });
  connected = true;
  listed = [DOOR, JUMP];
  calls = [];
  published = [];
  listeners = [];
  status = new Map();
  held = null;
  k = 0;
  workspace = { vcs: "git", revision: "abc1234def", dirty: 0 };
  pie = false;
  dialogs = 0;
  pieLate = false;
  gate = null;
});
afterEach(() => {
  hub?.dispose();
  hold?.dispose();
  fs.rmSync(root, { recursive: true, force: true });
});

describe("routes", () => {
  it("serves every route HUB_ROUTES names, under /v1, as a hub.* entry of DAEMON_ROUTES with a handler", () => {
    const handlers = hubRoutes(() => hub, path.join(root, "project"));
    for (const spec of Object.values(HUB_ROUTES)) {
      const [method, p] = spec.split(" ");
      // A path parameter stands for any one segment; the query is not part of the path.
      const hit = findRoute(method, `/v1${p.split("?")[0].replace(/:[A-Za-z]+/g, "x")}`);
      expect(hit?.name.startsWith("hub."), spec).toBe(true);
      expect(Object.keys(handlers), spec).toContain(hit!.name);
    }
    const hubNames = Object.keys(DAEMON_ROUTES).filter((n) => n.startsWith("hub."));
    expect(Object.keys(handlers).sort()).toEqual(hubNames.sort());
  });

  it("goals: create, read, update, checks, sign-off, delete, with 400 and 404", async () => {
    setup();
    expect((await call("POST", "/hub/goals", { title: "" })).status).toBe(400);
    const created = await call("POST", "/hub/goals", { title: "Door opens", checks: [{ kind: "script", target: DOOR }] });
    expect(created.status).toBe(200);
    const id = created.body.goal.id;
    // Kept in the goal store, never in the project.
    expect(fs.existsSync(path.join(root, "project", "QA"))).toBe(false);
    expect((await call("GET", `/hub/goals/${id}`)).body).toMatchObject({ status: "not_started", progress: { met: 0, total: 1 } });
    expect((await call("GET", "/hub/goals/nope")).status).toBe(404);
    expect((await call("PUT", `/hub/goals/${id}`, { priority: "p9" })).status).toBe(400);
    expect((await call("POST", `/hub/goals/${id}/checks`, { checks: [{ kind: "script", target: JUMP }] })).body.goal.checks).toHaveLength(2);
    expect((await call("DELETE", `/hub/goals/${id}/checks/${enc(`script:${JUMP}`)}`)).body.goal.checks).toHaveLength(1);
    expect((await call("DELETE", `/hub/goals/${id}`)).status).toBe(204);
    expect((await call("DELETE", `/hub/goals/${id}`)).status).toBe(404);
  });

  it("required goals: add, reorder, remove, a loop is 400 with its path, and the overview shows parents and unused", async () => {
    setup();
    const mk = async (title: string, extra: Record<string, unknown> = {}) => (await call("POST", "/hub/goals", { title, ...extra })).body.goal.id as string;
    const alpha = await mk("Alpha", { kind: "milestone" });
    const beta = await mk("Beta", { kind: "milestone" });
    const door = await mk("Door", { checks: [{ kind: "script", target: DOOR }] });
    const jump = await mk("Jump", { checks: [{ kind: "script", target: JUMP }, { kind: "script", target: DOOR }] });
    const loose = await mk("Loose");
    expect((await call("POST", `/hub/goals/${alpha}/requires`, { goal: door })).status).toBe(200);
    await call("POST", `/hub/goals/${alpha}/requires`, { goal: jump });
    await call("POST", `/hub/goals/${beta}/requires`, { goal: door });
    expect((await call("PUT", `/hub/goals/${alpha}/requires`, { order: [jump, door] })).body.goal.requires).toEqual([{ goal: jump }, { goal: door }]);
    const loop = await call("POST", `/hub/goals/${door}/requires`, { goal: alpha });
    expect(loop.status).toBe(400);
    expect(loop.body.error).toContain(`${door} > ${alpha} > ${door}`);
    expect((await call("PUT", `/hub/goals/${jump}/checks/order`, { order: [`script:${DOOR}`, `script:${JUMP}`] })).body.goal.checks[0].target).toBe(DOOR);
    const o = (await call("GET", "/hub")).body as HubOverview;
    const g = (id: string) => o.goals.find((x) => x.goal.id === id)!;
    expect(g(door).parents).toEqual([alpha, beta]);
    expect(g(alpha).requires).toEqual([{ goal: jump, required: true, status: "not_started" }, { goal: door, required: true, status: "not_started" }]);
    expect(g(loose).unused).toBe(true);
    expect(g(door).unused).toBeUndefined();
    expect(g(alpha).unused).toBeUndefined();
    // Running a milestone runs each check reachable through its goals once, in order.
    const plan = (await call("POST", "/hub/plans", { scope: { kind: "goal", goalId: alpha } })).body.plan as PlanRun;
    expect(plan.items.map((i) => i.check.target)).toEqual([DOOR, JUMP]);
    expect((await call("DELETE", `/hub/goals/${alpha}/requires/${jump}`)).body.goal.requires).toEqual([{ goal: door }]);
    expect((await call("DELETE", `/hub/goals/${alpha}/requires/${jump}`)).status).toBe(404);
  });

  it("deleting a goal unlinks it from the goals that required it, leaves its playtests, their records and runs, and is 409 while a plan for it is queued or running", async () => {
    setup();
    const mk = async (title: string, extra: Record<string, unknown> = {}) => (await call("POST", "/hub/goals", { title, ...extra })).body.goal.id as string;
    const doors = await mk("Doors", { checks: [{ kind: "script", target: DOOR }] });
    const jump = await mk("Jump", { checks: [{ kind: "script", target: DOOR }, { kind: "script", target: JUMP }] });
    const alpha = await mk("Alpha", { kind: "milestone", requires: [{ goal: doors }, { goal: jump }] });
    await call("PUT", `/hub/checks/${enc(`script:${DOOR}`)}`, { title: "Door opens", links: [{ url: "https://tracker.example/1" }] });

    // A plan made for the goal is running: refused, naming the plan and how to go on.
    const planId = (await call("POST", "/hub/plans", { scope: { kind: "goal", goalId: doors } })).body.plan.id as string;
    await settle();
    await settle();
    const refused = await call("DELETE", `/hub/goals/${doors}`);
    expect(refused.status).toBe(409);
    expect(refused.body.error).toBe(`"Doors" can't be deleted while a run made for it is queued or running: "Run Doors". Cancel that run or let it finish, then try again.`);
    expect((await call("GET", `/hub/goals/${doors}`)).status).toBe(200);

    // The plan's run finishes; the goal deletes.
    const run0 = ((await call("GET", "/hub/queue")).body as QueueState).current!.items[0].runId!;
    status.set(run0, { ...status.get(run0)!, state: "finished", steps: steps(["passed", "passed"]), awaiting: [] });
    await hub.tick();
    await settle();
    expect(((await call("GET", "/hub/queue")).body as QueueState).recent[0]).toMatchObject({ id: planId, state: "done" });
    expect((await call("DELETE", `/hub/goals/${doors}`)).status).toBe(204);

    const o = (await call("GET", "/hub")).body as HubOverview;
    expect(o.goals.map((g) => g.goal.id).sort()).toEqual([alpha, jump].sort());
    expect(o.goals.find((g) => g.goal.id === alpha)!.goal.requires).toEqual([{ goal: jump }]);
    expect(o.goals.find((g) => g.goal.id === jump)!.goal.checks.map((c) => c.target)).toEqual([DOOR, JUMP]);
    expect(o.warnings.some((w) => w.includes(doors))).toBe(false);
    // The playtest keeps its own record and its run, which stays readable.
    const door = (await call("GET", `/hub/checks/${enc(`script:${DOOR}`)}`)).body;
    expect(door.state).toMatchObject({ goalIds: [jump], name: "Door opens", links: [{ url: "https://tracker.example/1" }] });
    expect(door.runs.map((r: { runId: string }) => r.runId)).toEqual([run0]);
    expect((await call("GET", `/hub/runs/${run0}`)).status).toBe(200);

    // A milestone deletes the same way, and the goals it required stay.
    expect((await call("DELETE", `/hub/goals/${alpha}`)).status).toBe(204);
    expect(((await call("GET", "/hub")).body as HubOverview).goals.map((g) => g.goal.id)).toEqual([jump]);

    // A queued plan refuses it too, until it is cancelled.
    await call("PUT", "/hub/queue/paused", { paused: true });
    const queued = (await call("POST", "/hub/plans", { scope: { kind: "goal", goalId: jump } })).body.plan.id as string;
    expect((await call("DELETE", `/hub/goals/${jump}`)).status).toBe(409);
    expect((await call("POST", `/hub/plans/${queued}/cancel`)).status).toBe(200);
    expect((await call("DELETE", `/hub/goals/${jump}`)).status).toBe(204);
    expect(((await call("GET", "/hub")).body as HubOverview).goals).toEqual([]);
    expect(((await call("GET", "/hub")).body as HubOverview).checks.map((c) => c.id).sort()).toEqual([`script:${DOOR}`, `script:${JUMP}`].sort());
  });

  it("bad plan, rule and query bodies are 400; unknown ids 404", async () => {
    setup();
    expect((await call("POST", "/hub/plans", { scope: { kind: "nope" } })).status).toBe(400);
    expect((await call("POST", "/hub/plans", { scope: { kind: "all" }, options: { attended: "yes" } })).status).toBe(400);
    expect((await call("POST", "/hub/plans", { scope: { kind: "all" }, options: { verdictWaitMinutes: 7 } })).status).toBe(400);
    expect((await call("POST", "/hub/plans", { scope: { kind: "goal", goalId: "missing" } })).status).toBe(404);
    expect((await call("PUT", "/hub/rules", { flakyWindow: 0 })).status).toBe(400);
    expect((await call("POST", "/hub/fix", { checkId: "nope" })).status).toBe(400);
    expect((await call("PUT", "/hub/queue/paused", { paused: "yes" })).status).toBe(400);
    expect((await call("POST", "/hub/plans/nope/cancel")).status).toBe(404);
    expect((await call("POST", "/hub/plans/nope/move", { to: 0 })).status).toBe(404);
    expect((await call("POST", "/hub/plans/nope/move", { to: -1 })).status).toBe(400);
    expect((await call("POST", "/hub/plans", { scope: { kind: "all" }, next: "yes" })).status).toBe(400);
    expect((await call("POST", "/hub/schedules", { title: "N", scope: { kind: "all" }, at: "25:00", days: [1] })).status).toBe(400);
    expect((await call("POST", "/hub/schedules", { title: "N", scope: { kind: "all" }, at: "02:00", days: [] })).status).toBe(400);
    expect((await call("PUT", "/hub/schedules/nope", { enabled: false })).status).toBe(404);
    expect((await call("DELETE", "/hub/schedules/nope")).status).toBe(404);
    expect((await call("GET", "/hub/runs/nope")).status).toBe(404);
    expect((await call("GET", "/hub/runs?check=bad")).status).toBe(400);
    expect((await call("GET", `/hub/checks/${enc("script:/Game/Nothing")}`)).status).toBe(404);
  });
});

describe("overview", () => {
  it("checks are the goals' and the editor's scripts; one not on this checkout is missing, only while the editor is connected, and fails no goal", async () => {
    setup();
    const gone = (await call("POST", "/hub/goals", { title: "Gone", checks: [{ kind: "script", target: "/Game/Tests/Deleted" }] })).body.goal.id;
    const o = (await call("GET", "/hub")).body as HubOverview;
    expect(o.checks.map((c) => c.id).sort()).toEqual([`script:${DOOR}`, `script:${JUMP}`, "script:/Game/Tests/Deleted"].sort());
    expect(o.checks.find((c) => c.id === "script:/Game/Tests/Deleted")).toMatchObject({ health: "missing", missing: true, goalIds: [gone] });
    expect(o.checks.find((c) => c.id === `script:${DOOR}`)).toMatchObject({ health: "never_run", goalIds: [], map: "/Game/Maps/Test", name: "DoorOpens" });
    expect(o.goals[0].status).toBe("not_started");
    expect(o.attention).toEqual([]);
    expect(o.rules).toEqual({ staleAfterDays: null, flakyWindow: 3 });

    // Offline nothing is flagged missing.
    connected = false;
    setup();
    const off = (await call("GET", "/hub")).body as HubOverview;
    expect(off.checks.find((c) => c.id === "script:/Game/Tests/Deleted")?.health).toBe("never_run");
    expect(off.editor.connected).toBe(false);
  });

  it("a moved script is followed through its redirector: the goal keeps its path, and history follows", async () => {
    setup();
    const OLD = "/Game/Tests/OldDoor";
    const id = (await call("POST", "/hub/goals", { title: "Door", checks: [{ kind: "script", target: OLD }] })).body.goal.id;
    hub.runs.record(qaRunFromAnswer({ run_id: "before", script: OLD, script_name: "OldDoor", state: "finished", steps: [{ index: 0, instruction: "x", judge: "auto", status: "passed" }] }, { seenAt: "2026-11-01T00:00:00.000Z" })!);
    await call("GET", "/hub");
    // The overview asked ue-mcp about the path, read only.
    expect(calls.find((a) => a.action === "fixup_redirectors")?.args).toEqual({ paths: [OLD], dryRun: true });
    await settle();
    const o = (await call("GET", "/hub")).body as HubOverview;
    const c = o.goals.find((g) => g.goal.id === id)!.checks[0];
    expect(c).toMatchObject({ id: `script:${DOOR}`, renamedFrom: OLD });
    expect(c.missing).toBeUndefined();
    expect(c.last?.runId).toBe("before");
    expect(o.goals.find((g) => g.goal.id === id)!.goal.checks).toEqual([{ kind: "script", target: OLD }]);
    // A change made through the id a client shows reaches the goal's own entry.
    expect((await call("PUT", `/hub/goals/${id}/checks/${enc(`script:${DOOR}`)}`, { required: false })).body.goal.checks).toEqual([{ kind: "script", target: OLD, required: false }]);
  });

  it("imports QA/Goals into an empty store once, without touching the files", async () => {
    fs.mkdirSync(path.join(root, "project", "QA", "Goals"), { recursive: true });
    const file = path.join(root, "project", "QA", "Goals", "door.json");
    fs.writeFileSync(file, JSON.stringify({ title: "Door", checks: [{ kind: "script", target: DOOR }] }));
    fs.writeFileSync(path.join(root, "project", "QA", "Goals", "bad.json"), "{");
    setup();
    const o = (await call("GET", "/hub")).body as HubOverview;
    expect(o.goals.map((g) => g.goal.id)).toEqual(["door"]);
    expect(o.imported).toMatchObject({ from: "QA/Goals", goals: ["door"] });
    expect(o.imported?.problems[0]).toContain("bad.json is not valid JSON");
    expect(fs.readFileSync(file, "utf-8")).toBe(JSON.stringify({ title: "Door", checks: [{ kind: "script", target: DOOR }] }));
  });

  it("exports the goal store as one file and imports it, merged by id", async () => {
    setup();
    await call("POST", "/hub/goals", { id: "door", title: "Door" });
    await call("PUT", `/hub/checks/${enc(`script:${DOOR}`)}`, { title: "The door" });
    const file = (await call("GET", "/hub/store")).body;
    expect(file.records.map((r: { type: string }) => r.type).sort()).toEqual(["check", "goal"]);
    expect((await call("POST", "/hub/store", file)).body).toEqual({ added: 0, unchanged: 2, conflicts: [] });
    expect((await call("POST", "/hub/store", { format: "nope" })).status).toBe(400);
    const o = (await call("GET", "/hub")).body as HubOverview;
    expect(o.checks.find((c) => c.id === `script:${DOOR}`)?.name).toBe("The door");
  });
});

describe("sign-off", () => {
  /** A finished run of the door check, with what it tested. */
  const doorRun = (runId: string, at: string, passed: boolean, tested?: { revision: string; scriptHash: string }) => {
    hub.runs.record(qaRunFromAnswer({ run_id: runId, script: DOOR, script_name: "DoorOpens", state: "finished", steps: [{ index: 0, instruction: "x", judge: "auto", status: passed ? "passed" : "failed" }] }, { seenAt: at })!, { startedAt: at, endedAt: at });
    if (tested) hub.runs.setProvenance(runId, { vcs: "git", revision: tested.revision, dirty: 0, scriptHash: tested.scriptHash, engine: "5.5", players: null });
  };
  const goalState = async (id: string) => (await call("GET", `/hub/goals/${id}`)).body;
  /** Minutes before now, recent enough that no pass is stale. */
  const ago = (min: number) => new Date(Date.now() - min * 60_000).toISOString();

  it("is refused without a signed-in account, and never recorded as a placeholder", async () => {
    setup({ signer: () => null });
    const id = (await call("POST", "/hub/goals", { title: "Door", acceptance: { signOff: true }, checks: [{ kind: "script", target: DOOR }] })).body.goal.id;
    const r = await call("POST", `/hub/goals/${id}/sign-off`, {});
    expect(r.status).toBe(409);
    expect(r.body.error).toBe("Sign in to your ue-mcp account to sign off a goal: a sign-off records who gave it.");
    expect((await goalState(id)).signOff).toBeUndefined();
  });

  it("records the account, the device a client says it came through, the revision and the runs, as a record in the goal store", async () => {
    setup();
    const id = (await call("POST", "/hub/goals", { title: "Door", acceptance: { signOff: true }, checks: [{ kind: "script", target: DOOR }] })).body.goal.id;
    doorRun("r1", ago(30), true, { revision: "abc1234def", scriptHash: "sha256:1" });
    expect((await goalState(id)).status).toBe("ready_to_sign_off");
    const r = await call("POST", `/hub/goals/${id}/sign-off`, { note: "Played it" }, { [DAEMON_HEADERS.via]: encodeURIComponent("Ama's phone") });
    expect(r.body.signOff).toMatchObject({ goalId: id, by: "octocat", via: "Ama's phone", note: "Played it", revision: "abc1234def", runIds: ["r1"] });
    expect(fs.readdirSync(path.join(root, "data", "hub", "signoffs"))).toHaveLength(1);
    expect(fs.existsSync(path.join(root, "project", "QA"))).toBe(false);
    const g = await goalState(id);
    expect(g.status).toBe("met");
    expect(g.signOff).toMatchObject({ record: { by: "octocat" }, lapsed: null });
    // Without the header there is no device to name.
    expect((await call("POST", `/hub/goals/${id}/sign-off`, {})).body.signOff.via).toBeUndefined();
    expect((await call("DELETE", `/hub/goals/${id}/sign-off`)).status).toBe(204);
    expect((await goalState(id)).signOff).toBeUndefined();
  });

  it("lapses when the requirements change, the revision moves past what it judged, or a required check fails after it", async () => {
    setup();
    const id = (await call("POST", "/hub/goals", { title: "Door", acceptance: { signOff: true }, checks: [{ kind: "script", target: DOOR }] })).body.goal.id;
    doorRun("r1", ago(30), true, { revision: "abc1234def", scriptHash: "sha256:1" });
    await call("POST", `/hub/goals/${id}/sign-off`, {});
    await call("PUT", `/hub/goals/${id}`, { acceptance: { stableRuns: 2 } });
    expect((await goalState(id)).signOff.lapsed).toBe("requirements");
    await call("PUT", `/hub/goals/${id}`, { acceptance: { stableRuns: 1 } });
    expect((await goalState(id)).signOff.lapsed).toBeNull();

    // A pull: the sign-off is kept on this computer, and lapses until the check passes on the new revision with the same script.
    hub.dispose();
    workspace = { vcs: "git", revision: "fff0000", dirty: 0 };
    setup();
    let g = await goalState(id);
    expect(g.signOff.lapsed).toBe("revision");
    expect(g.status).not.toBe("met");
    doorRun("r2", ago(20), true, { revision: "fff0000", scriptHash: "sha256:1" });
    g = await goalState(id);
    expect(g.signOff.lapsed).toBeNull();
    expect(g.status).toBe("met");
    doorRun("r3", ago(10), true, { revision: "fff0000", scriptHash: "sha256:2" });
    expect((await goalState(id)).signOff.lapsed).toBe("revision");

    doorRun("r4", "2099-01-01T00:00:00.000Z", false, { revision: "fff0000", scriptHash: "sha256:2" });
    g = await goalState(id);
    expect(g.signOff.lapsed).toBe("failed");
    expect(g.status).toBe("failing");
  });
});

describe("plans", () => {
  it("runs a goal; the plan holds at its manual step with PIE paused until the verdict, then moves on", async () => {
    setup();
    const g = (await call("POST", "/hub/goals", { title: "Doors", checks: [{ kind: "script", target: DOOR }, { kind: "script", target: JUMP }] })).body.goal;
    const planned = await call("POST", "/hub/plans", { scope: { kind: "goal", goalId: g.id } });
    expect(planned.body.plan).toMatchObject({ title: "Run Doors", trigger: "person", options: { attended: true }, items: [{ name: "DoorOpens" }, { name: "JumpCheck" }] });
    await settle();
    await settle();

    // The run holds at manual steps (ue-mcp's default).
    const start = calls.find((c) => c.action === "script_run")!;
    expect(start.args).toMatchObject({ script: DOOR, start_pie: true });
    expect(start.args.block_on_manual).toBeUndefined();
    const plan = () => ((call("GET", "/hub/queue") as Promise<{ body: QueueState }>));
    let q = (await plan()).body;
    const run0 = q.current!.items[0].runId!;
    expect(run0).toBe("script_DoorOpens_0");
    expect(hub.runs.get(run0)).toMatchObject({ source: "plan", planRunId: q.current!.id });

    // The manual step waits: PIE is paused, the item needs you, and nothing else starts.
    status.set(run0, { ...status.get(run0)!, state: "awaiting_verdict", steps: steps(["passed", "awaiting_verdict"]), awaiting: [1] });
    await hub.tick();
    await settle();
    expect(calls.filter((c) => c.action === "set_pie_paused").map((c) => c.args)).toEqual([{ paused: true, mode: "debug" }]);
    expect(hold.held()).toMatchObject([{ runId: run0, step: 1, instruction: "It looks right", paused: true }]);
    expect(calls.some((c) => c.action === "script_stop")).toBe(false);
    q = (await plan()).body;
    expect(q.current!.items.map((i) => i.state)).toEqual(["needs_you", "queued"]);
    // A live verdict is asked for in the app bar and on the Run screen; the goal is only running.
    expect(((await call("GET", "/hub")).body as HubOverview).goals[0].status).toBe("running");

    // The verdict is given (pie(verdict), from any client): the next read shows the step judged, PIE runs again, the run finishes and the plan goes on.
    status.set(run0, { ...status.get(run0)!, state: "running", steps: steps(["passed", "passed"]), awaiting: [] });
    await hub.tick();
    await settle();
    expect(calls.filter((c) => c.action === "set_pie_paused").map((c) => c.args.paused)).toEqual([true, false]);
    expect(hold.held()).toEqual([]);
    status.set(run0, { ...status.get(run0)!, state: "finished", steps: steps(["passed", "passed"]), awaiting: [] });
    await hub.tick();
    await settle();
    q = (await plan()).body;
    expect(q.current!.items[0].state).toBe("passed");
    expect(q.current!.items[1]).toMatchObject({ state: "running", runId: "script_JumpCheck_1" });

    // The second item fails; the plan is done.
    const run1 = "script_JumpCheck_1";
    status.set(run1, { ...status.get(run1)!, state: "finished", steps: steps(["failed", "skipped"]) });
    await hub.tick();
    await settle();
    q = (await plan()).body;
    expect(q.current).toBeNull();
    expect(q.recent[0].items.map((i) => i.state)).toEqual(["passed", "failed"]);
  });

  it("a plan's own verdict wait sets the held run's deadline; Stop now ends the run and PIE, and the item is not judged", async () => {
    setup();
    await call("POST", "/hub/plans", { scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }] }, options: { verdictWaitMinutes: 2 } });
    await settle();
    await settle();
    const run0 = "script_DoorOpens_0";
    status.set(run0, { ...status.get(run0)!, state: "awaiting_verdict", steps: steps(["passed", "awaiting_verdict"]), awaiting: [1] });
    await hub.tick();
    const [h] = hold.held();
    expect(Date.parse(h.dueAt) - Date.parse(h.since)).toBe(2 * 60_000);

    // The fake editor records the stop's reason as ue-mcp does.
    expect("run" in (await broker.stopRun(run0, "no verdict"))).toBe(true);
    expect(calls.filter((c) => c.action === "script_stop").map((c) => c.args)).toEqual([{ run_id: run0, reason: "no verdict" }]);
    expect(calls.filter((c) => c.action === "play_in_editor").map((c) => c.args)).toEqual([{ pieAction: "stop" }]);
    await hub.tick();
    await settle();
    const q = (await call("GET", "/hub/queue")).body as QueueState;
    expect(q.recent[0].items[0]).toMatchObject({ state: "unjudged", reason: "no verdict" });
    // No result: the check keeps the health it had (never run here), it is not failing.
    expect(hub.runs.get(run0)?.outcome).toBe("unjudged");
    expect(((await call("GET", "/hub")).body as HubOverview).checks.find((c) => c.id === `script:${DOOR}`)?.health).toBe("never_run");
    expect(hold.held()).toEqual([]);
    // Stopped for the verdict, PIE was not resumed.
    expect(calls.filter((c) => c.action === "set_pie_paused").map((c) => c.args.paused)).toEqual([true]);
  });

  it("an unattended plan never starts a check that needs a person", async () => {
    setup();
    listed = [DOOR, JUMP, "/Game/Tests/AutoOnly"];
    await call("POST", "/hub/plans", { scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }] }, options: { attended: false } });
    await settle();
    await settle();
    const q = (await call("GET", "/hub/queue")).body as QueueState;
    expect(q.recent[0].items[0]).toMatchObject({ state: "skipped", reason: "needs a person" });
    expect(calls.some((c) => c.action === "script_run")).toBe(false);
    expect(hold.held()).toEqual([]);
  });

  it("run a check is a one-item plan; agents may not plan under Manual", async () => {
    setup();
    const r = await call("POST", `/hub/checks/${enc(`script:${DOOR}`)}/run`, {});
    expect((r.body.plan as PlanRun).items).toHaveLength(1);
    await expect(hub.plan({ scope: { kind: "all" } }, "agent")).rejects.toMatchObject({ status: 409 });
  });

  it("records what a run tested when it starts, and ue-mcp's own fields from results.json when it ends", async () => {
    workspace = { vcs: "git", revision: "abc1234def", dirty: 2 };
    setup();
    await call("POST", `/hub/checks/${enc(`script:${DOOR}`)}/run`, {});
    for (let i = 0; i < 4; i++) await settle();
    const runId = "script_DoorOpens_0";
    const started = hub.runs.get(runId)!;
    expect(started).toMatchObject({ trigger: "run", provenance: { vcs: "git", revision: "abc1234def", dirty: 2, engine: "5.5", players: null } });
    expect(started.provenance!.scriptHash).toMatch(/^sha256:[0-9a-f]{64}$/);
    // The overview says whose view it is and the workspace it compares with.
    expect(((await call("GET", "/hub")).body as HubOverview).evidence).toEqual({ store: "local", current: workspace });

    // ue-mcp's results.json carries its own script hash and fingerprints: they are taken, the revision stays Studio's.
    const dir = path.join(root, "project", "Saved", "MCPObservations", runId);
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, "results.json"), JSON.stringify({ run_id: runId, provenance: { script_hash: "f00d", dependencies: { "/Game/Maps/Test": "aa" } } }));
    status.set(runId, { ...status.get(runId)!, state: "finished", steps: steps(["passed", "passed"]) });
    await broker.status(runId);
    for (let i = 0; i < 4; i++) await settle();
    expect(hub.runs.get(runId)?.provenance).toMatchObject({ revision: "abc1234def", dirty: 2, scriptHash: "ue-mcp:f00d", dependencies: { "/Game/Maps/Test": "aa" } });
  });

  it("a plan with nothing in scope is refused", async () => {
    setup();
    expect((await call("POST", "/hub/plans", { scope: { kind: "failing" } })).status).toBe(409);
  });

  it("with the experiment off a queued plan waits and a tick asks the editor for nothing", async () => {
    let on = false;
    setup({ enabled: () => on });
    await call("POST", "/hub/plans", { scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }] } });
    calls = [];
    await hub.tick();
    await settle();
    expect(calls).toEqual([]);
    expect((await call("GET", "/hub/queue")).body.queued).toHaveLength(1);
    on = true;
    await hub.tick();
    await settle();
    expect(calls.some((c) => c.action === "script_run")).toBe(true);
  });

  it("waits while another run is on the editor", async () => {
    setup();
    await call("POST", "/hub/plans", { scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }] } });
    await settle();
    await settle();
    expect(calls.filter((c) => c.action === "script_run")).toHaveLength(1);
    // The plan's run ends; a person's run of another script holds the editor before the next plan starts.
    const run0 = "script_DoorOpens_0";
    status.set(run0, { ...status.get(run0)!, state: "finished", steps: steps(["passed", "passed"]) });
    await hub.tick();
    const other = broker.all().find((r) => r.runId === run0)!;
    // Another client's run: the broker is told of it as it would of any run it reads.
    (broker as unknown as { runs: Map<string, unknown> }).runs.set("chat_run", { ...other, runId: "chat_run", state: "running" });
    await call("POST", "/hub/plans", { scope: { kind: "checks", checks: [{ kind: "script", target: JUMP }] } });
    await settle();
    expect(calls.filter((c) => c.action === "script_run")).toHaveLength(1);
    expect((await call("GET", "/hub/queue")).body.queued).toHaveLength(1);
  });
});

describe("the editor lease", () => {
  const queue = async () => (await call("GET", "/hub/queue")).body as QueueState;
  const runDoor = async () => {
    await call("POST", "/hub/plans", { scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }] } });
    for (let i = 0; i < 3; i++) await settle();
  };

  it("never attaches to the developer's PIE: the plan waits, saying so, and starts once PIE stops", async () => {
    setup();
    pie = true;
    await runDoor();
    expect(calls.some((c) => c.action === "script_run")).toBe(false);
    expect(await queue()).toMatchObject({ waiting: "Play In Editor is in use", lease: null });
    pie = false;
    await hub.tick();
    await settle();
    expect(calls.filter((c) => c.action === "script_run")).toHaveLength(1);
  });

  it("waits while a dialog is open", async () => {
    setup();
    dialogs = 1;
    await runDoor();
    expect(calls.some((c) => c.action === "script_run")).toBe(false);
    expect((await queue()).waiting).toBe("A dialog is open in the editor");
  });

  it("holds the lease while its run plays, tells the editor once, and Release the editor stops the run, ends the PIE it started and pauses", async () => {
    setup();
    await runDoor();
    const run0 = "script_DoorOpens_0";
    expect((await call("GET", "/hub/lease")).body.lease).toMatchObject({ editor: "workspace", purpose: "Run DoorOpens: DoorOpens", runId: run0, pie: true });
    expect(published.some((p) => p.type === "hub.lease" && (p.data as { lease: { runId?: string } | null }).lease?.runId === run0)).toBe(true);
    expect(calls.filter((c) => c.action === "show_notification").map((c) => c.args)).toEqual([{ text: "UE-MCP is running a playtest", subText: "Run DoorOpens: DoorOpens", severity: "info" }]);
    status.set(run0, { ...status.get(run0)!, state: "awaiting_verdict", steps: steps(["passed", "awaiting_verdict"]), awaiting: [1] });
    await hub.tick();
    const q = (await call("POST", "/hub/release")).body as QueueState;
    expect(calls.filter((c) => c.action === "script_stop").map((c) => c.args)).toEqual([{ run_id: run0, reason: "The editor was released" }]);
    expect(calls.filter((c) => c.action === "play_in_editor").map((c) => c.args)).toEqual([{ pieAction: "stop" }]);
    expect(q).toMatchObject({ paused: true, pausedBy: "release", lease: null });
    expect(q.current!.items[0]).toMatchObject({ state: "queued", reason: "The editor was released" });
    expect(hold.held()).toEqual([]);
    expect((await call("GET", "/hub/lease")).body.lease).toBeNull();
  });

  it("after Release the editor, a person's Run starts at once and says so", async () => {
    setup();
    await call("POST", "/hub/release");
    expect(await queue()).toMatchObject({ paused: true, pausedBy: "release" });
    const r = await call("POST", `/hub/checks/${enc(`script:${DOOR}`)}/run`, {});
    expect(r.body.placement).toEqual({ state: "started", position: 0, ahead: 0, resumed: true });
    await settle();
    expect(calls.filter((c) => c.action === "script_run")).toHaveLength(1);
    expect(await queue()).toMatchObject({ paused: false });
  });

  it("a person's Run while another plan runs queues behind it and says where", async () => {
    setup();
    await call("POST", `/hub/checks/${enc(`script:${DOOR}`)}/run`, {});
    await settle();
    const r = await call("POST", `/hub/checks/${enc(`script:${JUMP}`)}/run`, {});
    expect(r.body.placement).toEqual({ state: "queued", position: 1, ahead: 1 });
  });

  it("a person's Run on a busy editor waits and names what holds it", async () => {
    setup();
    pie = true;
    const r = await call("POST", `/hub/checks/${enc(`script:${DOOR}`)}/run`, {});
    expect(r.body.placement).toEqual({ state: "waiting", position: 1, ahead: 0, reason: "Play In Editor is in use" });
  });

  it("after the editor reconnects, queued work waits for a person", async () => {
    setup();
    pie = true;
    await runDoor();
    pie = false;
    for (const l of listeners) l({ type: "editor.connected", data: {} });
    await hub.tick();
    expect(calls.some((c) => c.action === "script_run")).toBe(false);
    expect(await queue()).toMatchObject({ paused: true, pausedBy: "reconnect" });
    await call("PUT", "/hub/queue/paused", { paused: false });
    await settle();
    expect(calls.filter((c) => c.action === "script_run")).toHaveLength(1);
  });

  it("an editor crash during the run is the item's outcome, and the next item runs", async () => {
    setup();
    await call("POST", "/hub/plans", { scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }, { kind: "script", target: JUMP }] } });
    for (let i = 0; i < 3; i++) await settle();
    connected = false;
    pie = false;
    for (const l of listeners) l({ type: "editor.disconnected", data: { cause: "crashed" } });
    connected = true;
    for (const l of listeners) l({ type: "editor.connected", data: {} });
    await call("PUT", "/hub/queue/paused", { paused: false });
    await settle();
    const q = await queue();
    expect(q.current!.items[0]).toMatchObject({ state: "crashed", reason: "The editor crashed during this run" });
    expect(calls.filter((c) => c.action === "script_run").map((c) => c.args.script)).toEqual([DOOR, JUMP]);
  });

  it("a run that saw project files change is tainted", async () => {
    setup();
    await runDoor();
    const run0 = "script_DoorOpens_0";
    (hub as unknown as { watch: { touched(): void } }).watch.touched();
    status.set(run0, { ...status.get(run0)!, state: "finished", steps: steps(["passed", "passed"]) });
    await hub.tick();
    expect(hub.runs.get(run0)?.tainted).toBe(true);
  });

  it("Fix failing with an agent queues a fix plan whose items each hand their playtest to the client's fixer", async () => {
    const started: string[] = [];
    const looping = new Set<string>();
    // A client's fixer (Studio's Assign loop): the loop's run is the playtest's card, with an assignment running.
    const fixer: HubFixer = {
      start: async (item) => {
        started.push(item.check.target);
        looping.add(item.check.target);
        const card = { ...qaDraftFromScript({ path: item.check.target, name: "DoorOpens", steps: STEPS }, { sessionId: "session-fix" })!, assignment: { state: "running", sessionId: "session-fix" } };
        (broker as unknown as { save(run: unknown): void }).save(card);
        return { runId: card.runId, sessionId: "session-fix" };
      },
      stop: () => null,
      running: () => looping,
    };
    setup({ fixer });
    const g = (await call("POST", "/hub/goals", { title: "Doors", checks: [{ kind: "script", target: DOOR }] })).body.goal;
    hub.runs.record(qaRunFromAnswer({ run_id: "r-old", script: DOOR, script_name: "DoorOpens", state: "finished", steps: [{ index: 0, instruction: "x", judge: "auto", status: "failed" }] }, { seenAt: "2026-11-23T00:00:00.000Z" })!);
    const r = (await call("POST", "/hub/fix", { goalId: g.id })).body;
    expect(r).toMatchObject({ queued: 1, plan: { work: "fix", title: "Fix what is failing in Doors" } });
    // Asked again while it is queued, nothing more is.
    expect((await call("POST", "/hub/fix", { checkId: `script:${DOOR}` })).body).toEqual({ queued: 0, plan: null });
    for (let i = 0; i < 3; i++) await settle();
    const q = await queue();
    expect(q.current!.items[0]).toMatchObject({ state: "running", sessionId: "session-fix" });
    expect(started).toEqual([DOOR]);
    expect(q.lease).toMatchObject({ work: "fix" });
  });

  it("answers a fix request 409 when no client can run a fix loop", async () => {
    setup();
    expect((await call("POST", "/hub/fix", {})).status).toBe(409);
  });
});

describe("ending the PIE a playtest started", () => {
  const queue = async () => (await call("GET", "/hub/queue")).body as QueueState;
  const stops = () => calls.filter((c) => c.action === "play_in_editor" && c.args.pieAction === "stop").length;
  const until = async (cond: () => boolean | Promise<boolean>, ms = 2000) => {
    const end = Date.now() + ms;
    while (!(await cond()) && Date.now() < end) await new Promise((r) => setTimeout(r, 10));
  };
  const leaseGone = async () => (await queue()).lease === null;
  const runDoor = async () => {
    const plan = (await call("POST", "/hub/plans", { scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }] } })).body.plan as PlanRun;
    for (let i = 0; i < 3; i++) await settle();
    return plan;
  };
  const run0 = "script_DoorOpens_0";

  it.each([
    ["passed", { state: "finished", steps: steps(["passed", "passed"]) }],
    ["failed", { state: "finished", steps: steps(["failed", "skipped"]) }],
    ["error", { state: "finished", steps: steps(["pending", "pending"]), stop_reason: "observation ended" }],
    ["unjudged", { state: "stopped", steps: steps(["passed", "awaiting_verdict"]), stop_reason: "no verdict" }],
    ["cancelled", { state: "stopped", steps: steps(["passed", "pending"]), stop_reason: "aborted" }],
  ])("ends it when the run ends %s", async (outcome, end) => {
    setup();
    await runDoor();
    expect(pie).toBe(true);
    status.set(run0, { ...status.get(run0)!, ...end });
    await hub.tick();
    await until(leaseGone);
    expect((await queue()).recent[0].items[0].state).toBe(outcome);
    expect(stops()).toBe(1);
    expect(pie).toBe(false);
  });

  it("ends it when the plan is stopped mid-run", async () => {
    setup();
    const plan = await runDoor();
    await hub.cancelPlan(plan.id);
    expect(calls.filter((c) => c.action === "script_stop").map((c) => c.args.run_id)).toEqual([run0]);
    expect(stops()).toBe(1);
    expect(pie).toBe(false);
    expect(await queue()).toMatchObject({ lease: null, current: null });
  });

  it("ends it when the plan is stopped while its run is still starting", async () => {
    setup();
    let open!: () => void;
    gate = new Promise((r) => (open = r));
    const plan = (await call("POST", "/hub/plans", { scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }] } })).body.plan as PlanRun;
    await settle();
    await hub.cancelPlan(plan.id);
    open();
    await until(async () => stops() === 1 && (await leaseGone()));
    expect(calls.filter((c) => c.action === "script_stop").map((c) => c.args.run_id)).toEqual([run0]);
    expect(stops()).toBe(1);
    expect(pie).toBe(false);
  });

  it("ends PIE the run asked for that comes up only after the run ended, and holds the lease until then", async () => {
    setup();
    pieLate = true;
    const plan = await runDoor();
    expect(pie).toBe(false);
    setTimeout(() => (pie = true), 50);
    await hub.cancelPlan(plan.id);
    expect(stops()).toBe(1);
    expect(pie).toBe(false);
    expect((await queue()).lease).toBeNull();
  });

  it("stops nothing when the PIE the run asked for never comes up", async () => {
    setup();
    pieLate = true;
    const plan = await runDoor();
    await hub.cancelPlan(plan.id);
    expect(stops()).toBe(0);
    expect((await queue()).lease).toBeNull();
  });

  it("stops nothing when the PIE the run started has already ended", async () => {
    setup();
    await runDoor();
    // The run is followed while PIE plays, then PIE ends by itself and the run with it.
    status.set(run0, { ...status.get(run0)!, state: "running" });
    await hub.tick();
    pie = false;
    status.set(run0, { ...status.get(run0)!, state: "finished", steps: steps(["passed", "passed"]), stop_reason: "PIE ended" });
    await hub.tick();
    await until(leaseGone);
    expect(stops()).toBe(0);
  });

  it("lets another session's PIE call finish before ending it, and leaves PIE that call outlasts the wait for", async () => {
    let busy: BusyCall[] = [];
    setup({ busy: () => busy });
    await runDoor();
    // Another client drives the hub's PIE as the run ends.
    busy = [{ id: 1, category: "pie", action: "inject_input", startedAt: Date.now(), source: "client", client: "other", stopped: false }];
    status.set(run0, { ...status.get(run0)!, state: "finished", steps: steps(["passed", "passed"]) });
    setTimeout(() => (busy = []), 50);
    await hub.tick();
    await until(leaseGone);
    expect(stops()).toBe(1);

    // A second run, and a call that does not finish within the wait: PIE is left to that session.
    await runDoor();
    busy = [{ id: 2, category: "editor", action: "play_in_editor", startedAt: Date.now(), source: "agent", sessionId: "other-chat", stopped: false }];
    const run1 = "script_DoorOpens_1";
    status.set(run1, { ...status.get(run1)!, state: "finished", steps: steps(["passed", "passed"]) });
    await hub.tick();
    await until(leaseGone);
    expect(stops()).toBe(1);
    expect(pie).toBe(true);
  });

  it("ends the PIE a replay started when the plan is stopped before the replay finishes", async () => {
    setup();
    hub.runs.record(qaRunFromAnswer({ run_id: "r-old", script: DOOR, script_name: "DoorOpens", state: "finished", recording_id: "rec-old", steps: steps(["passed", "passed"]) }, { seenAt: "2026-11-23T00:00:00.000Z" })!);
    const plan = (await call("POST", `/hub/checks/${enc(`script:${DOOR}`)}/run`, { replay: true })).body.plan as PlanRun;
    for (let i = 0; i < 3; i++) await settle();
    expect(calls.some((c) => c.action === "script_replay")).toBe(true);
    expect((await queue()).lease).toMatchObject({ pie: true });
    await hub.cancelPlan(plan.id);
    expect(stops()).toBe(1);
    expect(pie).toBe(false);
  });

  it("the daemon stopping mid-run stops the run, ends its PIE, and puts the item back for a person to resume", async () => {
    setup();
    await runDoor();
    await hub.shutdown();
    expect(calls.filter((c) => c.action === "script_stop").map((c) => c.args)).toEqual([{ run_id: run0, reason: "The daemon stopped during this run" }]);
    expect(stops()).toBe(1);
    expect(pie).toBe(false);
    const q = await queue();
    expect(q).toMatchObject({ paused: true, pausedBy: "reconnect", lease: null });
    expect(q.current!.items[0]).toMatchObject({ state: "queued", reason: "The daemon stopped during this run" });
  });

  it("the daemon stopping with nothing on the editor asks the editor for nothing", async () => {
    setup();
    await hub.shutdown();
    expect(calls.filter((c) => c.action === "script_stop" || c.action === "play_in_editor")).toEqual([]);
  });
});

describe("write playtests", () => {
  it("runs the flow in a new session seeded with the goal, and links what it drafts", async () => {
    let seeded = "";
    let title = "";
    setup({
      // A client's flow (Studio: /playtest in a new chat session), which drafts a script in that session.
      writePlaytests: (request) => {
        seeded = request.text;
        title = request.title;
        const sessionId = "session-write";
        const done = (async () => {
          await Promise.resolve();
          (broker as unknown as { save(run: unknown): void }).save(qaDraftFromScript({ path: "/Game/Tests/NewOne", name: "NewOne", steps: STEPS }, { sessionId })!);
        })();
        return { sessionId, done };
      },
    });
    const g = (await call("POST", "/hub/goals", { title: "Doors", description: "Every door opens", checks: [{ kind: "script", target: DOOR }] })).body.goal;
    const r = await call("POST", `/hub/goals/${g.id}/write-playtests`, {});
    expect(r.status).toBe(202);
    await settle();
    expect(r.body.sessionId).toBe("session-write");
    expect(title).toBe("Playtests for Doors");
    expect(seeded).toContain('"Doors"');
    expect(seeded).toContain("Every door opens");
    expect(seeded).toContain("DoorOpens");
    // Names, not paths: a path would become the flow's target.
    expect(seeded).not.toContain(DOOR);
    expect(hub.goals.get(g.id)?.checks.map((c) => c.target)).toEqual([DOOR, "/Game/Tests/NewOne"]);
  });

  it("is a 409 without a flow and a 404 for no goal", async () => {
    setup();
    const g = (await call("POST", "/hub/goals", { title: "Doors" })).body.goal;
    expect((await call("POST", `/hub/goals/${g.id}/write-playtests`, {})).status).toBe(409);
    expect((await call("POST", "/hub/goals/nope/write-playtests", {})).status).toBe(404);
  });
});

describe("run screen", () => {
  it("a backfilled run with its results, observed series, last pass and changed steps", async () => {
    const obs = path.join(root, "project", "Saved", "MCPObservations");
    const write = (id: string, at: string, stepStatus: string[]) => {
      fs.mkdirSync(path.join(obs, id), { recursive: true });
      fs.writeFileSync(path.join(obs, id, "results.json"), JSON.stringify({ run_id: id, script: DOOR, script_name: "DoorOpens", state: "finished", started_at: at, ended_at: at, steps: steps(stepStatus).map((s, i) => ({ ...s, started_at_ms: i * 1000, ended_at_ms: i * 1000 + 500 })) }));
    };
    write("script_DoorOpens_old", "2026-11-20T00:00:00.000Z", ["passed", "passed"]);
    write("script_DoorOpens_new", "2026-11-22T00:00:00.000Z", ["failed", "passed"]);
    const rows = ["# observer", "frame,time,t:Door.Distance,speed2d", ...Array.from({ length: 1000 }, (_, i) => `${i},${(i / 60).toFixed(4)},${500 - i * 0.1},${i % 7}`)];
    fs.writeFileSync(path.join(obs, "script_DoorOpens_new", "observation.csv"), rows.join("\n"));
    // No editor: the results files are the backfill.
    connected = false;
    setup();
    await hub.backfill(true);
    const d = (await call("GET", "/hub/runs/script_DoorOpens_new")).body;
    expect(d.record).toMatchObject({ outcome: "failed", source: "outside", failedSteps: [0] });
    expect(d.steps[0]).toMatchObject({ index: 0, judge: "auto", status: "failed", startMs: 0, endMs: 500 });
    expect(d.series["Door.Distance"]).toHaveLength(300);
    expect(d.series["Door.Distance"][0]).toEqual([0, 500]);
    expect(d.series.speed2d).toBeDefined();
    // Backfilled runs tested an unknown script version, so no step is compared with another run's.
    expect(d.record.provenance).toBeUndefined();
    expect(d.lastPass).toBeNull();
    expect(d.changedSteps).toBeUndefined();
    expect(d.resultsUrl).toBe("/v1/hub/runs/script_DoorOpens_new/results");
    const page = (await call("GET", `/hub/runs?check=${enc(`script:${DOOR}`)}&limit=1`)).body;
    expect(page.runs.map((r: { runId: string }) => r.runId)).toEqual(["script_DoorOpens_new"]);
    expect(page.more).toBe(true);
    const o = (await call("GET", "/hub")).body as HubOverview;
    expect(o.attention.find((i) => i.kind === "regression")).toMatchObject({ runId: "script_DoorOpens_new", checkId: `script:${DOOR}` });
    const detail = (await call("GET", `/hub/checks/${enc(`script:${DOOR}`)}`)).body;
    expect(detail.state).toMatchObject({ health: "failing", recent: ["failed", "passed"] });
  });

  it("links: a run's are kept in the history and survive the run changing; a check's are its own, on every goal that lists it", async () => {
    const obs = path.join(root, "project", "Saved", "MCPObservations", "script_DoorOpens_1");
    fs.mkdirSync(obs, { recursive: true });
    fs.writeFileSync(path.join(obs, "results.json"), JSON.stringify({ run_id: "script_DoorOpens_1", script: DOOR, script_name: "DoorOpens", state: "finished", started_at: "2026-11-22T00:00:00.000Z", ended_at: "2026-11-22T00:00:00.000Z", steps: steps(["failed", "passed"]) }));
    connected = false;
    setup();
    const url = "https://git.example.com/ember/hollow/issues/42";
    // A run only results.json knows is recorded on its first link.
    const added = await call("POST", "/hub/runs/script_DoorOpens_1/links", { link: { url, rel: "filed-from-run" } });
    expect(added.status).toBe(200);
    expect(added.body.record.links).toEqual([{ url, rel: "filed-from-run" }]);
    expect((await call("POST", "/hub/runs/script_DoorOpens_1/links", { link: { url: "javascript:x" } })).status).toBe(400);
    expect((await call("POST", "/hub/runs/nope/links", { link: { url } })).status).toBe(404);
    await hub.backfill(true);
    expect((await call("GET", "/hub/runs/script_DoorOpens_1")).body.record.links).toEqual([{ url, rel: "filed-from-run" }]);
    expect((await call("DELETE", `/hub/runs/script_DoorOpens_1/links?url=${enc("https://other")}`)).status).toBe(404);
    expect((await call("DELETE", `/hub/runs/script_DoorOpens_1/links?url=${enc(url)}`)).body.record.links).toBeUndefined();

    const a = (await call("POST", "/hub/goals", { title: "A", checks: [{ kind: "script", target: DOOR }] })).body.goal;
    const b = (await call("POST", "/hub/goals", { title: "B", checks: [{ kind: "script", target: DOOR }] })).body.goal;
    const put = await call("PUT", `/hub/checks/${enc(`script:${DOOR}`)}`, { links: [{ url: "jira://HOL-9" }] });
    expect(put.body.meta).toEqual({ links: [{ url: "jira://HOL-9" }] });
    expect((await call("PUT", `/hub/checks/${enc(`script:${DOOR}`)}`, { links: [{ url: "javascript:x" }] })).status).toBe(400);
    expect((await call("PUT", `/hub/goals/${a.id}/checks/${enc("script:/Game/None")}`, { required: false })).status).toBe(404);
    const detail = (await call("GET", `/hub/checks/${enc(`script:${DOOR}`)}`)).body;
    expect(detail.state.links).toEqual([{ url: "jira://HOL-9" }]);
    expect((await call("GET", `/hub/goals/${a.id}`)).body.checks[0].links).toEqual([{ url: "jira://HOL-9" }]);
    expect((await call("GET", `/hub/goals/${b.id}`)).body.checks[0].links).toEqual([{ url: "jira://HOL-9" }]);
  });

  it("samples a series down to the cap and keeps its end", () => {
    const pts = Array.from({ length: 1001 }, (_, i) => [i, i] as [number, number]);
    const s = sample(pts, 300);
    expect(s).toHaveLength(300);
    expect(s[0]).toEqual([0, 0]);
    expect(s[299]).toEqual([1000, 1000]);
    expect(seriesOf("frame,time\n1,0")).toEqual({});
  });
});

describe("schedules", () => {
  /** Another session in the consent ledger, as the daemon reports it. */
  let others: Array<{ label: string; hold?: string | null; busy?: number; idleMs?: number | null }>;
  let now: number;
  const queue = async () => (await call("GET", "/hub/queue")).body as QueueState;
  const make = () => {
    const h = host();
    broker = new ScriptRunBroker(h);
    hub = new Hub({
      host: { ...h },
      broker,
      index: new ScriptIndex(h),
      signer: () => "octocat",
      playtestRuns: () => "manual",
      enabled: () => true,
      timers: false,
      now: () => now,
      workspace: async () => workspace,
      editor: { pie: () => pie, dialogs: () => dialogs, busy: () => ({ now, calls: [] }), inputAgeMs: () => null, sessions: () => others },
    });
  };
  beforeEach(() => {
    others = [];
    now = new Date(2026, 10, 23, 1, 0).getTime();
  });

  it("the next run is the next allowed day at that local time", () => {
    const mon = new Date(2026, 10, 23, 1, 0); // a Monday, 01:00 local
    expect(nextRunAt({ at: "02:00", days: [1] }, mon)?.getTime()).toBe(new Date(2026, 10, 23, 2, 0).getTime());
    expect(nextRunAt({ at: "00:30", days: [1] }, mon)?.getTime()).toBe(new Date(2026, 10, 30, 0, 30).getTime());
    expect(nextRunAt({ at: "02:00", days: [3] }, mon)?.getTime()).toBe(new Date(2026, 10, 25, 2, 0).getTime());
  });

  it("create, edit and delete; a schedule is kept with the queue across a restart", async () => {
    make();
    const s = (await call("POST", "/hub/schedules", { title: "Nightly", scope: { kind: "all" }, at: "02:00", days: [0, 1, 2, 3, 4, 5, 6] })).body.schedule;
    expect(s).toMatchObject({ title: "Nightly", enabled: true, options: { attended: false }, nextRunAt: new Date(2026, 10, 23, 2, 0).toISOString() });
    const edited = (await call("PUT", `/hub/schedules/${s.id}`, { at: "03:30", enabled: false })).body.schedule;
    expect(edited).toMatchObject({ at: "03:30", enabled: false, nextRunAt: null });
    hub.dispose();
    make();
    expect((await queue()).schedules.map((x: { id: string }) => x.id)).toEqual([s.id]);
    expect((await call("DELETE", `/hub/schedules/${s.id}`)).status).toBe(204);
    expect((await queue()).schedules).toEqual([]);
  });

  it("a due schedule waits while another session holds the editor, then runs once it lets go", async () => {
    make();
    const s = (await call("POST", "/hub/schedules", { title: "Nightly", scope: { kind: "all" }, at: "02:00", days: [0, 1, 2, 3, 4, 5, 6] })).body.schedule;
    others = [{ label: "Claude Code", hold: "refactoring the door blueprint" }];
    now = new Date(2026, 10, 23, 2, 1).getTime();
    await hub.tick();
    let got = (await queue()).schedules[0];
    expect(got).toMatchObject({ waiting: "Claude Code holds the editor: refactoring the door blueprint", lastRunAt: null });
    expect(calls.some((c) => c.action === "script_run")).toBe(false);
    // Let go, but used the editor a minute ago: a schedule still does not barge in.
    others = [{ label: "Claude Code", idleMs: 60_000 }];
    await hub.tick();
    expect((await queue()).schedules[0].waiting).toBe("Claude Code used the editor in the last 3 minutes");
    others = [{ label: "Claude Code", idleMs: 600_000 }];
    now = new Date(2026, 10, 23, 2, 10).getTime();
    await hub.tick();
    await settle();
    const q = await queue();
    got = q.schedules[0];
    expect(got).toMatchObject({ waiting: null, lastSkipped: null, lastRunAt: new Date(now).toISOString(), nextRunAt: new Date(2026, 10, 24, 2, 0).toISOString() });
    expect(q.current ?? q.queued[0] ?? q.recent[0]).toMatchObject({ trigger: "schedule", scheduleId: s.id, title: "Nightly" });
  });

  it("gives up after an hour of waiting, saying why, and is due again the next day", async () => {
    make();
    await call("POST", "/hub/schedules", { title: "Nightly", scope: { kind: "all" }, at: "02:00", days: [0, 1, 2, 3, 4, 5, 6] });
    connected = false;
    now = new Date(2026, 10, 23, 2, 1).getTime();
    await hub.tick();
    expect((await queue()).schedules[0].waiting).toBe("The editor is not connected");
    now = new Date(2026, 10, 23, 3, 2).getTime();
    await hub.tick();
    expect((await queue()).schedules[0]).toMatchObject({ waiting: null, lastSkipped: "waited an hour while the editor is not connected", nextRunAt: new Date(2026, 10, 24, 2, 0).toISOString() });
  });

  it("respects a paused queue: it waits, and never resumes it", async () => {
    make();
    await call("POST", "/hub/schedules", { title: "Nightly", scope: { kind: "all" }, at: "02:00", days: [0, 1, 2, 3, 4, 5, 6] });
    await call("PUT", "/hub/queue/paused", { paused: true });
    now = new Date(2026, 10, 23, 2, 1).getTime();
    await hub.tick();
    const q = await queue();
    expect(q).toMatchObject({ paused: true, queued: [], current: null });
    expect(q.schedules[0].waiting).toBe("the queue is paused");
  });

  it("an unattended schedule's plan skips a playtest that needs a person", async () => {
    make();
    const p = await hub.plan({ scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }] } }, "schedule");
    expect(p.options.attended).toBe(false);
    await settle();
    await settle();
    expect((await queue()).recent[0].items[0]).toMatchObject({ state: "skipped", reason: "needs a person" });
    expect(calls.some((c) => c.action === "script_run")).toBe(false);
  });
});

describe("events", () => {
  it("changes are gathered into one hub.changed", async () => {
    setup();
    published = [];
    await call("POST", "/hub/goals", { title: "A" });
    await call("POST", "/hub/goals", { title: "B" });
    hub.flush();
    const hubEvents = published.filter((e) => e.type === "hub.changed");
    expect(hubEvents).toHaveLength(1);
    expect(hubEvents[0].data).toEqual({ what: ["goals", "checks", "attention"] });
  });

  it("a lease change is published as hub.lease", async () => {
    setup();
    published = [];
    await call("POST", "/hub/plans", { scope: { kind: "checks", checks: [{ kind: "script", target: DOOR }] } });
    for (let i = 0; i < 3; i++) await settle();
    expect(published.some((p) => p.type === "hub.lease")).toBe(true);
  });
});

describe("a run's results", () => {
  it("serves the run's results.json, and 404s a run with none or an id that could leave the folder", async () => {
    const dir = path.join(root, "project", "Saved", "MCPObservations", "script_DoorOpens_9");
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, "results.json"), "{}");
    setup();
    const ok = await call("GET", "/hub/runs/script_DoorOpens_9/results");
    expect(ok).toMatchObject({ status: 200 });
    expect((await call("GET", "/hub/runs/script_DoorOpens_8/results")).status).toBe(404);
    expect((await call("GET", `/hub/runs/${enc("../../x")}/results`)).status).toBe(404);
  });
});
