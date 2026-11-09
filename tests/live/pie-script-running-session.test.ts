// @live-editor headless
/**
 * script_run with start_pie on a PIE session that is already running (#1625):
 * a pawn without an input component is still observed, and a controller with
 * no pawn ends the run with the reason instead of waiting forever.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
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

/** Starts a fresh PIE session and waits until the player pawn can be read. */
async function freshPie(): Promise<void> {
  await stopPie();
  await call("pie_control", { pieAction: "start" });
  for (let i = 0; i < 120; i++) {
    const r = await callBridge(bridge, "get_object_properties", { world: "pie", target: "playerpawn", propertyNames: ["CustomTimeDilation"] });
    if (succeeded(r)) return;
    await sleep(250);
  }
  throw new Error("PIE started but no player pawn could be read");
}

async function until(runId: string, done: (s: Json) => boolean, tries: number): Promise<Json> {
  let status: Json = {};
  for (let i = 0; i < tries; i++) {
    status = await call("pie_script_status", { run_id: runId });
    if (done(status)) return status;
    await sleep(250);
  }
  return status;
}

const STEPS = [{ instruction: "Time dilation starts below 2", judge: "auto", expect: { value: "CustomTimeDilation", op: "lt", target: 2 } }];

beforeAll(async () => {
  bridge = await getBridge();
}, 60_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await callBridge(bridge, "pie_script_stop", {});
    await stopPie();
  } finally {
    disconnectBridge();
  }
}, 60_000);

describe("script_run into a running PIE session", () => {
  it("observes a pawn that has no input component", async () => {
    await freshPie();
    await call("set_object_property", { world: "pie", target: "playerpawn", propertyName: "InputComponent", value: null });

    const started = await call("pie_script_run", { name: "running_session_no_input", start_pie: true, record: false, steps: STEPS });
    expect(started.pie_started).not.toBe(true);
    const runId = String(started.run_id);

    const finished = await until(runId, (s) => s.state === "finished" || s.state === "stopped", 80);
    expect(finished.state).toBe("finished");
    expect(finished.stop_reason).toBe("all steps settled");
    expect((finished.steps as Json[])[0].status).toBe("passed");
    await stopPie();
  }, 60_000);

  it("ends the run with the reason when the controller possesses no pawn", async () => {
    await freshPie();
    await call("invoke_object_function", { world: "pie", target: "playercontroller", functionName: "UnPossess" });

    const started = await call("pie_script_run", { name: "running_session_no_pawn", start_pie: true, record: false, steps: STEPS });
    const runId = String(started.run_id);

    const waiting = await until(runId, (s) => typeof s.waiting_for === "string", 40);
    expect(waiting.state).toBe("armed");
    expect(String(waiting.waiting_for)).toMatch(/possesses no pawn/);

    const finished = await until(runId, (s) => s.state === "finished" || s.state === "stopped", 200);
    expect(finished.state).toBe("finished");
    expect(String(finished.stop_reason)).toMatch(/^No pawn to observe after 30 s in .*possesses no pawn/);
    expect(finished.waiting_for).toBeUndefined();
    expect((finished.steps as Json[])[0].status).not.toBe("passed");
    await stopPie();
  }, 90_000);
});
