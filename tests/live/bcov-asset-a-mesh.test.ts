// @live-editor headless
/**
 * Static mesh reads and geometry edits on copies of the engine's basic shapes,
 * each edit read back through get_mesh_geometry, get_mesh_bounds,
 * get_mesh_collision or list_sockets. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AssetA/Mesh";
const CUBE = `${DIR}/SM_BCovCube`;
const SPHERE = `${DIR}/SM_BCovSphere`;

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

/** callBridge, or a direct call with a longer client budget for a slow handler. */
async function dispatch(method: string, params: Json, timeoutMs?: number): Promise<TestResult> {
  if (!timeoutMs) return callBridge(bridge, method, params);
  try {
    return { method, ok: true, ms: 0, result: await bridge.call(method, params, timeoutMs) };
  } catch (e) {
    return { method, ok: false, ms: 0, error: e instanceof Error ? e.message : String(e) };
  }
}

async function call(method: string, params: Json, timeoutMs?: number): Promise<Json> {
  const r = await dispatch(method, params, timeoutMs);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function triangles(assetPath: string): Promise<number> {
  const g = await call("get_mesh_geometry", { assetPath, include: ["triangles"] });
  return Number(g.triangleCount);
}

async function extent(assetPath: string): Promise<Vec> {
  return (await call("get_mesh_bounds", { assetPath })).boxExtent as Vec;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_folder", { path: DIR });
  await call("duplicate_asset", { sourcePath: "/Engine/BasicShapes/Cube", destinationPath: CUBE });
  await call("duplicate_asset", { sourcePath: "/Engine/BasicShapes/Sphere", destinationPath: SPHERE });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 180_000);

describe("mesh reads", () => {
  it("get_mesh_geometry and get_mesh_bounds describe the 100 cm cube", async () => {
    const geo = await call("get_mesh_geometry", { assetPath: CUBE, include: ["positions", "triangles"] });
    // The engine cube splits each face into a 2x2 grid: 6 x 8 triangles, 6 x 9 vertices.
    expect(geo.triangleCount).toBe(48);
    expect(geo.assetType).toBe("StaticMesh");
    expect(geo.vertexCount).toBe(54);

    const bounds = await call("get_mesh_bounds", { assetPath: CUBE });
    expect(bounds.meshKind).toBe("StaticMesh");
    const e = bounds.boxExtent as Vec;
    expect(e.x).toBeCloseTo(50, 1);
    expect(e.y).toBeCloseTo(50, 1);
    expect(e.z).toBeCloseTo(50, 1);
  }, 120_000);

  it("get_static_mesh_build_settings reads back a changed build setting", async () => {
    const before = await call("get_static_mesh_build_settings", { assetPath: CUBE });
    const settings = before.settings as Record<string, unknown>;
    expect(typeof settings.bRecomputeTangents).toBe("boolean");
    const flipped = !settings.bRecomputeTangents;

    await call("set_static_mesh_build_settings", { assetPath: CUBE, settings: { bRecomputeTangents: flipped } }, 120_000);
    const after = await call("get_static_mesh_build_settings", { assetPath: CUBE });
    expect((after.settings as Record<string, unknown>).bRecomputeTangents).toBe(flipped);
  }, 180_000);

  it("add_socket adds a socket list_sockets reports at its location", async () => {
    const added = await call("add_socket", {
      assetPath: CUBE, socketName: "BCovMuzzle", relativeLocation: { x: 10, y: 20, z: 30 },
    });
    expect(added.success).toBe(true);
    const listed = await call("list_sockets", { assetPath: CUBE });
    const socket = (listed.sockets as Array<{ name: string; relativeLocation: Vec }>).find((s) => s.name === "BCovMuzzle");
    expect(socket, "the new socket is listed").toBeDefined();
    expect(socket!.relativeLocation.x).toBeCloseTo(10, 3);
    expect(socket!.relativeLocation.z).toBeCloseTo(30, 3);
  }, 120_000);
});

describe("collision", () => {
  it("generate_mesh_collision clears and rebuilds the simple collision get_mesh_collision reports", async () => {
    const cleared = await call("generate_mesh_collision", { assetPath: CUBE, op: "clear" }, 300_000);
    expect(cleared.shapeCount).toBe(0);
    const empty = await call("get_mesh_collision", { assetPath: CUBE });
    expect(Number(empty.numBoxElems) + Number(empty.numConvexElems) + Number(empty.numSphereElems) + Number(empty.numSphylElems)).toBe(0);

    const built = await call("generate_mesh_collision", { assetPath: CUBE, method: "ConvexHulls", maxConvexHulls: 1 }, 300_000);
    expect(Number(built.shapeCount)).toBeGreaterThan(0);
    const after = await call("get_mesh_collision", { assetPath: CUBE });
    expect(after.hasSimpleCollision).toBe(true);
    const shapes = Number(after.numBoxElems) + Number(after.numConvexElems) + Number(after.numSphereElems) + Number(after.numSphylElems);
    expect(shapes).toBe(Number(built.shapeCount));
  }, 600_000);
});

describe("geometry edits", () => {
  it("apply_mesh_simplify writes a mesh with fewer triangles", async () => {
    const source = await triangles(SPHERE);
    const out = `${DIR}/SM_BCovSphere_Simplified`;
    const r = await call("apply_mesh_simplify", { assetPath: SPHERE, triangleCount: 64, outputPath: out }, 300_000);
    expect(r.written).toBe(true);
    const simplified = await triangles(out);
    expect(simplified).toBeLessThan(source);
    expect(simplified).toBeLessThan(source / 2);
    expect(await triangles(SPHERE)).toBe(source);
  }, 300_000);

  it("apply_mesh_remesh writes a mesh with a different triangle count", async () => {
    const source = await triangles(SPHERE);
    const out = `${DIR}/SM_BCovSphere_Remeshed`;
    const r = await call("apply_mesh_remesh", { assetPath: SPHERE, targetTriangleCount: 2000, outputPath: out }, 300_000);
    expect(r.written).toBe(true);
    const remeshed = await triangles(out);
    expect(remeshed).not.toBe(source);
    expect(remeshed).toBeGreaterThan(0);
  }, 300_000);

  it("apply_mesh_mirror reflects a cut half so the bounds change", async () => {
    const out = `${DIR}/SM_BCovCube_Mirrored`;
    const r = await call("apply_mesh_mirror", {
      assetPath: CUBE, axis: "x", planeOrigin: { x: 25, y: 0, z: 0 }, applyPlaneCut: true, outputPath: out,
    }, 300_000);
    expect(r.written).toBe(true);
    const e = await extent(out);
    // The source spans 100 cm in X; cutting at x=25 and reflecting cannot give that back.
    expect(Math.abs(e.x - 50)).toBeGreaterThan(5);
    expect(e.y).toBeCloseTo(50, 0);
  }, 300_000);

  it("apply_mesh_fracture cuts open pieces and apply_mesh_hole_fill closes one", async () => {
    const base = `${DIR}/SM_BCovPiece`;
    const fractured = await call("apply_mesh_fracture", {
      assetPath: CUBE, pattern: "slice", axis: "z", pieces: 2, fillHoles: false, outputBasePath: base, onConflict: "replace",
    }, 600_000);
    expect(fractured.writtenPieceCount).toBe(2);
    const written = fractured.writtenPaths as string[];
    expect(written).toHaveLength(2);
    const names = ((await call("list_assets", { directory: DIR, recursive: false })).assets as Array<{ name: string }>).map((a) => a.name);
    expect(names.filter((n) => n.startsWith("SM_BCovPiece"))).toHaveLength(2);

    const piece = written[0].split(".")[0];
    const pieceBounds = await extent(piece);
    expect(pieceBounds.z).toBeLessThan(40);

    const filledPath = `${DIR}/SM_BCovPiece_Filled`;
    const filled = await call("apply_mesh_hole_fill", { assetPath: piece, outputPath: filledPath }, 300_000);
    expect(filled.written).toBe(true);
    const before = filled.before as { openBorderEdges: number; closed: boolean };
    const after = filled.after as { openBorderEdges: number; closed: boolean };
    expect(before.openBorderEdges).toBeGreaterThan(0);
    expect(after.openBorderEdges).toBe(0);
    expect(await triangles(filledPath)).toBeGreaterThan(await triangles(piece));
  }, 900_000);
});
