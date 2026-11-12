// @live-editor headless
/**
 * A test script step that waits for the person playing to fire an Input Action: expect {op: "triggered"}. The
 * step's own act presses the action first, which must not pass it; the press that does comes from outside the
 * script, through pie(inject_input), standing in for the person at the controls. The step records when the action
 * fired and its value. A second triggered step nobody fires fails at its timeout. Also covers the draft-time check
 * that the named Input Action exists. PIE is stopped after the run.
 * Refs #1897
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Pie/Triggered";
const IA_PACKAGE = `${DIR}/IA_BCovTrigInteract`;
const IA = `${IA_PACKAGE}.IA_BCovTrigInteract`;
const IA_TYPO = `${DIR}/IA_BCovTrigInteractt.IA_BCovTrigInteractt`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);
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

async function startPie(): Promise<boolean> {
  await call("pie_control", { pieAction: "start" });
  for (let i = 0; i < 120; i++) {
    if (await pieRunning()) {
      const pawn = await callBridge(bridge, "get_pie_pawn", {});
      if (succeeded(pawn) && body(pawn).path) return true;
    }
    await sleep(250);
  }
  return false;
}

/** Polls a run until done holds or the tries run out. */
async function until(runId: string, done: (s: Json) => boolean, tries = 120): Promise<Json> {
  let status: Json = {};
  for (let i = 0; i < tries; i++) {
    status = await call("pie_script_status", { run_id: runId });
    if (done(status)) return status;
    await sleep(100);
  }
  return status;
}

const stepOf = (s: Json, i: number): Json => rows(s.steps)[i] ?? {};

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_input_action", { name: "IA_BCovTrigInteract", packagePath: DIR });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
  } finally {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("draft-time check on a triggered step", () => {
  it("script_validate reports an Input Action that does not exist with candidates, and takes a package path", async () => {
    const bad = await call("pie_script_validate", {
      steps: [{ instruction: "Press interact", expect: { op: "triggered", value: IA_TYPO } }],
    });
    expect(bad.valid).toBe(false);
    const issue = rows(bad.errors).find((e) => e.step === 0 && e.field === "expect.value");
    expect(issue, JSON.stringify(bad.errors)).toBeDefined();
    expect(String(issue?.message)).toMatch(/Input Action/);
    expect(rows(issue?.candidates).map(String)).toContain(IA);

    const good = await call("pie_script_validate", {
      steps: [{ instruction: "Press interact", expect: { op: "triggered", value: IA_PACKAGE, client: 0, within_ms: 600000 } }],
    });
    expect(good.valid, JSON.stringify(good.errors)).toBe(true);
    expect(obj(rows(good.steps)[0]?.expect)).toMatchObject({ op: "triggered", value: IA, within_ms: 600000 });
  }, 20_000);
});

describe("a step that waits for the person to fire an Input Action", () => {
  it("passes on a press from outside the script after act, not on act's own press, and records when and what", async () => {
    expect(await startPie(), "PIE did not start").toBe(true);
    const started = await call("pie_script_run", {
      name: "bcov_triggered",
      record: false,
      steps: [
        {
          instruction: "Press Interact when you are ready",
          act: [{ press: { action: IA } }],
          expect: { op: "triggered", value: IA_PACKAGE },
        },
        {
          instruction: "Press Interact again within a second and a half",
          expect: { op: "triggered", value: IA, within_ms: 1500 },
        },
      ],
    });
    const runId = String(started.run_id);
    expect(runId).toMatch(/^script_bcov_triggered_/);

    // The act pressed the action; the step keeps waiting on the person.
    const waiting = await until(runId, (s) => obj(stepOf(s, 0).act_result).status === "done");
    expect(obj(stepOf(waiting, 0).act_result).status).toBe("done");
    await sleep(750);
    const still = await call("pie_script_status", { run_id: runId });
    expect(stepOf(still, 0).status, JSON.stringify(stepOf(still, 0))).toBe("running");
    expect(stepOf(still, 0).within_ms).toBe(0);
    expect(obj(stepOf(still, 0).evidence).observed).toBe(true);

    // The person presses it.
    await call("pie_inject_input", { action_path: IA, value_x: 1 });
    const passed = await until(runId, (s) => stepOf(s, 0).status !== "running");
    const step = stepOf(passed, 0);
    expect(step.status, JSON.stringify(step)).toBe("passed");
    const evidence = obj(step.evidence);
    const actEnded = Number(obj(step.act_result).ended_at_ms);
    expect(Number(evidence.triggered_at_ms)).toBeGreaterThan(actEnded + 500);
    expect(evidence.action_value).toBe(true);
    expect(evidence.value_type).toBe("bool");

    // Nobody presses for the second step, which fails at its timeout and ends the run.
    const finished = await until(runId, (s) => s.state === "finished", 80);
    expect(finished.state).toBe("finished");
    const missed = stepOf(finished, 1);
    expect(missed.status).toBe("failed");
    expect(String(obj(missed.evidence).reason)).toMatch(/was not triggered by client 0 within 1500 ms/);
    expect(obj(missed.evidence).triggered_at_ms).toBeUndefined();

    const passedEvent = rows(finished.events).find((e) => e.type === "step_passed" && e.step === 0);
    expect(Number(obj(obj(passedEvent?.data).evidence).triggered_at_ms)).toBe(Number(evidence.triggered_at_ms));
    await stopPie();
  }, 20_000);
});
