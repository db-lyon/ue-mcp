// @live-editor headless
/**
 * Level actions that need a map of their own, run in scratch maps under /Game/MCPTest/BCov/Level: streaming
 * sub-levels and the current edit level, world settings, navmesh projection, exact-label deletes across maps
 * and World Partition actor descriptors. The map open before the file is reopened afterwards. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOME = "/Game/MCP_Home";
const DIR = `${TEST_PREFIX}/BCov/Level`;
const SUB_NAME = "L_BCovLevel_Sub";
const SUB = `${DIR}/${SUB_NAME}`;
const PERSISTENT = `${DIR}/L_BCovLevel_Persistent`;
const TARGET = `${DIR}/L_BCovLevel_Target`;
const WP = `${DIR}/L_BCovLevel_WP`;
const MAPS = [SUB, PERSISTENT, TARGET, WP];
const CUBE = "/Engine/BasicShapes/Cube.Cube";

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

let bridge: EditorBridge;
let originalMap: string | undefined;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function currentMap(): Promise<string | undefined> {
  return body(await callBridge(bridge, "get_current_level", {})).mapPackagePath as string | undefined;
}

/** Leave for the home map, then create a fresh map at path and open it. */
async function openNewMap(path: string, extra: Json = {}): Promise<Json> {
  await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  const created = await call("create_new_level", { levelPath: path, ...extra });
  expect(await currentMap()).toBe(path);
  return created;
}

async function cube(label: string, location: Vec, extra: Json = {}): Promise<void> {
  await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label, staticMesh: CUBE, location, ...extra });
}

const countLabelled = async (prefix: string) => Number((await call("get_actors_by_class", { className: "StaticMeshActor", labelPrefix: prefix })).count);

beforeAll(async () => {
  bridge = await getBridge();
  originalMap = await currentMap();
  // MCP_Home is not checked in; demo_go_home creates it on a fresh project and loads it.
  await call("demo_go_home");
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  const back = originalMap && originalMap.startsWith("/Game/") && !originalMap.startsWith(DIR) ? originalMap : HOME;
  const restored = await callBridge(bridge, "load_level", { levelPath: back, discardUnsaved: true });
  if (!succeeded(restored)) await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  for (const path of MAPS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  disconnectBridge();
}, 300_000);

describe("streaming sub-levels and the current edit level", () => {
  beforeAll(async () => {
    await openNewMap(SUB);
    await openNewMap(PERSISTENT);
  }, 300_000);

  it("add_streaming_sublevel adds a sub-level list_streaming_sublevels and list_levels report", async () => {
    const added = await call("add_streaming_sublevel", { levelPath: SUB, initiallyLoaded: true, initiallyVisible: true });
    expect(added.levelName).toBe(SUB_NAME);
    const listed = await call("list_streaming_sublevels");
    expect(listed.count).toBe(1);
    const row = (listed.sublevels as Json[])[0];
    expect(row.packageName).toBe(SUB);
    expect(row.levelName).toBe(SUB_NAME);
    expect(row.loaded).toBe(true);

    const levels = (await call("list_levels")).levels as Json[];
    expect(levels.some((l) => l.type === "persistent")).toBe(true);
    expect(levels.some((l) => l.type === "streaming" && String(l.name).endsWith(SUB_NAME))).toBe(true);
  }, 120_000);

  it("set_current_edit_level switches the level get_current_edit_level reports", async () => {
    const before = await call("get_current_edit_level");
    expect(before.isPersistent).toBe(true);
    await call("set_current_edit_level", { levelName: SUB_NAME });
    const during = await call("get_current_edit_level");
    expect(during.levelName).toBe(SUB_NAME);
    expect(during.isPersistent).toBe(false);
    await call("set_current_edit_level", { levelName: String(before.levelName) });
    expect((await call("get_current_edit_level")).isPersistent).toBe(true);
  }, 120_000);

  it("set_streaming_sublevel_properties writes visibility and offset", async () => {
    await call("set_streaming_sublevel_properties", { levelName: SUB_NAME, initiallyVisible: false, location: { x: 1000, y: 0, z: 0 } });
    const row = ((await call("list_streaming_sublevels")).sublevels as Json[])[0];
    expect(row.initiallyVisible).toBe(false);
    expect((row.location as Vec).x).toBeCloseTo(1000, 1);
  }, 120_000);

  it("remove_streaming_sublevel removes it from the persistent level", async () => {
    const out = await call("remove_streaming_sublevel", { levelName: SUB_NAME });
    expect(out.removed).toBe(true);
    expect((await call("list_streaming_sublevels")).count).toBe(0);
  }, 120_000);
});

describe("world settings and navigation in a scratch map", () => {
  it("set_world_settings writes values get_world_settings reads", async () => {
    expect(await currentMap()).toBe(PERSISTENT);
    const before = await call("get_world_settings");
    const out = await call("set_world_settings", { killZ: -4321, globalGravityZ: -500 });
    expect(out.changes).toEqual(expect.arrayContaining(["killZ", "globalGravityZ"]));
    const after = await call("get_world_settings");
    expect(Number(after.killZ)).toBeCloseTo(-4321, 1);
    expect(Number(after.globalGravityZ)).toBeCloseTo(-500, 1);
    await call("set_world_settings", { killZ: before.killZ, globalGravityZ: before.globalGravityZ });
    expect(Number((await call("get_world_settings")).killZ)).toBeCloseTo(Number(before.killZ), 1);
  }, 120_000);

  it("nav_project_point projects onto a navmesh built over a floor", async () => {
    // Cube is 100 cm: scale 30 x 30 x 1 at z 0 puts a 3000 cm floor's top face at z 50.
    await cube("BCov_Level_NavFloor", { x: 0, y: 0, z: 0 }, { scale: { x: 30, y: 30, z: 1 } });
    await call("spawn_volume", { volumeType: "NavMeshBoundsVolume", label: "BCov_Level_NavBounds", location: { x: 0, y: 0, z: 0 }, extent: { x: 2000, y: 2000, z: 500 } });
    await call("rebuild_navigation");
    let out: Json = {};
    for (let i = 0; i < 60; i++) {
      out = await call("nav_project_point", { point: { x: 200, y: 300, z: 200 }, extent: { x: 100, y: 100, z: 500 } });
      if (out.onNavMesh === true) break;
      await sleep(1000);
    }
    expect(out.onNavMesh).toBe(true);
    const projected = out.projectedLocation as Vec;
    expect(projected.x).toBeCloseTo(200, -1);
    expect(projected.z).toBeGreaterThan(30);
    expect(projected.z).toBeLessThan(150);
    const off = await call("nav_project_point", { point: { x: 9000, y: 9000, z: 200 }, extent: { x: 100, y: 100, z: 100 } });
    expect(off.onNavMesh).toBe(false);
  }, 120_000);
});

describe("exact-label deletes across maps", () => {
  beforeAll(async () => {
    await openNewMap(TARGET);
    await cube("BCov_Level_DelMe", { x: 0, y: 0, z: 100 });
    await cube("BCov_Level_Keep", { x: 300, y: 0, z: 100 });
    await call("save_level");
    // The action refuses while any package is dirty; earlier files in the run can leave some.
    await call("save_all_dirty", {});
  }, 300_000);

  it("delete_exact_labeled_actors_in_levels previews, then deletes and saves only the named actor", async () => {
    const levels = [{ levelPath: TARGET, actorLabels: ["BCov_Level_DelMe"], expectedClassPath: "/Script/Engine.StaticMeshActor" }];
    const preview = await call("delete_exact_labeled_actors_in_levels", { levels, dryRun: true });
    expect(preview.matchedActors).toBe(1);
    expect(preview.deletedActors).toBe(0);
    expect(await countLabelled("BCov_Level_DelMe")).toBe(1);

    const out = await call("delete_exact_labeled_actors_in_levels", { levels, dryRun: false });
    expect(out.deletedActors).toBe(1);
    expect(out.restoredOriginalLevel).toBe(true);
    expect(await currentMap()).toBe(TARGET);
    expect(await countLabelled("BCov_Level_DelMe")).toBe(0);
    expect(await countLabelled("BCov_Level_Keep")).toBe(1);
  }, 300_000);
});

describe("World Partition actor descriptors", () => {
  const LABEL = "BCov_Level_WPActor";
  let guid = "";

  beforeAll(async () => {
    const created = await openNewMap(WP, { worldPartition: true });
    expect(created.worldPartition).toBe(true);
    await cube(LABEL, { x: 0, y: 0, z: 100 });
    await call("save_level");
    // Reopened from disk, as a saved World Partition map is used: the descriptors and the
    // pinned-actor loader belong to the loaded map, not to the one just created in memory.
    await call("load_level", { levelPath: HOME });
    await call("load_level", { levelPath: WP });
  }, 300_000);

  it("list_actor_descs finds the saved actor's descriptor", async () => {
    const out = await call("list_actor_descs", { filter: LABEL });
    expect(out.matched).toBe(1);
    const desc = (out.actorDescs as Json[])[0];
    expect(desc.label).toBe(LABEL);
    guid = String(desc.guid);
    // Reopened from disk, the actor is a descriptor until something loads it; pinning below does.
    expect(desc.loaded).toBe(false);
    expect(guid).not.toBe("");
  }, 120_000);

  it("load_actor_descs pins and unpins the descriptor", async () => {
    const pinned = await call("load_actor_descs", { mode: "pin", guids: [guid] });
    expect(pinned.matched).toBe(1);
    expect(pinned.pinStateChanged).toBe(1);
    expect(pinned.residentAfter).toBe(1);
    // Pinning again finds it already pinned.
    expect((await call("load_actor_descs", { mode: "pin", guids: [guid] })).pinStateChanged).toBe(0);

    const unpinned = await call("load_actor_descs", { mode: "unpin", guids: [guid] });
    expect(unpinned.pinStateChanged).toBe(1);
    expect((await call("load_actor_descs", { mode: "unpin", guids: [guid] })).pinStateChanged).toBe(0);
  }, 120_000);
});
