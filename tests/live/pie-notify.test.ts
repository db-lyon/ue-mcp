// @live-editor headless
/**
 * The editor pushes notifications/pie to every connected client when a Play
 * session starts and ends, however it was started. The session here is begun
 * and ended through LevelEditorSubsystem's play requests, the path the toolbar's
 * Play button takes, never through pie_control, so the frames prove the bridge
 * sees a session it did not start. The daemon's mapping of those frames and of
 * the status read it resyncs from is run over the real answers too.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { BridgeNotification, EditorBridge } from "../../src/bridge/bridge.js";
import { pieEventFromNotification, pieEventFromStatus } from "../../src/daemon/pie-events.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;
const frames: BridgeNotification[] = [];
let unsubscribe: (() => void) | null = null;

async function python(code: string): Promise<void> {
  const r = await callBridge(bridge, "execute_python", { code });
  expect(r.ok && body(r).success !== false, `execute_python: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
}

async function status(): Promise<Json> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" }));
}

async function waitForFrame(state: string, timeoutMs = 60_000): Promise<BridgeNotification> {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const hit = frames.find((f) => f.method === "notifications/pie" && f.params.state === state);
    if (hit) return hit;
    if (Date.now() > deadline) throw new Error(`no notifications/pie ${state} within ${timeoutMs}ms; saw ${JSON.stringify(frames)}`);
    await new Promise((r) => setTimeout(r, 100));
  }
}

const LES = "unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)";

beforeAll(async () => {
  bridge = await getBridge();
  if ((await status()).isPlaying === true) {
    await python(`import unreal\n${LES}.editor_request_end_play()`);
    for (let i = 0; i < 60 && (await status()).isPlaying === true; i++) await new Promise((r) => setTimeout(r, 500));
  }
  unsubscribe = bridge.onNotification((n) => frames.push(n));
}, 60_000);

afterAll(async () => {
  unsubscribe?.();
  if ((await status()).isPlaying === true) await callBridge(bridge, "pie_control", { pieAction: "stop" });
  disconnectBridge();
}, 60_000);

describe("notifications/pie", () => {
  it("pushes started and ended for a session begun and ended outside the bridge's pie actions", async () => {
    const idle = pieEventFromStatus("Live", await status());
    expect(idle).toMatchObject({ state: "ended", resync: true, simulate: false });

    await python(`import unreal\n${LES}.editor_request_begin_play()`);
    const started = await waitForFrame("started");
    expect(started.params).toMatchObject({ state: "started", simulate: false });
    expect(Number.isNaN(Date.parse(String(started.params.at)))).toBe(false);
    expect(pieEventFromNotification("Live", started)).toMatchObject({ editor: "Live", state: "started", simulate: false });

    const playing = await status();
    expect(playing).toMatchObject({ isPlaying: true, isSimulating: false, isPaused: false });
    expect(pieEventFromStatus("Live", playing)).toMatchObject({ state: "started", paused: false, resync: true });

    await python(`import unreal\n${LES}.editor_request_end_play()`);
    const ended = await waitForFrame("ended");
    expect(ended.params).toMatchObject({ state: "ended", simulate: false });
    expect(pieEventFromNotification("Live", ended)?.state).toBe("ended");

    const states = frames.filter((f) => f.method === "notifications/pie").map((f) => f.params.state);
    expect(states.indexOf("started")).toBeLessThan(states.indexOf("ended"));
  }, 120_000);
});
