import { afterEach, beforeEach, describe, expect, it } from "vitest";
import type { ElicitFn, ElicitParams, ElicitResult, ToolContext } from "../../src/core/types.js";
import { subscribeScriptEvents, type ScriptEvent } from "../../src/pie/script-events.js";
import {
  getScriptWatch,
  mapBridgeEvent,
  scriptRunFollowUp,
  scriptStatusFollowUp,
  startScriptWatch,
  stopAllScriptWatches,
  verdictForm,
  waitForScriptRun,
  type WatchClock,
} from "../../src/pie/script-watch.js";

type Status = Record<string, unknown>;

/** Time advances only when the watcher sleeps; each sleep yields a macrotask so nothing spins. */
function fakeClock(): WatchClock & { slept: number[] } {
  let t = 0;
  const slept: number[] = [];
  return {
    slept,
    now: () => t,
    sleep: async (ms) => {
      slept.push(ms);
      t += ms;
      await new Promise((r) => setImmediate(r));
    },
  };
}

const STEPS = [
  { index: 0, instruction: "Walk to the door", judge: "auto", expect: { value: "Door.Distance", op: "decreasing" }, status: "passed" },
  { index: 1, instruction: "The door opens smoothly", judge: "manual", status: "awaiting_verdict" },
  { index: 2, instruction: "The light turns green", judge: "manual", status: "awaiting_verdict" },
];

/**
 * A bridge whose pie_script_status answers come from a script, one per poll, the last repeating. A function entry is
 * evaluated at call time, so an answer can depend on verdicts posted so far.
 */
function bridge(statuses: Array<Status | ((verdicts: Array<Record<string, unknown>>) => Status) | Error>) {
  const calls: Array<{ method: string; params: Record<string, unknown> }> = [];
  const verdicts: Array<Record<string, unknown>> = [];
  let i = 0;
  const call = async (method: string, params: Record<string, unknown> = {}) => {
    calls.push({ method, params });
    if (method === "pie_script_verdict") {
      verdicts.push(params);
      return { success: true };
    }
    if (method !== "pie_script_status") return { success: false, error: `unexpected ${method}` };
    const entry = statuses[Math.min(i, statuses.length - 1)];
    i++;
    if (entry instanceof Error) throw entry;
    const s = typeof entry === "function" ? entry(verdicts) : entry;
    return { success: true, run_id: "run-1", ...s };
  };
  return { call, calls, verdicts };
}

function ctxWith(b: ReturnType<typeof bridge>, elicit?: ElicitFn): ToolContext {
  return { bridge: { call: b.call }, ...(elicit ? { elicit } : {}) } as unknown as ToolContext;
}

function fakeElicit(answer: (form: ElicitParams) => ElicitResult) {
  const forms: ElicitParams[] = [];
  const fn = (async (form: ElicitParams) => {
    forms.push(form);
    return answer(form);
  }) as ElicitFn;
  fn.clientAdvertisesElicitation = () => true;
  return { fn, forms };
}

const ev = (seq: number, type: string, step?: number, data: Record<string, unknown> = {}) => ({
  seq, type, run_id: "run-1", ...(step !== undefined ? { step } : {}), at_ms: seq * 100, wall_time: `2026-10-23T10:00:0${seq}.000Z`, data,
});

const awaitingStatus = (events: unknown[] = []): Status => ({
  state: "awaiting_verdict",
  steps: STEPS,
  awaiting: [1, 2],
  events,
});

let seen: ScriptEvent[];
let off: () => void;
const savedMin = process.env.UE_MCP_ELICIT_MIN_HUMAN_MS;

beforeEach(() => {
  seen = [];
  off = subscribeScriptEvents((e) => seen.push(e));
  process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "0";
});

afterEach(() => {
  off();
  stopAllScriptWatches();
  if (savedMin === undefined) delete process.env.UE_MCP_ELICIT_MIN_HUMAN_MS;
  else process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = savedMin;
});

describe("script watcher events", () => {
  it("maps each bridge event once, by seq, and asks for the events after the last one seen", async () => {
    const b = bridge([
      { state: "running", steps: STEPS, events: [ev(1, "run_started", undefined, { script: "/Game/S/Door" }), ev(2, "step_started", 0)] },
      { state: "running", steps: STEPS, events: [ev(2, "step_started", 0), ev(3, "step_passed", 0, { evidence: { value: "Door.Distance", at_start: 400, at_end: 20, samples: 12 } })] },
      { state: "running", steps: STEPS, events: [ev(4, "awaiting_verdict", 1)] },
      { state: "finished", steps: STEPS, summary: { total: 3, passed: 2, failed: 1, skipped: 0, pending: 0 }, results_path: "C:/Saved/MCPObservations/run-1/results.json", events: [ev(5, "verdict", 1, { verdict: "pass", source: "action" }), ev(6, "run_finished")] },
    ]);
    const { wait } = await waitForScriptRun(ctxWith(b), "run-1", { clock: fakeClock() });
    expect(wait.state).toBe("finished");

    const mine = seen.filter((e) => e.runId === "run-1");
    expect(mine.map((e) => [e.seq, e.type])).toEqual([
      [1, "run_started"], [2, "step_started"], [3, "step_passed"], [4, "verdict_requested"], [5, "verdict"], [6, "run_finished"],
    ]);
    expect(mine.every((e) => e.script === "/Game/S/Door")).toBe(true);
    expect(mine[2]).toMatchObject({
      atMs: 300,
      at: "2026-10-23T10:00:03.000Z",
      step: { index: 0, instruction: "Walk to the door", judge: "auto", status: "passed", expect: { op: "decreasing" }, evidence: { at_end: 20 } },
    });
    expect(mine[3]).toMatchObject({ awaiting: [{ index: 1, instruction: "The door opens smoothly" }] });
    expect(mine[4]).toMatchObject({ step: { index: 1, status: "passed", verdictSource: "action" } });
    expect(mine[5]).toMatchObject({ summary: { total: 3, passed: 2, failed: 1 }, resultsPath: "C:/Saved/MCPObservations/run-1/results.json", state: "finished" });

    const since = b.calls.filter((c) => c.method === "pie_script_status").map((c) => c.params.since_seq);
    expect(since.slice(0, 4)).toEqual([0, 2, 3, 4]);
  });

  it("follows a run once however often it is started", async () => {
    const b = bridge([{ state: "running", events: [] }]);
    const clock = fakeClock();
    const a = startScriptWatch(ctxWith(b), "run-1", { clock });
    expect(startScriptWatch(ctxWith(b), "run-1", { clock })).toBe(a);
    expect(getScriptWatch("run-1")).toBe(a);
  });

  it("backs off between polls while nothing happens", async () => {
    const b = bridge([{ state: "running", events: [] }]);
    const clock = fakeClock();
    await waitForScriptRun(ctxWith(b), "run-1", { clock, timeoutSeconds: 4 });
    expect(clock.slept.slice(0, 4)).toEqual([250, 500, 1000, 1000]);
  });
});

describe("script watcher verdicts", () => {
  it("puts every awaiting manual step on one form and posts the answers as elicitation verdicts", async () => {
    const elicit = fakeElicit(() => ({ action: "accept", content: { step_1: "pass", step_1_note: " opens fine ", step_2: "fail" } }));
    const b = bridge([
      awaitingStatus([ev(1, "awaiting_verdict", undefined, { awaiting: [1, 2] })]),
      (v) => (v.length >= 2 ? { state: "finished", summary: { total: 3, passed: 2, failed: 1, skipped: 0, pending: 0 }, events: [] } : awaitingStatus()),
    ]);
    const { wait } = await waitForScriptRun(ctxWith(b, elicit.fn), "run-1", { clock: fakeClock() });
    expect(wait).toMatchObject({ state: "finished", elicitation: "accepted" });
    expect(elicit.forms).toHaveLength(1);
    expect(elicit.forms[0].requestedSchema.properties).toEqual(verdictForm("run-1", "inline", [
      { index: 1, instruction: "The door opens smoothly" },
      { index: 2, instruction: "The light turns green" },
    ]).requestedSchema.properties);
    expect(elicit.forms[0].requestedSchema.properties.step_1).toMatchObject({ type: "string", enum: ["pass", "fail", "skip"], title: "Step 2", description: "The door opens smoothly" });
    expect(elicit.forms[0].requestedSchema.properties.step_1_note).toMatchObject({ type: "string" });
    expect(elicit.forms[0].requestedSchema.required).toBeUndefined();
    expect(elicit.forms[0].message).toContain("run-1");
    expect(elicit.forms[0].message).toContain("The light turns green");
    expect(b.verdicts).toEqual([
      { run_id: "run-1", step: 1, verdict: "pass", note: "opens fine", source: "elicitation" },
      { run_id: "run-1", step: 2, verdict: "fail", source: "elicitation" },
    ]);
    expect(seen.find((e) => e.type === "verdict_requested")).toMatchObject({ awaiting: [{ index: 1 }, { index: 2 }] });
  });

  it("without elicitation returns awaiting_verdict, still emits verdict_requested and posts nothing", async () => {
    const b = bridge([awaitingStatus([ev(1, "awaiting_verdict", 1), ev(2, "awaiting_verdict", 2)])]);
    const { wait, status } = await waitForScriptRun(ctxWith(b), "run-1", { clock: fakeClock() });
    expect(wait).toMatchObject({ state: "awaiting_verdict", awaiting: [1, 2], elicitation: "unavailable" });
    expect(status?.state).toBe("awaiting_verdict");
    expect(seen.filter((e) => e.type === "verdict_requested").map((e) => (e as { awaiting: unknown }).awaiting)).toEqual([
      [{ index: 1, instruction: "The door opens smoothly" }],
      [{ index: 2, instruction: "The light turns green" }],
    ]);
    expect(b.verdicts).toHaveLength(0);
  });

  it("treats a client that advertised nothing as having no form, whatever function it handed over", async () => {
    const fn = (async () => ({ action: "accept", content: { step_1: "pass" } })) as unknown as ElicitFn;
    fn.clientAdvertisesElicitation = () => false;
    const b = bridge([awaitingStatus()]);
    const { wait } = await waitForScriptRun(ctxWith(b, fn), "run-1", { clock: fakeClock() });
    expect(wait.state).toBe("awaiting_verdict");
    expect(b.verdicts).toHaveLength(0);
  });

  it("does not ask again about steps the person declined", async () => {
    const elicit = fakeElicit(() => ({ action: "decline" }));
    const b = bridge([awaitingStatus()]);
    const ctx = ctxWith(b, elicit.fn);
    const first = await waitForScriptRun(ctx, "run-1", { clock: fakeClock() });
    expect(first.wait).toMatchObject({ state: "awaiting_verdict", awaiting: [1, 2], elicitation: "declined" });
    const second = await waitForScriptRun(ctx, "run-1", { clock: fakeClock() });
    expect(second.wait.state).toBe("awaiting_verdict");
    expect(elicit.forms).toHaveLength(1);
    expect(b.verdicts).toHaveLength(0);
  });

  it("calls an instant decline an unrendered form, not a person's answer", async () => {
    process.env.UE_MCP_ELICIT_MIN_HUMAN_MS = "1000";
    const elicit = fakeElicit(() => ({ action: "decline" }));
    const { wait } = await waitForScriptRun(ctxWith(bridge([awaitingStatus()]), elicit.fn), "run-1", { clock: fakeClock() });
    expect(wait).toMatchObject({ state: "awaiting_verdict", elicitation: "auto_answered" });
  });

  it("leaves a step the form left blank for pie(verdict)", async () => {
    const elicit = fakeElicit(() => ({ action: "accept", content: { step_1: "skip" } }));
    const b = bridge([awaitingStatus(), (v) => (v.length >= 1 ? { state: "awaiting_verdict", steps: STEPS, awaiting: [2], events: [] } : awaitingStatus())]);
    const { wait } = await waitForScriptRun(ctxWith(b, elicit.fn), "run-1", { clock: fakeClock() });
    expect(wait).toMatchObject({ state: "awaiting_verdict", awaiting: [2] });
    expect(b.verdicts).toEqual([{ run_id: "run-1", step: 1, verdict: "skip", source: "elicitation" }]);
    expect(elicit.forms).toHaveLength(1);
  });
});

describe("script watcher ends", () => {
  it("times out on a run that keeps going, and keeps following it", async () => {
    const b = bridge([{ state: "running", events: [] }]);
    const { wait } = await waitForScriptRun(ctxWith(b), "run-1", { clock: fakeClock(), timeoutSeconds: 3 });
    expect(wait.state).toBe("timed_out");
    expect(wait.durationMs).toBeGreaterThanOrEqual(3000);
    expect(wait.error).toMatch(/3s/);
    expect(getScriptWatch("run-1")?.ended).toBeNull();
  });

  it("gives up after the bridge fails several times in a row", async () => {
    const b = bridge([new Error("socket closed")]);
    const { wait } = await waitForScriptRun(ctxWith(b), "run-1", { clock: fakeClock(), watch: { maxFailures: 3 } });
    expect(wait.state).toBe("unreachable");
    expect(wait.error).toMatch(/3 times.*socket closed/);
    expect(getScriptWatch("run-1")).toBeUndefined();
  });

  it("reports a stopped run as stopped", async () => {
    const b = bridge([{ state: "stopped", events: [ev(1, "run_finished", undefined, { state: "stopped", summary: { total: 2, passed: 1, failed: 0, skipped: 0, pending: 1 } })] }]);
    const { wait } = await waitForScriptRun(ctxWith(b), "run-1", { clock: fakeClock() });
    expect(wait.state).toBe("stopped");
    expect(seen.find((e) => e.type === "run_finished")).toMatchObject({ state: "stopped", summary: { pending: 1 } });
  });
});

describe("script_run and script_status follow-ups", () => {
  const runAnswer = { success: true, run_id: "run-1", state: "running", output_dir: "C:/Saved/MCPObservations/run-1", results_path: "C:/Saved/MCPObservations/run-1/results.json", observation_run_id: "obs-1", steps: STEPS };

  it("starts following without waiting when wait is not set", async () => {
    const b = bridge([{ state: "running", events: [] }]);
    const out = await scriptRunFollowUp(ctxWith(b), { script: "/Game/S/Door" }, runAnswer, fakeClock());
    expect(out).toBe(runAnswer);
    expect(getScriptWatch("run-1")?.script).toBe("/Game/S/Door");
  });

  it("with wait merges the final status and the wait, with the verdict hint when steps await", async () => {
    const b = bridge([awaitingStatus([ev(1, "awaiting_verdict", undefined, { awaiting: [1, 2] })])]);
    const out = (await scriptRunFollowUp(ctxWith(b), { script: "/Game/S/Door", wait: true, timeout_seconds: 30 }, runAnswer, fakeClock())) as Record<string, unknown>;
    expect(out).toMatchObject({ run_id: "run-1", state: "awaiting_verdict", observation_run_id: "obs-1", wait: { state: "awaiting_verdict", awaiting: [1, 2] } });
    expect(out.verdictHint).toBe('call pie(verdict, step, verdict, note?); an agent passes source="agent" with evidence {note, captures?}');
    expect(out.events).toBeUndefined();
  });

  it("leaves a refused run alone", async () => {
    const b = bridge([]);
    const refused = { success: false, error: "No PIE session" };
    expect(await scriptRunFollowUp(ctxWith(b), { wait: true }, refused, fakeClock())).toBe(refused);
    expect(b.calls).toHaveLength(0);
  });

  it("script_status starts a watcher for a live run, not a finished one", async () => {
    const b = bridge([{ state: "running", events: [] }]);
    const done = { success: true, run_id: "run-2", state: "finished", events: [] };
    expect(await scriptStatusFollowUp(ctxWith(b), {}, done, fakeClock())).toBe(done);
    expect(getScriptWatch("run-2")).toBeUndefined();
    const live = { success: true, run_id: "run-1", state: "running", events: [] };
    expect(await scriptStatusFollowUp(ctxWith(b), {}, live, fakeClock())).toBe(live);
    expect(getScriptWatch("run-1")).toBeDefined();
  });

  it("script_status with wait answers with the caller's since_seq window", async () => {
    const b = bridge([
      { state: "running", steps: STEPS, events: [ev(1, "run_started"), ev(2, "step_started", 0)] },
      { state: "finished", steps: STEPS, events: [ev(3, "run_finished")], summary: { total: 3, passed: 3, failed: 0, skipped: 0, pending: 0 } },
    ]);
    const live = { success: true, run_id: "run-1", state: "running", events: [] };
    const out = (await scriptStatusFollowUp(ctxWith(b), { run_id: "run-1", since_seq: 1, wait: true }, live, fakeClock())) as Record<string, unknown>;
    expect(out).toMatchObject({ state: "finished", wait: { state: "finished" } });
    expect(out.verdictHint).toBeUndefined();
    expect(b.calls.at(-1)?.params).toEqual({ run_id: "run-1", since_seq: 1 });
  });
});

describe("replay step payloads", () => {
  it("carries the step window, timing and regression through to the stream", () => {
    const mapped = mapBridgeEvent(
      { seq: 7, type: "step_failed", run_id: "script_replay_x", step: 1, at_ms: 900, wall_time: "2026-10-24T00:00:00Z",
        data: { index: 1, instruction: "Slow down", judge: "auto", within_ms: 800, started_at_ms: 100, ended_at_ms: 900,
          source_status: "passed", regression: true, verdict_source: "auto" } },
      { runId: "script_replay_x", script: "inline", replayOf: "script_x" },
      null,
    );
    expect(mapped?.type).toBe("step_failed");
    const step = (mapped as unknown as { step: Record<string, unknown> }).step;
    expect(step).toMatchObject({ index: 1, status: "failed", withinMs: 800, startedAtMs: 100, endedAtMs: 900, sourceStatus: "passed", regression: true });
    expect((mapped as unknown as { replayOf?: string }).replayOf).toBe("script_x");
  });
});

describe("agent verdict payloads", () => {
  it("keeps an agent verdict's source, evidence and agent_judgeable on the stream", () => {
    const mapped = mapBridgeEvent(
      { seq: 4, type: "verdict", run_id: "script_x", step: 0, at_ms: 300, wall_time: "2026-10-24T00:00:00Z",
        data: { status: "passed", verdict: "pass", verdict_source: "agent", evidence: { note: "door open in capture", captures: ["C:/x/shot.png"] } } },
      { runId: "script_x", script: "inline" },
      { steps: [{ index: 0, instruction: "Door open?", judge: "manual", agent_judgeable: true, status: "passed" }] },
    );
    const step = (mapped as unknown as { step: Record<string, unknown> }).step;
    expect(step).toMatchObject({ index: 0, status: "passed", verdictSource: "agent", agentJudgeable: true,
      evidence: { note: "door open in capture", captures: ["C:/x/shot.png"] } });
  });

  it("counts human and agent verdicts apart in the run_finished summary", () => {
    const mapped = mapBridgeEvent(
      { seq: 9, type: "run_finished", run_id: "script_x", step: -1, at_ms: 900, wall_time: "2026-10-24T00:00:00Z",
        data: { state: "finished", summary: { total: 3, passed: 2, failed: 1, skipped: 0, pending: 0, awaiting: 0, human_verdicts: 1, agent_verdicts: 2 } } },
      { runId: "script_x", script: "inline" },
      null,
    );
    expect((mapped as unknown as { summary: Record<string, unknown> }).summary).toMatchObject({ humanVerdicts: 1, agentVerdicts: 2 });
  });
});

describe("arrange and act payloads", () => {
  it("publishes step_acted with what the act did and how long it took", () => {
    const actResult = {
      status: "done", arranged: 1, started_at_ms: 50, ended_at_ms: 410, duration_ms: 360, wall_ms: 372,
      actions: [{ index: 0, action: "place", ok: true, at_ms: 50 }, { index: 1, action: "press", ok: true, at_ms: 50 }],
    };
    const mapped = mapBridgeEvent(
      { seq: 3, type: "step_acted", run_id: "script_door", step: 0, at_ms: 410, wall_time: "2026-10-24T00:00:00Z",
        data: { status: "running", act_result: actResult } },
      { runId: "script_door", script: "/Game/PIEStudio/Scripts/Door" },
      { steps: [{ index: 0, instruction: "Open the door", judge: "auto", status: "running",
        act: [{ place: { at: "Door" } }, { press: { action: "/Game/Input/IA_Interact.IA_Interact" } }] }] },
    );
    expect(mapped?.type).toBe("step_acted");
    const step = (mapped as unknown as { step: Record<string, unknown> }).step;
    expect(step).toMatchObject({ index: 0, status: "running", actResult });
    expect(step.act).toHaveLength(2);
  });
});
