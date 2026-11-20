// @live-editor headless
/**
 * Test script place locations and script deletion, read back from the
 * running pawn and from disk.
 *
 * - A place act takes location as [x, y, z] or {x, y, z}, and the pawn ends
 *   up there. Anything else is refused when the script is saved, with the
 *   accepted shape in the error, instead of teleporting the pawn to the world
 *   origin with ok:true (#1813).
 * - script_delete says deleted only when the .uasset has left the disk, and
 *   refuses while PIE runs, when the asset library cannot see or delete
 *   assets (#1815).
 */
import fs from "node:fs";
import path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import { TEST_PROJECT_DIR } from "../../scripts/bridge-target.mjs";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/PieScriptPlace";
const SCRIPT = `${DIR}/TS_PlaceDelete`;
const SCRIPT_FILE = path.join(TEST_PROJECT_DIR, "Content", "MCPTest", "PieScriptPlace", "TS_PlaceDelete.uasset");
const AS_OBJECT = { x: -2500, y: 3000, z: 600 };
const AS_ARRAY = [2500, -3000, 600];

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

async function refused(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method} should be refused: ${JSON.stringify(r.result)}`).toBe(false);
  return { ...body(r), error: String(body(r).error ?? r.error ?? "") };
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

async function until(runId: string, done: (s: Json) => boolean, tries = 160): Promise<Json> {
  let status: Json = {};
  for (let i = 0; i < tries; i++) {
    status = await call("pie_script_status", { run_id: runId });
    if (done(status)) return status;
    await sleep(250);
  }
  return status;
}

const placeStep = (location: unknown) => ({ instruction: "The pawn stands where it was placed", judge: "manual", act: [{ place: { location, yaw: 90 } }] });

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
  } finally {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("place location", () => {
  it("script_create refuses a location that is not three numbers, and the error shows the accepted shape", async () => {
    for (const bad of [{ x: -2500, y: 0 }, [-2500, 0], "-2500,0,200", { x: "a", y: 0, z: 0 }, { x: 1, y: 2, z: 3, w: 4 }]) {
      const r = await refused("pie_script_create", { name: "TS_PlaceBad", package_path: DIR, steps: [placeStep(bad)] });
      expect(r.error, JSON.stringify(bad)).toMatch(/act\[0\]: place location must be \[x, y, z\] or \{x, y, z\}/);
      expect(r.error).toContain("[-2500, 0, 200]");
    }
    const offset = await call("pie_script_validate", { steps: [{ instruction: "x", judge: "manual", act: [{ place: { at: "Anything", offset: { x: 1 } } }] }] });
    expect(offset.valid).toBe(false);
    expect(JSON.stringify(offset.errors)).toMatch(/place offset must be \[x, y, z\] or \{x, y, z\}/);
    expect(fs.existsSync(path.join(path.dirname(SCRIPT_FILE), "TS_PlaceBad.uasset"))).toBe(false);
  }, 60_000);

  it("a place act puts the pawn at a location written as {x, y, z} or as [x, y, z]", async () => {
    const created = await call("pie_script_create", { name: "TS_PlaceDelete", package_path: DIR, steps: [placeStep(AS_OBJECT), placeStep(AS_ARRAY)] });
    expect(created.created).toBe(true);
    const stored = rows((await call("pie_script_read", { path: SCRIPT })).steps);
    expect(obj(obj(rows(stored[0].act)[0]).place).location).toEqual(AS_OBJECT);

    expect(await startPie(), "PIE did not start").toBe(true);
    const runId = String((await call("pie_script_run", { script: SCRIPT })).run_id);

    for (const [index, [x, y]] of [[0, [AS_OBJECT.x, AS_OBJECT.y]], [1, [AS_ARRAY[0], AS_ARRAY[1]]]] as const) {
      const status = await until(runId, (s) => rows(s.awaiting).some((a) => a.index === index) || s.state === "finished");
      const act = obj(rows(status.steps)[index]?.act_result);
      expect(act.status, JSON.stringify(act)).toBe("done");
      expect(rows(act.actions).map((a) => [a.action, a.ok])).toEqual([["place", true]]);
      const at = obj((await call("get_pie_pawn", {})).location);
      expect(Math.abs(Number(at.x) - x), `step ${index} x: ${JSON.stringify(at)}`).toBeLessThan(400);
      expect(Math.abs(Number(at.y) - y), `step ${index} y: ${JSON.stringify(at)}`).toBeLessThan(400);
      await call("pie_script_verdict", { run_id: runId, step: index, verdict: "pass", note: "placed" });
    }
    expect((await until(runId, (s) => s.state === "finished")).state).toBe("finished");
  }, 180_000);
});

describe("script_delete", () => {
  it("refuses while PIE runs and leaves the script in place", async () => {
    if (!(await pieRunning())) expect(await startPie(), "PIE did not start").toBe(true);
    const r = await refused("pie_script_delete", { path: SCRIPT, confirm: true });
    expect(r.reason).toBe("play_in_editor");
    expect(r.error).toMatch(/play-in-editor/);
    await stopPie();
    expect(fs.existsSync(SCRIPT_FILE)).toBe(true);
    expect(rows((await call("pie_script_read", { path: SCRIPT })).steps)).toHaveLength(2);
  }, 120_000);

  it("reports deleted only once the .uasset is gone from disk", async () => {
    await stopPie();
    expect(fs.existsSync(SCRIPT_FILE)).toBe(true);
    const deleted = await call("pie_script_delete", { path: SCRIPT, confirm: true });
    expect(deleted.deleted).toBe(true);
    expect(fs.existsSync(SCRIPT_FILE)).toBe(false);
    await refused("pie_script_read", { path: SCRIPT });
    expect((await call("pie_script_delete", { path: SCRIPT, confirm: true })).already_deleted).toBe(true);
  }, 60_000);
});
