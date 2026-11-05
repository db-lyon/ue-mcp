// @live-editor headless
/**
 * Skeleton and skeletal mesh edits on a copy of the engine's SkeletalCube
 * with its own skeleton: virtual bones, a committed hierarchy edit, mesh and
 * skeleton sockets, plus the tutorial mesh's physics asset bodies.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AnimA/Skeleton";
const MESH = `${DIR}/SKM_BCovA_Cube`;
const SKELETON = `${DIR}/SK_BCovA_Cube`;
const TUTORIAL_MESH = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP";
const SESSION = "BCovA_SkelEdit";

type Json = Record<string, unknown>;
type Bone = { name: string; index: number; parentIndex: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let bones: Bone[] = [];

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "cancel_skeleton_edit", { sessionTag: SESSION });
  await callBridge(bridge, "delete_asset", { assetPath: MESH, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: SKELETON, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
  await call("duplicate_asset", { sourcePath: "/Engine/EngineMeshes/SkeletalCube", destinationPath: MESH });
  await call("create_skeleton", { name: "SK_BCovA_Cube", skeletalMeshPath: MESH, packagePath: DIR });
  bones = (await call("get_skeleton_info", { assetPath: MESH })).bones as Bone[];
  expect(bones.length).toBeGreaterThanOrEqual(2);
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await cleanup();
    disconnectBridge();
  }
}, 120_000);

describe("skeleton edits", () => {
  it("add_virtual_bone adds a virtual bone between two real bones", async () => {
    const source = bones[0].name;
    const target = bones[bones.length - 1].name;
    const added = await call("add_virtual_bone", { skeletonPath: SKELETON, sourceBone: source, targetBone: target, name: "BCovTip" });
    expect(String(added.virtualBoneName)).toContain("BCovTip");

    const info = await call("get_skeleton_info", { assetPath: SKELETON });
    const virtualBones = info.virtualBones as Array<{ name: string; sourceBone: string; targetBone: string }>;
    expect(virtualBones).toContainEqual({ name: added.virtualBoneName, sourceBone: source, targetBone: target });
  }, 120_000);

  it("commit_skeleton_edit writes a bone added in the session to the mesh and its skeleton", async () => {
    const parent = bones[bones.length - 1].name;
    await call("begin_skeleton_edit", { skeletalMeshPath: MESH, sessionTag: SESSION });
    await call("edit_skeleton_bones", { sessionTag: SESSION, edits: [{ op: "add", bone: "BCovExtra", parent }] });

    const before = await call("get_skeleton_info", { assetPath: MESH });
    expect((before.bones as Bone[]).map((b) => b.name)).not.toContain("BCovExtra");

    const committed = await call("commit_skeleton_edit", { sessionTag: SESSION });
    expect(committed.committed).toBe(true);
    expect(committed.editsCommitted).toBe(1);

    const after = await call("get_skeleton_info", { assetPath: MESH });
    const afterBones = after.bones as Bone[];
    const extra = afterBones.find((b) => b.name === "BCovExtra");
    expect(extra).toBeDefined();
    expect(afterBones[extra!.parentIndex].name).toBe(parent);
    // boneCount includes the virtual bone the first test added.
    expect(after.boneCount).toBe(Number(before.boneCount) + 1);
    const skeleton = await call("get_skeleton_info", { assetPath: SKELETON });
    expect((skeleton.bones as Bone[]).map((b) => b.name)).toContain("BCovExtra");
  }, 180_000);

  it("list_animation_sockets lists a mesh socket and a skeleton socket with their bones", async () => {
    const bone = bones[0].name;
    await call("add_socket", { assetPath: MESH, socketName: "BCovMeshSocket", boneName: bone, relativeLocation: { x: 0, y: 0, z: 12 } });
    await call("add_socket", { assetPath: SKELETON, socketName: "BCovSkelSocket", boneName: bone });

    const listed = await call("list_animation_sockets", { assetPath: MESH });
    const sockets = listed.sockets as Array<{ name: string; boneName: string; source: string; relativeLocation: { z: number } }>;
    const meshSocket = sockets.find((s) => s.name === "BCovMeshSocket");
    expect(meshSocket).toMatchObject({ boneName: bone, source: "mesh" });
    expect(meshSocket?.relativeLocation.z).toBeCloseTo(12, 3);
    expect(sockets.find((s) => s.name === "BCovSkelSocket")).toMatchObject({ boneName: bone, source: "skeleton" });
    expect(listed.count).toBe(sockets.length);
  }, 120_000);

  it("get_physics_asset_info lists the tutorial mesh's physics bodies by bone", async () => {
    const info = await call("get_physics_asset_info", { assetPath: TUTORIAL_MESH });
    expect(String(info.physicsAssetPath)).toContain("TutorialTPP_PhysicsAsset");
    const bodies = (info.bodies as Array<{ boneName: string }>).map((b) => b.boneName);
    expect(bodies.length).toBe(info.bodyCount);
    expect(bodies).toContain("pelvis");
  }, 120_000);
});
