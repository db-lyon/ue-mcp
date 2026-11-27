// @live-editor headless
/**
 * The plugin's arrangement player (#2146), through the bridge as the hub calls it: pie(arrangement_play) plays an
 * arrangement whose track drives the editor's player through the probe arena, judges the Checks track in each mode
 * (a manual check pauses PIE until arrangement_verdict), plays one region alone, starts at a playhead, loops until
 * stopped, and ends the PIE session it started. pie(clip_record_start) with a punch plays a clip's lead-in and
 * replaces the punched range with the take, which pie(clip_record_stop) answers.
 * Refs #2146
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { setupProbeArena, teardownProbeArena, type ProbeActions } from "./clip-fixture.js";

const DIR = "/Game/MCPTest/HubPlayer";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;
let ia: ProbeActions;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

async function waitPieStopped(): Promise<boolean> {
  for (let i = 0; i < 60 && (await pieRunning()); i++) await sleep(500);
  return !(await pieRunning());
}

async function stopPie(): Promise<void> {
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    await waitPieStopped();
  }
}

/** Polls arrangement_status until the run reaches one of `states`, or `done` says so. */
async function until(runId: string, states: string[], done: (run: Json) => boolean = () => false, polls = 1200): Promise<Json> {
  let run: Json = {};
  for (let i = 0; i < polls; i++) {
    run = await call("pie_arrangement_status", { runId });
    if (states.includes(String(run.state)) || done(run)) break;
    await sleep(100);
  }
  return run;
}

// Walk forward for 1.5 s, then throttle for half a second from frame 180.
const clips = (): Json => ({
  walk: { performer: "player", rate: 60, length: 120, input: [{ action: ia.move, type: "axis2d", notes: [], keys: [{ id: "k1", t: 0, v: [0, 1], interp: "constant" }, { id: "k2", t: 90, v: [0, 0], interp: "constant" }] }] },
  throttle: { performer: "player", rate: 60, length: 40, input: [{ action: ia.throttle, type: "axis1d", notes: [{ id: "n1", start: 0, length: 30, value: 1 }], keys: [] }] },
});

function arrangement(checks: Json[]): Json {
  return {
    id: "hub-player",
    title: "Hub player",
    level: null,
    rate: 60,
    tracks: [{ id: "hero", title: "Hero", performer: { kind: "player", client: 0 }, mute: false, solo: false, instances: [{ id: "i1", clip: "walk", at: 0 }, { id: "i2", clip: "throttle", at: 180 }] }],
    checks,
    regions: [{ id: "walk", name: "Walk", start: 0, end: 120 }, { id: "throttle", name: "Throttle", start: 120, end: 260 }],
  };
}

const WALKED = { id: "walked", title: "Walked", at: 0, length: 100, mode: "by_end", expect: { value: "pos_x", op: "increasing", target: 200 } };
const THROTTLED = { id: "throttled", title: "Throttled", at: 215, length: 40, mode: "throughout", expect: { value: "ThrottleSum", op: "gt", target: 1.2 } };

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  ia = await setupProbeArena(bridge, DIR, "HubPlayer", { chaser: false });
}, 240_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
  } finally {
    await teardownProbeArena(bridge, DIR);
    disconnectBridge();
  }
}, 120_000);

describe("arrangement player", () => {
  it("drives the player, judges every check mode, waits on a manual verdict and ends its PIE", async () => {
    const checks = [
      WALKED,
      { id: "still", title: "At rest", at: 100, length: 0, timeout: 120, mode: "hold", expect: { value: "speed2d", op: "lt", target: 1 } },
      { id: "floor", title: "On the floor", at: 0, length: 200, mode: "throughout", expect: { value: "pos_z", op: "gt", target: -100000 } },
      THROTTLED,
      { id: "look", title: "Look", at: 260, length: 0, mode: "manual", instruction: "Did the character walk?" },
    ];
    const runId = "live-hub-full";
    const started = await call("pie_arrangement_play", { runId, arrangement: arrangement(checks), clips: clips() });
    expect(started.pieStarted).toBe(true);

    const waiting = await until(runId, ["awaiting_verdict", "finished", "stopped", "error"]);
    expect(waiting.state, JSON.stringify(waiting)).toBe("awaiting_verdict");
    expect(rows(waiting.checks).find((c) => c.id === "look")?.status).toBe("awaiting_verdict");
    // Paused while it waits: the frame stands still.
    const at = Number(waiting.frame);
    await sleep(500);
    expect(Number((await call("pie_arrangement_status", { runId })).frame)).toBeCloseTo(at, 0);
    await call("pie_arrangement_verdict", { runId, check: "look", verdict: "pass", note: "it walked" });

    const run = await until(runId, ["finished", "stopped", "error"]);
    expect(run.state, JSON.stringify(run)).toBe("finished");
    expect(rows(run.checks).map((c) => [c.id, c.status]), JSON.stringify(run.checks)).toEqual([
      ["walked", "passed"],
      ["floor", "passed"],
      ["still", "passed"],
      ["throttled", "passed"],
      ["look", "passed"],
    ]);
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("plays one region alone and skips the checks outside it", async () => {
    const runId = "live-hub-region";
    await call("pie_arrangement_play", { runId, arrangement: arrangement([WALKED, THROTTLED]), clips: clips(), region: "walk" });
    const run = await until(runId, ["finished", "stopped", "error"]);
    expect(run.state, JSON.stringify(run)).toBe("finished");
    expect(rows(run.checks).map((c) => [c.id, c.status])).toEqual([["walked", "passed"], ["throttled", "skipped"]]);
    expect(Number(run.frame)).toBeLessThan(150);
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("starts at the playhead, skipping the checks before it", async () => {
    const runId = "live-hub-playhead";
    await call("pie_arrangement_play", { runId, arrangement: arrangement([WALKED, THROTTLED]), clips: clips(), from: 170 });
    const first = await until(runId, ["running", "finished", "stopped", "error"], (r) => typeof r.frame === "number");
    expect(Number(first.frame), JSON.stringify(first)).toBeGreaterThanOrEqual(170);
    const run = await until(runId, ["finished", "stopped", "error"]);
    expect(run.state, JSON.stringify(run)).toBe("finished");
    expect(rows(run.checks).map((c) => [c.id, c.status]), JSON.stringify(run.checks)).toEqual([["walked", "skipped"], ["throttled", "passed"]]);
    expect(String(rows(run.checks)[0]!.message)).toContain("playhead");
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("loops a region until stopped, judging its checks again on each pass", async () => {
    const runId = "live-hub-loop";
    await call("pie_arrangement_play", { runId, arrangement: arrangement([WALKED]), clips: clips(), region: "walk", loop: true });
    const looped = await until(runId, ["finished", "stopped", "error"], (r) => Number(r.pass) >= 2 && Number(r.frame) > 20);
    expect(looped.state, JSON.stringify(looped)).toBe("running");
    expect(Number(looped.pass)).toBeGreaterThanOrEqual(2);
    expect(Number(looped.frame)).toBeLessThanOrEqual(120);
    await call("pie_arrangement_stop", { runId, reason: "the test stopped the loop" });
    expect((await call("pie_arrangement_status", { runId })).state).toBe("stopped");
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("stops a play on request and fails what had not settled", async () => {
    const runId = "live-hub-stop";
    await call("pie_arrangement_play", {
      runId,
      arrangement: arrangement([{ id: "never", title: "Never", at: 0, length: 0, timeout: 100000, mode: "hold", expect: { value: "pos_z", op: "lt", target: -100000 } }]),
      clips: clips(),
    });
    await until(runId, ["running"]);
    await call("pie_arrangement_stop", { runId, reason: "the test stopped it" });
    const run = await call("pie_arrangement_status", { runId });
    expect(run.state).toBe("stopped");
    expect(rows(run.checks)[0]).toMatchObject({ id: "never", status: "failed" });
    expect(String(rows(run.checks)[0]!.message)).toContain("the test stopped it");
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("punches a take into a clip's range and keeps what lies outside it", async () => {
    await stopPie();
    const clip = {
      performer: "player",
      rate: 60,
      length: 180,
      input: [{ action: ia.jump, type: "bool", notes: [{ id: "a", start: 10, length: 10 }, { id: "b", start: 80, length: 10 }, { id: "c", start: 150, length: 10 }], keys: [] }],
    };
    await call("pie_clip_record_start", { performer: { kind: "player", client: 0 }, punch: { clip, from: 60, to: 120 } });
    // The take ends at the punch-out; nobody plays, so the range comes back silent.
    for (let i = 0; i < 600; i++) {
      await sleep(100);
      if (!(await pieRunning()) && i > 20) break;
    }
    const take = (await call("pie_clip_record_stop", {})).clip as Json;
    expect(take.rate).toBe(60);
    expect(take.lanes).toBeUndefined();
    const notes = rows(rows(take.input).find((l) => String(l.action).includes("IA_ProbeJump"))?.notes);
    expect(notes.map((n) => n.start), JSON.stringify(take)).toEqual([10, 150]);
    expect(Number(take.length)).toBeGreaterThanOrEqual(180);
  }, 240_000);
});
