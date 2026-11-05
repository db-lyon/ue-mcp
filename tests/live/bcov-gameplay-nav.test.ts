// @live-editor headless
/**
 * Navigation in the editor world: a navmesh built over a floor the test lays
 * down, point projection and path queries against it, a NavModifierVolume that
 * cuts a hole the path has to route around, and nav invoker discovery.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

/** Far from anything else a test places, so the navmesh here is this file's own. */
const CX = 40000;
const CY = 40000;
const FLOOR = "BCov_Gameplay_NavFloor";
const BOUNDS = "BCov_Gameplay_NavBounds";
const HOLE = "BCov_Gameplay_NavHole";
const INVOKER = "BCov_Gameplay_NavInvoker";

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Row[] => (Array.isArray(v) ? (v as Row[]) : []);
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));
const at = (dx: number, dy: number): Vec => ({ x: CX + dx, y: CY + dy, z: 50 });

let bridge: EditorBridge;
/** Navigation data actors that existed before this file, so only new ones are removed. */
let priorNavData: string[] = [];

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** Poll until the predicate holds or the deadline passes; returns the last value. */
async function until<T>(read: () => Promise<T>, done: (v: T) => boolean, timeoutMs = 60_000): Promise<T> {
  const deadline = Date.now() + timeoutMs;
  let value = await read();
  while (!done(value) && Date.now() < deadline) {
    await sleep(500);
    value = await read();
  }
  return value;
}

const project = (p: Vec) => call("project_point_to_navigation", { location: p });
const crossFloor = () => call("find_nav_path", { start: at(-1000, 0), end: at(1000, 0) });

async function wipeActors(): Promise<void> {
  for (const label of [HOLE, INVOKER, BOUNDS, FLOOR]) await callBridge(bridge, "delete_actor", { actorLabel: label });
}

beforeAll(async () => {
  bridge = await getBridge();
  await wipeActors();
  const info = body(await callBridge(bridge, "get_navmesh_info"));
  priorNavData = rows(info.navData).map((d) => String(d.name));
  // A 3000 x 3000 slab whose top face sits at z = 0.
  await call("place_actor", {
    actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: FLOOR,
    location: { x: CX, y: CY, z: -50 }, scale: { x: 30, y: 30, z: 1 },
  });
  await call("spawn_volume", {
    volumeType: "navmesh", label: BOUNDS, location: { x: CX, y: CY, z: 0 }, extent: { x: 1600, y: 1600, z: 400 },
  });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await wipeActors();
    const info = body(await callBridge(bridge, "get_navmesh_info"));
    for (const d of rows(info.navData)) {
      const name = String(d.name);
      if (!priorNavData.includes(name)) await callBridge(bridge, "delete_actor", { actorLabel: name });
    }
    disconnectBridge();
  }
});

describe("navmesh build and queries", () => {
  it("rebuild_navigation builds a navmesh project_point_to_navigation, get_navmesh_info and get_navmesh_details see", async () => {
    const rebuilt = await call("rebuild_navigation");
    expect(rebuilt.status).toBe("rebuild_triggered");
    const projected = await until(() => project(at(0, 0)), (r) => r.projected === true);
    expect(projected.projected).toBe(true);
    const point = projected.projectedLocation as Vec;
    expect(Math.abs(point.x - CX)).toBeLessThan(5);
    expect(Math.abs(point.z)).toBeLessThan(60);
    // Off the floor there is nothing to project onto.
    expect((await project({ x: CX + 5000, y: CY, z: 50 })).projected).toBe(false);

    const info = await call("get_navmesh_info");
    expect(info.status).toBe("active");
    expect(rows(info.navData).some((d) => String(d.class).includes("RecastNavMesh"))).toBe(true);
    const details = await call("get_navmesh_details");
    expect(Number(details.navMeshBoundsVolumeCount)).toBeGreaterThanOrEqual(1);
    expect(Number(details.agentRadius)).toBeGreaterThan(0);
  }, 120_000);

  it("find_nav_path returns a straight path across the open floor", async () => {
    const result = await until(() => crossFloor(), (r) => r.valid === true);
    expect(result.valid).toBe(true);
    expect(result.partial).toBe(false);
    expect(Number(result.length)).toBeGreaterThan(1990);
    expect(Number(result.length)).toBeLessThan(2100);
    expect(rows(result.points).length).toBeGreaterThanOrEqual(2);
  }, 120_000);

  it("spawn_nav_modifier_volume cuts a hole that projection misses and the path routes around", async () => {
    const spawned = await call("spawn_nav_modifier_volume", {
      label: HOLE, location: { x: CX, y: CY, z: 0 }, extent: { x: 300, y: 1200, z: 300 }, areaClass: "NavArea_Null",
    });
    expect(spawned.areaClass).toBe("NavArea_Null");
    expect(Number((spawned.extent as Vec).y)).toBeCloseTo(1200, 0);

    await call("rebuild_navigation");
    const hole = await until(() => project(at(0, 0)), (r) => r.projected === false);
    expect(hole.projected).toBe(false);
    const around = await until(() => crossFloor(), (r) => r.valid === true && Number(r.length) > 2300);
    expect(around.valid).toBe(true);
    expect(Number(around.length)).toBeGreaterThan(2300);
  }, 180_000);
});

describe("nav invokers", () => {
  it("list_nav_invokers finds an actor carrying a NavigationInvokerComponent", async () => {
    const before = await call("list_nav_invokers");
    expect(rows(before.invokers).some((i) => i.actorLabel === INVOKER)).toBe(false);
    await call("place_actor", {
      actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: INVOKER, location: at(1200, 1200),
    });
    await call("add_component_to_actor", { actorLabel: INVOKER, componentClass: "NavigationInvokerComponent", componentName: "Invoker" });
    const after = await call("list_nav_invokers");
    const entry = rows(after.invokers).find((i) => i.actorLabel === INVOKER);
    expect(entry?.componentClass).toBe("NavigationInvokerComponent");
    expect(Number(entry?.tileGenerationRadius)).toBeGreaterThan(0);
    expect(Number(after.count)).toBe(Number(before.count) + 1);
  }, 120_000);
});
