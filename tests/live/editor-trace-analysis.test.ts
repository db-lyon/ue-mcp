// @live-editor headless
/**
 * Reading a recorded trace back: a trace recorded around a known hitch and
 * bookmark is analysed with editor(analyze_trace).
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const MARK = `TraceAnalysis_Mark_${process.pid}`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const obj = (v: unknown): Json => (v && typeof v === "object" ? (v as Json) : {});
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

let bridge: EditorBridge;
let scratch = "";
let tracingAtStart = false;
let traceFile = "";

async function call(method: string, params: Json, timeoutMs = 120_000): Promise<Json> {
  let r: TestResult;
  try {
    r = { method, ok: true, ms: 0, result: await bridge.call(method, params, timeoutMs) };
  } catch (e) {
    r = { method, ok: false, ms: 0, error: e instanceof Error ? e.message : String(e) };
  }
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "trace-analysis-"));
  // The live gate fails a skipped test, so a trace someone left running is stopped
  // rather than worked around; the suite needs the trace slot for its own recording.
  tracingAtStart = (await call("get_insights_trace_status", {})).tracing === true;
  if (tracingAtStart) await call("stop_insights_trace", {});

  // A trace with a 300 ms stall and a bookmark in the middle of ordinary frames.
  traceFile = path.join(scratch, "analysis.utrace");
  await call("start_insights_trace", { channels: "default", file: traceFile });
  try {
    await sleep(1000);
    expect((await call("add_trace_bookmark", { bookmarkName: MARK })).recorded).toBe(true);
    await call("trigger_hitch", { hitchMilliseconds: 300, bookmark: false });
    await sleep(1000);
  } finally {
    await call("stop_insights_trace", {});
  }
  let tracing = (await call("get_insights_trace_status", {})).tracing;
  for (let i = 0; i < 100 && tracing !== false; i++) {
    await sleep(50);
    tracing = (await call("get_insights_trace_status", {})).tracing;
  }
  expect(tracing).toBe(false);
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
  disconnectBridge();
}, 60_000);

describe("editor(analyze_trace)", () => {
  it("finds the frames, the 300 ms hitch, the bookmark and the timers in a recorded trace", async () => {
    const result = await call("analyze_insights_trace", { file: traceFile, hitchThresholdMs: 200, worstFrames: 3, maxBookmarks: 1000 });
    expect(path.resolve(String(result.traceFile))).toBe(path.resolve(traceFile));
    expect(Number(result.traceFileBytes)).toBeGreaterThan(0);
    expect(Number(result.sessionDurationSeconds)).toBeGreaterThan(1);

    const frames = obj(result.frames);
    expect(Number(frames.count)).toBeGreaterThan(2);
    expect(Number(frames.maxMs)).toBeGreaterThanOrEqual(280);
    expect(Number(frames.p50Ms)).toBeLessThan(Number(frames.maxMs));
    expect(Number(frames.p95Ms)).toBeGreaterThanOrEqual(Number(frames.p50Ms));
    expect(Number(frames.p99Ms)).toBeGreaterThanOrEqual(Number(frames.p95Ms));

    const hitches = obj(result.hitches);
    expect(hitches.thresholdMs).toBe(200);
    expect(Number(hitches.count)).toBeGreaterThanOrEqual(1);
    expect(rows(hitches.list).some((h) => Number(h.durationMs) >= 280)).toBe(true);

    const worst = rows(result.worstFrames);
    expect(worst.length).toBeGreaterThan(0);
    expect(worst.length).toBeLessThanOrEqual(3);
    expect(Number(worst[0].durationMs)).toBe(Number(frames.maxMs));
    expect(Array.isArray(worst[0].topTimers)).toBe(true);

    const timers = obj(result.topTimers);
    expect(timers.threads).toBe("game");
    expect(timers.sortedBy).toBe("inclusive");
    const timerRows = rows(timers.rows);
    expect(timerRows.length).toBeGreaterThan(0);
    for (let i = 1; i < timerRows.length; i++) {
      expect(Number(timerRows[i - 1].inclusiveMs)).toBeGreaterThanOrEqual(Number(timerRows[i].inclusiveMs));
    }

    const bookmarks = obj(result.bookmarks);
    const mark = rows(bookmarks.list).find((b) => String(b.text).includes(MARK));
    expect(mark, JSON.stringify(bookmarks)).toBeDefined();
    expect(Number(mark!.timeSeconds)).toBeGreaterThan(0);

    // A -nullrhi editor emits no GPU timing, and the result says why rather than reporting zeros.
    expect(typeof obj(result.gpu).present).toBe("boolean");
  }, 180_000);

  it("narrows to a time window and sorts by exclusive time on request", async () => {
    const whole = await call("analyze_insights_trace", { file: traceFile });
    const duration = Number(whole.sessionDurationSeconds);
    const half = await call("analyze_insights_trace", {
      file: traceFile, startSeconds: 0, endSeconds: duration / 2, timerSort: "exclusive", threads: "all",
    });
    expect(obj(half.window).wholeTrace).toBe(false);
    expect(Number(obj(half.window).endSeconds)).toBeCloseTo(duration / 2, 3);
    expect(Number(obj(half.frames).count)).toBeLessThan(Number(obj(whole.frames).count));
    const timers = obj(half.topTimers);
    expect(timers.sortedBy).toBe("exclusive");
    expect(timers.threads).toBe("all");
    const timerRows = rows(timers.rows);
    for (let i = 1; i < timerRows.length; i++) {
      expect(Number(timerRows[i - 1].exclusiveMs)).toBeGreaterThanOrEqual(Number(timerRows[i].exclusiveMs));
    }
  }, 180_000);

  it("writes the whole analysis to a file with dumpToFile and keeps the headline inline", async () => {
    const out = path.join(scratch, "analysis.json");
    const dumped = await call("analyze_insights_trace", { file: traceFile, dumpToFile: true, outputPath: out });
    expect(dumped.dumpedToFile).toBe(true);
    expect(path.resolve(String(dumped.outputPath))).toBe(path.resolve(out));
    const onDisk = JSON.parse(fs.readFileSync(out, "utf8")) as Json;
    expect(obj(onDisk.frames).count).toBe(obj(dumped.frames).count);
    expect(Array.isArray(onDisk.worstFrames)).toBe(true);
    expect(dumped.worstFrames).toBeUndefined();
  }, 180_000);

  it("refuses a file that does not exist and a window that holds nothing", async () => {
    const missing = await callBridge(bridge, "analyze_insights_trace", { file: path.join(scratch, "absent.utrace") });
    expect(succeeded(missing)).toBe(false);
    expect(JSON.stringify(missing.result ?? missing.error)).toMatch(/not found/i);

    const backwards = await callBridge(bridge, "analyze_insights_trace", { file: traceFile || "x.utrace", startSeconds: 5, endSeconds: 1 });
    expect(succeeded(backwards)).toBe(false);
  }, 60_000);
});
