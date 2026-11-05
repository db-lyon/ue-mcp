/**
 * A PIE test script run started through the daemon streams its steps on /v1/events: the script watcher polls the
 * editor's pie_script_status and the daemon republishes each new event as script.<type>.
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { McpServer, Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import { base, bearer, call, readSse, startDaemonCli, type Discovery } from "./daemon-fakes.js";

const RUN = "script-run-integ";
const STEPS = [
  { index: 0, instruction: "Walk to the door", judge: "auto", expect: { value: "Door.Distance", op: "decreasing", within_ms: 3000 } },
  { index: 1, instruction: "The door is open", judge: "auto", expect: { value: "Door.IsOpen", op: "eq", target: true } },
];
const step = (i: number, status: string, extra: Record<string, unknown> = {}) => ({ ...STEPS[i], status, ...extra });
const ev = (seq: number, type: string, s?: number, data: Record<string, unknown> = {}) => ({
  seq, type, run_id: RUN, ...(s !== undefined ? { step: s } : {}), at_ms: seq * 250, wall_time: new Date(Date.UTC(2026, 9, 23, 10, 0, seq)).toISOString(), data,
});
const SUMMARY = { total: 2, passed: 1, failed: 1, skipped: 0, pending: 0 };
const RESULTS = "C:/Proj/Saved/MCPObservations/script-run-integ/results.json";

/** What pie_script_status answers, poll by poll; the last one repeats. */
const STATUS = [
  { state: "running", current_step: 0, steps: [step(0, "running"), step(1, "pending")], awaiting: [], last_seq: 2,
    events: [ev(1, "run_started", undefined, { script: "inline" }), ev(2, "step_started", 0)] },
  { state: "running", current_step: 1, steps: [step(0, "passed"), step(1, "running")], awaiting: [], last_seq: 4,
    events: [ev(3, "step_passed", 0, { evidence: { value: "Door.Distance", at_start: 400, at_end: 30, samples: 9 } }), ev(4, "step_started", 1)] },
  { state: "finished", current_step: 1, steps: [step(0, "passed"), step(1, "failed")], awaiting: [], last_seq: 6, summary: SUMMARY, results_path: RESULTS,
    events: [ev(5, "step_failed", 1, { evidence: { value: "Door.IsOpen", at_start: false, at_end: false, samples: 12 } }), ev(6, "run_finished", undefined, { summary: SUMMARY, results_path: RESULTS })] },
];

let sb: Sandbox;
let bridge: FakeBridge;
let d: Discovery;
let project: string;
let polls = 0;

beforeAll(async () => {
  sb = new Sandbox();
  project = sb.makeProject("Scripts");
  bridge = await FakeBridge.start({
    projectDir: path.dirname(project),
    handlers: {
      pie_script_run: () => ({
        success: true, run_id: RUN, state: "running", output_dir: path.dirname(RESULTS), results_path: RESULTS,
        observation_run_id: "obs-integ", recording_id: "rec-integ", steps: STEPS.map((s) => ({ ...s, status: "pending" })),
      }),
      pie_script_status: (p) => {
        const answer = STATUS[Math.min(polls, STATUS.length - 1)];
        polls++;
        // Only the events after since_seq, as the C++ runner answers.
        const since = typeof p.since_seq === "number" ? p.since_seq : 0;
        return { success: true, run_id: RUN, ...answer, events: answer.events.filter((e) => e.seq > since) };
      },
    },
  });
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
  await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  await bridge?.stop();
  sb.cleanup();
});

describe("test script events", () => {
  it("streams a script run's steps from pie(script_run) to its end", async () => {
    covers("daemon.script-events");
    const before = (await call(d, "GET", "/v1/health")).body.lastEventId as number;
    const stream = readSse(
      `${base(d)}/v1/events`,
      { ...bearer(d), "last-event-id": String(before) },
      (es) => es.some((e) => e.type === "script.run_finished"),
    );

    const server = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null });
    try {
      const run = await server.call("tools", { action: "call", category: "pie", method: "script_run", args: { steps: STEPS.map(({ index: _i, ...s }) => s) } });
      expect(run.isError, run.text).toBe(false);
      expect(run.text).toContain(RUN);
    } finally {
      await server.client.close();
    }

    const events = (await stream).filter((e) => e.type.startsWith("script."));
    expect(events.map((e) => e.type)).toEqual([
      "script.run_started",
      "script.step_started",
      "script.step_passed",
      "script.step_started",
      "script.step_failed",
      "script.run_finished",
    ]);
    expect(events.map((e) => e.data.seq)).toEqual([1, 2, 3, 4, 5, 6]);
    expect(events.every((e) => e.data.runId === RUN && e.data.script === "inline")).toBe(true);
    expect(events.every((e) => Number(e.id) > before)).toBe(true);

    const passed = events.find((e) => e.type === "script.step_passed")!.data;
    expect(passed).toMatchObject({
      atMs: 750,
      at: "2026-10-23T10:00:03.000Z",
      step: { index: 0, instruction: "Walk to the door", judge: "auto", status: "passed", expect: { op: "decreasing", within_ms: 3000 }, evidence: { at_end: 30 } },
    });
    expect(events.find((e) => e.type === "script.step_failed")!.data).toMatchObject({ step: { index: 1, status: "failed", expect: { op: "eq", target: true } } });
    expect(events.find((e) => e.type === "script.run_finished")!.data).toMatchObject({ summary: SUMMARY, resultsPath: RESULTS, state: "finished" });

    // The watcher asked only for what it had not seen, and stopped once the run finished.
    const sinceSeqs = bridge.calls.filter((c) => c.method === "pie_script_status").map((c) => c.params.since_seq);
    expect(sinceSeqs).toEqual([0, 2, 4]);
    expect(bridge.calls.find((c) => c.method === "pie_script_run")).toBeDefined();
  }, 90_000);
});
