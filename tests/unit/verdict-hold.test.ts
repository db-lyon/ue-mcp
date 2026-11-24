/**
 * A run holding for a person's verdict: PIE paused when a step starts to
 * wait, Resume and watch, PIE resumed once the step is judged, the deadline
 * read from the wait at that moment, and a run stopped when no verdict comes.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import type { ActionResult } from "../../src/contract/daemon.js";
import { qaRunFromAnswer, type QaRun } from "../../src/hub/contract.js";
import { VerdictHold } from "../../src/hub/verdict-hold.js";

const ok: ActionResult = { isError: false, content: [{ type: "text", text: JSON.stringify({ success: true }) }] };
const T0 = Date.parse("2026-11-23T10:00:00.000Z");

let calls: Array<{ action: string; args: Record<string, unknown> }>;
let stops: Array<{ runId: string; reason: string }>;
let published: unknown[];
let wait: number;

const run = (state: string, statuses: string[], extra: Record<string, unknown> = {}): QaRun => ({
  ...qaRunFromAnswer({
    run_id: "r1",
    script: "/Game/T/Door",
    script_name: "Door",
    state,
    steps: statuses.map((status, index) => ({ index, instruction: `step ${index}`, judge: index === 0 ? "auto" : "manual", status, ...(index === 2 ? { agent_judgeable: false } : {}) })),
    // The run started PIE itself, unless a test says otherwise.
    pie_started: true,
  }, { sessionId: "s1", seenAt: "2026-11-23T10:00:00.000Z" })!,
  ...extra,
});

const make = () => new VerdictHold({
  callAction: async (_c, action, args = {}) => {
    calls.push({ action, args });
    return ok;
  },
  publish: (_type, data) => void published.push(data),
  log: { warn() {} },
  stop: async (runId, reason) => void stops.push({ runId, reason }),
  waitMinutes: () => wait,
  now: () => Date.now(),
});
const paused = () => calls.filter((c) => c.action === "set_pie_paused").map((c) => c.args);

beforeEach(() => {
  vi.useFakeTimers();
  vi.setSystemTime(T0);
  calls = [];
  stops = [];
  published = [];
  wait = 10;
});
afterEach(() => vi.useRealTimers());

describe("VerdictHold", () => {
  it("pauses PIE when a step starts waiting, resumes it once judged, and publishes the deadline", async () => {
    const h = make();
    h.observe(run("running", ["passed", "pending"]));
    expect(h.held()).toEqual([]);
    h.observe(run("awaiting_verdict", ["passed", "awaiting_verdict"]));
    h.observe(run("awaiting_verdict", ["passed", "awaiting_verdict"]));
    await vi.advanceTimersByTimeAsync(0);
    expect(paused()).toEqual([{ paused: true, mode: "debug" }]);
    expect(h.held()).toEqual([{ runId: "r1", scriptName: "Door", sessionId: "s1", step: 1, instruction: "step 1", since: "2026-11-23T10:00:00.000Z", dueAt: "2026-11-23T10:10:00.000Z", paused: true }]);
    expect(published.at(-1)).toMatchObject({ held: [{ runId: "r1", dueAt: "2026-11-23T10:10:00.000Z" }] });
    h.observe(run("running", ["passed", "passed"]));
    await vi.advanceTimersByTimeAsync(0);
    expect(paused()).toEqual([{ paused: true, mode: "debug" }, { paused: false, mode: "debug" }]);
    expect(h.held()).toEqual([]);
    expect(published.at(-1)).toEqual({ held: [] });
  });

  it("Resume and watch unpauses without moving the deadline, and the verdict then resumes nothing", async () => {
    const h = make();
    h.observe(run("awaiting_verdict", ["passed", "awaiting_verdict"]));
    await vi.advanceTimersByTimeAsync(60_000);
    const r = await h.resume("r1");
    expect(r).toMatchObject({ held: { paused: false, dueAt: "2026-11-23T10:10:00.000Z" } });
    expect(paused().map((p) => p.paused)).toEqual([true, false]);
    h.observe(run("finished", ["passed", "passed"]));
    await vi.advanceTimersByTimeAsync(0);
    expect(paused().map((p) => p.paused)).toEqual([true, false]);
    expect(await h.resume("r1")).toMatchObject({ status: 409 });
  });

  it("reads the wait when the step starts waiting", () => {
    const h = make();
    wait = 2;
    h.observe(run("awaiting_verdict", ["passed", "awaiting_verdict"]));
    wait = 60;
    expect(h.held()[0].dueAt).toBe("2026-11-23T10:02:00.000Z");
  });

  it("stops the run when no verdict comes in time", async () => {
    const h = make();
    h.observe(run("awaiting_verdict", ["passed", "awaiting_verdict"]));
    await vi.advanceTimersByTimeAsync(10 * 60_000 - 1);
    expect(stops).toEqual([]);
    await vi.advanceTimersByTimeAsync(1);
    expect(stops).toEqual([{ runId: "r1", reason: "no verdict" }]);
  });

  it("holds nothing an assigned agent will judge, or a manual run; a step kept with a person is held under an agent too", () => {
    const h = make();
    const agent = { assignment: { callId: "c", sessionId: "s1", state: "running", at: "" } };
    h.observe(run("awaiting_verdict", ["passed", "awaiting_verdict", "pending"], agent));
    expect(h.held()).toEqual([]);
    h.observe(run("awaiting_verdict", ["passed", "awaiting_verdict"], { manual: true }));
    expect(h.held()).toEqual([]);
    h.observe(run("awaiting_verdict", ["passed", "passed", "awaiting_verdict"], agent));
    expect(h.held()).toMatchObject([{ step: 2 }]);
    h.dispose();
  });

  it("never touches the developer's PIE: a run that joined it holds for its verdict with PIE playing, and its stop leaves PIE alone", async () => {
    const h = make();
    h.observe(run("awaiting_verdict", ["passed", "awaiting_verdict"], { pieStarted: undefined }));
    await vi.advanceTimersByTimeAsync(0);
    expect(paused()).toEqual([]);
    expect(h.held()).toMatchObject([{ runId: "r1", paused: false }]);
    expect(h.stopping("r1")).toBe(false);
    h.observe(run("finished", ["passed", "passed"], { pieStarted: undefined }));
    await vi.advanceTimersByTimeAsync(0);
    expect(paused()).toEqual([]);
  });

  it("a run that started PIE is stopped with it", () => {
    const h = make();
    h.observe(run("awaiting_verdict", ["passed", "awaiting_verdict"]));
    expect(h.stopping("r1")).toBe(true);
    h.dispose();
  });

  it("holds nothing another MCP client started: no pause, no deadline, no stop", async () => {
    const h = make();
    h.observe(run("awaiting_verdict", ["passed", "awaiting_verdict"], { sessionId: null }));
    await vi.advanceTimersByTimeAsync(11 * 60_000);
    expect(h.held()).toEqual([]);
    expect(paused()).toEqual([]);
    expect(stops).toEqual([]);
  });
});
