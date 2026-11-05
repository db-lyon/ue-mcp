// @live-editor headless
/**
 * Level instance, Nanite and skeletal mesh actions read back through another action: ISM instances added,
 * moved, snapped onto a floor and removed; Nanite settings on a duplicated cube; a spawned skeletal mesh
 * actor and its mesh swap. Fixtures live under /Game/MCPTest/BCov/Level and BCov_Level_* labels. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Level`;
const PREFIX = "BCov_Level_Inst";
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const NANITE_MESH = `${DIR}/SM_BCovLevelNanite`;
const SKEL_SOURCE = "/Engine/EngineMeshes/SkeletalCube";
const SKEL_COPY = `${DIR}/SK_BCovLevelCube`;
const ISM_ACTOR = `${PREFIX}_Holder`;
const FLOOR = `${PREFIX}_Floor`;
const SKEL_ACTOR = `${PREFIX}_Skel`;
const ISM = "BCovISM";

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
type Instance = { index: number; location: Vec; scale: Vec };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function instances(): Promise<Instance[]> {
  return (await call("get_instance_transforms", { actorLabel: ISM_ACTOR, componentName: ISM })).instances as Instance[];
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "delete_actors", { labelPrefix: PREFIX });
  for (const path of [NANITE_MESH, SKEL_COPY]) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await cleanup();
    disconnectBridge();
  }
}, 120_000);

describe("instanced static mesh instances", () => {
  beforeAll(async () => {
    await call("place_actor", { actorClass: "/Script/Engine.Actor", label: ISM_ACTOR, location: { x: 0, y: 40000, z: 0 } });
    await call("add_component_to_actor", { actorLabel: ISM_ACTOR, componentClass: "InstancedStaticMeshComponent", componentName: ISM });
  }, 120_000);

  it("add_instances adds world-space instances get_instance_transforms reads", async () => {
    const out = await call("add_instances", {
      actorLabel: ISM_ACTOR,
      componentName: ISM,
      transforms: [
        { location: { x: 0, y: 40000, z: 500 } },
        { location: { x: 200, y: 40000, z: 500 } },
        { location: { x: 400, y: 40000, z: 500 }, scale: { x: 2, y: 2, z: 2 } },
      ],
    });
    expect(out.addedCount).toBe(3);
    const read = await instances();
    expect(read).toHaveLength(3);
    expect(read[1].location.x).toBeCloseTo(200, 1);
    expect(read[2].scale.x).toBeCloseTo(2, 3);
  }, 120_000);

  it("update_instance_transform moves one instance", async () => {
    await call("update_instance_transform", { actorLabel: ISM_ACTOR, componentName: ISM, index: 1, location: { x: 250, y: 40100, z: 500 } });
    const read = await instances();
    expect(read[1].location.x).toBeCloseTo(250, 1);
    expect(read[1].location.y).toBeCloseTo(40100, 1);
    expect(read[0].location.x).toBeCloseTo(0, 1);
  }, 120_000);

  it("snap_instances_to_surface projects instances onto the floor below", async () => {
    // Cube is 100 cm: scale 20 x 20 x 1 at z 0 puts the top face at z 50.
    await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label: FLOOR, staticMesh: CUBE, location: { x: 200, y: 40000, z: 0 }, scale: { x: 20, y: 20, z: 1 } });
    const preview = await call("snap_instances_to_surface", { actorLabel: ISM_ACTOR, componentName: ISM, instanceIndices: [0], surfaceActorLabels: [FLOOR], dryRun: true });
    expect(preview.mutationPerformed).toBe(false);
    expect((await instances())[0].location.z).toBeCloseTo(500, 1);

    const out = await call("snap_instances_to_surface", { actorLabel: ISM_ACTOR, componentName: ISM, instanceIndices: [0], surfaceActorLabels: [FLOOR], dryRun: false });
    expect(out.hitCount).toBe(1);
    expect(out.mutationPerformed).toBe(true);
    const read = await instances();
    expect(read[0].location.z).toBeCloseTo(50, -1);
    expect(read[1].location.z).toBeCloseTo(500, 1);
  }, 120_000);

  it("remove_instance removes one instance", async () => {
    const out = await call("remove_instance", { actorLabel: ISM_ACTOR, componentName: ISM, index: 2 });
    expect(out.remainingInstances).toBe(2);
    const read = await instances();
    expect(read).toHaveLength(2);
    expect(read.every((i) => i.scale.x < 1.5)).toBe(true);
  }, 120_000);
});

describe("Nanite settings", () => {
  it("set_nanite_settings changes what get_nanite_info reports", async () => {
    await call("duplicate_asset", { sourcePath: CUBE, destinationPath: NANITE_MESH });
    const before = await call("get_nanite_info", { assetPath: NANITE_MESH });
    expect(typeof before.naniteEnabled).toBe("boolean");

    await call("set_nanite_settings", { assetPath: NANITE_MESH, enabled: true, positionPrecision: 4 });
    let info = await call("get_nanite_info", { assetPath: NANITE_MESH });
    expect(info.naniteEnabled).toBe(true);
    expect(info.positionPrecision).toBe(4);

    await call("set_nanite_settings", { assetPath: NANITE_MESH, enabled: false });
    info = await call("get_nanite_info", { assetPath: NANITE_MESH });
    expect(info.naniteEnabled).toBe(false);
  }, 300_000);
});

describe("skeletal mesh actors", () => {
  async function skinnedMesh(): Promise<string> {
    const tree = (await call("get_component_tree", { actorLabel: SKEL_ACTOR })).components as Json[];
    const comp = tree.find((c) => c.class === "SkeletalMeshComponent");
    expect(comp).toBeDefined();
    return String(comp!.skeletalMesh ?? "");
  }

  it("spawn_skeletal_mesh_actor places an actor carrying the mesh", async () => {
    const out = await call("spawn_skeletal_mesh_actor", { skeletalMesh: SKEL_SOURCE, label: SKEL_ACTOR, location: { x: 0, y: 41000, z: 100 } });
    expect(out.actorLabel).toBe(SKEL_ACTOR);
    expect(await skinnedMesh()).toContain(SKEL_SOURCE);
    const actor = await call("get_actor_details", { actorLabel: SKEL_ACTOR });
    expect(actor.class).toBe("SkeletalMeshActor");
    expect((actor.location as Vec).y).toBeCloseTo(41000, 0);
  }, 120_000);

  it("set_component_skeletal_mesh swaps and clears the component's mesh", async () => {
    await call("duplicate_asset", { sourcePath: SKEL_SOURCE, destinationPath: SKEL_COPY });
    await call("set_component_skeletal_mesh", { actorLabel: SKEL_ACTOR, skeletalMesh: SKEL_COPY });
    expect(await skinnedMesh()).toContain(SKEL_COPY);
    await call("set_component_skeletal_mesh", { actorLabel: SKEL_ACTOR, skeletalMesh: null });
    expect(await skinnedMesh()).toBe("");
  }, 120_000);
});
