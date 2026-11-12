// @live-editor render
/**
 * Movie Render Queue: a job created on a short Level Sequence, rendered in the
 * editor through the PIE executor, its frames found on disk, and a long render
 * started and cancelled. Needs the Movie Render Pipeline plugin, which the test
 * project enables.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/MRQ";
const SEQ = `${DIR}/LS_BCovMRQ`;
const LONG_SEQ = `${DIR}/LS_BCovMRQLong`;
const JOB = "BCovMRQ_Short";
const LONG_JOB = "BCovMRQ_Long";

type Json = Record<string, unknown>;
type Job = { jobName: string; outputDirectory?: string; width?: number; height?: number; outputFormats?: string[]; renderPasses?: string[]; startFrame?: number; endFrame?: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const norm = (p: string): string => path.resolve(p).toLowerCase();
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;
let scratch = "";
let mapPath = "";

async function raw(method: string, params: Json, timeoutMs?: number): Promise<TestResult> {
  if (!timeoutMs) return callBridge(bridge, method, params);
  try {
    return { method, ok: true, ms: 0, result: await bridge.call(method, params, timeoutMs) };
  } catch (e) {
    return { method, ok: false, ms: 0, error: e instanceof Error ? e.message : String(e) };
  }
}

async function call(method: string, params: Json = {}, timeoutMs?: number): Promise<Json> {
  const r = await raw(method, params, timeoutMs);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function jobs(): Promise<Job[]> {
  return (await call("mrq_list_jobs")).jobs as Job[];
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "mrq_cancel_render", {});
  for (const name of [JOB, LONG_JOB]) await callBridge(bridge, "mrq_delete_job", { jobName: name });
  for (const asset of [SEQ, LONG_SEQ]) await callBridge(bridge, "delete_asset", { assetPath: asset, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "bcov-mrq-"));
  await call("demo_go_home", { discardUnsaved: true });
  mapPath = String((await call("get_current_level")).mapPackagePath);
  expect(mapPath.startsWith("/Game/")).toBe(true);
  await cleanup();
  await call("create_level_sequence", { name: "LS_BCovMRQ", packagePath: DIR });
  await call("set_sequence_playback_range", { sequencePath: SEQ, startSeconds: 0, endSeconds: 1 });
  await call("create_level_sequence", { name: "LS_BCovMRQLong", packagePath: DIR });
  await call("set_sequence_playback_range", { sequencePath: LONG_SEQ, startSeconds: 0, endSeconds: 60 });
}, 180_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await cleanup();
  } finally {
    if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
    disconnectBridge();
  }
}, 180_000);

describe("Movie Render Queue jobs", () => {
  it("mrq_create_job adds a preset job with the requested output settings", async () => {
    const created = await call("mrq_create_job", {
      sequence: SEQ, map: mapPath, jobName: JOB, outputDirectory: scratch,
      width: 64, height: 64, format: "png", startFrame: 0, endFrame: 3,
      fileNameFormat: "{sequence_name}.{frame_number}",
    });
    expect(created.created).toBe(true);
    expect((created.rollback as Json).method).toBe("mrq_delete_job");
    const job = created.job as Job;
    expect(job.jobName).toBe(JOB);
    expect(job.width).toBe(64);
    expect(job.height).toBe(64);
    expect(job.startFrame).toBe(0);
    expect(job.endFrame).toBe(3);
    expect(job.outputFormats).toContain("MoviePipelineImageSequenceOutput_PNG");
    expect((job.renderPasses ?? []).length).toBeGreaterThan(0);

    const listed = (await jobs()).find((j) => j.jobName === JOB);
    expect(listed, "the job is in the queue").toBeDefined();
    expect(norm(String(listed!.outputDirectory))).toBe(norm(scratch));
  }, 120_000);

  it("a second create with the same name reports existed and adds nothing", async () => {
    const before = (await jobs()).length;
    const again = await call("mrq_create_job", { sequence: SEQ, map: mapPath, jobName: JOB });
    expect(again.existed).toBe(true);
    expect((await jobs()).length).toBe(before);
  }, 120_000);

  it("refuses a preset and a graph together, and output overrides on a graph job", async () => {
    const both = await raw("mrq_create_job", { sequence: SEQ, jobName: "BCovMRQ_Bad", preset: "/Game/X", graph: "/Game/Y" });
    expect(succeeded(both)).toBe(false);
    const graphOverride = await raw("mrq_create_job", { sequence: SEQ, jobName: "BCovMRQ_Bad", graph: "/Game/Y", width: 64, height: 64 });
    expect(succeeded(graphOverride)).toBe(false);
    expect((await jobs()).some((j) => j.jobName === "BCovMRQ_Bad")).toBe(false);
  }, 60_000);
});

describe("Movie Render Queue rendering", () => {
  it("mrq_render waits for the render and reports the frames it wrote", async () => {
    const res = await call("mrq_render", { jobNames: [JOB], wait: true }, 900_000);
    expect(res.rendered, JSON.stringify(res.render)).toBe(true);
    const render = res.render as Json;
    expect(render.finished).toBe(true);
    expect(render.cancelled).toBe(false);
    const outputs = render.outputs as Array<{ path: string }>;
    const pngs = outputs.filter((o) => o.path.toLowerCase().endsWith(".png"));
    expect(pngs.length).toBeGreaterThanOrEqual(3);
    for (const file of pngs) expect(fs.existsSync(file.path), file.path).toBe(true);
    expect(pngs.every((o) => norm(o.path).startsWith(norm(scratch)))).toBe(true);
  }, 900_000);

  it("mrq_get_render_status reports the finished render as the last one", async () => {
    const status = await call("mrq_get_render_status");
    expect(status.isRendering).toBe(false);
    const last = status.lastRender as Json;
    expect(last.finished).toBe(true);
    expect(last.jobNames).toEqual([JOB]);
    expect(Number(last.outputCount)).toBeGreaterThanOrEqual(3);
  }, 60_000);

  it("mrq_list_outputs lists the job's files on disk", async () => {
    const listed = await call("mrq_list_outputs", { jobName: JOB });
    const files = listed.files as Array<{ path: string; sizeBytes: number }>;
    expect(files.filter((f) => f.path.toLowerCase().endsWith(".png")).length).toBeGreaterThanOrEqual(3);
    expect(files.every((f) => f.sizeBytes > 0)).toBe(true);
    const byFolder = await call("mrq_list_outputs", { outputDirectory: scratch, limit: 1 });
    expect(byFolder.returned).toBe(1);
    expect(Number(byFolder.count)).toBeGreaterThanOrEqual(3);
  }, 60_000);

  it("mrq_cancel_render stops a long render started with wait=false", async () => {
    await call("mrq_create_job", { sequence: LONG_SEQ, map: mapPath, jobName: LONG_JOB, outputDirectory: path.join(scratch, "long"), width: 64, height: 64 });
    const started = await call("mrq_render", { jobNames: [LONG_JOB], wait: false });
    expect(started.started).toBe(true);
    expect((await call("mrq_get_render_status")).isRendering).toBe(true);

    const cancelled = await call("mrq_cancel_render");
    expect(cancelled.cancelled).toBe(true);
    let status: Json = {};
    for (let i = 0; i < 240; i++) {
      status = await call("mrq_get_render_status");
      if (status.isRendering === false && (status.lastRender as Json | undefined)?.finished === true) break;
      await sleep(500);
    }
    expect(status.isRendering).toBe(false);
    const last = status.lastRender as Json;
    expect(last.cancelled).toBe(true);
    expect(last.jobNames).toEqual([LONG_JOB]);

    const again = await call("mrq_cancel_render");
    expect(again.alreadyStopped).toBe(true);
  }, 300_000);

  it("mrq_delete_job removes the job and a second delete reports alreadyRemoved", async () => {
    const removed = await call("mrq_delete_job", { jobName: LONG_JOB });
    expect(removed.alreadyRemoved).toBe(false);
    expect((removed.rollback as Json).method).toBe("mrq_create_job");
    expect((await jobs()).some((j) => j.jobName === LONG_JOB)).toBe(false);
    const again = await call("mrq_delete_job", { jobName: LONG_JOB });
    expect(again.alreadyRemoved).toBe(true);
  }, 60_000);
});
