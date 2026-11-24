/**
 * The plan queue: one item on the editor at a time, manual steps (the plan
 * waits for the verdict; a run that ended without one is not judged), stop
 * on failure, pause, cancel, move, and a plan that survives a restart.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { DEFAULT_PLAN_OPTIONS, qaRunFromAnswer, type PlanOptions, type PlanRun, type QaRun } from "../../src/hub/contract.js";
import { PlanQueue, type ItemStart, type QueueRunner } from "../../src/hub/queue.js";

let root: string;
let runs: Map<string, QaRun>;
let starts: string[];
let stops: Array<{ runId: string; reason: string }>;
let connected: boolean;
/** What occupies the editor, as the runner answers occupied(). */
let busy: string | null;
let fixStops: string[];
let pieEnds: number;
/** What starting each script does; a run id by default. */
let onStart: (script: string) => ItemStart;
let n: number;

const qa = (runId: string, state: string, statuses: string[], judges: string[] = []): QaRun => qaRunFromAnswer({
  run_id: runId,
  script: "/Game/T/X",
  script_name: "X",
  state,
  steps: statuses.map((status, index) => ({ index, instruction: `s${index}`, judge: judges[index] ?? "auto", status })),
}, { sessionId: "s", seenAt: "2026-11-23T00:00:00.000Z" })!;

const runner = (): QueueRunner => ({
  connected: () => connected,
  occupied: () => busy,
  start: async (item) => {
    starts.push(item.check.target);
    const r = onStart(item.check.target);
    if ("runId" in r && !runs.has(r.runId)) runs.set(r.runId, { ...qa(r.runId, "running", ["running", "pending"]), pieStarted: true });
    return r;
  },
  run: (id) => runs.get(id),
  stop: async (runId, reason) => {
    stops.push({ runId, reason });
  },
  refresh: async () => {},
  stopFix: async (item) => void fixStops.push(item.check.target),
  endPie: async () => void (pieEnds += 1),
});

const plan = (scripts: string[], options: Partial<PlanOptions> = {}): PlanRun => ({
  id: `p${n++}`,
  title: "Plan",
  scope: { kind: "all" },
  options: { ...DEFAULT_PLAN_OPTIONS, ...options },
  state: "queued",
  items: scripts.map((s) => ({ check: { kind: "script" as const, target: s }, name: s, state: "queued" as const })),
  trigger: "person",
  createdAt: "2026-11-23T00:00:00.000Z",
});

const make = () => new PlanQueue({ dataDir: root, runner: runner(), now: () => "2026-11-23T01:00:00.000Z" });
const settle = () => new Promise((r) => setTimeout(r, 0));

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "hub-queue-"));
  runs = new Map();
  starts = [];
  stops = [];
  connected = true;
  busy = null;
  fixStops = [];
  pieEnds = 0;
  n = 0;
  let i = 0;
  onStart = () => ({ runId: `run${i++}` });
});
afterEach(() => fs.rmSync(root, { recursive: true, force: true }));

describe("PlanQueue", () => {
  it("runs a plan's items one at a time, each when the one before ended", async () => {
    const q = make();
    const p = q.enqueue(plan(["/Game/A", "/Game/B"]));
    await settle();
    expect(starts).toEqual(["/Game/A"]);
    expect(q.state().current?.state).toBe("running");
    q.observe(qa("run0", "finished", ["passed", "passed"]));
    await settle();
    expect(starts).toEqual(["/Game/A", "/Game/B"]);
    q.observe(qa("run1", "finished", ["passed", "failed"]));
    await settle();
    const done = q.state().recent[0];
    expect(done.id).toBe(p.id);
    expect(done.state).toBe("done");
    expect(done.items.map((i) => i.state)).toEqual(["passed", "failed"]);
    expect(q.state().current).toBeNull();
  });

  it("a run stopped because no verdict came: the item is not judged, with the run's reason, and the plan moves on", async () => {
    const q = make();
    q.enqueue(plan(["/Game/A", "/Game/B"]));
    await settle();
    q.observe(qa("run0", "awaiting_verdict", ["passed", "awaiting_verdict", "pending"], ["auto", "manual", "auto"]));
    expect(q.state().current!.items[0]).toMatchObject({ state: "needs_you", reason: "waiting for a verdict" });
    q.observe({ ...qa("run0", "stopped", ["passed", "awaiting_verdict", "pending"], ["auto", "manual", "auto"]), stopReason: "no verdict" });
    await settle();
    expect(q.state().current!.items[0]).toMatchObject({ state: "unjudged", reason: "no verdict" });
    expect(stops).toEqual([]);
    expect(starts).toEqual(["/Game/A", "/Game/B"]);
  });

  it("the plan waits at the verdict, then carries on", async () => {
    const q = make();
    q.enqueue(plan(["/Game/A", "/Game/B"]));
    await settle();
    q.observe(qa("run0", "awaiting_verdict", ["passed", "awaiting_verdict"], ["auto", "manual"]));
    await settle();
    expect(stops).toEqual([]);
    expect(q.state().current!.items[0].state).toBe("needs_you");
    expect(starts).toEqual(["/Game/A"]);
    expect(q.currentRun()).toBe("run0");
    q.observe(qa("run0", "finished", ["passed", "passed"], ["auto", "manual"]));
    await settle();
    expect(starts).toEqual(["/Game/A", "/Game/B"]);
  });

  it("stop on failure cancels what is left, and the plan failed", async () => {
    const q = make();
    q.enqueue(plan(["/Game/A", "/Game/B"], { stopOnFailure: true }));
    await settle();
    q.observe(qa("run0", "finished", ["failed"]));
    await settle();
    const p = q.state().recent[0];
    expect(p.state).toBe("failed");
    expect(p.items.map((i) => i.state)).toEqual(["failed", "cancelled"]);
    expect(starts).toEqual(["/Game/A"]);
  });

  it("an item that cannot run is skipped or an error, and the plan goes on", async () => {
    onStart = (s) => (s === "/Game/A" ? { error: "the open level has unsaved changes" } : { skipped: "Manual check checks cannot run yet" });
    const q = make();
    q.enqueue(plan(["/Game/A", "Looks right"]));
    await settle();
    const p = q.state().recent[0];
    expect(p.items.map((i) => [i.state, i.reason])).toEqual([["error", "the open level has unsaved changes"], ["skipped", "Manual check checks cannot run yet"]]);
    expect(p.state).toBe("done");
  });

  it("waits while paused, while the editor is away, and while something else occupies it, saying why", async () => {
    const q = make();
    q.setPaused(true);
    q.enqueue(plan(["/Game/A"]));
    await settle();
    expect(starts).toEqual([]);
    connected = false;
    q.setPaused(false);
    await settle();
    expect(starts).toEqual([]);
    connected = true;
    busy = "Play In Editor is in use";
    await q.pump();
    expect(starts).toEqual([]);
    expect(q.state().waiting).toBe("Play In Editor is in use");
    expect(q.state().lease).toBeNull();
    busy = null;
    await q.pump();
    expect(starts).toEqual(["/Game/A"]);
    expect(q.state().waiting).toBeNull();
  });

  it("takes the lease before an item starts, records the run and its PIE, and releases it when the item settles, ending only the PIE the run started", async () => {
    const q = make();
    q.enqueue({ ...plan(["/Game/A", "/Game/B"]), title: "Run Tutorial" });
    await settle();
    expect(q.state().lease).toMatchObject({ editor: "workspace", purpose: "Run Tutorial: /Game/A", item: 0, work: "run", runId: "run0", pie: true });
    q.observe(qa("run0", "finished", ["passed", "passed"]));
    await settle();
    expect(pieEnds).toBe(1);
    expect(q.state().lease).toMatchObject({ item: 1, runId: "run1" });
    // A run that joined PIE already running: the lease holds no PIE, so none is ended.
    runs.set("run1", qa("run1", "running", ["running"]));
    q.lease.update({ pie: false });
    q.observe(qa("run1", "finished", ["passed"]));
    await settle();
    expect(pieEnds).toBe(1);
    expect(q.state().lease).toBeNull();
  });

  it("Release the editor: pauses, stops the hub's run, ends its PIE, puts the item back, and only a person resumes", async () => {
    const q = make();
    q.enqueue(plan(["/Game/A", "/Game/B"]));
    await settle();
    await q.release();
    expect(stops).toEqual([{ runId: "run0", reason: "The editor was released" }]);
    expect(pieEnds).toBe(1);
    const s = q.state();
    expect(s).toMatchObject({ paused: true, pausedBy: "release", lease: null });
    expect(s.current!.items[0]).toMatchObject({ state: "queued", runId: null, reason: "The editor was released" });
    // The stopped run's events no longer move the item.
    q.observe({ ...qa("run0", "stopped", ["running", "pending"]), stopReason: "The editor was released" });
    await q.pump();
    expect(starts).toEqual(["/Game/A"]);
    q.setPaused(false);
    await settle();
    expect(starts).toEqual(["/Game/A", "/Game/A"]);
    expect(q.state().pausedBy).toBeNull();
  });

  it("after the editor reconnects with work waiting, the queue waits for a person; with nothing waiting it stays as it was", async () => {
    const q = make();
    expect(q.reconnected()).toBe(false);
    expect(q.paused()).toBe(false);
    q.setPaused(true);
    q.enqueue(plan(["/Game/A"]));
    q.setPaused(false);
    await settle();
    q.observe(qa("run0", "finished", ["passed"]));
    q.enqueue(plan(["/Game/B"]));
    busy = "The editor is restarting";
    await settle();
    expect(q.reconnected()).toBe(true);
    busy = null;
    await q.tick();
    expect(starts).toEqual(["/Game/A"]);
    expect(q.state()).toMatchObject({ paused: true, pausedBy: "reconnect", waiting: null });
    q.setPaused(false);
    await settle();
    expect(starts).toEqual(["/Game/A", "/Game/B"]);
  });

  it("a crash during an item's run is that item's outcome, once: crashed, with the reason, never retried", async () => {
    const q = make();
    q.enqueue(plan(["/Game/A", "/Game/B"]));
    await settle();
    connected = false;
    q.editorLost("crashed");
    expect(q.state().current!.items[0]).toMatchObject({ state: "crashed", reason: "The editor crashed during this run", runId: "run0" });
    expect(q.state().lease).toBeNull();
    connected = true;
    q.reconnected();
    q.setPaused(false);
    await settle();
    // The next item runs; the crashed one is not started again.
    expect(starts).toEqual(["/Game/A", "/Game/B"]);
    q.observe(qa("run1", "finished", ["passed"]));
    await settle();
    expect(q.state().recent[0].items.map((i) => i.state)).toEqual(["crashed", "passed"]);
  });

  it("a fix plan runs one fix loop at a time in its chat session, follows the loop on its card, and Release stops the loop", async () => {
    onStart = (s) => ({ runId: `card-${s}`, sessionId: `fix-${s}` });
    const q = make();
    q.enqueue({ ...plan(["/Game/A", "/Game/B"]), work: "fix", title: "Fix what is failing" });
    await settle();
    expect(q.state().current!.items[0]).toMatchObject({ state: "running", sessionId: "fix-/Game/A" });
    expect(q.state().lease).toMatchObject({ work: "fix", sessionId: "fix-/Game/A" });
    const loopRun = (runId: string, state: "running" | "done", loop: "running" | "passed" | "needs_you", blocker?: string): QaRun => ({
      ...qa(runId, "finished", ["failed"]),
      script: "/Game/A",
      assignment: { callId: "c", sessionId: "fix-/Game/A", state, at: "", loop: { id: "l", state: loop, attempts: [], baseline: "", startedAt: "", ...(blocker ? { blocker: { kind: "no_progress", reason: blocker, evidence: [] } } : {}) } },
    });
    q.observe(loopRun("attempt1", "running", "running"));
    expect(q.state().current!.items[0]).toMatchObject({ state: "running", runId: "attempt1" });
    q.observe(loopRun("attempt1", "done", "needs_you", "No progress: Step 1 fails the same way on two attempts in a row."));
    await settle();
    expect(q.state().current!.items[0]).toMatchObject({ state: "failed", reason: "No progress: Step 1 fails the same way on two attempts in a row." });
    expect(starts).toEqual(["/Game/A", "/Game/B"]);
    await q.release();
    expect(fixStops).toEqual(["/Game/B"]);
    expect(q.state().current!.items[1]).toMatchObject({ state: "queued", sessionId: "fix-/Game/B", reason: "The editor was released" });
  });

  it("cancel: a queued plan leaves, the running one stops its run", async () => {
    const q = make();
    const a = q.enqueue(plan(["/Game/A"]));
    const b = q.enqueue(plan(["/Game/B"]));
    await settle();
    expect((await q.cancel(b.id)).state).toBe("cancelled");
    await q.cancel(a.id);
    expect(stops).toEqual([{ runId: "run0", reason: "The plan was cancelled" }]);
    expect(q.state().recent.map((p) => p.id)).toEqual([a.id, b.id]);
    await expect(q.cancel(a.id)).rejects.toMatchObject({ status: 409 });
    await expect(q.cancel("nope")).rejects.toMatchObject({ status: 404 });
  });

  it("an editor that goes away mid-run queues the item again", async () => {
    const q = make();
    q.enqueue(plan(["/Game/A"]));
    await settle();
    connected = false;
    await q.tick();
    expect(q.state().current!.items[0]).toMatchObject({ state: "queued", runId: null });
    connected = true;
    await q.tick();
    expect(starts).toEqual(["/Game/A", "/Game/A"]);
  });

  it("survives a restart: a run Studio still has is followed, one it lost is run again", async () => {
    const q = make();
    q.enqueue(plan(["/Game/A"]));
    await settle();
    // Studio still holds run0: the new queue follows it.
    const again = make();
    expect(again.state().current!.items[0]).toMatchObject({ state: "running", runId: "run0" });
    again.observe(qa("run0", "finished", ["passed", "passed"]));
    await settle();
    expect(again.state().recent[0].items[0].state).toBe("passed");

    // A plan whose run Studio lost is queued again for when the editor is back.
    connected = false;
    const q2 = make();
    q2.enqueue(plan(["/Game/B"]));
    const file = path.join(root, "hub", "queue.json");
    const stored = JSON.parse(fs.readFileSync(file, "utf-8"));
    stored.current = { ...stored.queued[0], state: "running", items: [{ ...stored.queued[0].items[0], state: "running", runId: "gone" }] };
    stored.queued = [];
    fs.writeFileSync(file, JSON.stringify(stored));
    const q3 = make();
    expect(q3.state().current!.items[0]).toMatchObject({ state: "queued", runId: null, reason: "queued again after the hub restarted" });
  });

  it("reads a plan an earlier Studio saved, with its schedules and manualSteps, as a person's plan", () => {
    const old = plan(["/Game/A"]);
    const file = path.join(root, "hub", "queue.json");
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, JSON.stringify({ schema: 1, paused: true, current: null, queued: [{ ...old, options: { manualSteps: "skip", stopOnFailure: true } }], recent: [], schedules: [{ id: "nightly" }] }));
    expect(make().state().queued[0].options).toEqual({ ...DEFAULT_PLAN_OPTIONS, attended: true, stopOnFailure: true });
  });
});
