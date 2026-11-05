// @live-editor headless
/**
 * Static mesh edits on copies of engine meshes, each read back through a
 * second action: sockets, material slots, navigation, build settings, the
 * measured geometry, and a pivot recenter.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AssetB/Mesh";
const CUBE = `${DIR}/SM_BCovB_Cube`;
const SCALED = `${DIR}/SM_BCovB_Scaled`;
const OFFSET = `${DIR}/SM_BCovB_Offset`;
const WORLD_GRID = "/Engine/EngineMaterials/WorldGridMaterial";
const DEFAULT_MATERIAL = "/Engine/EngineMaterials/DefaultMaterial";
// Engine meshes whose bounds may sit off the origin; the first that does is the recenter fixture.
const OFF_CENTER_CANDIDATES = [
  "/Engine/EngineMeshes/SM_MatPreviewMesh_02",
  "/Engine/EditorMeshes/EditorPlane",
  "/Engine/EditorMeshes/PhAT_FloorBox",
  "/Engine/EngineMeshes/SM_MatPreviewMesh_01",
  "/Engine/EditorMeshes/MatineeCam_SM",
];

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
type Socket = { name: string; relativeLocation: Vec; relativeScale: Vec };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function slotMaterial(assetPath: string, index: number): Promise<string> {
  const info = await call("get_mesh_info", { assetPath });
  const slots = info.materialSlots as Array<{ index: number; materialPath: string }>;
  return slots.find((s) => s.index === index)?.materialPath ?? "";
}

async function boundsCenter(assetPath: string): Promise<Vec> {
  const measured = await call("measure_mesh_geometry", { assetPath });
  return (measured.bounds as { center: Vec }).center;
}

async function sockets(method: "list_sockets" | "list_asset_sockets"): Promise<Socket[]> {
  const listed = await call(method, { assetPath: CUBE });
  expect(listed.meshType).toBe("StaticMesh");
  return listed.sockets as Socket[];
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("duplicate_asset", { sourcePath: "/Engine/BasicShapes/Cube", destinationPath: CUBE });
  await call("duplicate_asset", { sourcePath: "/Engine/BasicShapes/Cube", destinationPath: SCALED });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("static mesh sockets", () => {
  it("set_socket_transform moves a socket and remove_socket drops it, read through list_sockets and list_asset_sockets", async () => {
    await call("add_socket", { assetPath: CUBE, socketName: "BCov_Muzzle", relativeLocation: { x: 0, y: 0, z: 50 } });
    expect((await sockets("list_sockets")).map((s) => s.name)).toContain("BCov_Muzzle");

    await call("set_socket_transform", {
      assetPath: CUBE,
      socketName: "BCov_Muzzle",
      relativeLocation: { x: 12, y: -4, z: 30 },
      relativeScale: { x: 2, y: 2, z: 2 },
    });
    const moved = (await sockets("list_asset_sockets")).find((s) => s.name === "BCov_Muzzle")!;
    expect(moved.relativeLocation.x).toBeCloseTo(12, 3);
    expect(moved.relativeLocation.y).toBeCloseTo(-4, 3);
    expect(moved.relativeLocation.z).toBeCloseTo(30, 3);
    expect(moved.relativeScale.x).toBeCloseTo(2, 3);

    const removed = await call("remove_socket", { assetPath: CUBE, socketName: "BCov_Muzzle" });
    expect(removed.removed).toBe("BCov_Muzzle");
    expect((await sockets("list_sockets")).map((s) => s.name)).not.toContain("BCov_Muzzle");
  }, 120_000);
});

describe("static mesh materials and navigation", () => {
  it("set_mesh_material assigns a slot that get_mesh_info reports", async () => {
    // The engine cube ships with WorldGridMaterial in slot 0.
    const set = await call("set_mesh_material", { assetPath: CUBE, materialPath: DEFAULT_MATERIAL, slotIndex: 0 });
    expect(String(set.previousMaterialPath)).toContain("WorldGridMaterial");
    expect(await slotMaterial(CUBE, 0)).toContain("DefaultMaterial");
  }, 120_000);

  it("set_mesh_materials_batch assigns slots on several meshes", async () => {
    await call("set_mesh_materials_batch", {
      assignments: [
        { assetPath: CUBE, materialPath: WORLD_GRID, slotIndex: 0 },
        { assetPath: SCALED, materialPath: DEFAULT_MATERIAL, slotIndex: 0 },
      ],
    });
    expect(await slotMaterial(CUBE, 0)).toContain("WorldGridMaterial");
    expect(await slotMaterial(SCALED, 0)).toContain("DefaultMaterial");
  }, 120_000);

  it("set_mesh_nav toggles whether the mesh affects navigation", async () => {
    await call("set_mesh_nav", { assetPath: CUBE, bHasNavigationData: false });
    expect((await call("get_mesh_collision", { assetPath: CUBE })).bCanEverAffectNavigation).toBe(false);
    await call("set_mesh_nav", { assetPath: CUBE, bHasNavigationData: true });
    expect((await call("get_mesh_collision", { assetPath: CUBE })).bCanEverAffectNavigation).toBe(true);
  }, 120_000);
});

describe("static mesh geometry", () => {
  it("measure_mesh_geometry reports the 100 cm cube's size, area and volume", async () => {
    const measured = await call("measure_mesh_geometry", { assetPath: "/Engine/BasicShapes/Cube" });
    // Each face of the engine cube is a 2x2 quad grid: 6 faces x 8 triangles.
    expect(measured.triangleCount).toBe(48);
    expect(measured.isClosed).toBe(true);
    const dims = measured.dimensions as Vec;
    expect(dims.x).toBeCloseTo(100, 1);
    expect(dims.y).toBeCloseTo(100, 1);
    expect(dims.z).toBeCloseTo(100, 1);
    expect(Number(measured.surfaceArea)).toBeCloseTo(60000, -1);
    if (measured.volumeIsMeaningful === true) expect(Number(measured.volume)).toBeCloseTo(1_000_000, -3);
  }, 120_000);

  it("set_static_mesh_build_settings rescales the build, read back and measured", async () => {
    const set = await call("set_static_mesh_build_settings", { assetPath: SCALED, settings: { BuildScale3D: { x: 2, y: 2, z: 2 } } });
    expect(set.rebuilt).toBe(true);
    const read = await call("get_static_mesh_build_settings", { assetPath: SCALED });
    expect((read.settings as { BuildScale3D: Vec }).BuildScale3D.x).toBeCloseTo(2, 4);
    const measured = await call("measure_mesh_geometry", { assetPath: SCALED });
    expect((measured.dimensions as Vec).x).toBeCloseTo(200, 0);
  }, 180_000);

  it("recenter_pivot shifts the mesh by the offset it reports", async () => {
    let source = "";
    for (const candidate of OFF_CENTER_CANDIDATES) {
      const r = await callBridge(bridge, "measure_mesh_geometry", { assetPath: candidate });
      if (!succeeded(r)) continue;
      const c = (body(r).bounds as { center: Vec }).center;
      if (Math.hypot(c.x, c.y, c.z) > 5) { source = candidate; break; }
    }
    await callBridge(bridge, "delete_asset", { assetPath: OFFSET, force: true });
    if (source) {
      await call("duplicate_asset", { sourcePath: source, destinationPath: OFFSET });
    } else {
      // No engine mesh sat off the origin: build one by mirroring the cube about a far plane.
      await call("duplicate_asset", { sourcePath: "/Engine/BasicShapes/Cube", destinationPath: `${DIR}/SM_BCovB_MirrorSrc` });
      await call("apply_mesh_mirror", {
        assetPath: `${DIR}/SM_BCovB_MirrorSrc`,
        outputPath: OFFSET,
        axis: "x",
        planeOrigin: { x: 200, y: 0, z: 0 },
        applyPlaneCut: false,
        weldAlongPlane: false,
      });
    }

    const before = await boundsCenter(OFFSET);
    const recentered = await call("recenter_pivot", { assetPath: OFFSET });
    expect(recentered.meshCount).toBe(1);
    const match = /\(([-\d.]+), ([-\d.]+), ([-\d.]+)\)/.exec(String(recentered.offsetApplied));
    expect(match, String(recentered.offsetApplied)).not.toBeNull();
    const offset = { x: Number(match![1]), y: Number(match![2]), z: Number(match![3]) };
    expect(Math.hypot(offset.x, offset.y, offset.z)).toBeGreaterThan(1);

    const after = await boundsCenter(OFFSET);
    expect(after.x).toBeCloseTo(before.x - offset.x, 0);
    expect(after.y).toBeCloseTo(before.y - offset.y, 0);
    expect(after.z).toBeCloseTo(before.z - offset.z, 0);
  }, 300_000);
});
