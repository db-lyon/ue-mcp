// @live-editor headless
/**
 * PIE runtime controls (#1215, #1203, #1250): pausing, the player's view
 * target and the debug camera, each read back from the running session.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const CAMERA = "PIE_ViewTargetProbe";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let playing = false;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  const status = await callBridge(bridge, "pie_control", { pieAction: "status" });
  return body(status).isPlaying === true;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "demo_go_home", {});
  await callBridge(bridge, "pie_control", { pieAction: "start" });
  for (let i = 0; i < 30 && !playing; i++) {
    playing = await pieRunning();
    if (!playing) await new Promise((r) => setTimeout(r, 1000));
  }
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    for (let i = 0; i < 20 && (await pieRunning()); i++) await new Promise((r) => setTimeout(r, 500));
    disconnectBridge();
  }
});

describe("PIE runtime controls", () => {
  it("set_pie_paused unpauses a game the console paused, and reports the previous state (#1215)", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    await call("execute_command", { command: "pause", world: "pie" });
    const resumed = await call("set_pie_paused", { paused: false });
    expect(resumed.previousGamePaused).toBe(true);
    expect(resumed.gamePaused).toBe(false);
    const again = await call("set_pie_paused", { paused: false });
    expect(again.previousGamePaused).toBe(false);
  }, 60_000);

  it("set_pie_view_target makes a spawned camera the player's view target (#1203)", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    await call("place_actor", { actorClass: "CameraActor", label: CAMERA, location: { x: -600, y: 0, z: 300 }, world: "pie" });
    const set = await call("set_pie_view_target", { actorLabel: CAMERA });
    expect(String(set.viewTarget)).toContain("CameraActor");
    const rollback = set.rollback as { method: string; payload: Json };
    const restored = await call(rollback.method, rollback.payload);
    expect(restored.viewTarget).toBe(set.previousViewTarget);
  }, 60_000);

  it("pose_debug_camera places the debug camera and hands control back (#1250)", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const posed = await call("pose_debug_camera", { location: { x: 0, y: -300, z: 200 }, rotation: { pitch: -10, yaw: 90, roll: 0 } });
    expect(posed.enabled).toBe(true);
    const at = posed.cameraLocation as { x: number; y: number; z: number };
    expect(Math.abs(at.x - 0) + Math.abs(at.y + 300) + Math.abs(at.z - 200)).toBeLessThan(5);
    const off = await call("pose_debug_camera", { enabled: false });
    expect(off.enabled).toBe(false);
  }, 60_000);
});
