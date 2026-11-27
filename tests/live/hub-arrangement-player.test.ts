// @live-editor headless
/**
 * The plugin's arrangement player (#2146), through the bridge as the hub calls it: pie(arrangement_play) plays an
 * arrangement whose tracks drive the editor's player and an AI-controlled pawn, judges the Checks track in each mode
 * (a manual check pauses PIE until arrangement_verdict), plays one region alone, and ends the PIE session it started.
 * pie(clip_record_start) with a punch plays a clip's lead-in and replaces the punched range with the take, which
 * pie(clip_record_stop) answers. The pawn is a DefaultPawn placed in the probe arena, which an AI controller
 * possesses in PIE.
 * Refs #2146
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { setupProbeArena, teardownProbeArena, type ProbeActions } from "./clip-fixture.js";

const DIR = "/Game/MCPTest/HubPlayer";
const GUARD = "UEMCP_Guard";
// The arena sits at (0, 60000, 4000); the guard stands beside the probe character, on the arena's floor.
const START = { x: 400, y: 60400, z: 4100 };

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

/** Polls arrangement_status until the run reaches one of `states`. */
async function until(runId: string, states: string[], polls = 1200): Promise<Json> {
  let run: Json = {};
  for (let i = 0; i < polls; i++) {
    run = await call("pie_arrangement_status", { runId });
    if (states.includes(String(run.state))) break;
    await sleep(100);
  }
  return run;
}

const guardClip = (): Json => ({
  performer: "actor",
  rate: 60,
  length: 240,
  input: [],
  lanes: [
    { id: "walk", kind: "move", points: [{ id: "p1", t: 0, at: [START.x + 600, START.y, START.z], radius: 20 }] },
    { id: "grow", kind: "action", calls: [{ id: "a1", t: 30, fn: "SetActorScale3D", args: ["(X=2,Y=2,Z=2)"] }] },
    { id: "life", kind: "value", target: "InitialLifeSpan", type: "axis1d", keys: [{ id: "k1", t: 0, v: 0 }, { id: "k2", t: 60, v: 42 }] },
  ],
});

const jumpClip = (): Json => ({
  performer: "player",
  rate: 60,
  length: 60,
  input: [{ action: ia.jump, type: "bool", notes: [{ id: "n1", start: 10, length: 5 }], keys: [] }],
  lanes: [],
});

function arrangement(checks: Json[]): Json {
  return {
    id: "hub-player",
    title: "Hub player",
    level: null,
    rate: 60,
    tracks: [
      { id: "player", title: "Player", performer: { kind: "player", client: 0 }, mute: false, solo: false, instances: [{ id: "i1", clip: "jump", at: 0 }] },
      { id: "guard", title: "Guard", performer: { kind: "actor", actor: GUARD }, mute: false, solo: false, instances: [{ id: "i1", clip: "guard", at: 0 }] },
    ],
    checks,
    regions: [{ id: "early", name: "Early", start: 0, end: 120 }],
  };
}

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  ia = await setupProbeArena(bridge, DIR, "HubPlayer", { chaser: false });
  await call("place_actor", { actorClass: "DefaultPawn", label: GUARD, location: START });
  await call("save_level", {});
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
  it("drives a player and an AI pawn, judges every check mode, waits on a manual verdict and ends its PIE", async () => {
    const checks = [
      { id: "walked", title: "Walked", at: 0, length: 240, mode: "by_end", expect: { value: "RootComponent.RelativeLocation.X", op: "increasing", target: 300, subject: GUARD } },
      { id: "grown", title: "Grown", at: 0, length: 120, mode: "by_end", expect: { value: `${GUARD}.RootComponent.RelativeScale3D.X`, op: "gt", target: 1.5 } },
      { id: "floor", title: "On the floor", at: 0, length: 200, mode: "throughout", expect: { value: "pos_z", op: "gt", target: -100000 } },
      // A hold stands the whole timeline still, value lanes too, so it waits past the lane's last key.
      { id: "life", title: "Life span set", at: 70, length: 0, timeout: 120, mode: "hold", expect: { value: `${GUARD}.InitialLifeSpan`, op: "gt", target: 40 } },
      { id: "look", title: "Look", at: 200, length: 0, mode: "manual", instruction: "Did the guard walk?" },
    ];
    const runId = "live-hub-full";
    const started = await call("pie_arrangement_play", { runId, arrangement: arrangement(checks), clips: { guard: guardClip(), jump: jumpClip() } });
    expect(started.pieStarted).toBe(true);

    const waiting = await until(runId, ["awaiting_verdict", "finished", "stopped", "error"]);
    expect(waiting.state, JSON.stringify(waiting)).toBe("awaiting_verdict");
    expect(rows(waiting.checks).find((c) => c.id === "look")?.status).toBe("awaiting_verdict");
    expect(Number(waiting.frame)).toBeCloseTo(200, 0);
    // Paused while it waits: the frame stands still.
    await sleep(500);
    expect(Number((await call("pie_arrangement_status", { runId })).frame)).toBeCloseTo(200, 0);
    await call("pie_arrangement_verdict", { runId, check: "look", verdict: "pass", note: "it walked" });

    const run = await until(runId, ["finished", "stopped", "error"]);
    expect(run.state, JSON.stringify(run)).toBe("finished");
    expect(rows(run.checks).map((c) => [c.id, c.status]), JSON.stringify(run.checks)).toEqual([
      ["walked", "passed"],
      ["grown", "passed"],
      ["floor", "passed"],
      ["life", "passed"],
      ["look", "passed"],
    ]);
    expect(run.laneErrors, JSON.stringify(run)).toBeUndefined();
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("plays one region alone and skips the checks outside it", async () => {
    const checks = [
      { id: "grown", title: "Grown", at: 0, length: 110, mode: "by_end", expect: { value: `${GUARD}.RootComponent.RelativeScale3D.X`, op: "gt", target: 1.5 } },
      { id: "late", title: "Late", at: 150, length: 10, mode: "by_end", expect: { value: "pos_z", op: "lt", target: -100000 } },
    ];
    const runId = "live-hub-region";
    await call("pie_arrangement_play", { runId, arrangement: arrangement(checks), clips: { guard: guardClip(), jump: jumpClip() }, region: "early" });
    const run = await until(runId, ["finished", "stopped", "error"]);
    expect(run.state, JSON.stringify(run)).toBe("finished");
    expect(rows(run.checks).map((c) => [c.id, c.status])).toEqual([["grown", "passed"], ["late", "skipped"]]);
    expect(Number(run.frame)).toBeLessThan(150);
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("stops a play on request and fails what had not settled", async () => {
    const runId = "live-hub-stop";
    await call("pie_arrangement_play", {
      runId,
      arrangement: arrangement([{ id: "never", title: "Never", at: 0, length: 0, timeout: 100000, mode: "hold", expect: { value: "pos_z", op: "lt", target: -100000 } }]),
      clips: { guard: guardClip(), jump: jumpClip() },
    });
    await until(runId, ["running"]);
    await call("pie_arrangement_stop", { runId, reason: "the test stopped it" });
    const run = await call("pie_arrangement_status", { runId });
    expect(run.state).toBe("stopped");
    expect(rows(run.checks)[0]).toMatchObject({ id: "never", status: "failed" });
    expect(String(rows(run.checks)[0].message)).toContain("the test stopped it");
    expect(await waitPieStopped()).toBe(true);
  }, 240_000);

  it("punches a take into a clip's range and keeps what lies outside it", async () => {
    await stopPie();
    const clip = {
      performer: "player",
      rate: 60,
      length: 180,
      input: [{ action: ia.jump, type: "bool", notes: [{ id: "a", start: 10, length: 10 }, { id: "b", start: 80, length: 10 }, { id: "c", start: 150, length: 10 }], keys: [] }],
      lanes: [],
    };
    await call("pie_clip_record_start", { performer: { kind: "player", client: 0 }, kind: "player", punch: { clip, from: 60, to: 120 } });
    // The take ends at the punch-out; nobody plays, so the range comes back silent.
    let answer: Json = {};
    for (let i = 0; i < 600; i++) {
      await sleep(100);
      if (!(await pieRunning()) && i > 20) break;
    }
    answer = await call("pie_clip_record_stop", {});
    const take = answer.clip as Json;
    expect(take.rate).toBe(60);
    const notes = rows(rows(take.input).find((l) => String(l.action).includes("IA_ProbeJump"))?.notes);
    expect(notes.map((n) => n.start), JSON.stringify(take)).toEqual([10, 150]);
    expect(Number(take.length)).toBeGreaterThanOrEqual(180);
  }, 240_000);
});
