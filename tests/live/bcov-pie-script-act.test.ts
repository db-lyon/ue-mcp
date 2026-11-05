// @live-editor headless
/**
 * Test script steps that arrange and act: a step sets a property on the
 * player pawn, places the pawn next to an actor, presses an Input Action and
 * only then opens its expect window, so the run passes with nobody at the
 * controls. Covers the draft-time refusal of an unknown actor or Input
 * Action, a manual step whose act sets the scene, an act that fails its
 * step, the step_acted events and act_result, and the replay and manual-run
 * rules that skip arrange and act. PIE is stopped after every run.
 * Refs #1507
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Pie/Act";
const IA = `${DIR}/IA_BCovActInteract.IA_BCovActInteract`;
const IA_GONE = `${DIR}/IA_BCovActGone.IA_BCovActGone`;
const CUBE = "BCov_Pie_ActCube";
const CUBE_Y = -15000;

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
    await sleep(500);
  }
  return false;
}

/** Polls a run until done holds or the tries run out. */
async function until(runId: string, done: (s: Json) => boolean, tries = 160): Promise<Json> {
  let status: Json = {};
  for (let i = 0; i < tries; i++) {
    status = await call("pie_script_status", { run_id: runId });
    if (done(status)) return status;
    await sleep(250);
  }
  return status;
}

const stepOf = (s: Json, i: number): Json => rows(s.steps)[i] ?? {};
const actOf = (s: Json, i: number): Json => obj(stepOf(s, i).act_result);

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_input_action", { name: "IA_BCovActInteract", packagePath: DIR });
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: CUBE, location: { x: 0, y: CUBE_Y, z: 300 } });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
  } finally {
    await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("draft-time checks on arrange and act", () => {
  it("script_validate refuses an actor or Input Action that does not exist, and accepts ones that do", async () => {
    const bad = await call("pie_script_validate", {
      steps: [{
        instruction: "Walk to a door that is not there",
        arrange: [{ kind: "set", target: "BCov_NoSuchDoor", path: "CustomTimeDilation", value: 1 }],
        act: [{ place: { at: "BCov_NoSuchDoor" } }, { press: { action: IA_GONE } }],
        expect: { value: CUBE, op: "exists" },
      }],
    });
    expect(bad.valid).toBe(false);
    const fields = rows(bad.errors).map((e) => `${e.step}:${e.field}`);
    expect(fields).toContain("0:arrange[0].target");
    expect(fields).toContain("0:act[0].place.at");
    expect(fields).toContain("0:act[1].press.action");
    const actionIssue = rows(bad.errors).find((e) => e.field === "act[1].press.action");
    expect(String(actionIssue?.message)).toMatch(/^steps\[0\]\.act\[1\]\.press\.action:/);
    expect(rows(actionIssue?.candidates).map(String)).toContain(IA);

    const good = await call("pie_script_validate", {
      steps: [{
        instruction: "Walk to the cube and interact",
        act: [{ place: { at: CUBE, offset: [-400, 0, 0] } }, { press: { action: IA } }],
        expect: { value: CUBE, op: "exists" },
      }],
    });
    expect(good.valid, JSON.stringify(good.errors)).toBe(true);
    expect(rows(good.steps)[0]?.act).toHaveLength(2);
  }, 60_000);
});

describe("a script that arranges and acts on its own", () => {
  let runId = "";

  it("script_run applies arrange, runs act, then judges the expectation from the end of act", async () => {
    expect(await startPie(), "PIE did not start").toBe(true);
    const pawnPath = String((await call("get_pie_pawn", {})).path);
    const pawn = pawnPath.split(".").pop() ?? "";
    expect(pawn).not.toBe("");
    await call("set_object_property", { world: "pie", target: "playerpawn", propertyName: "CustomTimeDilation", value: 1 });

    const started = await call("pie_script_run", {
      name: "bcov_act",
      steps: [
        {
          instruction: "Slow the pawn down",
          arrange: [{ kind: "set", target: pawn, path: "CustomTimeDilation", value: 0.25 }],
          expect: { value: "CustomTimeDilation", op: "lt", target: 0.5, within_ms: 3000 },
        },
        {
          instruction: "Walk to the cube and interact",
          act: [{ place: { at: CUBE, offset: [-400, 0, 0] } }, { press: { action: IA } }, { wait_ms: 200 }],
          expect: { value: "RootComponent.RelativeLocation.Y", op: "lt", target: CUBE_Y + 1000, within_ms: 3000 },
        },
        {
          instruction: "The pawn stands beside the cube",
          judge: "manual",
          act: [{ place: { at: CUBE, offset: [0, -400, 0] } }],
        },
        {
          instruction: "An Input Action that is gone fails the step",
          act: [{ inject: { action: IA_GONE, value: 1 } }],
          expect: { value: CUBE, op: "exists" },
        },
      ],
    });
    runId = String(started.run_id);
    expect(runId).toMatch(/^script_bcov_act_/);

    const awaiting = await until(runId, (s) => s.state === "awaiting_verdict" || s.state === "finished");
    expect(awaiting.state).toBe("awaiting_verdict");

    // Arrange: the property the step set is what the observer read, and what the pawn holds.
    expect(stepOf(awaiting, 0).status).toBe("passed");
    expect(actOf(awaiting, 0)).toMatchObject({ status: "done", arranged: 1 });
    expect(Number(obj(stepOf(awaiting, 0).evidence).at_end)).toBeCloseTo(0.25, 3);
    const dilation = await call("get_object_properties", { target: "playerpawn", world: "pie", propertyNames: ["CustomTimeDilation"] });
    expect(Number(obj(dilation.properties).CustomTimeDilation)).toBeCloseTo(0.25, 3);

    // Act: the pawn was placed beside the cube before the window opened, and the act took its wait.
    const walked = stepOf(awaiting, 1);
    expect(walked.status).toBe("passed");
    expect(Number(obj(walked.evidence).at_end)).toBeLessThan(CUBE_Y + 1000);
    const act = actOf(awaiting, 1);
    expect(act.status).toBe("done");
    expect(rows(act.actions).map((a) => [a.action, a.ok])).toEqual([["place", true], ["press", true], ["wait_ms", true]]);
    expect(Number(act.wall_ms)).toBeGreaterThanOrEqual(200);
    expect(Number(act.ended_at_ms)).toBeGreaterThanOrEqual(Number(act.started_at_ms));
    expect(Number(act.duration_ms)).toBe(Number(act.ended_at_ms) - Number(act.started_at_ms));
    expect(Number(walked.ended_at_ms)).toBeGreaterThanOrEqual(Number(act.ended_at_ms));

    // A manual step's act sets the scene, then the step asks for its verdict.
    expect(stepOf(awaiting, 2).status).toBe("awaiting_verdict");
    expect(actOf(awaiting, 2).status).toBe("done");
    expect(rows(awaiting.awaiting).map((a) => a.index)).toEqual([2]);
    await call("pie_script_verdict", { run_id: runId, step: 2, verdict: "pass", note: "beside the cube" });

    const finished = await until(runId, (s) => s.state === "finished");
    expect(finished.state).toBe("finished");
    const gone = stepOf(finished, 3);
    expect(gone.status).toBe("failed");
    expect(String(obj(gone.evidence).reason)).toMatch(/act\[0\] could not run: InputAction .*IA_BCovActGone.* not found/);
    expect(actOf(finished, 3).status).toBe("failed");

    const acted = rows(finished.events).filter((e) => e.type === "step_acted");
    expect(acted.map((e) => e.step)).toEqual([0, 1, 2, 3]);
    expect(obj(obj(acted[1].data).act_result).status).toBe("done");
    const types = rows(finished.events).map((e) => `${e.type}:${e.step}`);
    expect(types.indexOf("step_acted:1")).toBeGreaterThan(types.indexOf("step_started:1"));
    expect(types.indexOf("step_passed:1")).toBeGreaterThan(types.indexOf("step_acted:1"));
  }, 180_000);

  it("results.json keeps each step's arrange, act and act_result", async (ctx) => {
    if (!runId) ctx.skip("the run did not start");
    await stopPie();
    const results = await call("pie_script_results", { run_id: runId });
    expect(results.summary).toMatchObject({ total: 4, passed: 3, failed: 1 });
    const steps = rows(results.steps);
    expect(rows(steps[0].arrange)).toHaveLength(1);
    expect(rows(steps[1].act)).toHaveLength(3);
    expect(obj(steps[1].act_result).status).toBe("done");
    expect(obj(steps[3].act_result).status).toBe("failed");
  }, 60_000);

  it("script_replay replays the input without running arrange or act again", async (ctx) => {
    if (!runId) ctx.skip("the run did not start");
    await stopPie();
    const source = await call("pie_script_results", { run_id: runId });
    const replay = await call("pie_script_replay", { run_id: runId });
    const replayId = String(replay.run_id);
    const done = await until(replayId, (s) => s.state === "finished" || s.state === "stopped", 240);
    expect(done.state).toBe("finished");
    await stopPie();
    const results = await call("pie_script_results", { run_id: replayId });
    const steps = rows(results.steps);
    for (const i of [0, 1, 2, 3]) expect(obj(steps[i].act_result).status, `step ${i}`).toBe("skipped");
    // The arrange that slowed the pawn did not run again, so the step that read it regresses.
    expect(steps[0].status).toBe("failed");
    expect(steps[0].regression).toBe(true);
    // The window opens where the source step's act ended.
    const sourceAct = obj(rows(source.steps)[1].act_result);
    expect(Number(steps[1].window_start_ms)).toBeCloseTo(Number(sourceAct.ended_at_ms) + Number(results.replay_offset_ms), 3);
  }, 240_000);
});

describe("a manual run", () => {
  it("skips arrange and act, since nothing is in PIE, and says so on the step", async () => {
    await stopPie();
    const started = await call("pie_script_run", {
      name: "bcov_act_manual",
      mode: "manual",
      steps: [{ instruction: "The pawn stands beside the cube", judge: "manual", act: [{ place: { at: CUBE } }] }],
    });
    const runId = String(started.run_id);
    const status = await call("pie_script_status", { run_id: runId });
    expect(stepOf(status, 0).status).toBe("awaiting_verdict");
    expect(actOf(status, 0).status).toBe("skipped");
    expect(String(actOf(status, 0).reason)).toMatch(/no PIE/);
    expect(await pieRunning()).toBe(false);
    await call("pie_script_stop", { run_id: runId, reason: "bcov" });
  }, 60_000);
});
