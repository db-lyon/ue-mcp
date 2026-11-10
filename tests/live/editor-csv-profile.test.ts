// @live-editor headless
/**
 * The CSV profiler with a named capture: start_csv_profile reports the file it
 * is writing, stop_csv_profile waits for it to be finished, and the server-side
 * summariser reads the real capture back.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { readCsvProfileText, summarizeCsvProfile } from "../../src/tools/editor-csv-profile.js";

const CAPTURE = `CsvProfile_${process.pid}_${Date.now()}`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

let bridge: EditorBridge;
let scratch = "";

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
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "csv-profile-"));
  await callBridge(bridge, "stop_csv_profile", {});
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await callBridge(bridge, "stop_csv_profile", {});
  } finally {
    if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
    disconnectBridge();
  }
}, 60_000);

describe("CSV profiler capture", () => {
  it("start_csv_profile names the file, stop_csv_profile finishes it, and the summariser reads it", async () => {
    const started = await call("start_csv_profile", { captureName: CAPTURE, folder: scratch }, 60_000);
    let csvPath = "";
    try {
      expect(started.started).toBe(true);
      expect(started.captureConfirmed).toBe(true);
      csvPath = String(started.csvPath);
      expect(path.basename(csvPath)).toMatch(new RegExp(`^${CAPTURE}\\.csv(\\.gz)?$`));
      expect(path.resolve(path.dirname(csvPath))).toBe(path.resolve(scratch));

      const again = await call("start_csv_profile", { captureName: `${CAPTURE}_second` }, 60_000);
      expect(again.alreadyCapturing).toBe(true);
      expect(again.csvPath).toBe(csvPath);

      await sleep(1500);
    } finally {
      const stopped = await call("stop_csv_profile", {}, 120_000);
      expect(stopped.wasCapturing).toBe(true);
      expect(stopped.writing).toBe(false);
      expect(Number(stopped.capturedFrames)).toBeGreaterThan(0);
      expect(path.resolve(String(stopped.csvPath))).toBe(path.resolve(csvPath));
    }

    expect(fs.existsSync(csvPath)).toBe(true);
    const summary = summarizeCsvProfile(readCsvProfileText(csvPath));
    expect(summary.complete).toBe(true);
    expect(summary.frameCount).toBeGreaterThan(0);
    expect(summary.statCount).toBeGreaterThan(0);

    const replay = await call("stop_csv_profile", {});
    expect(replay.wasCapturing).toBe(false);
    expect(replay.unchanged).toBe(true);
  }, 240_000);

  it("maxFrames ends the capture by itself", async () => {
    const started = await call("start_csv_profile", { captureName: `${CAPTURE}_short`, folder: scratch, maxFrames: 5 }, 60_000);
    expect(started.maxFrames).toBe(5);
    const csvPath = String(started.csvPath);
    // Nothing stops it here: the file gains its closing metadata row only when
    // the profiler ends the capture, which it does after five frames.
    const finished = (): boolean => {
      try {
        return summarizeCsvProfile(readCsvProfileText(csvPath)).complete;
      } catch {
        return false;
      }
    };
    for (let i = 0; i < 200 && !finished(); i++) await sleep(100);
    expect(finished()).toBe(true);
    expect(summarizeCsvProfile(readCsvProfileText(csvPath)).frameCount).toBeLessThanOrEqual(6);

    const stop = await call("stop_csv_profile", {});
    expect(stop.wasCapturing).toBe(false);
  }, 120_000);
});
