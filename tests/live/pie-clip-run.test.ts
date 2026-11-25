// @live-editor headless
/**
 * Playing playtest clips (#2097), the way an agent works: write a clip from nothing through the bridge, play it and
 * read which check notes passed, region by region; break it with one note-level edit, read why it failed, and fix it
 * with another; play one region; answer a manual check the run is paused on; stop a run early. The probe arena's
 * Charge action has a Hold trigger and its Throttle action a Scalar modifier, so a clip that fed either twice would
 * fail the checks on ChargeCount and ThrottleSum. PIE is stopped after every run.
 * Refs #2097
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { setupProbeArena, teardownProbeArena, type ProbeActions } from "./clip-fixture.js";

const DIR = "/Game/MCPTest/ClipRun";
const CLIP = `${DIR}/CL_Walk`;
const MANUAL = `${DIR}/CL_Manual`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);
const obj = (v: unknown): Json => (v && typeof v === "object" ? (v as Json) : {});
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;
let a: ProbeActions;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

async function stopPie(): Promise<void> {
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    for (let i = 0; i < 60 && (await pieRunning()); i++) await sleep(500);
  }
}

async function until(runId: string, done: (s: Json) => boolean, tries = 600): Promise<Json> {
  let status: Json = {};
  for (let i = 0; i < tries; i++) {
    status = await call("pie_clip_status", { run_id: runId });
    if (done(status)) return status;
    await sleep(100);
  }
  return status;
}

const ended = (s: Json) => s.state === "finished" || s.state === "stopped";
const check = (s: Json, id: string): Json => rows(s.checks).find((c) => c.id === id) ?? {};
const region = (s: Json, id: string): Json => rows(s.regions).find((r) => r.id === id) ?? {};

async function play(path: string, extra: Json = {}): Promise<Json> {
  await stopPie();
  const started = await call("pie_clip_run", { path, start_pie: true, ...extra });
  expect(started.state).toBe("armed");
  const status = await until(String(started.run_id), ended);
  await stopPie();
  return status;
}

// What an agent writes: walk forward for 1.5 s, wait until the character is at rest, hold Charge past its Hold
// trigger, then Throttle for half a second. Times are frames at 60 fps.
const walkClip = () => [
  "clip rate=60 length=260",
  `region r1 0 120 "Walk forward"`,
  `region r2 120 260 "Charge and throttle"`,
  `track ${a.move} axis2d`,
  "  key k1 0 0,1 constant",
  "  key k2 90 0,0 constant",
  `track ${a.charge} bool`,
  "  note n1 130 45",
  `track ${a.throttle} axis1d`,
  "  note n2 180 30 1",
  `check c1 0 100 by_end pos_x increasing 200 name="Walks forward"`,
  "check c2 100 120 hold speed2d lt 1",
  "check c3 130 60 by_end ChargeCount eq 1 resolve_in_pie=true",
  "check c4 215 40 throughout ThrottleSum gt 1.2 resolve_in_pie=true",
  "check c5 215 40 throughout ThrottleSum lt 1.3 resolve_in_pie=true",
].join("\n");

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await callBridge(bridge, "pie_clip_stop", {});
  a = await setupProbeArena(bridge, DIR, "ClipRun");
  await call("pie_clip_create", { name: "CL_Walk", package_path: DIR, text: walkClip() });
}, 180_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await callBridge(bridge, "pie_clip_stop", {});
    await stopPie();
  } finally {
    await teardownProbeArena(bridge, DIR);
    disconnectBridge();
  }
}, 120_000);

describe("an agent's clip, written from nothing", () => {
  it("plays on a fixed timestep and every check passes, region by region, each action's trigger and modifier run once", async () => {
    const s = await play(CLIP);
    expect(s.state, JSON.stringify(s)).toBe("finished");
    expect(s.fixed_timestep).toBe(true);
    for (const id of ["c1", "c2", "c3", "c4", "c5"]) expect(check(s, id).status, `${id}: ${JSON.stringify(check(s, id))}`).toBe("passed");
    expect(region(s, "r1").status).toBe("passed");
    expect(region(s, "r2").status).toBe("passed");
    expect(s.passed).toBe(true);
    expect(obj(check(s, "c2").hold).resolved_frame).toBeDefined();
    expect(Number(s.position)).toBeGreaterThanOrEqual(260);
  }, 120_000);
});

describe("fixing a failing clip", () => {
  it("one edit breaks it, the results say which check failed in which region and why, and one edit fixes it", async () => {
    const broken = await call("pie_clip_edit", { path: CLIP, ops: [{ op: "move", id: "k2", to: 12 }] });
    expect(rows(broken.changed)).toEqual(["k2"]);
    const fail = await play(CLIP);
    expect(check(fail, "c1").status).toBe("failed");
    expect(String(obj(check(fail, "c1").evidence).reason)).toContain("by frame 100");
    expect(region(fail, "r1").status).toBe("failed");
    expect(region(fail, "r2").status).toBe("passed");
    expect(fail.passed).toBe(false);

    await call("pie_clip_edit", { path: CLIP, ops: [{ op: "move", id: "k2", to: 90 }] });
    const pass = await play(CLIP);
    expect(check(pass, "c1").status).toBe("passed");
    expect(pass.passed).toBe(true);
  }, 240_000);

  it("region plays from frame 0 but judges only that region and stops at its end", async () => {
    const s = await play(CLIP, { region: "r1" });
    expect(s.region).toBe("r1");
    expect(check(s, "c1").status).toBe("passed");
    expect(check(s, "c3").status).toBe("skipped");
    expect(Number(s.position)).toBeLessThan(200);
    expect(String(s.stop_reason)).toContain("r1");
  }, 120_000);
});

describe("a manual check", () => {
  it("pauses the run until clip_verdict answers it, and an agent verdict needs evidence", async () => {
    await call("pie_clip_create", {
      name: "CL_Manual",
      package_path: DIR,
      clip: { rate: 60, regions: [{ id: "r1", name: "Look", start: 0, end: 60 }], checks: [{ id: "m1", start: 30, length: 0, manual: true, instruction: "Is the character standing?" }] },
    });
    await stopPie();
    const started = await call("pie_clip_run", { path: MANUAL, start_pie: true });
    const runId = String(started.run_id);
    const waiting = await until(runId, (s) => rows(s.awaiting).length > 0);
    expect(rows(waiting.awaiting)).toEqual(["m1"]);
    expect(waiting.holding).toBe("m1");
    const at = Number(waiting.position);
    await sleep(500);
    expect(Number((await call("pie_clip_status", { run_id: runId })).position)).toBe(at);

    const bare = await callBridge(bridge, "pie_clip_verdict", { check: "m1", verdict: "pass", source: "agent" });
    expect(body(bare).success).toBe(false);
    const answered = await call("pie_clip_verdict", { check: "m1", verdict: "pass", source: "agent", evidence: { note: "The character stands on the floor." } });
    expect(check(answered, "m1").status).toBe("passed");
    expect(check(answered, "m1").verdict_source).toBe("agent");
    const s = await until(runId, ended);
    expect(s.passed).toBe(true);
    await stopPie();
  }, 120_000);

  it("clip_stop ends a run early and fails the check it was waiting on", async () => {
    await stopPie();
    const started = await call("pie_clip_run", { path: MANUAL, start_pie: true });
    await until(String(started.run_id), (s) => rows(s.awaiting).length > 0);
    const stopped = await call("pie_clip_stop", { reason: "enough" });
    expect(stopped.state).toBe("stopped");
    expect(stopped.stop_reason).toBe("enough");
    expect(check(stopped, "m1").status).toBe("failed");
    await stopPie();
  }, 120_000);
});
