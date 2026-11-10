// @live-editor render
/**
 * Landscape spline authoring on a small landscape the file creates and deletes:
 * control points, a segment, its meshes, edits and their rollbacks, deletes,
 * and applying the network so it raises the terrain and paints its layer.
 * Applying writes through the edit layer merge, which runs on the GPU.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/LandscapeSplines`;
const LABEL = "LspTest_Landscape";
const LAYER = "LspRoad";
const CUBE = "/Engine/BasicShapes/Cube.Cube";
// 2x2 components of 15 quads: 31 vertices a side, 100 cm apart.
const BX = 620000;
const BY = 620000;
const Z = 400;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

let bridge: EditorBridge;
let actorPath = "";
let ownerPath = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function network(): Promise<{ points: Json[]; segments: Json[] }> {
  const listed = await call("list_landscape_splines", {});
  const mine = (row: Json) => row.landscapePath === actorPath;
  return {
    points: (listed.controlPoints as Json[]).filter(mine),
    segments: (listed.segments as Json[]).filter(mine),
  };
}

const point = async (name: string) => (await network()).points.find((p) => p.name === name);
const segment = async (name: string) => (await network()).segments.find((s) => s.name === name);

async function deleteLeftovers(): Promise<void> {
  for (let i = 0; i < 3; i++) {
    if (!succeeded(await callBridge(bridge, "delete_actor", { actorLabel: LABEL }))) break;
  }
  await callBridge(bridge, "delete_asset", { assetPath: `${DIR}/LI_${LAYER}`, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  // On the home level, where the points land on the landscape's own splines component. In the
  // untitled map a fresh editor opens on, they were put in a separate spline actor instead.
  await call("demo_go_home", { discardUnsaved: true });
  await deleteLeftovers();
  const created = await call("create_landscape", {
    label: LABEL, location: { x: BX, y: BY, z: 0 },
    componentCountX: 2, componentCountY: 2, subsectionSizeQuads: 15, numSubsections: 1,
  });
  actorPath = String(created.actorPath);
  await call("set_landscape_material", { landscapeName: actorPath, materialPath: "/Engine/EngineMaterials/WorldGridMaterial" });
  await call("add_landscape_layer_info", { landscapeName: actorPath, layerName: LAYER, packagePath: DIR });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await deleteLeftovers();
    disconnectBridge();
  }
}, 120_000);

describe("landscape spline control points and segments", () => {
  it("create_landscape_spline_point adds named points that list_splines reports in world space, and a repeat reports existed", async () => {
    const a = await call("create_landscape_spline_point", {
      actorPath, name: "LspA", location: { x: BX + 500, y: BY + 1500, z: Z }, halfWidth: 200, sideFalloff: 200, endFalloff: 200,
    });
    expect(a.created).toBe(true);
    expect(a.ownerPath).toBe(actorPath);
    expect((a.rollback as Json).method).toBe("delete_landscape_spline_point");
    ownerPath = String(a.ownerPath);

    const again = await call("create_landscape_spline_point", { actorPath, name: "LspA", location: { x: 0, y: 0, z: 0 } });
    expect(again.existed).toBe(true);
    expect(again.rollback).toBeUndefined();

    await call("create_landscape_spline_point", {
      actorPath, ownerPath, name: "LspB", location: { x: BX + 2500, y: BY + 1500, z: Z }, halfWidth: 200, sideFalloff: 200, endFalloff: 200,
    });
    const listedA = await point("LspA");
    expect(listedA, JSON.stringify(await network())).toBeDefined();
    const location = listedA!.location as Json;
    expect(Number(location.x)).toBeCloseTo(BX + 500, 0);
    expect(Number(location.z)).toBeCloseTo(Z, 0);
    expect(listedA!.halfWidth).toBe(200);
    expect(listedA!.owner).toBe(actorPath);
    expect((await point("LspB"))).toBeDefined();
  }, 60_000);

  it("connect_landscape_spline_points joins the two points once, with tangents the length of the gap", async () => {
    const joined = await call("connect_landscape_spline_points", { startPoint: "LspA", endPoint: "LspB", name: "LspSeg", layerName: LAYER });
    expect(joined.created).toBe(true);
    expect((joined.rollback as Json).method).toBe("delete_landscape_spline_segment");
    const seg = await segment("LspSeg");
    expect(seg, JSON.stringify(await network())).toBeDefined();
    expect(seg!.startPoint).toBe("LspA");
    expect(seg!.endPoint).toBe("LspB");
    expect(Math.abs(Number(seg!.startTangentLength))).toBeCloseTo(2000, -1);
    expect(seg!.layerName).toBe(LAYER);
    expect(Number(seg!.length)).toBeGreaterThan(1900);
    expect((await point("LspA"))!.connectedSegments).toEqual(["LspSeg"]);

    const again = await call("connect_landscape_spline_points", { startPoint: "LspA", endPoint: "LspB" });
    expect(again.existed).toBe(true);
    expect((await network()).segments).toHaveLength(1);
  }, 60_000);

  it("update_landscape_spline_segment changes a tangent and its rollback restores it", async () => {
    const before = Number((await segment("LspSeg"))!.startTangentLength);
    const updated = await call("update_landscape_spline_segment", { segment: "LspSeg", startTangentLength: 1500, raiseTerrain: true });
    expect(updated.updated).toBe(true);
    expect(Number((await segment("LspSeg"))!.startTangentLength)).toBeCloseTo(1500, 0);
    await call("update_landscape_spline_segment", (updated.rollback as { payload: Json }).payload);
    expect(Number((await segment("LspSeg"))!.startTangentLength)).toBeCloseTo(before, 0);

    const same = await call("update_landscape_spline_segment", { segment: "LspSeg", startTangentLength: before });
    expect(same.unchanged).toBe(true);
  }, 60_000);

  it("update_landscape_spline_point moves a point and its rollback puts it back", async () => {
    const moved = await call("update_landscape_spline_point", { controlPoint: "LspB", location: { x: BX + 2500, y: BY + 1500, z: Z + 200 } });
    expect(moved.updated).toBe(true);
    expect(Number(((await point("LspB"))!.location as Json).z)).toBeCloseTo(Z + 200, 0);
    await call("update_landscape_spline_point", (moved.rollback as { payload: Json }).payload);
    expect(Number(((await point("LspB"))!.location as Json).z)).toBeCloseTo(Z, 0);
  }, 60_000);

  it("set_landscape_spline_segment_meshes builds spline mesh components and an empty list removes them", async () => {
    // With no meshes of its own the editor draws a segment with its spline editor mesh,
    // which is also a spline mesh component, so "removed" means back to that count.
    const bare = Number((await segment("LspSeg"))!.meshComponentCount);
    const set = await call("set_landscape_spline_segment_meshes", {
      segment: "LspSeg", meshes: [{ mesh: CUBE, scale: { x: 1, y: 1, z: 0.2 }, forwardAxis: "X" }], collisionProfile: "NoCollision",
    });
    expect(set.updated).toBe(true);
    const seg = await segment("LspSeg");
    expect((seg!.meshes as Json[])[0].mesh).toBe(CUBE);
    expect(Number(seg!.meshComponentCount)).toBeGreaterThan(0);
    expect(seg!.collisionProfile).toBe("NoCollision");

    const cleared = await call("set_landscape_spline_segment_meshes", { segment: "LspSeg", meshes: [] });
    expect(cleared.updated).toBe(true);
    const afterClear = (await segment("LspSeg"))!;
    expect(afterClear.meshes).toEqual([]);
    expect(Number(afterClear.meshComponentCount)).toBe(bare);
    // The rollback carries the cube list back.
    await call("set_landscape_spline_segment_meshes", (cleared.rollback as { payload: Json }).payload);
    expect(Number((await segment("LspSeg"))!.meshComponentCount)).toBeGreaterThan(0);
    await call("set_landscape_spline_segment_meshes", { segment: "LspSeg", meshes: [] });
  }, 60_000);
});

describe("applying splines to the landscape", () => {
  it("apply_landscape_splines raises the terrain under the segment to its height and paints its layer", async () => {
    const mid = { x: BX + 1500, y: BY + 1500 };
    const before = await call("get_landscape_height_at_point", { actorPath, point: mid });
    expect(Number(before.height)).toBeLessThan(50);

    const applied = await call("apply_landscape_splines", { actorPath });
    expect(applied.segments).toBe(1);
    expect(applied.controlPoints).toBe(2);
    expect(applied.splineOwners).toBe(1);
    expect(typeof applied.editLayer).toBe("string");

    let height = 0;
    let weight = 0;
    for (let i = 0; i < 15 && (height < Z - 20 || weight < 0.5); i++) {
      await call("merge_landscape_edit_layers", { actorPath });
      height = Number((await call("get_landscape_height_at_point", { actorPath, point: mid })).height);
      const sample = await call("sample_landscape", { actorPath, point: mid, layerName: LAYER });
      weight = Number(((sample.layers as Json[]) ?? []).find((l) => l.name === LAYER || l.layerName === LAYER)?.weight ?? 0);
      await sleep(300);
    }
    expect(height).toBeGreaterThan(Z - 20);
    expect(height).toBeLessThan(Z + 20);
    expect(weight).toBeGreaterThan(0.5);
    // Far outside the spline's width and falloff the ground stays where it was.
    const far = await call("get_landscape_height_at_point", { actorPath, point: { x: BX + 1500, y: BY + 100 } });
    expect(Number(far.height)).toBeLessThan(50);
  }, 180_000);
});

describe("deleting landscape spline parts", () => {
  it("delete_landscape_spline_segment removes the segment and its rollback reconnects it", async () => {
    const deleted = await call("delete_landscape_spline_segment", { segment: "LspSeg" });
    expect(deleted.deleted).toBe(true);
    expect(await segment("LspSeg")).toBeUndefined();
    expect((await point("LspA"))!.connectedSegments).toEqual([]);

    const rollback = deleted.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("connect_landscape_spline_points");
    await call(rollback.method, rollback.payload);
    const back = await segment("LspSeg");
    expect(back).toBeDefined();
    expect(back!.layerName).toBe(LAYER);

    const again = await call("delete_landscape_spline_segment", { segment: "LspSeg" });
    expect(again.deleted).toBe(true);
    expect((await call("delete_landscape_spline_segment", { segment: "LspSeg" })).alreadyDeleted).toBe(true);
  }, 60_000);

  it("delete_landscape_spline_point removes a point with its segments, and a lone point rolls back to a create", async () => {
    await call("connect_landscape_spline_points", { startPoint: "LspA", endPoint: "LspB", name: "LspSeg2" });
    const withSegment = await call("delete_landscape_spline_point", { controlPoint: "LspA" });
    expect(withSegment.deleted).toBe(true);
    expect(withSegment.removedSegments).toEqual(["LspSeg2"]);
    expect(withSegment.rollbackPossible).toBe(false);
    expect(await point("LspA")).toBeUndefined();
    expect((await network()).segments).toHaveLength(0);

    const lone = await call("delete_landscape_spline_point", { controlPoint: "LspB" });
    expect(lone.deleted).toBe(true);
    const rollback = lone.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("create_landscape_spline_point");
    await call(rollback.method, rollback.payload);
    const restored = await point("LspB");
    expect(restored).toBeDefined();
    expect(Number((restored!.location as Json).x)).toBeCloseTo(BX + 2500, 0);
    expect(restored!.halfWidth).toBe(200);

    await call("delete_landscape_spline_point", { controlPoint: "LspB" });
    expect((await call("delete_landscape_spline_point", { controlPoint: "LspB" })).alreadyDeleted).toBe(true);
    expect((await network()).points).toHaveLength(0);
  }, 60_000);
});
