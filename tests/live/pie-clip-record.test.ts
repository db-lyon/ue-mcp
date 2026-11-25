// @live-editor headless
/**
 * Recording into a playtest clip (#2097): clip_record plays the clip to the punch-in with the person's input locked
 * out, then records the armed tracks from what they play. The probe's autopilot stands in for the person: it presses
 * keys through the player's own input, mapping modifiers included. The take becomes notes and curve keys, the clip's
 * start pose and drift reference; replaying it measures its drift against the take. A second take over one region
 * replaces only that region and keeps what it held as history. PIE is stopped after every run.
 * Refs #2097
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { setupProbeArena, teardownProbeArena, type ProbeActions } from "./clip-fixture.js";

const DIR = "/Game/MCPTest/ClipRecord";
const TAKE = `${DIR}/CL_Take`;

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

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await callBridge(bridge, "pie_clip_stop", {});
  a = await setupProbeArena(bridge, DIR, "ClipRecord");
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

describe("recording a clip", () => {
  it("records the person's input as notes and curves, and the take replays against itself within the drift thresholds", async () => {
    await call("pie_clip_create", { name: "CL_Take", package_path: DIR, text: "clip rate=60\ncheck c1 0 0 by_end pos_z gt -100000\n" });
    await stopPie();
    const rec = await call("pie_clip_record", { path: TAKE, armed: [a.move, a.jump, a.charge, a.throttle] });
    expect(rec.mode).toBe("record");
    const runId = String(rec.run_id);
    await until(runId, (s) => s.state === "running" && obj(s.take).punched_in === true);
    await call("invoke_function", { world: "pie", actorLabel: "ProbeCharacter", functionName: "StartAutopilot", args: { StartAtSeconds: 0 } });
    await sleep(7000);
    const stopped = await call("pie_clip_record_stop", {});
    expect(obj(stopped.take).saved, JSON.stringify(stopped.take)).toBe(true);
    await stopPie();

    const clip = obj((await call("pie_clip_read", { path: TAKE })).clip);
    const tracks = rows(clip.tracks);
    expect(rows(tracks.find((t) => t.action === a.move)?.keys).length).toBeGreaterThan(1);
    expect(rows(tracks.find((t) => t.action === a.charge)?.notes).length).toBeGreaterThan(0);
    expect(rows(tracks.find((t) => t.action === a.jump)?.notes).length).toBeGreaterThan(0);
    expect(String(clip.reference)).toBe(runId);
    expect(obj(clip.start).location).toBeDefined();

    const started = await call("pie_clip_run", { path: TAKE, start_pie: true });
    const s = await until(String(started.run_id), ended);
    await stopPie();
    expect(s.state).toBe("finished");
    const drift = obj(s.drift);
    expect(drift.compared, JSON.stringify(drift)).toBe(true);
    expect(Number(drift.frames_compared)).toBeGreaterThan(100);
    expect(Number(drift.max_position_cm), JSON.stringify(drift)).toBeLessThan(5);
  }, 240_000);

  it("a take over one region replaces only that region's armed tracks and keeps the old notes as history", async () => {
    const before = obj((await call("pie_clip_read", { path: TAKE })).clip);
    const length = Number(before.effective_length);
    await call("pie_clip_edit", { path: TAKE, ops: [{ op: "add", kind: "region", name: "First", start: 0, end: 120 }, { op: "add", kind: "region", name: "Rest", start: 120, end: Math.max(240, length) }] });
    const moveKeys = (c: Json) => rows(rows(c.tracks).find((t) => t.action === a.move)?.keys);
    const moving = (k: Json) => rows(k.v).some((x) => Number(x) !== 0);
    expect(moveKeys(before).filter((k) => Number(k.t) < 120 && moving(k)).length, "the first take walks in r1").toBeGreaterThan(0);
    const laterKeys = JSON.stringify(moveKeys(before).filter((k) => Number(k.t) >= 120));

    await stopPie();
    const rec = await call("pie_clip_record", { path: TAKE, region: "r1", armed: [a.move] });
    const take = obj(rec.take);
    expect(take.punch_in).toBe(0);
    expect(take.punch_out).toBe(120);
    // Nobody touches the stick this time: the region's Move keys are replaced by standing still, and the run ends
    // at the punch-out.
    const s = await until(String(rec.run_id), ended);
    await stopPie();
    expect(s.stop_reason).toBe("take recorded");
    expect(obj(s.take).saved).toBe(true);

    const after = obj((await call("pie_clip_read", { path: TAKE })).clip);
    expect(moveKeys(after).filter((k) => Number(k.t) < 120 && moving(k)).length).toBe(0);
    expect(JSON.stringify(moveKeys(after).filter((k) => Number(k.t) >= 120).map((k) => ({ ...k, id: undefined }))))
      .toBe(JSON.stringify(JSON.parse(laterKeys).map((k: Json) => ({ ...k, id: undefined }))));
    const history = rows(after.takes);
    expect(history.length).toBe(1);
    expect(rows(rows(history[0].tracks).find((t) => t.action === a.move)?.keys).some(moving)).toBe(true);
  }, 240_000);
});
