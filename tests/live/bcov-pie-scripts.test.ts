// @live-editor headless
/**
 * Test script runs in Play-In-Editor: auto steps judged from observed values,
 * a manual step held until its verdict, the run's events and results.json,
 * a replay that re-evaluates the auto steps from the run's recording, and a
 * run stopped early, and agent verdicts with their evidence. PIE is stopped
 * after every run.
 */
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const CUBE = "BCov_Pie_ScriptCube";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);
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

/** Polls a run until a step reaches a status or the run reaches a state. */
async function until(runId: string, done: (s: Json) => boolean, tries = 120): Promise<Json> {
  let status: Json = {};
  for (let i = 0; i < tries; i++) {
    status = await call("pie_script_status", { run_id: runId });
    if (done(status)) return status;
    await sleep(250);
  }
  return status;
}

const stepOf = (s: Json, i: number): Json => rows(s.steps)[i] ?? {};

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: CUBE, location: { x: 0, y: -15000, z: 300 } });
}, 60_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
  } finally {
    await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
    disconnectBridge();
  }
}, 60_000);

describe("a script run with auto and manual steps", () => {
  let runId = "";
  let replayId = "";

  it("script_run evaluates auto steps live, holds at the manual step, and takes its verdict", async () => {
    const started = await call("pie_script_run", {
      name: "bcov_script",
      start_pie: true,
      steps: [
        { instruction: "The cube is in the level", judge: "auto", expect: { value: CUBE, op: "exists" } },
        { instruction: "Time dilation starts below 2", judge: "auto", expect: { value: "CustomTimeDilation", op: "lt", target: 2 } },
        { instruction: "The view looks right", judge: "manual" },
        { instruction: "Slow the pawn down", judge: "auto", expect: { value: "CustomTimeDilation", op: "decreasing", within_ms: 10000 } },
        { instruction: "No missing widget shows", judge: "auto", expect: { value: "WBP_BCovMissing", op: "visible", target: false, within_ms: 500 } },
      ],
    });
    runId = String(started.run_id);
    expect(runId).toMatch(/^script_/);
    expect(started.recording_id).toBe(runId);

    const awaiting = await until(runId, (s) => s.state === "awaiting_verdict");
    expect(awaiting.state).toBe("awaiting_verdict");
    expect(stepOf(awaiting, 0).status).toBe("passed");
    expect(stepOf(awaiting, 1).status).toBe("passed");
    expect(stepOf(awaiting, 2).status).toBe("awaiting_verdict");
    expect(rows(awaiting.awaiting).map((a) => a.index)).toEqual([2]);

    // An auto step's verdict is the editor's, never a caller's.
    expect(succeeded(await callBridge(bridge, "pie_script_verdict", { run_id: runId, step: 0, verdict: "fail" }))).toBe(false);
    const verdict = await call("pie_script_verdict", { run_id: runId, step: 2, verdict: "pass", note: "looks fine" });
    expect(verdict.success).toBe(true);

    const slowing = await until(runId, (s) => stepOf(s, 3).status === "running");
    expect(stepOf(slowing, 2).status).toBe("passed");
    expect(stepOf(slowing, 2).verdict_source).toBe("action");
    await call("set_object_property", { world: "pie", target: "playerpawn", propertyName: "CustomTimeDilation", value: 0.5 });

    const finished = await until(runId, (s) => s.state === "finished");
    expect(finished.state).toBe("finished");
    expect(rows(finished.steps).map((s) => s.status)).toEqual(["passed", "passed", "passed", "passed", "passed"]);
    expect((stepOf(finished, 3).evidence as Json).at_start).toBe(1);
    expect((stepOf(finished, 3).evidence as Json).at_end).toBe(0.5);
  }, 120_000);

  it("script_status returns the run's events after since_seq, in order", async (ctx) => {
    if (!runId) ctx.skip("the run did not start");
    const all = await call("pie_script_status", { run_id: runId });
    const types = rows(all.events).map((e) => e.type);
    expect(types[0]).toBe("run_started");
    expect(types).toContain("awaiting_verdict");
    expect(types).toContain("verdict");
    expect(types.filter((t) => t === "step_passed").length).toBe(4);
    expect(types[types.length - 1]).toBe("run_finished");
    const seqs = rows(all.events).map((e) => Number(e.seq));
    expect(seqs).toEqual([...seqs].sort((a, b) => a - b));
    const tail = await call("pie_script_status", { run_id: runId, since_seq: seqs[seqs.length - 2] });
    expect(rows(tail.events).map((e) => e.type)).toEqual(["run_finished"]);
    expect(tail.last_seq).toBe(seqs[seqs.length - 1]);
  });

  it("script_results reads results.json with every step's status, evidence and verdict", async (ctx) => {
    if (!runId) ctx.skip("the run did not start");
    await stopPie();
    const results = await call("pie_script_results", { run_id: runId });
    expect(results.run_id).toBe(runId);
    expect(String(results.results_path)).toMatch(/results\.json$/);
    expect(results.summary).toMatchObject({ total: 5, passed: 5, failed: 0 });
    const manual = rows(results.steps)[2];
    expect(manual.note).toBe("looks fine");
    expect(manual.verdict_source).toBe("action");
    expect(Number(rows(results.steps)[3].started_at_ms)).toBeGreaterThan(0);
  }, 60_000);

  it("script_replay re-evaluates the auto steps on the run's recording and reports what regressed", async (ctx) => {
    if (!runId) ctx.skip("the run did not start");
    await stopPie();
    const replay = await call("pie_script_replay", { run_id: runId });
    replayId = String(replay.run_id);
    expect(replayId).not.toBe(runId);
    const done = await until(replayId, (s) => s.state === "finished" || s.state === "stopped", 240);
    expect(done.state).toBe("finished");
    await stopPie();
    const results = await call("pie_script_results", { run_id: replayId });
    expect(results.replay_of).toBe(runId);
    const steps = rows(results.steps);
    expect(steps[0].status).toBe("passed");
    expect(steps[2].verdict_source).toBe("source_run");
    // The dilation was set by a call, not by input, so the replay cannot reproduce it.
    expect(steps[3].status).toBe("failed");
    expect(steps[3].regression).toBe(true);
    expect(Number((results.summary as Json).regressions)).toBeGreaterThanOrEqual(1);

    // The source run is no longer the one the editor holds, so its status comes from its results.json.
    const source = await call("pie_script_status", { run_id: runId, since_seq: 1 });
    expect(source.state).toBe("finished");
    expect(source.live).toBe(false);
    expect(rows(source.events).every((e) => Number(e.seq) > 1)).toBe(true);
    expect(rows(source.events).map((e) => e.type)).toContain("run_finished");
  }, 180_000);
});

describe("stopping a run", () => {
  it("script_stop ends a run early and leaves the unsettled steps pending", async () => {
    const started = await call("pie_script_run", {
      name: "bcov_stop", start_pie: true, record: false,
      steps: [{ instruction: "Wait for a person", judge: "manual" }, { instruction: "Never reached", judge: "manual" }],
    });
    const runId = String(started.run_id);
    expect(started.recording_id ?? null).toBeNull();
    await until(runId, (s) => s.state === "awaiting_verdict");
    const stopped = await call("pie_script_stop", { run_id: runId, reason: "bcov" });
    expect(stopped.state ?? (await call("pie_script_status", { run_id: runId })).state).toBe("stopped");
    const results = await call("pie_script_results", { run_id: runId });
    expect(results.state).toBe("stopped");
    expect(rows(results.steps).map((s) => s.status)).not.toContain("passed");
    const again = await callBridge(bridge, "pie_script_stop", {});
    expect(body(again).alreadyStopped).toBe(true);
    await stopPie();
  }, 120_000);
});

describe("agent verdicts", () => {
  it("verdict with source agent needs evidence, honours agent_judgeable, and is counted apart from human verdicts", async () => {
    const started = await call("pie_script_run", {
      name: "bcov_agent", start_pie: true, record: false, block_on_manual: false,
      steps: [
        { instruction: "The cube is on screen", judge: "manual" },
        { instruction: "The motion feels right", judge: "manual", agent_judgeable: false },
      ],
    });
    const runId = String(started.run_id);
    const awaiting = await until(runId, (s) => rows(s.awaiting).length === 2);
    expect(rows(awaiting.awaiting).map((a) => [a.index, a.agent_judgeable])).toEqual([[0, true], [1, false]]);
    expect(stepOf(awaiting, 1).agent_judgeable).toBe(false);

    const refused = async (params: Json): Promise<string> => {
      const r = await callBridge(bridge, "pie_script_verdict", { run_id: runId, ...params });
      expect(succeeded(r), JSON.stringify(r.result)).toBe(false);
      return String(r.error ?? body(r).error ?? "");
    };
    expect(await refused({ step: 0, verdict: "pass", source: "agent" })).toMatch(/evidence/);
    expect(await refused({ step: 1, verdict: "pass", source: "agent", evidence: { note: "looks smooth" } })).toMatch(/agent_judgeable/);
    expect(await refused({ step: 0, verdict: "pass", source: "agent", evidence: { note: "seen", captures: ["bcov_missing_capture.png"] } })).toMatch(/does not exist/);
    const held = await call("pie_script_status", { run_id: runId });
    expect(stepOf(held, 0).status).toBe("awaiting_verdict");
    expect(stepOf(held, 1).status).toBe("awaiting_verdict");

    const outputDir = String(started.output_dir);
    const capture = join(outputDir, "bcov_agent_capture.png");
    mkdirSync(outputDir, { recursive: true });
    writeFileSync(capture, "capture");
    try {
      const agent = await call("pie_script_verdict", {
        run_id: runId, step: 0, verdict: "pass", source: "agent",
        evidence: { note: "The cube fills the centre of the capture", captures: ["bcov_agent_capture.png"] },
      });
      expect(agent.verdict_source).toBe("agent");
      const captures = rows((agent.evidence as Json | undefined)?.captures).map(String);
      expect(captures).toHaveLength(1);
      expect(captures[0].replace(/\\/g, "/")).toMatch(/\/bcov_agent_capture\.png$/);

      await call("pie_script_verdict", { run_id: runId, step: 1, verdict: "fail", note: "a hitch at the turn" });
      const finished = await until(runId, (s) => s.state === "finished");
      expect(finished.state).toBe("finished");
      expect(stepOf(finished, 0).verdict_source).toBe("agent");
      expect(stepOf(finished, 1).verdict_source).toBe("action");
      const agentEvent = rows(finished.events).find((e) => e.type === "verdict" && e.step === 0);
      const eventData = (agentEvent?.data ?? {}) as Json;
      expect(eventData.verdict_source).toBe("agent");
      expect((eventData.evidence as Json | undefined)?.note).toBe("The cube fills the centre of the capture");

      await stopPie();
      const results = await call("pie_script_results", { run_id: runId });
      expect(results.summary).toMatchObject({ total: 2, passed: 1, failed: 1, agent_verdicts: 1, human_verdicts: 1 });
      const step0 = rows(results.steps)[0];
      expect(step0.verdict_source).toBe("agent");
      expect((step0.evidence as Json).note).toBe("The cube fills the centre of the capture");
      expect(rows((step0.evidence as Json).captures)).toHaveLength(1);
      expect(rows(results.steps)[1].agent_judgeable).toBe(false);
    } finally {
      rmSync(capture, { force: true });
      await stopPie();
    }
  }, 120_000);
});
