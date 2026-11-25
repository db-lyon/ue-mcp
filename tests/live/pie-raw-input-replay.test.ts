// @live-editor headless
/**
 * A recording replays each Input Action's raw value (#2097): the value that reached the action before its own
 * modifiers and triggers, fed back on the frame it was read. The probe character's autopilot plays through the
 * player's own input (keys and mapping modifiers, a Hold-triggered Charge, a Throttle with a 2.5 Scalar modifier);
 * a replay at a fixed timestep must charge as often and throttle as far as the source did, and keep the character
 * within the drift thresholds. Fed the final value instead, the Scalar applies twice and the Hold trigger eats the
 * press. PIE is stopped after every session.
 * Refs #2097
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { setupProbeArena, teardownProbeArena } from "./clip-fixture.js";

const DIR = "/Game/MCPTest/RawReplay";
const ID = "raw_input_replay";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const obj = (v: unknown): Json => (v && typeof v === "object" ? (v as Json) : {});
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;

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

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await setupProbeArena(bridge, DIR, "RawReplay");
}, 180_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
    await callBridge(bridge, "pie_record_delete", { id: ID, confirm: true });
  } finally {
    await teardownProbeArena(bridge, DIR);
    disconnectBridge();
  }
}, 120_000);

describe("replaying raw Input Action values", () => {
  it("records the raw value, and a fixed-timestep replay runs each action's trigger and modifier once, within the drift thresholds", async () => {
    await callBridge(bridge, "pie_record_delete", { id: ID, confirm: true });
    await call("pie_record_arm", { id: ID, sample_hz: 60, track_values: ["ChargeCount", "ThrottleSum", "JumpCount"] });
    await call("pie_control", { pieAction: "start" });
    for (let i = 0; i < 120; i++) {
      if (String(obj(await call("pie_record_status", {})).state) === "recording") break;
      await sleep(250);
    }
    await call("invoke_function", { world: "pie", actorLabel: "ProbeCharacter", functionName: "StartAutopilot", args: { StartAtSeconds: 0 } });
    await sleep(7500);
    await stopPie();

    const manifest = JSON.parse(String((await call("pie_record_read", { id: ID, file: "manifest" })).content));
    expect(manifest.input_values).toBe("raw");

    await call("pie_replay_run", { recording_id: ID, fixed_timestep: true, settle_ms: 0 });
    for (let i = 0; i < 240; i++) {
      const s = await call("pie_replay_status", {});
      if (s.pie_active === false && s.state !== "replaying" && s.state !== "waiting_for_pawn" && s.state !== "armed") break;
      await sleep(250);
    }
    const drift = JSON.parse(String((await call("pie_record_read", { id: ID, file: "drift" })).content));
    const deltas = obj(drift.tracked_value_max_deltas);
    // 7.5 s of autopilot: one Charge (Hold trigger at 0.5 s) and 1.5 s of Throttle (Scalar 2.5). A counter compared
    // frame by frame differs by at most 1 on the frame an event lands a frame apart; the Scalar applied twice would
    // put ThrottleSum 2.5 times too high.
    expect(Number(deltas.ChargeCount), JSON.stringify(deltas)).toBeLessThanOrEqual(1);
    expect(Number(deltas.JumpCount), JSON.stringify(deltas)).toBeLessThanOrEqual(1);
    expect(Number(deltas.ThrottleSum), JSON.stringify(deltas)).toBeLessThan(0.05);
    expect(Math.abs(Number(obj(drift.input_onset).offset_ms))).toBeLessThan(17);
    expect(Number(drift.max_position_drift_cm), JSON.stringify(drift.summary)).toBeLessThan(5);
  }, 180_000);
});
