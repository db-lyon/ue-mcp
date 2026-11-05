// @live-editor headless
/**
 * IK rig configuration and retargeting between two copies of the engine
 * tutorial mesh: configure_ik_rig read back through read_ik_rig, retarget pose
 * auto-alignment read back through read_ik_retargeter, and a batch retarget.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AnimA/Retarget";
const OUT_DIR = `${DIR}/Out`;
const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const SRC_MESH = `${DIR}/SKM_BCovA_Src`;
const TGT_MESH = `${DIR}/SKM_BCovA_Tgt`;
const IDLE = `${DIR}/AS_BCovA_RtIdle`;
const SRC_RIG = `${DIR}/IK_BCovA_Src`;
const TGT_RIG = `${DIR}/IK_BCovA_Tgt`;
const RETARGETER = `${DIR}/RTG_BCovA`;
const OUTPUT = `${OUT_DIR}/AS_BCovA_RtIdle_Retargeted`;
const ASSETS = [OUTPUT, RETARGETER, SRC_RIG, TGT_RIG, IDLE, SRC_MESH, TGT_MESH];
const ARM = { name: "LeftArm", startBone: "upperarm_l", endBone: "hand_l" };
const SPINE = { name: "Spine", startBone: "spine_01", endBone: "spine_03" };

type Json = Record<string, unknown>;
type Quat = { x: number; y: number; z: number; w: number };
type Pose = { name: string; current: boolean; rotationOffsets: Array<{ bone: string; rotationQuaternion: Quat }> };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** Angle in degrees of a bone's offset in the current target pose; a missing offset is identity. */
async function targetOffsetDegrees(bone: string): Promise<number> {
  const read = await call("read_ik_retargeter", { assetPath: RETARGETER });
  const pose = (read.targetPoses as Pose[]).find((p) => p.current);
  const q = pose?.rotationOffsets.find((o) => o.bone === bone)?.rotationQuaternion;
  if (!q) return 0;
  return (2 * Math.acos(Math.min(1, Math.abs(q.w))) * 180) / Math.PI;
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/TutorialTPP`, destinationPath: SRC_MESH });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/TutorialTPP`, destinationPath: TGT_MESH });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: IDLE });
  await call("create_ik_rig", { name: "IK_BCovA_Src", packagePath: DIR, skeletalMeshPath: SRC_MESH, retargetRoot: "pelvis", chains: [SPINE, ARM] });
  await call("create_ik_rig", { name: "IK_BCovA_Tgt", packagePath: DIR, skeletalMeshPath: TGT_MESH, chains: [SPINE] });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("IK rig and retargeting", () => {
  it("configure_ik_rig sets the retarget root and upserts a chain", async () => {
    const before = await call("read_ik_rig", { assetPath: TGT_RIG });
    expect((before.retargetChains as Array<{ name: string }>).map((c) => c.name)).not.toContain("LeftArm");

    await call("configure_ik_rig", { rigPath: TGT_RIG, retargetRoot: "pelvis", chains: [ARM] });
    const after = await call("read_ik_rig", { assetPath: TGT_RIG });
    expect(after.retargetRoot).toBe("pelvis");
    const chains = after.retargetChains as Array<{ name: string; startBone: string; endBone: string }>;
    expect(chains).toContainEqual(expect.objectContaining(ARM));
    expect(chains.map((c) => c.name)).toContain("Spine");
  }, 180_000);

  it("auto_align_retarget_pose aligns a hand-rotated target arm back to the identical source", async () => {
    await call("create_ik_retargeter", { name: "RTG_BCovA", packagePath: DIR, sourceRig: SRC_RIG, targetRig: TGT_RIG });
    await call("configure_ik_retargeter", { retargeterPath: RETARGETER, sourcePreviewMesh: SRC_MESH, targetPreviewMesh: TGT_MESH });
    const read = await call("read_ik_retargeter", { assetPath: RETARGETER });
    const poseName = String(read.currentTargetPose);
    const half = Math.PI / 8;
    await call("configure_ik_retargeter", {
      retargeterPath: RETARGETER, ensureDefaultOps: false,
      pose: { side: "target", name: poseName, rotationOffsets: [{ bone: "upperarm_l", rotationQuaternion: { x: 0, y: 0, z: Math.sin(half), w: Math.cos(half) } }] },
    });
    expect(await targetOffsetDegrees("upperarm_l")).toBeCloseTo(45, 0);

    const aligned = await call("auto_align_retarget_pose", { retargeterPath: RETARGETER, side: "target" });
    expect(aligned.side).toBe("target");
    expect(await targetOffsetDegrees("upperarm_l")).toBeLessThan(1);
  }, 180_000);

  it("batch_retarget_animations bakes the source clip onto the target mesh", async () => {
    const source = await call("read_anim_sequence", { assetPath: IDLE });
    const result = await call("batch_retarget_animations", {
      retargeterPath: RETARGETER, sourceMesh: SRC_MESH, targetMesh: TGT_MESH, animPaths: [IDLE], outputPath: OUT_DIR,
    });
    expect(result.createdCount).toBe(1);
    const created = String((result.createdAssets as string[])[0]);
    expect(created).toContain(OUT_DIR);

    const output = await call("read_anim_sequence", { assetPath: created });
    expect(output.numFrames).toBe(source.numFrames);
    expect(String(output.skeleton)).toContain("TutorialTPP_Skeleton");
    await call("delete_asset", { assetPath: created, force: true });
  }, 300_000);
});
