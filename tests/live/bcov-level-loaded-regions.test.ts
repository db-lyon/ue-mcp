// @live-editor headless
/**
 * Editor loaded regions on a World Partition map: load_region makes a user loaded region (the World Partition
 * editor's Load Region from Selection), list_loaded_regions reports it, and unload_loaded_regions releases the
 * actors it holds, which load_actor_descs(mode=unpin) cannot. Runs in a scratch map under
 * /Game/MCPTest/BCov/LoadedRegions and reopens the map that was open before the file.
 *
 * The engine symbols behind these actions were written without engine source to read (see the header of
 * LevelHandlers_WorldPartition.cpp), so this file is what proves them: every assertion on residentAfter is read
 * back from the world, not from the request.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOME = "/Game/MCP_Home";
const DIR = `${TEST_PREFIX}/BCov/LoadedRegions`;
const WP = `${DIR}/L_LoadedRegions_WP`;
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const LABEL_A = "LR_ActorA";
const LABEL_B = "LR_ActorB";

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

/** Two cubes two cells apart, and boxes around each, so a region over one never holds the other. */
const box = (cx: number): { min: Vec; max: Vec } => ({ min: { x: cx - 500, y: -500, z: -500 }, max: { x: cx + 500, y: 500, z: 1000 } });
const BOX_A = box(0);
const BOX_B = box(20000);
const BOX_BOTH = { min: BOX_A.min, max: BOX_B.max };

let bridge: EditorBridge;
let originalMap: string | undefined;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** A call the handler is expected to refuse; returns the message and whatever body came with it. */
async function refused(method: string, params: Json = {}): Promise<{ message: string; body: Json }> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method} should have been refused`).toBe(false);
  return { message: r.error ?? String(body(r).error ?? ""), body: body(r) };
}

async function currentMap(): Promise<string | undefined> {
  return body(await callBridge(bridge, "get_current_level", {})).mapPackagePath as string | undefined;
}

async function cube(label: string, location: Vec): Promise<void> {
  await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label, staticMesh: CUBE, location });
}

const countLabelled = async (prefix: string) =>
  Number((await call("get_actors_by_class", { className: "StaticMeshActor", labelPrefix: prefix })).count);

const regionRow = (list: Json, id: string): Json | undefined => (list.regions as Json[]).find((r) => r.id === id);

beforeAll(async () => {
  bridge = await getBridge();
  originalMap = await currentMap();
  await call("demo_go_home");
  await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: WP, force: true });
  const created = await call("create_new_level", { levelPath: WP, worldPartition: true });
  expect(created.worldPartition).toBe(true);
  await cube(LABEL_A, { x: 0, y: 0, z: 100 });
  await cube(LABEL_B, { x: 20000, y: 0, z: 100 });
  await call("save_level");
  // Reopened from disk, as a saved World Partition map is used: nothing is resident until a loader holds it.
  await call("load_level", { levelPath: HOME });
  await call("load_level", { levelPath: WP });
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  const back = originalMap && originalMap.startsWith("/Game/") && !originalMap.startsWith(DIR) ? originalMap : HOME;
  const restored = await callBridge(bridge, "load_level", { levelPath: back, discardUnsaved: true });
  if (!succeeded(restored)) await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: WP, force: true });
  disconnectBridge();
}, 300_000);

describe("a loaded region keeps actors resident, and list_loaded_regions says so", () => {
  let regionId = "";

  it("load_region previews, then loads a box as a user region and returns the inverse", async () => {
    expect(await countLabelled("LR_Actor")).toBe(0);

    const preview = await call("load_region", { bounds: BOX_A, dryRun: true });
    expect(preview.wouldCreate).toBe(true);
    expect(Number(preview.actorCount)).toBeGreaterThanOrEqual(1);
    expect(preview.changed).toBe(false);
    expect(await countLabelled(LABEL_A)).toBe(0);

    const loaded = await call("load_region", { bounds: BOX_A, label: "LR Box A" });
    regionId = String(loaded.id);
    expect(regionId).not.toBe("");
    expect(loaded.created).toBe(true);
    expect(loaded.kind).toBe("userRegion");
    expect(loaded.loaded).toBe(true);
    expect(Number(loaded.actorCount)).toBeGreaterThanOrEqual(1);
    expect(loaded.residentAfter).toBe(loaded.actorCount);
    expect(await countLabelled(LABEL_A)).toBe(1);
    expect(await countLabelled(LABEL_B)).toBe(0);

    const rollback = loaded.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("unload_loaded_regions");
    expect(rollback.payload.ids).toEqual([regionId]);
  }, 300_000);

  it("list_loaded_regions reports the region with its kind, bounds and the actors it holds", async () => {
    const listed = await call("list_loaded_regions");
    const row = regionRow(listed, regionId);
    expect(row, `region ${regionId} in ${JSON.stringify(listed.regions)}`).toBeDefined();
    expect(row!.kind).toBe("userRegion");
    expect(row!.loaded).toBe(true);
    expect(Number(row!.actorCount)).toBeGreaterThanOrEqual(1);
    expect(row!.residentActors).toBe(row!.actorCount);
    expect(((row!.bounds as Json).min as Vec).x).toBeCloseTo(BOX_A.min.x, 0);
    expect(((row!.bounds as Json).max as Vec).x).toBeCloseTo(BOX_A.max.x, 0);
    expect(Number(listed.loadedRegions)).toBeGreaterThanOrEqual(1);

    // A bounds filter that misses the region leaves it out.
    expect(regionRow(await call("list_loaded_regions", { bounds: BOX_B }), regionId)).toBeUndefined();
  }, 120_000);

  it("load_region over the same box reports the region that already exists", async () => {
    const again = await call("load_region", { bounds: BOX_A });
    expect(again.existed).toBe(true);
    expect(again.created).toBe(false);
    expect(again.id).toBe(regionId);
    expect(again.unchanged).toBe(true);
  }, 120_000);

  it("load_actor_descs unpin cannot release a region's actors", async () => {
    const guid = String(((await call("list_actor_descs", { filter: LABEL_A })).actorDescs as Json[])[0].guid);
    const unpinned = await call("load_actor_descs", { mode: "unpin", guids: [guid] });
    expect(unpinned.pinStateChanged).toBe(0);
    expect(unpinned.residentAfter).toBe(1);
    expect(await countLabelled(LABEL_A)).toBe(1);
  }, 120_000);

  it("unload_loaded_regions refuses a region holding unsaved actors until force=true", async () => {
    await call("move_actor", { actorLabel: LABEL_A, location: { x: 10, y: 0, z: 100 } });

    const refusal = await refused("unload_loaded_regions", { ids: [regionId] });
    expect(refusal.message).toMatch(/unsaved/i);
    if (refusal.body.dirtyPackages !== undefined) expect((refusal.body.dirtyPackages as unknown[]).length).toBeGreaterThanOrEqual(1);
    expect(await countLabelled(LABEL_A)).toBe(1);

    // force=true previews what would be discarded without discarding it.
    const forcedPreview = await call("unload_loaded_regions", { ids: [regionId], force: true, dryRun: true });
    expect(forcedPreview.unloadedRegions).toBe(0);
    expect((forcedPreview.discardedDirtyPackages as unknown[]).length).toBeGreaterThanOrEqual(1);
    expect(forcedPreview.residentAfter).toBe(forcedPreview.residentBefore);
    expect(await countLabelled(LABEL_A)).toBe(1);

    await call("save_level");
  }, 300_000);

  it("unload_loaded_regions refuses a call that names nothing and reports an id it does not know", async () => {
    expect((await refused("unload_loaded_regions", {})).message).toMatch(/Name the regions/);

    const stale = await call("unload_loaded_regions", { ids: ["/Game/Nope.Nope:PersistentLevel.Missing_0"], dryRun: true });
    expect(stale.missingIds).toEqual(["/Game/Nope.Nope:PersistentLevel.Missing_0"]);
    expect(stale.unchanged).toBe(true);
  }, 120_000);

  it("unload_loaded_regions previews, then unloads by bounds and reads residentAfter back from the world", async () => {
    const preview = await call("unload_loaded_regions", { bounds: BOX_A, dryRun: true });
    expect(preview.dryRun).toBe(true);
    expect(preview.loadedRegionsMatched, JSON.stringify(preview.regions)).toBe(1);
    expect(preview.unloadedRegions).toBe(0);
    expect(preview.residentAfter).toBe(preview.residentBefore);
    expect(await countLabelled(LABEL_A)).toBe(1);

    const out = await call("unload_loaded_regions", { bounds: BOX_A });
    expect(out.unloadedRegions).toBe(1);
    expect(Number(out.residentBefore)).toBeGreaterThanOrEqual(1);
    expect(out.residentAfter).toBe(0);
    expect(out.changed).toBe(true);
    expect(out.rollbackPossible).toBe(false);
    expect(((out.regions as Json[])[0]).id).toBe(regionId);

    // The world agrees with the response: the actor is a descriptor again.
    expect(await countLabelled(LABEL_A)).toBe(0);
    const desc = ((await call("list_actor_descs", { filter: LABEL_A })).actorDescs as Json[])[0];
    expect(desc.loaded).toBe(false);

    // The region no longer holds anything, whether the editor kept the box or released it.
    const after = regionRow(await call("list_loaded_regions"), regionId);
    expect(after).toBeUndefined();

    // Replaying the same call finds nothing left to unload.
    const replay = await call("unload_loaded_regions", { bounds: BOX_A });
    expect(replay.unloadedRegions).toBe(0);
    expect(replay.unchanged).toBe(true);
  }, 300_000);
});

describe("the guards on unload_loaded_regions", () => {
  let idA = "";
  let idB = "";
  let rollbackB: { method: string; payload: Json };

  it("maxRegions refuses a box that matches more regions than allowed, and unloads nothing", async () => {
    const a = await call("load_region", { bounds: BOX_A });
    const b = await call("load_region", { bounds: BOX_B });
    idA = String(a.id);
    idB = String(b.id);
    rollbackB = b.rollback as { method: string; payload: Json };
    expect(idA).not.toBe(idB);
    expect(await countLabelled("LR_Actor")).toBe(2);

    const listed = await call("list_loaded_regions", { bounds: BOX_BOTH });
    expect(regionRow(listed, idA)).toBeDefined();
    expect(regionRow(listed, idB)).toBeDefined();

    expect((await refused("unload_loaded_regions", { bounds: BOX_BOTH, maxRegions: 1 })).message).toMatch(/limit/);
    expect(await countLabelled("LR_Actor")).toBe(2);
  }, 300_000);

  it("unload_loaded_regions by id leaves the other region loaded", async () => {
    const out = await call("unload_loaded_regions", { ids: [idA] });
    expect(out.unloadedRegions).toBe(1);
    expect(out.residentAfter).toBe(0);
    expect(await countLabelled(LABEL_A)).toBe(0);
    expect(await countLabelled(LABEL_B)).toBe(1);
  }, 300_000);

  it("the rollback load_region returned unloads the region it made", async () => {
    const undone = await call(rollbackB.method, rollbackB.payload);
    expect(undone.unloadedRegions).toBe(1);
    expect(undone.residentAfter).toBe(0);
    expect(await countLabelled("LR_Actor")).toBe(0);
  }, 300_000);

  it("unload_loaded_regions says when a pin still holds actors it unloaded the region of", async () => {
    const loaded = await call("load_region", { bounds: BOX_A });
    const guid = String(((await call("list_actor_descs", { filter: LABEL_A })).actorDescs as Json[])[0].guid);
    await call("load_actor_descs", { mode: "pin", guids: [guid] });

    const out = await call("unload_loaded_regions", { ids: [String(loaded.id)] });
    expect(out.unloadedRegions).toBe(1);
    expect(out.residentAfter).toBe(1);
    expect(out.residentPinnedAfter).toBe(1);
    expect(String(out.residentNote)).toMatch(/pin/);

    const unpinned = await call("load_actor_descs", { mode: "unpin", guids: [guid] });
    expect(unpinned.residentAfter).toBe(0);
    expect(await countLabelled(LABEL_A)).toBe(0);
  }, 300_000);
});
