// @live-editor headless
/**
 * Foliage actions against fixtures the file builds: a FoliageType asset, its
 * instances in the open level, and a procedural spawner simulated over a
 * ProceduralFoliageVolume on a ground slab. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Foliage`;
const FT = `${DIR}/FT_BCovCone`;
const SPAWNER = `${DIR}/PFS_BCov`;
const SPAWNER_OBJECT = `${SPAWNER}.PFS_BCov`;
const GROUND = "BCov_Foliage_Ground";
const VOLUME = "BCov_Foliage_Volume";
// Hand-placed instances and the procedural slab sit apart so neither counts the other.
const PX = -400000;
const PY = 400000;
const GX = -400000;
const GY = 420000;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let volumePath = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const isMine = (path: unknown) => typeof path === "string" && path.startsWith(FT);

async function instancesNear(x: number, y: number, radius: number): Promise<Json> {
  return call("get_foliage_instances", { foliageTypePath: FT, center: { x, y, z: 0 }, radius, limit: 2000 });
}

async function removeFixtures(): Promise<void> {
  await callBridge(bridge, "clear_procedural_foliage", { actorLabel: VOLUME });
  await callBridge(bridge, "remove_foliage_type_from_level", { foliageTypePath: FT, force: true });
  await callBridge(bridge, "delete_actor", { actorLabel: VOLUME });
  await callBridge(bridge, "delete_actor", { actorLabel: GROUND });
  await callBridge(bridge, "delete_asset", { assetPath: SPAWNER, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: FT, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await removeFixtures();

  // A 20 m slab whose top face is at Z 0, and a volume around it for the simulation to trace into.
  await call("place_actor", {
    actorClass: "StaticMeshActor", label: GROUND, staticMesh: "/Engine/BasicShapes/Cube",
    location: { x: GX, y: GY, z: -50 }, scale: { x: 20, y: 20, z: 1 },
  });
  const volume = await call("spawn_volume", {
    volumeType: "ProceduralFoliageVolume", label: VOLUME,
    location: { x: GX, y: GY, z: 0 }, extent: { x: 1000, y: 1000, z: 500 },
  });
  volumePath = String(volume.actorPath);
  await call("create_asset_by_class", { name: "PFS_BCov", className: "/Script/Foliage.ProceduralFoliageSpawner", packagePath: DIR });
  await call("set_property", {
    objectPath: volumePath, propertyName: "ProceduralComponent.FoliageSpawner", value: SPAWNER_OBJECT, save: false,
  });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await removeFixtures();
    disconnectBridge();
  }
}, 180_000);

describe("foliage types", () => {
  it("create_foliage_type makes a type whose settings set_foliage_type_settings changes and get reads back", async () => {
    const created = await call("create_foliage_type", { meshPath: "/Engine/BasicShapes/Cone", name: "FT_BCovCone", packagePath: DIR });
    expect(created.path).toBe(FT);
    expect(created.meshName).toBe("Cone");

    const before = await call("get_foliage_type_settings", { foliageTypePath: FT });
    expect(String(before.meshPath)).toContain("/Engine/BasicShapes/Cone");
    expect(before.castShadow).toBe(true);

    const set = await call("set_foliage_type_settings", { foliageTypePath: FT, settings: { Density: 250, CastShadow: false } });
    expect(set.changedCount).toBe(2);

    const after = await call("get_foliage_type_settings", { foliageTypePath: FT });
    expect(after.density).toBe(250);
    expect(after.castShadow).toBe(false);
  }, 120_000);

  it("add_foliage_type_to_level puts the type in the palette list_foliage_types shows, and remove takes it out", async () => {
    const added = await call("add_foliage_type_to_level", { foliageTypePath: FT });
    expect(isMine(added.foliageTypePath)).toBe(true);
    expect(added.instanceCount).toBe(0);
    const listed = await call("list_foliage_types", { limit: 2000 });
    expect((listed.foliageTypes as Json[]).some((t) => isMine(t.path))).toBe(true);

    const removed = await call("remove_foliage_type_from_level", { foliageTypePath: FT });
    expect(removed.instancesDestroyed).toBe(0);
    const after = await call("list_foliage_types", { limit: 2000 });
    expect((after.foliageTypes as Json[]).some((t) => isMine(t.path))).toBe(false);
  }, 120_000);
});

describe("foliage instances", () => {
  it("add_foliage_instances places exact transforms that get, sample and remove all see", async () => {
    const locations = [0, 300, 600].map((dx) => ({ x: PX + dx, y: PY, z: 0 }));
    const added = await call("add_foliage_instances", {
      foliageTypePath: FT, transforms: locations.map((location) => ({ location })),
      projectToGround: false, applyTypeRules: false,
    });
    expect(added.added).toBe(3);

    const got = await instancesNear(PX + 300, PY, 1000);
    expect(got.matched).toBe(3);
    const xs = ((got.types as Json[]).find((t) => isMine(t.foliageTypePath))!.instances as Json[])
      .map((i) => Math.round(Number((i.location as Json).x)))
      .sort((a, b) => a - b);
    expect(xs).toEqual([PX, PX + 300, PX + 600]);

    const sampled = await call("sample_foliage", { center: { x: PX + 300, y: PY, z: 0 }, radius: 1000 });
    const row = (sampled.types as Json[]).find((t) => t.type === "FT_BCovCone");
    expect(row?.count).toBe(3);

    const removedOne = await call("remove_foliage_instances", { foliageTypePath: FT, transforms: [{ location: locations[2] }] });
    expect(removedOne.removed).toBe(1);
    expect((await instancesNear(PX + 300, PY, 1000)).matched).toBe(2);

    const removedAll = await call("remove_foliage_instances", { foliageTypePath: FT, all: true });
    expect(removedAll.removed).toBe(2);
    expect((await instancesNear(PX + 300, PY, 1000)).matched).toBe(0);
  }, 120_000);
});

describe("procedural foliage", () => {
  it("set_procedural_foliage_spawner_types fills the spawner list read_procedural_foliage_spawner returns", async () => {
    const before = await call("read_procedural_foliage_spawner", { spawnerPath: SPAWNER });
    expect(before.foliageTypeCount).toBe(0);
    const volumes = before.volumes as Json[];
    expect(volumes.map((v) => v.actorPath)).toContain(volumePath);

    const set = await call("set_procedural_foliage_spawner_types", { spawnerPath: SPAWNER, foliageTypePaths: [FT], mode: "replace" });
    expect(set.foliageTypeCount).toBe(1);

    const after = await call("read_procedural_foliage_spawner", { spawnerPath: SPAWNER });
    expect(after.foliageTypeCount).toBe(1);
    expect(isMine((after.foliageTypes as Json[])[0].foliageTypePath)).toBe(true);
  }, 120_000);

  it("simulate_procedural_foliage places instances on the slab and clear_procedural_foliage removes them", async () => {
    const simulated = await call("simulate_procedural_foliage", { actorPath: volumePath });
    expect(simulated.volumeCount).toBe(1);
    expect(Number(simulated.placed)).toBeGreaterThan(0);
    const placed = await instancesNear(GX, GY, 1500);
    expect(placed.matched).toBe(simulated.placed);
    const spawned = (await call("read_procedural_foliage_spawner", { spawnerPath: SPAWNER })).volumes as Json[];
    expect(spawned.find((v) => v.actorPath === volumePath)?.hasSpawnedInstances).toBe(true);

    const cleared = await call("clear_procedural_foliage", { actorPath: volumePath });
    expect(cleared.cleared).toBe(1);
    expect((await instancesNear(GX, GY, 1500)).matched).toBe(0);
    const after = (await call("read_procedural_foliage_spawner", { spawnerPath: SPAWNER })).volumes as Json[];
    expect(after.find((v) => v.actorPath === volumePath)?.hasSpawnedInstances).toBe(false);
  }, 300_000);
});
