// @live-editor headless
/**
 * Skeletal mesh reads and edits on a copy of the engine's SkeletalCube: bones,
 * cloth bindings, material slots, skin weights and the bone vertex bounds they
 * drive, and the instancing build flag.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AssetB/Skel";
const MESH = `${DIR}/SKM_BCovB_Cube`;
const SOURCE = "/Engine/EngineMeshes/SkeletalCube";
const WORLD_GRID = "/Engine/EngineMaterials/WorldGridMaterial";

type Json = Record<string, unknown>;
type Influence = { boneName: string; weight: number };
type Vertex = { vertexIndex: number; influences: Influence[] };
type BoneBounds = { boneName: string; vertexCount: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function boneNames(): Promise<string[]> {
  const listed = await call("list_skeleton_bones", { assetPath: MESH });
  return (listed.bones as Array<{ name: string }>).map((b) => b.name);
}

async function vertexZero(): Promise<Vertex> {
  const read = await call("read_skeletal_mesh_skin_weights", { assetPath: MESH, vertexIndices: [0] });
  return (read.vertices as Vertex[])[0];
}

async function boneVertexCounts(): Promise<Map<string, number>> {
  const read = await call("read_skeletal_mesh_bone_vertex_bounds", { assetPath: MESH });
  return new Map((read.bones as BoneBounds[]).map((b) => [b.boneName, b.vertexCount]));
}

async function optimizeForInstancing(): Promise<boolean> {
  const read = await call("read_skeletal_mesh_build_settings", { assetPath: MESH, lodIndex: 0 });
  return (read.lods as Array<{ beforeOptimizeForInstancing: boolean }>)[0].beforeOptimizeForInstancing;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("duplicate_asset", { sourcePath: SOURCE, destinationPath: MESH });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("skeletal mesh reads", () => {
  it("list_skeleton_bones lists the cube's bones with a root and parents", async () => {
    const listed = await call("list_skeleton_bones", { assetPath: MESH });
    const bones = listed.bones as Array<{ name: string; index: number; parentIndex: number }>;
    expect(Number(listed.boneCount)).toBeGreaterThanOrEqual(2);
    expect(bones).toHaveLength(Number(listed.boneCount));
    expect(bones[0].parentIndex).toBe(-1);
    for (const bone of bones.slice(1)) expect(bone.parentIndex).toBeGreaterThanOrEqual(0);
    const page = await call("list_skeleton_bones", { assetPath: MESH, limit: 1, offset: 1, includeTransforms: false });
    expect((page.bones as Array<{ name: string }>).map((b) => b.name)).toEqual([bones[1].name]);
  }, 120_000);

  it("read_cloth_data reports every render section and no clothing on an unclothed mesh", async () => {
    const read = await call("read_cloth_data", { skeletalMeshPath: MESH });
    expect(read.clothingAssetCount).toBe(0);
    const sections = read.sections as Array<{ lodIndex: number; sectionIndex: number; usesCloth: boolean }>;
    expect(sections.length).toBeGreaterThan(0);
    expect(sections.some((s) => s.lodIndex === 0 && s.sectionIndex === 0)).toBe(true);
    for (const s of sections) expect(s.usesCloth).toBe(false);
  }, 120_000);
});

describe("skeletal mesh edits", () => {
  it("set_sk_material_slots assigns a slot that get_mesh_info reports", async () => {
    const set = await call("set_sk_material_slots", { assetPath: MESH, slots: [{ slotIndex: 0, materialPath: WORLD_GRID }] });
    expect((set.applied as unknown[]).length).toBe(1);
    const info = await call("get_mesh_info", { assetPath: MESH });
    const slot = (info.materialSlots as Array<{ index: number; materialPath: string }>).find((s) => s.index === 0)!;
    expect(slot.materialPath).toContain("WorldGridMaterial");
  }, 120_000);

  it("set_skeletal_mesh_skin_weights rebinds a vertex, read back per vertex and per bone", async () => {
    const bones = await boneNames();
    const before = await vertexZero();
    expect(before.influences.length).toBeGreaterThan(0);
    const dominant = [...before.influences].sort((a, b) => b.weight - a.weight)[0].boneName;
    const target = bones.find((b) => b !== dominant)!;
    const countsBefore = await boneVertexCounts();

    await call("set_skeletal_mesh_skin_weights", {
      assetPath: MESH,
      edits: [{ vertexIndex: 0, influences: [{ boneName: target, weight: 1 }] }],
    });

    const after = await vertexZero();
    expect(after.vertexIndex).toBe(0);
    expect(after.influences).toHaveLength(1);
    expect(after.influences[0].boneName).toBe(target);
    expect(after.influences[0].weight).toBeCloseTo(1, 3);

    const countsAfter = await boneVertexCounts();
    expect(countsAfter.get(target) ?? 0).toBeGreaterThan(countsBefore.get(target) ?? 0);
  }, 180_000);

  it("set_skeletal_mesh_optimize_for_instancing flips the build flag read_skeletal_mesh_build_settings reports", async () => {
    const initial = await optimizeForInstancing();
    expect(typeof initial).toBe("boolean");
    const set = await call("set_skeletal_mesh_optimize_for_instancing", { assetPath: MESH, enabled: !initial, lodIndex: 0 });
    expect(set.updated).toBe(true);
    expect(await optimizeForInstancing()).toBe(!initial);
    await call("set_skeletal_mesh_optimize_for_instancing", { assetPath: MESH, enabled: initial, lodIndex: 0 });
    expect(await optimizeForInstancing()).toBe(initial);
  }, 180_000);
});
