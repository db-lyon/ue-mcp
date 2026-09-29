import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, resultArray } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

// Read-only, so the engine mesh is read in place: two bones, 24 source vertices.
const MESH = "/Engine/EngineMeshes/SkeletalCube";
const METHOD = "read_skeletal_mesh_bone_vertex_bounds";

type Vec = { x: number; y: number; z: number };
type Box = { min: Vec; max: Vec; center: Vec; extent: Vec };
type Bone = {
  boneName: string;
  boneIndex: number;
  parentName: string;
  vertexCount: number;
  bindPose: { location: Vec };
  componentSpace?: Box;
  boneSpace?: Box;
};

let bridge: EditorBridge;

async function read(params: Record<string, unknown>) {
  const response = await callBridge(bridge, METHOD, { assetPath: MESH, ...params });
  expect(response.ok, response.error).toBe(true);
  return response.result as Record<string, unknown>;
}

function bonesOf(result: Record<string, unknown>): Bone[] {
  return (resultArray(result, "bones") ?? []) as Bone[];
}

beforeAll(async () => {
  bridge = await getBridge();
});

afterAll(() => {
  disconnectBridge();
});

describe("asset.read_skeletal_mesh_bone_vertex_bounds", () => {
  it("assigns every weighted vertex to exactly one dominant bone", async () => {
    const result = await read({});
    expect(result.success, String(result.error)).toBe(true);
    expect(result.mode).toBe("dominant");
    expect(result.profileName).toBe("default");

    const bones = bonesOf(result);
    expect(bones.length).toBeGreaterThan(0);
    const counted = bones.reduce((sum, bone) => sum + bone.vertexCount, 0);
    expect(counted + Number(result.unweightedVertexCount)).toBe(result.vertexCount);

    for (const bone of bones) {
      expect(bone.vertexCount).toBeGreaterThan(0);
      const box = bone.componentSpace!;
      for (const axis of ["x", "y", "z"] as const) {
        expect(box.min[axis]).toBeLessThanOrEqual(box.max[axis]);
        expect(box.center[axis]).toBeCloseTo((box.min[axis] + box.max[axis]) / 2, 4);
        expect(box.extent[axis]).toBeCloseTo((box.max[axis] - box.min[axis]) / 2, 4);
      }
      expect(bone.boneSpace).toBeDefined();
    }
  });

  it("measures bone space from the bone's bind pose", async () => {
    // The cube's bind pose has no scale, so a box keeps its size in either
    // frame, and the component-space extent must reappear in bone space.
    const bones = bonesOf(await read({}));
    for (const bone of bones) {
      const component = bone.componentSpace!.extent;
      const local = bone.boneSpace!.extent;
      const sorted = (v: Vec) => [v.x, v.y, v.z].sort((a, b) => a - b);
      const [c0, c1, c2] = sorted(component);
      const [l0, l1, l2] = sorted(local);
      expect(l2).toBeCloseTo(c2, 0);
      expect(l1).toBeCloseTo(c1, 0);
      expect(l0).toBeLessThan(0.5 + c0);
    }
  });

  it("reports only the named bones", async () => {
    const all = bonesOf(await read({}));
    const target = all[all.length - 1];
    const filtered = await read({ boneNames: [target.boneName] });
    const bones = bonesOf(filtered);
    expect(bones).toHaveLength(1);
    expect(bones[0].boneName).toBe(target.boneName);
    expect(bones[0].vertexCount).toBe(target.vertexCount);
  });

  it("counts a vertex for every bone at or above minWeight", async () => {
    const dominant = bonesOf(await read({}));
    const threshold = await read({ minWeight: 0.01 });
    expect(threshold.mode).toBe("minWeight");
    const byName = new Map(bonesOf(threshold).map((bone) => [bone.boneName, bone]));
    // A low threshold can only add influences, never drop the dominant one.
    for (const bone of dominant) {
      expect(byName.get(bone.boneName)?.vertexCount ?? 0).toBeGreaterThanOrEqual(bone.vertexCount);
    }
  });

  it("refuses bad input before reading", async () => {
    for (const [params, message] of [
      [{ boneNames: ["MCPTest_NoSuchBone"] }, "is not a bone"],
      [{ boneNames: [] }, "non-empty array"],
      [{ minWeight: 0 }, "0 < minWeight <= 1"],
      [{ minWeight: 1.5 }, "0 < minWeight <= 1"],
      [{ profileName: "MCPTest_NoSuchProfile" }, "does not exist"],
    ] as const) {
      const result = await read(params);
      expect(result.success).toBe(false);
      expect(String(result.error)).toContain(message);
    }
    const all = bonesOf(await read({}));
    const duplicate = await read({ boneNames: [all[0].boneName, all[0].boneName] });
    expect(duplicate.success).toBe(false);
    expect(String(duplicate.error)).toContain("duplicate bone");
  });
});
