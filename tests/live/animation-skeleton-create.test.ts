import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

// Runs only against the dedicated disposable test project.

const SOURCE_MESH = "/Engine/EngineMeshes/SkeletalCube";
const TEST_MESH = `${TEST_PREFIX}/SKM_CreateSkeleton`;
const TEST_SKELETON_NAME = "SK_CreateSkeleton";
const TEST_SKELETON = `${TEST_PREFIX}/${TEST_SKELETON_NAME}.${TEST_SKELETON_NAME}`;

type Bone = { name?: string; index?: number; parentIndex?: number };

function hierarchy(result: unknown): Array<Pick<Bone, "name" | "index" | "parentIndex">> {
  const bones = resultArray(result, "bones") as Bone[] | undefined;
  return (bones ?? []).map(({ name, index, parentIndex }) => ({ name, index, parentIndex }));
}

let bridge: EditorBridge;
let sourceSkeletonPath = "";
let sourceHierarchy: Array<Pick<Bone, "name" | "index" | "parentIndex">> = [];
let createRollback: { method?: string; payload?: Record<string, unknown> } | undefined;

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: TEST_SKELETON, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: TEST_MESH, force: true });

  const duplicate = await callBridge(bridge, "duplicate_asset", {
    sourcePath: SOURCE_MESH,
    destinationPath: TEST_MESH,
  });
  expect(duplicate.ok, duplicate.error).toBe(true);

  const meshInfo = await callBridge(bridge, "get_mesh_info", { assetPath: TEST_MESH });
  expect(meshInfo.ok, meshInfo.error).toBe(true);
  sourceSkeletonPath = String((meshInfo.result as Record<string, unknown>).skeletonPath ?? "");
  expect(sourceSkeletonPath).toBeTruthy();

  const skeleton = await callBridge(bridge, "get_skeleton_info", { assetPath: TEST_MESH });
  expect(skeleton.ok, skeleton.error).toBe(true);
  sourceHierarchy = hierarchy(skeleton.result);
  expect(sourceHierarchy.length).toBeGreaterThan(0);
});

afterAll(async () => {
  if (bridge && sourceSkeletonPath) {
    await callBridge(bridge, "set_asset_property", {
      assetPath: TEST_MESH,
      propertyName: "Skeleton",
      value: sourceSkeletonPath,
      save: true,
    });
  }
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: TEST_SKELETON, force: true });
    await callBridge(bridge, "delete_asset", { assetPath: TEST_MESH, force: true });
    disconnectBridge();
  }
});

describe("animation.create_skeleton", () => {
  it("creates, assigns, saves, reloads, and precisely replays a factory skeleton", async () => {
    const created = await callBridge(bridge, "create_skeleton", {
      name: TEST_SKELETON_NAME,
      skeletalMeshPath: TEST_MESH,
      packagePath: TEST_PREFIX,
      onConflict: "skip",
    });
    expect(created.ok, created.error).toBe(true);

    const createResult = created.result as Record<string, unknown>;
    expect(createResult.success).toBe(true);
    expect(createResult.created).toBe(true);
    expect(createResult.path).toBe(TEST_SKELETON);
    expect(createResult.meshAssigned).toBe(true);
    expect(createResult.exactReferenceHierarchy).toBe(true);
    expect(createResult.skeletonSaved).toBe(true);
    expect(createResult.sourceSkeletalMeshSaved).toBe(true);
    createRollback = createResult.rollback as typeof createRollback;
    expect(createRollback?.method).toBe("set_asset_property");
    expect(createRollback?.payload).toBeTruthy();

    const assignedMesh = await callBridge(bridge, "get_mesh_info", { assetPath: TEST_MESH });
    expect(assignedMesh.ok, assignedMesh.error).toBe(true);
    expect((assignedMesh.result as Record<string, unknown>).skeletonPath).toBe(TEST_SKELETON);

    const assignedSkeleton = await callBridge(bridge, "get_skeleton_info", { assetPath: TEST_MESH });
    expect(assignedSkeleton.ok, assignedSkeleton.error).toBe(true);
    expect(hierarchy(assignedSkeleton.result)).toEqual(sourceHierarchy);

    const reloaded = await callBridge(bridge, "force_reload_asset", {
      assetPath: TEST_MESH,
      discardUnsaved: false,
    });
    expect(reloaded.ok, reloaded.error).toBe(true);
    expect((reloaded.result as Record<string, unknown>).success).toBe(true);

    const persistedMesh = await callBridge(bridge, "get_mesh_info", { assetPath: TEST_MESH });
    expect(persistedMesh.ok, persistedMesh.error).toBe(true);
    expect((persistedMesh.result as Record<string, unknown>).skeletonPath).toBe(TEST_SKELETON);
    const persistedSkeleton = await callBridge(bridge, "get_skeleton_info", { assetPath: TEST_MESH });
    expect(persistedSkeleton.ok, persistedSkeleton.error).toBe(true);
    expect(hierarchy(persistedSkeleton.result)).toEqual(sourceHierarchy);

    const replay = await callBridge(bridge, "create_skeleton", {
      name: TEST_SKELETON_NAME,
      skeletalMeshPath: TEST_MESH,
      packagePath: TEST_PREFIX,
      onConflict: "skip",
    });
    expect(replay.ok, replay.error).toBe(true);
    const replayResult = replay.result as Record<string, unknown>;
    expect(replayResult.success).toBe(true);
    expect(replayResult.created).toBe(false);
    expect(replayResult.existed).toBe(true);
    expect(replayResult.meshAssigned).toBe(true);
    expect(replayResult.exactReferenceHierarchy).toBe(true);

    const conflict = await callBridge(bridge, "create_skeleton", {
      name: TEST_SKELETON_NAME,
      skeletalMeshPath: TEST_MESH,
      packagePath: TEST_PREFIX,
      onConflict: "error",
    });
    expect(conflict.ok, conflict.error).toBe(true);
    const conflictResult = conflict.result as Record<string, unknown>;
    expect(conflictResult.success).toBe(false);
    expect(String(conflictResult.error)).toContain("already exists");
  });

  it("restores the duplicate before deleting the disposable skeleton", async () => {
    expect(createRollback?.method).toBe("set_asset_property");
    const restored = await callBridge(bridge, createRollback!.method!, createRollback!.payload!);
    expect(restored.ok, restored.error).toBe(true);
    expect((restored.result as Record<string, unknown>).success).toBe(true);

    const restoredMesh = await callBridge(bridge, "get_mesh_info", { assetPath: TEST_MESH });
    expect(restoredMesh.ok, restoredMesh.error).toBe(true);
    expect((restoredMesh.result as Record<string, unknown>).skeletonPath).toBe(sourceSkeletonPath);
  });
});
