// @live-editor headless
/**
 * Actor performers (#2137): an agent drives an AI-controlled pawn through a clip's move, action and value lanes and
 * its checks pass, and a recording captures what the pawn actually did: where it went and the calls it took. The
 * pawn is a DefaultPawn placed in the probe arena, which an AI controller possesses in PIE. PIE is stopped after
 * every run. Playing a hub arrangement whose track drives the pawn is covered with the hub core (#2146).
 * Refs #2137
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { setupProbeArena, teardownProbeArena } from "./clip-fixture.js";

const DIR = "/Game/MCPTest/ActorLanes";
const GUARD = "UEMCP_Guard";
// The arena sits at (0, 60000, 4000); the guard stands beside the probe character, on the arena's floor.
const START = { x: 400, y: 60400, z: 4100 };

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

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await callBridge(bridge, "pie_clip_stop", {});
  await setupProbeArena(bridge, DIR, "ActorLanes", { chaser: false });
  await call("place_actor", { actorClass: "DefaultPawn", label: GUARD, location: START });
  await call("save_level", {});
}, 240_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await callBridge(bridge, "pie_clip_stop", {});
    await stopPie();
  } finally {
    await teardownProbeArena(bridge, DIR);
    disconnectBridge();
  }
}, 120_000);

describe("actor lanes", () => {
  it("drives an AI pawn through move, action and value lanes, and the checks on it pass", async () => {
    const text = [
      "clip rate=60 length=240",
      `performer actor:${GUARD}`,
      "lane l1 move",
      `  point p1 0 ${START.x + 600},${START.y},${START.z} radius=20`,
      "lane l2 action",
      '  call a1 30 SetActorScale3D ["(X=2,Y=2,Z=2)"]',
      "lane l3 value InitialLifeSpan axis1d",
      "  key k1 0 0",
      "  key k2 60 42",
      `check c1 0 240 by_end ${GUARD}.RootComponent.RelativeLocation.X increasing 300`,
      `check c2 0 120 by_end ${GUARD}.RootComponent.RelativeScale3D.X gt 1.5`,
      `check c3 0 120 by_end ${GUARD}.InitialLifeSpan gt 40`,
    ].join("\n");
    const made = await call("pie_clip_create", { name: "CL_Guard", package_path: DIR, text, onConflict: "replace" });
    expect(rows(made.warnings).filter((w) => w.field === "performer")).toEqual([]);
    const read = await call("pie_clip_read", { path: `${DIR}/CL_Guard` });
    expect(rows((read.clip as Json).lanes).map((l) => l.kind)).toEqual(["move", "action", "value"]);

    const started = await call("pie_clip_run", { path: `${DIR}/CL_Guard`, start_pie: true });
    let run: Json = {};
    for (let i = 0; i < 1200; i++) {
      run = await call("pie_clip_status", { run_id: started.run_id });
      if (run.state === "finished" || run.state === "stopped") break;
      await sleep(100);
    }
    expect(run.state, JSON.stringify(run)).toBe("finished");
    expect(rows(run.checks).map((c) => [c.id, c.status]), JSON.stringify(run.checks)).toEqual([["c1", "passed"], ["c2", "passed"], ["c3", "passed"]]);
    const lanes = rows(run.lanes);
    expect(lanes.map((l) => [l.id, l.kind, (l.performer as Json).actor]), JSON.stringify(lanes)).toEqual([
      ["l1", "move", GUARD],
      ["l2", "action", GUARD],
      ["l3", "value", GUARD],
    ]);
    for (const l of lanes) expect(rows(l.errors), JSON.stringify(l)).toEqual([]);
    expect(lanes.every((l) => Number(l.fired) > 0)).toBe(true);
    expect(run.passed).toBe(true);
    await stopPie();
  }, 240_000);

  it("records where the pawn went and the calls it took into armed lanes", async () => {
    const at = `"(X=${START.x + 500},Y=${START.y},Z=${START.z})"`;
    const text = [
      "clip rate=60 length=120",
      `performer actor:${GUARD}`,
      'region r1 0 120 "Take"',
      "lane l1 move",
      "lane l2 action",
      `  call a1 30 K2_SetActorLocation [${at},false,true]`,
      "lane l3 action watch=K2_SetActorLocation",
      "check c1 0 0 by_end pos_z gt -100000",
    ].join("\n");
    await call("pie_clip_create", { name: "CL_Record", package_path: DIR, text, onConflict: "replace" });
    await stopPie();
    const rec = await call("pie_clip_record", { path: `${DIR}/CL_Record`, region: "r1", armed: ["l1", "l3"] });
    expect(rec.mode).toBe("record");
    let status: Json = {};
    for (let i = 0; i < 600; i++) {
      status = await call("pie_clip_status", { run_id: rec.run_id });
      if (status.state === "finished" || status.state === "stopped") break;
      await sleep(100);
    }
    expect(status.state, JSON.stringify(status)).toBe("finished");
    expect((status.take as Json).armed_lanes).toEqual(["l1", "l3"]);
    expect((status.take as Json).saved, JSON.stringify(status.take)).toBe(true);
    await stopPie();

    const clip = (await call("pie_clip_read", { path: `${DIR}/CL_Record` })).clip as Json;
    const lanes = rows(clip.lanes);
    const moved = rows(lanes.find((l) => l.id === "l1")?.points);
    expect(moved.length, JSON.stringify(lanes)).toBeGreaterThan(0);
    const last = moved[moved.length - 1].at as number[];
    expect(Math.abs(last[0] - (START.x + 500)), JSON.stringify(moved)).toBeLessThan(5);
    const calls = rows(lanes.find((l) => l.id === "l3")?.calls);
    expect(calls.map((c) => c.fn), JSON.stringify(lanes)).toEqual(["K2_SetActorLocation"]);
    expect(Math.abs(Number(calls[0].t) - 30), JSON.stringify(calls)).toBeLessThan(3);
  }, 240_000);
});
