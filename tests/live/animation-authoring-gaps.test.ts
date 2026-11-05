// @live-editor headless
/**
 * Animation authoring gaps (#1227, #1230, #1372, #1288, #1369), each read back.
 *
 * The fixtures are copies of the engine's tutorial character so every write
 * lands on an asset under /Game, and a pair of IK rigs over the engine's
 * skeletal cube for the retargeter.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const DIR = `${TEST_PREFIX}/AnimGaps`;
const IDLE = `${DIR}/AS_Idle`;
const WALK = `${DIR}/AS_Walk`;
const MESH = `${DIR}/SKM_TPP`;
const COMPOSED = `${DIR}/AS_Composed`;
const RIG_SRC = `${DIR}/IK_GapsSrc`;
const RIG_TGT = `${DIR}/IK_GapsTgt`;
const RETARGETER = `${DIR}/RTG_Gaps`;
const CONTROL_RIG = `${DIR}/CR_Gaps`;

type Json = Record<string, unknown>;
type Quat = { x: number; y: number; z: number; w: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function rotationAt(assetPath: string, bone: string, frame: number): Promise<Quat> {
  const read = await call("read_bone_track", { assetPath, boneName: bone, rotationFormat: "quaternion", frames: [frame] });
  return (read.samples as Array<{ rotation: Quat }>)[0].rotation;
}

function mul(a: Quat, b: Quat): Quat {
  return {
    x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
    y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
    z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
  };
}

function slerp(a: Quat, bIn: Quat, t: number): Quat {
  let b = bIn;
  let d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0) { b = { x: -b.x, y: -b.y, z: -b.z, w: -b.w }; d = -d; }
  const th = Math.acos(Math.min(1, d));
  const s = Math.sin(th);
  if (s < 1e-6) return a;
  const wa = Math.sin((1 - t) * th) / s;
  const wb = Math.sin(t * th) / s;
  return { x: wa * a.x + wb * b.x, y: wa * a.y + wb * b.y, z: wa * a.z + wb * b.z, w: wa * a.w + wb * b.w };
}

function expectSameRotation(actual: Quat, expected: Quat, tolerance = 1e-3): void {
  const dot = Math.abs(actual.x * expected.x + actual.y * expected.y + actual.z * expected.z + actual.w * expected.w);
  expect(1 - dot, `${JSON.stringify(actual)} vs ${JSON.stringify(expected)}`).toBeLessThan(tolerance);
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of [CONTROL_RIG, RETARGETER, RIG_SRC, RIG_TGT, COMPOSED, IDLE, WALK, MESH]) {
    await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  }
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: IDLE });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Walk_Fwd`, destinationPath: WALK });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/TutorialTPP`, destinationPath: MESH });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    for (const path of [CONTROL_RIG, RETARGETER, RIG_SRC, RIG_TGT, COMPOSED, IDLE, WALK, MESH]) {
      await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    }
    disconnectBridge();
  }
});

describe("animation authoring gaps", () => {
  it("transform_bone_tracks rotates a track by the given yaw and the readback matches (#1227)", async () => {
    const before = await rotationAt(IDLE, "pelvis", 0);
    const result = await call("transform_bone_tracks", {
      assetPaths: [IDLE],
      trackOps: [{ op: "rotate", bones: ["pelvis"], rotation: { pitch: 0, yaw: 35, roll: 0 } }],
    });
    const half = (35 * Math.PI) / 360;
    const expected = mul({ x: 0, y: 0, z: Math.sin(half), w: Math.cos(half) }, before);
    expectSameRotation(await rotationAt(IDLE, "pelvis", 0), expected);

    const rollback = result.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("bake_keyframes_batch");
    await call(rollback.method, rollback.payload);
    expectSameRotation(await rotationAt(IDLE, "pelvis", 0), before);
  }, 120_000);

  it("compose_pose_sequence blends the layered bones at frame 0 and eases back to the base (#1230)", async () => {
    const base = await rotationAt(WALK, "upperarm_l", 0);
    const layer = await rotationAt(IDLE, "upperarm_l", 0);
    const composed = await call("compose_pose_sequence", {
      name: "AS_Composed",
      packagePath: DIR,
      sources: [{ assetPath: WALK, frame: 0 }, { assetPath: IDLE, frame: 0 }],
      layers: [{ source: 1, weight: 0.7, nameContains: ["upperarm"] }],
      numFrames: 10,
      easeToSource: 0,
    });
    expect(composed.numFrames).toBe(10);
    expectSameRotation(await rotationAt(COMPOSED, "upperarm_l", 0), slerp(base, layer, 0.7));
    expectSameRotation(await rotationAt(COMPOSED, "upperarm_l", 10), base);
  }, 120_000);

  it("generate_skeletal_mesh_lods builds the requested LODs and skips a mesh already there (#1372)", async () => {
    const first = await call("generate_skeletal_mesh_lods", { assetPath: MESH, lodCount: 3, trianglePercentages: [50, 25] });
    const mesh = (first.meshes as Array<Json>)[0];
    expect(mesh.lodCountAfter).toBe(3);
    const lods = mesh.lods as Array<{ triangles: number }>;
    expect(lods[1].triangles / lods[0].triangles).toBeCloseTo(0.5, 1);
    expect(lods[2].triangles / lods[0].triangles).toBeCloseTo(0.25, 1);
    const again = await call("generate_skeletal_mesh_lods", { assetPath: MESH, lodCount: 3, trianglePercentages: [50, 25] });
    expect((again.meshes as Array<Json>)[0].status).toBe("skipped");
  }, 180_000);

  it("renames and removes a retarget pose, and keeps the default pose (#1288)", async () => {
    for (const [path, name] of [[RIG_SRC, "IK_GapsSrc"], [RIG_TGT, "IK_GapsTgt"]] as const) {
      await call("create_ik_rig", {
        name, packagePath: DIR, skeletalMeshPath: "/Engine/EngineMeshes/SkeletalCube",
        chains: [{ name: "Spine", startBone: "Bone01", endBone: "Bone02" }],
      });
      expect(path).toContain(name);
    }
    await call("create_ik_retargeter", { name: "RTG_Gaps", packagePath: DIR, sourceRig: RIG_SRC, targetRig: RIG_TGT });
    await call("configure_ik_retargeter", { retargeterPath: RETARGETER, pose: { side: "target", name: "Scratch", create: true } });

    const renamed = await call("rename_retarget_pose", { retargeterPath: RETARGETER, poseName: "Scratch", newName: "Scratch2", side: "target" });
    expect(renamed.poses).toContain("Scratch2");
    expect(renamed.poses).not.toContain("Scratch");

    const removed = await call("remove_retarget_pose", { retargeterPath: RETARGETER, poseName: "Scratch2", side: "target" });
    expect(removed.poses).not.toContain("Scratch2");
    expect(typeof removed.currentPose).toBe("string");

    const refused = await callBridge(bridge, "remove_retarget_pose", { retargeterPath: RETARGETER, poseName: removed.currentPose, side: "target" });
    expect(succeeded(refused)).toBe(false);
  }, 180_000);

  it("set_control_rig_preview_mesh sets the preview mesh and it survives a reload (#1369)", async () => {
    await call("create_control_rig", { name: "CR_Gaps", packagePath: DIR, skeletalMeshPath: "/Engine/EngineMeshes/SkeletalCube" });
    const set = await call("set_control_rig_preview_mesh", { assetPath: CONTROL_RIG, skeletalMeshPath: MESH });
    expect(String(set.previewMesh)).toContain("SKM_TPP");
    await call("reload_package", { assetPath: CONTROL_RIG });
    const again = await call("set_control_rig_preview_mesh", { assetPath: CONTROL_RIG, skeletalMeshPath: MESH });
    expect(again.unchanged).toBe(true);
    expect(String(again.previousPreviewMesh)).toContain("SKM_TPP");
  }, 180_000);
});
