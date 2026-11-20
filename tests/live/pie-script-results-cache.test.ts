// @live-editor headless
/**
 * pie_script_runs and pie_script_list read each run's results.json once and again
 * only when the file changes. Hundreds of runs are written straight to the
 * observations folder, listed, then edited, replaced and deleted to prove the
 * listing follows the files rather than a stale copy.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

const PREFIX = "zz_cache_run_";
const RUNS = 300;
const SCRIPT = "/Game/MCPTest/Cache/TS_CacheList";

let bridge: EditorBridge;
let root = "";

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const runId = (i: number): string => `${PREFIX}${String(i).padStart(4, "0")}`;
// Later runs start later, so the newest is the highest index.
const startedAt = (i: number): string => new Date(Date.UTC(2030, 0, 1, 0, 0, i)).toISOString();

function writeRun(i: number, state = "finished", stepCount = 3): void {
  const dir = join(root, runId(i));
  mkdirSync(dir, { recursive: true });
  const steps = Array.from({ length: stepCount }, (_, s) => ({ index: s, status: "passed", instruction: `step ${s}`.padEnd(200, ".") }));
  writeFileSync(
    join(dir, "results.json"),
    JSON.stringify({
      run_id: runId(i), script: `${SCRIPT}.TS_CacheList`, script_name: "TS_CacheList", state, mode: "full",
      started_at: startedAt(i), ended_at: startedAt(i), summary: { passed: stepCount, failed: 0, skipped: 0, unchecked: 0 },
      steps,
      // A real run also records its event log, which the listing never uses but every parse reads.
      events: Array.from({ length: 400 }, (_, e) => ({ seq: e, kind: "step_progress", step: e % stepCount, detail: `event ${e}`.padEnd(120, ".") })),
    }),
  );
}

async function listed(params: Json = {}): Promise<Json[]> {
  return (await call("pie_script_runs", { script: SCRIPT, limit: 1000, ...params })).runs as Json[];
}

beforeAll(async () => {
  bridge = await getBridge();
  root = String((await call("pie_script_runs", { limit: 1 })).observations_dir);
  for (let i = 0; i < RUNS; i++) writeRun(i);
}, 60_000);

afterAll(async () => {
  for (let i = 0; i < RUNS + 5; i++) rmSync(join(root, runId(i)), { recursive: true, force: true });
  disconnectBridge();
});

describe("script run listing cache", () => {
  it("lists every written run, then follows edits, additions and deletions without a stale row", async () => {
    const t0 = performance.now();
    const first = await listed();
    const coldMs = performance.now() - t0;
    expect(first).toHaveLength(RUNS);
    expect(first[0].run_id).toBe(runId(RUNS - 1));
    expect((first[0].steps as Json[]).length).toBe(3);

    const t1 = performance.now();
    const again = await listed();
    const warmMs = performance.now() - t1;
    expect(again.map((r) => r.run_id)).toEqual(first.map((r) => r.run_id));
    console.log(`pie_script_runs over ${RUNS} runs: first ${coldMs.toFixed(0)} ms, repeat ${warmMs.toFixed(0)} ms`);

    // Without steps, a cached row still drops them, and the cached copy keeps them for the next call.
    const slim = await listed({ include_steps: false });
    expect(slim).toHaveLength(RUNS);
    expect(slim.every((r) => r.steps === undefined)).toBe(true);
    expect((await listed())[0].steps).toHaveLength(3);

    // An edited file (different size) is read again.
    writeRun(10, "stopped", 5);
    const edited = (await listed()).find((r) => r.run_id === runId(10)) as Json;
    expect(edited.state).toBe("stopped");
    expect(edited.steps).toHaveLength(5);

    // A new folder appears, a deleted one disappears.
    writeRun(RUNS, "finished", 1);
    rmSync(join(root, runId(0)), { recursive: true, force: true });
    const after = await listed();
    expect(after).toHaveLength(RUNS);
    expect(after[0].run_id).toBe(runId(RUNS));
    expect(after.some((r) => r.run_id === runId(0))).toBe(false);
  }, 120_000);
});
