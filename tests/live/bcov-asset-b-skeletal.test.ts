// @live-editor headless
/**
 * Skeletal mesh reads and edits on a copy of the engine's SkeletalCube: bones,
 * cloth bindings, material slots, skin weights and the bone vertex bounds they
 * drive, the instancing build flag, a bone moved with its skinned part, and
 * the bulk source-vertex read.
 * Refs #1223, #1614, #1567
 */
import { existsSync, readFileSync, rmSync } from "node:fs";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AssetB/Skel";
const MESH = `${DIR}/SKM_BCovB_Cube`;
const PART_MESH = `${DIR}/SKM_BCovB_Part`;
const SOURCE = "/Engine/EngineMeshes/SkeletalCube";
const WORLD_GRID = "/Engine/EngineMaterials/WorldGridMaterial";

type Json = Record<string, unknown>;
type Influence = { boneName: string; weight: number };
type Vertex = { vertexIndex: number; influences: Influence[] };
type BoneBounds = { boneName: string; vertexCount: number };
type Vec = { x: number; y: number; z: number };
type PartBounds = BoneBounds & {
  parentName: string;
  bindPose: { location: Vec };
  componentSpace?: { center: Vec };
};
type PartLod = {
  lodIndex: number;
  geometry: string;
  verticesMoved?: number;
  renderVerticesMoved?: number;
  maxRenderPositionErrorCm?: number;
  maxRenderNormalChangeDegrees?: number;
};
type BulkVertex = { vertexIndex: number; position?: Vec; selectedWeight?: number; influences?: Influence[] };
type Box = { min: Vec; max: Vec };
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
  await call("duplicate_asset", { sourcePath: SOURCE, destinationPath: PART_MESH });
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

describe("read_skeletal_mesh_vertices", () => {
  const inside = (p: Vec, box: Box, eps = 1e-3): boolean =>
    p.x >= box.min.x - eps && p.x <= box.max.x + eps
    && p.y >= box.min.y - eps && p.y <= box.max.y + eps
    && p.z >= box.min.z - eps && p.z <= box.max.z + eps;

  it("reads every source vertex with positions and weights, agreeing with the per-vertex read", async () => {
    const all = await call("read_skeletal_mesh_vertices", { assetPath: MESH, limit: 20000 });
    const vertices = all.vertices as BulkVertex[];
    expect(Number(all.vertexCount)).toBeGreaterThan(0);
    expect(all.selectedVertexCount).toBe(all.vertexCount);
    expect(vertices).toHaveLength(Number(all.vertexCount));
    expect(all.hasMore).toBe(false);
    for (const v of vertices) {
      expect(v.position).toBeDefined();
      expect(v.influences!.length).toBeGreaterThan(0);
      expect(inside(v.position!, all.selectedBounds as Box)).toBe(true);
    }

    const zero = vertices.find((v) => v.vertexIndex === 0)!;
    const exact = await vertexZero();
    expect(zero.influences!.map((i) => i.boneName).sort()).toEqual(exact.influences.map((i) => i.boneName).sort());
    for (const influence of exact.influences) {
      expect(zero.influences!.find((i) => i.boneName === influence.boneName)!.weight).toBeCloseTo(influence.weight, 4);
    }

    const page = await call("read_skeletal_mesh_vertices", { assetPath: MESH, offset: 1, limit: 1, include: ["weights"] });
    const paged = page.vertices as BulkVertex[];
    expect(paged).toHaveLength(1);
    expect(paged[0].vertexIndex).toBe(vertices[1].vertexIndex);
    expect(paged[0].position).toBeUndefined();
    expect(page.hasMore).toBe(vertices.length > 2);
    if (vertices.length > 2) expect(page.nextOffset).toBe(2);
  }, 120_000);

  it("selects by bone and by box, matching the bone's vertex bounds", async () => {
    const bounds = await call("read_skeletal_mesh_bone_vertex_bounds", { assetPath: MESH, minWeight: 0.0001 });
    const bone = (bounds.bones as Array<BoneBounds & { componentSpace?: Box }>)[0];
    expect(bone.vertexCount).toBeGreaterThan(0);

    const byBone = await call("read_skeletal_mesh_vertices", { assetPath: MESH, boneNames: [bone.boneName], minWeight: 0.0001, limit: 20000 });
    expect(byBone.selectedVertexCount).toBe(bone.vertexCount);
    expect(byBone.selectedBones).toEqual([bone.boneName]);
    const box = byBone.selectedBounds as Box;
    for (const axis of ["x", "y", "z"] as const) {
      expect(box.min[axis]).toBeCloseTo(bone.componentSpace!.min[axis], 3);
      expect(box.max[axis]).toBeCloseTo(bone.componentSpace!.max[axis], 3);
    }
    for (const v of byBone.vertices as BulkVertex[]) {
      expect(v.selectedWeight!).toBeGreaterThanOrEqual(0.0001);
      expect(v.influences!.some((i) => i.boneName === bone.boneName)).toBe(true);
    }

    const byBox = await call("read_skeletal_mesh_vertices", { assetPath: MESH, box: bone.componentSpace!, limit: 20000 });
    expect(Number(byBox.selectedVertexCount)).toBeGreaterThanOrEqual(bone.vertexCount);
    for (const v of byBox.vertices as BulkVertex[]) expect(inside(v.position!, bone.componentSpace!)).toBe(true);

    const refused = await callBridge(bridge, "read_skeletal_mesh_vertices", { assetPath: MESH, boneNames: ["NoSuchBone_1567"] });
    expect(succeeded(refused)).toBe(false);
  }, 120_000);

  it("returns triangles once across pages and dumps the whole selection to a file", async () => {
    const whole = await call("read_skeletal_mesh_vertices", { assetPath: MESH, include: ["triangles"], limit: 20000 });
    const total = Number(whole.selectedTriangleCount);
    expect(total).toBeGreaterThan(0);
    const ids = new Set((whole.vertices as BulkVertex[]).map((v) => v.vertexIndex));
    const triangles = whole.triangles as Array<{ triangleIndex: number; vertexIndices: number[] }>;
    expect(triangles).toHaveLength(total);
    for (const t of triangles) {
      expect(t.vertexIndices).toHaveLength(3);
      for (const corner of t.vertexIndices) expect(ids.has(corner)).toBe(true);
    }

    const seen: number[] = [];
    let offset = 0;
    const limit = Math.max(1, Math.ceil(ids.size / 3));
    for (;;) {
      const page = await call("read_skeletal_mesh_vertices", { assetPath: MESH, include: ["triangles"], offset, limit });
      seen.push(...(page.triangles as Array<{ triangleIndex: number }>).map((t) => t.triangleIndex));
      if (!page.hasMore) break;
      offset = Number(page.nextOffset);
    }
    expect(seen.sort((a, b) => a - b)).toEqual(triangles.map((t) => t.triangleIndex).sort((a, b) => a - b));

    const dumped = await call("read_skeletal_mesh_vertices", {
      assetPath: MESH, include: ["positions", "weights", "triangles"], dumpToFile: true,
      outputPath: "UE_MCP/SkeletalMeshVertices/bcov-1567.json",
    });
    const file = String(dumped.outputPath);
    expect(dumped.vertices).toBeUndefined();
    expect(existsSync(file)).toBe(true);
    const content = JSON.parse(readFileSync(file, "utf8")) as Json;
    rmSync(file, { force: true });
    expect((content.vertices as BulkVertex[]).length).toBe(whole.selectedVertexCount);
    expect((content.triangles as unknown[]).length).toBe(total);
  }, 180_000);
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

describe("transform_skinned_part", () => {
  async function partBones(): Promise<PartBounds[]> {
    const read = await call("read_skeletal_mesh_bone_vertex_bounds", { assetPath: PART_MESH, minWeight: 0.0001 });
    return read.bones as PartBounds[];
  }
  async function bone(name: string): Promise<PartBounds> {
    const read = await call("read_skeletal_mesh_bone_vertex_bounds", { assetPath: PART_MESH, boneNames: [name], minWeight: 0.0001 });
    return (read.bones as PartBounds[])[0];
  }

  it("moves a bone and its vertices together in every LOD, refuses before writing, and rolls back", async () => {
    const weighted = await partBones();
    // A child bone that owns vertices, or the root when it owns them all.
    const target = weighted.find((b) => b.parentName !== "") ?? weighted[0];
    expect(target.vertexCount).toBeGreaterThan(0);
    const before = await bone(target.boneName);

    const refused = await callBridge(bridge, "transform_skinned_part", {
      assetPath: PART_MESH, boneName: "NoSuchBone_1614", translation: { x: 0, y: 0, z: 5 },
    });
    expect(succeeded(refused)).toBe(false);

    const dry = await call("transform_skinned_part", {
      assetPath: PART_MESH, boneName: target.boneName, translation: { x: 0, y: 0, z: 5 }, space: "component", dryRun: true,
    });
    expect(dry.updated).toBe(false);
    expect((dry.lods as PartLod[])[0].verticesMoved).toBeGreaterThan(0);
    const unchanged = await bone(target.boneName);
    expect(unchanged.bindPose.location.z).toBeCloseTo(before.bindPose.location.z, 3);
    expect(unchanged.componentSpace!.center.z).toBeCloseTo(before.componentSpace!.center.z, 3);

    const moved = await call("transform_skinned_part", {
      assetPath: PART_MESH, boneName: target.boneName, translation: { x: 0, y: 0, z: 5 }, space: "component",
    });
    expect(moved.updated).toBe(true);
    const lods = moved.lods as PartLod[];
    const lod0 = lods[0];
    expect(lod0.geometry).toBe("source");
    expect(lod0.verticesMoved).toBeGreaterThan(0);
    expect(lod0.renderVerticesMoved).toBeGreaterThan(0);
    expect(lod0.maxRenderPositionErrorCm).toBeLessThan(0.01);
    expect(lod0.maxRenderNormalChangeDegrees).toBeLessThan(1);

    const after = await bone(target.boneName);
    expect(after.bindPose.location.z - before.bindPose.location.z).toBeCloseTo(5, 2);
    // Each vertex moves by its weight on the bone, so the part's centre rises by at most the full offset.
    const rise = after.componentSpace!.center.z - before.componentSpace!.center.z;
    expect(rise).toBeGreaterThan(0);
    expect(rise).toBeLessThanOrEqual(5.01);

    const rollback = moved.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("transform_skinned_part");
    await call(rollback.method, rollback.payload);
    const restored = await bone(target.boneName);
    expect(restored.bindPose.location.z).toBeCloseTo(before.bindPose.location.z, 2);
    expect(restored.componentSpace!.center.z).toBeCloseTo(before.componentSpace!.center.z, 2);
  }, 240_000);
});
