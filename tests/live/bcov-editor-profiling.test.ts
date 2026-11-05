// @live-editor headless
/**
 * Profiling: an Insights trace written to a scratch file (start, status,
 * channels, pause, regions, bookmarks, stop), a deliberate hitch, and a
 * standalone game process launched and terminated. Refs #1223.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const REGION = "BCov_Editor_Region";

type Json = Record<string, unknown>;
type Channel = { name: string; enabled: boolean };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

let bridge: EditorBridge;
let scratch = "";
let tracingAtStart = false;

async function call(method: string, params: Json, timeoutMs?: number): Promise<Json> {
  let r: TestResult;
  if (timeoutMs) {
    try {
      r = { method, ok: true, ms: 0, result: await bridge.call(method, params, timeoutMs) };
    } catch (e) {
      r = { method, ok: false, ms: 0, error: e instanceof Error ? e.message : String(e) };
    }
  } else {
    r = await callBridge(bridge, method, params);
  }
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function channel(name: string): Promise<Channel | undefined> {
  const listed = await call("list_trace_channels", { filter: name });
  return (listed.channels as Channel[]).find((c) => c.name.toLowerCase() === name.toLowerCase());
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "bcov-editor-trace-"));
  tracingAtStart = (await call("get_insights_trace_status", {})).tracing === true;
  await callBridge(bridge, "end_profile_region", { regionName: REGION });
  await callBridge(bridge, "stop_standalone_game", {});
}, 60_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await callBridge(bridge, "end_profile_region", { regionName: REGION });
    if (!tracingAtStart) await callBridge(bridge, "stop_insights_trace", {});
    await callBridge(bridge, "stop_standalone_game", {});
  } finally {
    if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
    disconnectBridge();
  }
}, 60_000);

describe("an Insights trace", () => {
  it("start_insights_trace writes a .utrace that get_insights_trace_status and stop_insights_trace report", async (ctx) => {
    if (tracingAtStart) ctx.skip("a trace was already running before this suite; it is left alone");
    const file = path.join(scratch, "bcov-editor.utrace");
    try {
      const started = await call("start_insights_trace", { channels: "default", file });
      expect(started.started).toBe(true);
      expect(started.alreadyTracing).toBe(false);

      const status = await call("get_insights_trace_status", {});
      expect(status.tracing).toBe(true);
      expect(String(status.destination)).toContain("bcov-editor");

      const again = await call("start_insights_trace", { channels: "default", file });
      expect(again.alreadyTracing).toBe(true);

      const bookmarkChannel = await channel("Bookmark");
      expect(bookmarkChannel?.enabled).toBe(true);
      const mark = await call("add_trace_bookmark", { bookmarkName: "BCov_Editor_Mark" });
      expect(mark.recorded).toBe(true);
      expect(mark.bookmarkChannelActive).toBe(true);
    } finally {
      const stopped = await call("stop_insights_trace", {});
      expect(stopped.wasTracing).toBe(true);
      expect(stopped.stopped).toBe(true);
    }
    // The trace writer closes on its own thread, so the status follows the stop shortly after.
    let tracing = (await call("get_insights_trace_status", {})).tracing;
    for (let i = 0; i < 100 && tracing !== false; i++) {
      await sleep(50);
      tracing = (await call("get_insights_trace_status", {})).tracing;
    }
    expect(tracing).toBe(false);
    expect(fs.existsSync(file)).toBe(true);
    expect(fs.statSync(file).size).toBeGreaterThan(0);
  }, 120_000);

  it("set_trace_channels turns a channel off and on, as list_trace_channels and add_trace_bookmark see it", async () => {
    const before = await channel("Bookmark");
    expect(before).toBeDefined();
    try {
      const off = await call("set_trace_channels", { disable: "Bookmark" });
      const row = (off.channels as Array<{ name: string; enabled: boolean }>)[0];
      expect(row.enabled).toBe(false);
      expect((await channel("Bookmark"))?.enabled).toBe(false);
      expect((await call("add_trace_bookmark", { bookmarkName: "BCov_Editor_Off" })).bookmarkChannelActive).toBe(false);

      await call("set_trace_channels", { enable: "Bookmark" });
      expect((await channel("Bookmark"))?.enabled).toBe(true);
      expect((await call("add_trace_bookmark", { bookmarkName: "BCov_Editor_On" })).bookmarkChannelActive).toBe(true);
    } finally {
      await call("set_trace_channels", before!.enabled ? { enable: "Bookmark" } : { disable: "Bookmark" });
    }
  }, 60_000);

  it("pause_insights_trace pauses and resumes a running trace, as get_insights_trace_status reads it", async (ctx) => {
    if (tracingAtStart) ctx.skip("a trace was already running before this suite; it is left alone");
    // A file target: the engine refuses to start a trace with no destination on newer versions.
    await call("start_insights_trace", { channels: "default", file: path.join(scratch, "bcov-pause.utrace") });
    try {
      const paused = await call("pause_insights_trace", { paused: true });
      expect(paused.paused).toBe(true);
      expect(paused.previouslyPaused).toBe(false);
      expect((await call("get_insights_trace_status", {})).paused).toBe(true);

      const resumed = await call("pause_insights_trace", { paused: false });
      expect(resumed.paused).toBe(false);
      expect((await call("get_insights_trace_status", {})).paused).toBe(false);
    } finally {
      await call("stop_insights_trace", {});
    }
  }, 60_000);
});

describe("profile regions and hitches", () => {
  it("begin_profile_region opens a region the status lists and end_profile_region closes it", async () => {
    const opened = await call("begin_profile_region", { regionName: REGION, regionCategory: "BCov" });
    expect(opened.opened).toBe(true);
    const open = (await call("get_insights_trace_status", {})).openRegions as Array<{ regionName: string }>;
    expect(open.map((r) => r.regionName)).toContain(REGION);

    await sleep(200);
    const closed = await call("end_profile_region", { regionName: REGION });
    expect(closed.closed).toBe(true);
    expect(Number(closed.durationMs)).toBeGreaterThanOrEqual(150);
    const after = (await call("get_insights_trace_status", {})).openRegions as Array<{ regionName: string }>;
    expect(after.map((r) => r.regionName)).not.toContain(REGION);
  }, 60_000);

  it("trigger_hitch stalls the game thread for the time asked", async () => {
    const t0 = Date.now();
    const hitch = await call("trigger_hitch", { hitchMilliseconds: 300, bookmark: false });
    expect(Date.now() - t0).toBeGreaterThanOrEqual(280);
    expect(Number(hitch.requestedMs)).toBe(300);
    expect(Number(hitch.actualMs)).toBeGreaterThanOrEqual(290);
  }, 60_000);
});

describe("a standalone game process", () => {
  it("launch_standalone_game starts a process get_standalone_status sees and stop_standalone_game ends", async () => {
    const launched = await call("launch_standalone_game", { windowed: true, resX: 320, resY: 240, extraArgs: "-nosound -unattended" }, 120_000);
    try {
      expect(launched.launched).toBe(true);
      expect(Number(launched.processId)).toBeGreaterThan(0);

      const status = await call("get_standalone_status", {});
      expect(status.launched).toBe(true);
      expect(status.running).toBe(true);
      expect(status.processId).toBe(launched.processId);
      expect(((await call("get_insights_trace_status", {})).standalone as Json).running).toBe(true);
    } finally {
      const stopped = await call("stop_standalone_game", {}, 60_000);
      expect(stopped.stopped).toBe(true);
    }
    let running = true;
    for (let i = 0; i < 20 && running; i++) {
      running = (await call("get_standalone_status", {})).running === true;
      if (running) await sleep(500);
    }
    expect(running).toBe(false);
  }, 180_000);
});
