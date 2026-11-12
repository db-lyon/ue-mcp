// @live-editor headless
/**
 * Walking the PIE player (#1565): pie_drive_pawn pushes movement input every
 * frame and samples the pawn, pie_drive_status reads the track, pie_drive_stop
 * ends a drive, and get_pie_pawn and find_nav_path read the running session.
 * The pawn is teleported onto a floor of its own first, so nothing in the
 * level is in its way. PIE is stopped in a finally block.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const FLOOR = "Drive1565_Floor";
const HOME = { x: 0, y: 24000, z: 0 };

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const vec = (v: unknown): Vec => {
  const o = (v ?? {}) as Record<string, number>;
  return { x: Number(o.x), y: Number(o.y), z: Number(o.z) };
};

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

async function waitForPie(playing: boolean): Promise<boolean> {
  for (let i = 0; i < 60; i++) {
    if ((await pieRunning()) === playing) return true;
    await sleep(500);
  }
  return false;
}

async function stopPie(): Promise<void> {
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    await waitForPie(false);
  }
}

/** Polls the drive the way the server's wait does, and returns its finished report. */
async function driveToEnd(driveId: string, budgetMs: number): Promise<Json> {
  const deadline = Date.now() + budgetMs;
  for (;;) {
    const status = await call("pie_drive_status", { driveId });
    if (status.running !== true) return status;
    if (Date.now() > deadline) throw new Error(`drive ${driveId} still running after ${budgetMs} ms`);
    await sleep(200);
  }
}

/** Puts the player pawn just above the test floor, facing +X. */
async function placePawn(): Promise<Vec> {
  const pawn = await call("get_pie_pawn", {});
  const at = { x: HOME.x, y: HOME.y, z: HOME.z + 200 };
  await call("teleport_runtime_actor", { actorPath: pawn.path, location: at, rotation: { pitch: 0, yaw: 0, roll: 0 } });
  await sleep(500);
  return vec((await call("get_pie_pawn", {})).location);
}

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  await callBridge(bridge, "delete_actor", { actorLabel: FLOOR });
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: FLOOR, location: HOME, scale: { x: 30, y: 30, z: 0.5 } });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
  } finally {
    await callBridge(bridge, "delete_actor", { actorLabel: FLOOR });
    disconnectBridge();
  }
}, 120_000);

describe("driving the PIE player", () => {
  let playing = false;

  beforeAll(async () => {
    await call("pie_control", { pieAction: "start" });
    playing = await waitForPie(true);
  }, 120_000);

  afterAll(async () => {
    await stopPie();
  }, 120_000);

  it("get_pie_pawn reports the controller rotation and the map the pawn is in", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    await call("pie_set_player_view", { pitch: 0, yaw: 25, roll: 0 });
    const pawn = await call("get_pie_pawn", {});
    expect(Number((pawn.controlRotation as Json).yaw)).toBeCloseTo(25, 0);
    expect(String(pawn.map)).toMatch(/^\/(Game|Engine|Temp)\//);
    expect(String(pawn.map)).not.toContain("UEDPIE");
    expect(String(pawn.worldPath)).toContain("UEDPIE");
  }, 60_000);

  it("pie_drive_pawn along a direction moves the pawn that way and samples the track", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const start = await placePawn();
    const started = await call("pie_drive_pawn", { direction: { x: 1, y: 0, z: 0 }, durationSeconds: 1.5, sampleEvery: 0.1 });
    expect(started.running).toBe(true);
    const done = await driveToEnd(String(started.driveId), 20_000);

    expect(done.state).toBe("completed");
    const samples = done.samples as Json[];
    expect(samples.length).toBeGreaterThanOrEqual(5);
    const xs = samples.map((s) => vec(s.location).x);
    expect(xs[xs.length - 1] - start.x).toBeGreaterThan(50);
    for (let i = 1; i < xs.length; i++) expect(xs[i]).toBeGreaterThanOrEqual(xs[i - 1] - 1);
    expect(Number(done.distanceTravelled)).toBeGreaterThan(50);
    expect(done.finalMap).toBe(done.startMap);
    expect(done.mapChanges).toEqual([]);
    const now = vec((await call("get_pie_pawn", {})).location);
    expect(Math.abs(now.x - vec((done.final as Json).location).x)).toBeLessThan(100);
  }, 60_000);

  it("pie_drive_pawn toward a targetLocation ends as arrived inside the acceptance radius", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const start = await placePawn();
    const target = { x: start.x + 300, y: start.y + 150, z: start.z };
    const started = await call("pie_drive_pawn", { targetLocation: target, acceptanceRadius: 60, durationSeconds: 15 });
    const done = await driveToEnd(String(started.driveId), 30_000);

    expect(done.state).toBe("arrived");
    expect(Number(done.distanceToTarget)).toBeLessThanOrEqual(60);
    const end = vec((done.final as Json).location);
    expect(Math.hypot(end.x - target.x, end.y - target.y)).toBeLessThanOrEqual(60);
  }, 60_000);

  it("pie_drive_stop ends a running drive and reports alreadyStopped on a second stop", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    await placePawn();
    const started = await call("pie_drive_pawn", { direction: { x: 0, y: 1, z: 0 }, durationSeconds: 60, driveId: "drive_1565_stop" });
    expect(started.driveId).toBe("drive_1565_stop");
    await sleep(500);
    const stopped = await call("pie_drive_stop", { driveId: "drive_1565_stop" });
    expect(stopped.state).toBe("stopped");
    expect(stopped.alreadyStopped).toBe(false);
    expect(Number(stopped.elapsedSeconds)).toBeLessThan(30);

    const again = await call("pie_drive_stop", { driveId: "drive_1565_stop" });
    expect(again.alreadyStopped).toBe(true);
    const status = await call("pie_drive_status", { driveId: "drive_1565_stop", includeSamples: false });
    expect(status.state).toBe("stopped");
    expect(status.samples).toBeUndefined();
  }, 60_000);

  it("a navigate drive and find_nav_path read the PIE world's navigation", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const start = await placePawn();
    const target = { x: start.x + 400, y: start.y, z: start.z };

    const path = await call("find_nav_path", { start, end: target, world: "pie" });
    expect(path.world).toBe("pie");
    expect(path.map).toBe((await call("get_pie_pawn", {})).map);

    // This floor carries no navmesh, so the navigate drive must refuse to move rather than walk blind.
    const started = await call("pie_drive_pawn", { targetLocation: target, mode: "navigate", durationSeconds: 5 });
    if (path.valid === true) {
      const done = await driveToEnd(String(started.driveId), 20_000);
      expect(["arrived", "nav_ended"]).toContain(done.state);
      expect((done.navPath as Json).valid).toBe(true);
    } else {
      expect(started.state).toBe("no_path");
      expect((started.navPath as Json).valid).toBe(false);
    }
  }, 60_000);
});
