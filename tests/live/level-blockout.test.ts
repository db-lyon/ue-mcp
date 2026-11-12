// @live-editor headless
/**
 * Level blockout: generated primitives sized and tagged, the reader that finds
 * them, ranking candidate meshes by bounds, swapping a blockout for a fitted
 * asset, and restoring it from the record the swap leaves.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const TAG = "BlockoutTest";
const MESH_FOLDER = `${TEST_PREFIX}/Blockout/Meshes`;
const SHAPES = "/Engine/BasicShapes";
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const Y = -20000;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const listed = async (): Promise<Json[]> => (await call("list_blockout", { tag: TAG })).blockouts as Json[];
const byLabel = async (label: string) => (await listed()).find((b) => b.actorLabel === label);
const vec = (v: unknown) => v as { x: number; y: number; z: number };

async function cleanup(): Promise<void> {
  await callBridge(bridge, "delete_actors", { labelPrefix: "BlkTest_", dryRun: false });
  for (const name of ["Box", "Ramp", "Wedge", "Cylinder_12", "Stairs_6"]) {
    await callBridge(bridge, "delete_asset", { assetPath: `${MESH_FOLDER}/SM_Blockout_${name}`, force: true });
  }
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

describe("create_blockout and list_blockout", () => {
  it("creates every shape at the requested size, bottom-centred, tagged, and the reader reports them", async () => {
    const shapes: Array<[string, Json]> = [
      ["box", { size: { x: 400, y: 200, z: 300 } }],
      ["ramp", { size: { x: 600, y: 300, z: 150 } }],
      ["wedge", { size: { x: 300, y: 300, z: 200 } }],
      ["cylinder", { size: { x: 200, y: 200, z: 500 }, sides: 12 }],
      ["stairs", { size: { x: 400, y: 200, z: 200 }, steps: 6 }],
    ];
    for (const [i, [shape, extra]] of shapes.entries()) {
      const created = await call("create_blockout", {
        shape, label: `BlkTest_${shape}`, tag: TAG, meshFolder: MESH_FOLDER, location: { x: i * 1000, y: Y, z: 0 }, ...extra,
      });
      expect(created.created).toBe(true);
      expect((created.rollback as Json).method).toBe("delete_actor");
      expect(String(created.mesh)).toContain(MESH_FOLDER);
    }

    const rows = await listed();
    expect(rows.map((r) => r.actorLabel).sort()).toEqual(shapes.map(([s]) => `BlkTest_${s}`).sort());
    for (const [i, [shape, extra]] of shapes.entries()) {
      const row = rows.find((r) => r.actorLabel === `BlkTest_${shape}`)!;
      expect(row.shape).toBe(shape);
      const want = vec(extra.size);
      const size = vec(row.size);
      expect(size.x).toBeCloseTo(want.x, 0);
      expect(size.y).toBeCloseTo(want.y, 0);
      expect(size.z).toBeCloseTo(want.z, 0);
      const bottom = vec(row.bottomCentre);
      expect(bottom.x).toBeCloseTo(i * 1000, 0);
      expect(bottom.z).toBeCloseTo(0, 0);
      expect(row.tags as string[]).toEqual(expect.arrayContaining([TAG, `BlockoutShape:${shape}`]));
    }
    const counts = (await call("list_blockout", { tag: TAG })).byShape as Json;
    expect(counts.stairs).toBe(1);

    // A second box reuses the cached shape mesh rather than building another.
    const again = await call("create_blockout", { shape: "box", label: "BlkTest_box2", tag: TAG, meshFolder: MESH_FOLDER, location: { x: 0, y: Y + 1500, z: 0 } });
    expect(again.meshCreated).toBe(false);
    // The label is the natural key.
    expect((await call("create_blockout", { shape: "box", label: "BlkTest_box2", tag: TAG, meshFolder: MESH_FOLDER })).existed).toBe(true);
  }, 120_000);
});

describe("match_asset_to_blockout", () => {
  it("ranks the engine basic shapes against a 100 cm box first and the flat plane last", async () => {
    await call("create_blockout", { shape: "box", label: "BlkTest_unit", tag: TAG, meshFolder: MESH_FOLDER, location: { x: 0, y: Y + 3000, z: 0 } });
    const match = await call("match_asset_to_blockout", { actorLabel: "BlkTest_unit", candidateFolder: SHAPES, limit: 10 });
    expect(Number(match.candidateCount)).toBeGreaterThanOrEqual(4);
    const candidates = (match.matches as Json[])[0].candidates as Json[];
    expect(Number(candidates[0].score)).toBeGreaterThan(0.95);
    expect(Number(candidates[0].uniformScale)).toBeCloseTo(1, 1);
    expect(String(candidates[candidates.length - 1].assetPath)).toContain("Plane");
    for (let i = 1; i < candidates.length; i++) {
      expect(Number(candidates[i - 1].score)).toBeGreaterThanOrEqual(Number(candidates[i].score));
    }
  }, 60_000);

  it("with tag ranks every blockout in the set", async () => {
    const match = await call("match_asset_to_blockout", { tag: TAG, candidateFolder: SHAPES, limit: 2 });
    expect(match.count).toBe((await listed()).length);
    for (const row of match.matches as Json[]) {
      expect(row.candidates as Json[]).toHaveLength(2);
      expect(typeof row.bestMatch).toBe("string");
    }
  }, 60_000);
});

describe("replace_blockout and restore_blockout", () => {
  it("stretch fits the cube to the box's bounds and restore puts the box back", async () => {
    const replaced = await call("replace_blockout", { actorLabel: "BlkTest_box", assetPath: CUBE, fit: "stretch" });
    expect(replaced.replacedCount).toBe(1);
    const row = (replaced.replaced as Json[])[0];
    expect(row.actorLabel).toBe("BlkTest_box");
    const scale = vec(row.scale);
    expect(scale.x).toBeCloseTo(4, 2);
    expect(scale.y).toBeCloseTo(2, 2);
    expect(scale.z).toBeCloseTo(3, 2);
    // The engine cube's pivot is its centre, so the actor sits half its height up.
    expect(vec(row.location).z).toBeCloseTo(150, 0);
    const replacementPath = String(row.actorPath);
    expect(await byLabel("BlkTest_box")).toBeUndefined();

    const bounds = await call("get_actor_bounds", { actorPath: replacementPath });
    expect(Number(vec(bounds.extent).x)).toBeCloseTo(200, 0);
    expect(Number(vec(bounds.origin).z)).toBeCloseTo(150, 0);

    const withReplaced = await call("list_blockout", { tag: TAG, includeReplaced: true });
    const replacement = (withReplaced.replaced as Json[]).find((r) => r.actorPath === replacementPath);
    expect(replacement?.restorable).toBe(true);

    const rollback = replaced.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("restore_blockout");
    const restored = await call(rollback.method, rollback.payload);
    expect(restored.restoredCount).toBe(1);
    const back = await byLabel("BlkTest_box");
    expect(back?.shape).toBe("box");
    expect(vec(back!.size).x).toBeCloseTo(400, 0);
    expect((restored.rollback as Json).method).toBe("replace_blockout");
  }, 60_000);

  it("uniform fit keeps the asset's proportions inside the bounds", async () => {
    const replaced = await call("replace_blockout", { actorLabel: "BlkTest_box", assetPath: CUBE });
    const scale = vec((replaced.replaced as Json[])[0].scale);
    expect(scale.x).toBeCloseTo(2, 2);
    expect(scale.y).toBeCloseTo(2, 2);
    expect(scale.z).toBeCloseTo(2, 2);
    await call("restore_blockout", { actorPaths: replaced.actorPaths });
    expect(await byLabel("BlkTest_box")).toBeDefined();
  }, 60_000);

  it("with tag and candidateFolder swaps every blockout for its best match in one call", async () => {
    const before = (await listed()).length;
    const replaced = await call("replace_blockout", { tag: TAG, candidateFolder: SHAPES });
    expect(replaced.replacedCount).toBe(before);
    expect(await listed()).toHaveLength(0);
    for (const row of replaced.replaced as Json[]) expect(String(row.assetPath)).toContain(SHAPES);

    const restored = await call("restore_blockout", { actorPaths: replaced.actorPaths });
    expect(restored.restoredCount).toBe(before);
    expect(await listed()).toHaveLength(before);
  }, 120_000);
});
