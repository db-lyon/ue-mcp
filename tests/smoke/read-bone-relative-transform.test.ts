import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

const SOURCE_MESH = "/Engine/EngineMeshes/SkeletalCube";
const TEST_MESH = `${TEST_PREFIX}/SKM_ReadBoneRelative`;
const ACTOR_LABEL = "MCPTest_ReadBoneRelative";
const UNSCALED_ACTOR_LABEL = "MCPTest_ReadBoneRelativeUnscaled";
const SAMPLE_SOCKET = "MCPTest_Sample";
const REFERENCE_SOCKET = "MCPTest_Reference";

function socketLocation(result: unknown): Record<string, number> {
  const samples = resultArray(result, "samples") as Array<Record<string, unknown>> | undefined;
  const transform = samples?.[0]?.transform as Record<string, unknown> | undefined;
  return (transform?.location ?? {}) as Record<string, number>;
}

let bridge: EditorBridge;
let rootBone = "root";

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: ACTOR_LABEL });
  await callBridge(bridge, "delete_actor", { actorLabel: UNSCALED_ACTOR_LABEL });
  await callBridge(bridge, "delete_asset", { assetPath: TEST_MESH, force: true });

  const duplicate = await callBridge(bridge, "duplicate_asset", {
    sourcePath: SOURCE_MESH,
    destinationPath: TEST_MESH,
  });
  expect(duplicate.ok, duplicate.error).toBe(true);

  const skeleton = await callBridge(bridge, "get_skeleton_info", { assetPath: TEST_MESH });
  expect(skeleton.ok, skeleton.error).toBe(true);
  const bones = resultArray(skeleton.result, "bones") as Array<Record<string, unknown>> | undefined;
  rootBone = String(bones?.[0]?.name ?? "root");

  for (const [socketName, x] of [[SAMPLE_SOCKET, 10], [REFERENCE_SOCKET, 3]] as const) {
    const added = await callBridge(bridge, "add_socket", {
      assetPath: TEST_MESH,
      socketName,
      boneName: rootBone,
      relativeLocation: { x, y: 2, z: 0 },
    });
    expect(added.ok, added.error).toBe(true);
  }

  const spawned = await callBridge(bridge, "spawn_skeletal_mesh_actor", {
    skeletalMesh: TEST_MESH,
    label: ACTOR_LABEL,
    location: { x: 101, y: 203, z: 307 },
    rotation: { pitch: 11, yaw: 47, roll: 5 },
    scale: { x: 2, y: 3, z: 1 },
  });
  expect(spawned.ok, spawned.error).toBe(true);

  // Same mesh, uniform scale, somewhere else entirely. Component space is
  // defined to be independent of both, so the two actors must read identically.
  const spawnedUnscaled = await callBridge(bridge, "spawn_skeletal_mesh_actor", {
    skeletalMesh: TEST_MESH,
    label: UNSCALED_ACTOR_LABEL,
    location: { x: -640, y: 55, z: 12 },
    rotation: { pitch: -23, yaw: 133, roll: 61 },
    scale: { x: 1, y: 1, z: 1 },
  });
  expect(spawnedUnscaled.ok, spawnedUnscaled.error).toBe(true);
});

afterAll(async () => {
  await callBridge(bridge, "delete_actor", { actorLabel: ACTOR_LABEL });
  await callBridge(bridge, "delete_actor", { actorLabel: UNSCALED_ACTOR_LABEL });
  await callBridge(bridge, "delete_asset", { assetPath: TEST_MESH, force: true });
  disconnectBridge();
});

describe("editor.read_bone_transforms relativeTo", () => {
  it("returns sample relative to reference in the correct order", async () => {
    const read = await callBridge(bridge, "read_bone_transforms", {
      actorLabel: ACTOR_LABEL,
      bones: [SAMPLE_SOCKET],
      relativeTo: REFERENCE_SOCKET,
      space: "world",
      world: "editor",
    });
    expect(read.ok, read.error).toBe(true);

    const result = read.result as Record<string, unknown>;
    expect(result.success).toBe(true);
    expect(result.space).toBe("relative");
    expect(result.relativeTo).toBe(REFERENCE_SOCKET);

    const samples = resultArray(result, "samples") as Array<Record<string, unknown>> | undefined;
    const transform = samples?.[0]?.transform as Record<string, unknown> | undefined;
    const location = transform?.location as Record<string, number> | undefined;
    expect(location?.x).toBeCloseTo(7, 3);
    expect(location?.y).toBeCloseTo(0, 3);
    expect(location?.z).toBeCloseTo(0, 3);

    const relativeToBone = await callBridge(bridge, "read_bone_transforms", {
      actorLabel: ACTOR_LABEL,
      bones: [SAMPLE_SOCKET],
      relativeTo: rootBone,
      world: "editor",
    });
    expect(relativeToBone.ok, relativeToBone.error).toBe(true);
    const boneResult = relativeToBone.result as Record<string, unknown>;
    const boneSamples = resultArray(boneResult, "samples") as Array<Record<string, unknown>> | undefined;
    const boneTransform = boneSamples?.[0]?.transform as Record<string, unknown> | undefined;
    const boneLocation = boneTransform?.location as Record<string, number> | undefined;
    expect(boneLocation?.x).toBeCloseTo(10, 3);
    expect(boneLocation?.y).toBeCloseTo(2, 3);
    expect(boneLocation?.z).toBeCloseTo(0, 3);
  });

  it("reports socket component space free of the component's non-uniform scale", async () => {
    const scaled = await callBridge(bridge, "read_bone_transforms", {
      actorLabel: ACTOR_LABEL,
      bones: [SAMPLE_SOCKET],
      space: "component",
      world: "editor",
    });
    expect(scaled.ok, scaled.error).toBe(true);
    expect((scaled.result as Record<string, unknown>).space).toBe("component");

    const unscaled = await callBridge(bridge, "read_bone_transforms", {
      actorLabel: UNSCALED_ACTOR_LABEL,
      bones: [SAMPLE_SOCKET],
      space: "component",
      world: "editor",
    });
    expect(unscaled.ok, unscaled.error).toBe(true);

    const scaledLocation = socketLocation(scaled.result);
    const unscaledLocation = socketLocation(unscaled.result);
    expect(scaledLocation.x).toBeCloseTo(unscaledLocation.x, 3);
    expect(scaledLocation.y).toBeCloseTo(unscaledLocation.y, 3);
    expect(scaledLocation.z).toBeCloseTo(unscaledLocation.z, 3);
  });

  it("fails clearly when relativeTo is unknown", async () => {
    const read = await callBridge(bridge, "read_bone_transforms", {
      actorLabel: ACTOR_LABEL,
      bones: [SAMPLE_SOCKET],
      relativeTo: "MCPTest_Missing",
      world: "editor",
    });
    expect(read.ok, read.error).toBe(true);

    const result = read.result as Record<string, unknown>;
    expect(result.success).toBe(false);
    expect(String(result.error)).toContain("Relative bone or socket not found");
    expect(String(result.error)).toContain("MCPTest_Missing");
  });
});
