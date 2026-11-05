// @live-editor headless
/**
 * AnimSequence, skeleton and blendspace authoring, each read back: bone keys,
 * curves, notifies, root motion, sequence properties, sync markers, pose and
 * speed sampling, track scans, virtual bones and blend samples. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const ENGINE_SKELETON = `${ENGINE}/TutorialTPP_Skeleton`;
const DIR = `${TEST_PREFIX}/BCov/AnimB/Seq`;
const IDLE = `${DIR}/AS_BCovIdle`;
const WALK = `${DIR}/AS_BCovWalk`;
const PROPS = `${DIR}/AS_BCovProps`;
const MESH = `${DIR}/SKM_BCovTPP`;
const SKELETON = `${DIR}/SK_BCovTPP`;
const BLENDSPACE = `${DIR}/BS_BCov`;
// A walk on the engine skeleton the blendspace uses: WALK moves to the duplicate skeleton and PROPS is edited.
const BS_WALK = `${DIR}/AS_BCovBlendWalk`;
const ASSETS = [BLENDSPACE, IDLE, WALK, PROPS, MESH, SKELETON, BS_WALK];

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function readSequence(assetPath: string): Promise<Json> {
  return call("read_anim_sequence", { assetPath });
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: IDLE });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Walk_Fwd`, destinationPath: WALK });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: PROPS });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/TutorialTPP`, destinationPath: MESH });
  await call("duplicate_asset", { sourcePath: ENGINE_SKELETON, destinationPath: SKELETON });
  // Sync markers register on the skeleton, so the walk copy needs a writable one.
  await call("set_asset_property", { assetPath: WALK, propertyName: "Skeleton", value: `${SKELETON}.SK_BCovTPP` });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
});

describe("sequence authoring", () => {
  it("set_bone_keyframes writes a track that sample_pose evaluates back", async () => {
    const frames = Number((await readSequence(IDLE)).numFrames);
    expect(frames).toBeGreaterThan(0);
    const location: Vec = { x: 11, y: 22, z: 33 };
    const set = await call("set_bone_keyframes", {
      assetPath: IDLE,
      boneName: "spine_01",
      keyframes: Array.from({ length: frames }, () => ({ location })),
    });
    expect(set.keyframesSet).toBe(frames);

    const pose = await call("sample_pose", { assetPath: IDLE, boneNames: ["spine_01"], frames: [0], space: "local" });
    const bone = (pose.samples as Array<{ bones: Array<{ name: string; location: Vec }> }>)[0].bones[0];
    expect(bone.name).toBe("spine_01");
    expect(bone.location.x).toBeCloseTo(11, 2);
    expect(bone.location.y).toBeCloseTo(22, 2);
    expect(bone.location.z).toBeCloseTo(33, 2);
  }, 120_000);

  it("scan_animation_tracks lists the bone tracks of the scanned sequence", async () => {
    const scan = await call("scan_animation_tracks", { assetPaths: [IDLE], includeTrackNames: true });
    const rows = scan.sequences as Array<{ assetPath: string; numBoneTracks: number; boneTrackNames: string[] }>;
    expect(rows).toHaveLength(1);
    expect(rows[0].assetPath).toContain("AS_BCovIdle");
    expect(rows[0].numBoneTracks).toBeGreaterThan(0);
    expect(rows[0].boneTrackNames).toContain("spine_01");
  }, 120_000);

  it("set_anim_curve_keys adds a keyed curve and remove_anim_curve takes it away", async () => {
    const curveNames = async (): Promise<string[]> => (await readSequence(IDLE)).curveNames as string[];
    const first = await call("set_anim_curve_keys", {
      assetPath: IDLE,
      curveName: "BCovCurve",
      keys: [{ time: 0, value: 0 }, { time: 0.5, value: 1 }],
    });
    expect(first.curveCreated).toBe(true);
    expect(await curveNames()).toContain("BCovCurve");

    const second = await call("set_anim_curve_keys", {
      assetPath: IDLE,
      curveName: "BCovCurve",
      keys: [{ time: 0, value: 0 }, { time: 0.25, value: 0.5 }, { time: 0.5, value: 1 }],
    });
    expect(second.curveCreated).toBe(false);
    expect(second.previousKeyCount).toBe(2);

    const removed = await call("remove_anim_curve", { assetPath: IDLE, curveName: "BCovCurve" });
    expect(removed.removedKeyCount).toBe(3);
    expect(await curveNames()).not.toContain("BCovCurve");
  }, 120_000);

  it("remove_anim_notify and remove_anim_notify_state drop what was added", async () => {
    const notifies = async (): Promise<Array<{ name: string; notifyStateClass?: string }>> =>
      (await readSequence(IDLE)).notifies as Array<{ name: string; notifyStateClass?: string }>;
    await call("add_anim_notify", { assetPath: IDLE, notifyName: "BCovPing", triggerTime: 0.1 });
    await call("add_anim_notify_state", {
      assetPath: IDLE, notifyName: "BCovWindow", notifyStateClass: "TimedParticleEffect", triggerTime: 0.1, duration: 0.2,
    });
    let names = (await notifies()).map((n) => n.name);
    expect(names).toContain("BCovPing");
    expect((await notifies()).some((n) => n.notifyStateClass !== undefined)).toBe(true);

    const removedNotify = await call("remove_anim_notify", { assetPath: IDLE, notifyName: "BCovPing" });
    expect(removedNotify.alreadyDeleted).not.toBe(true);
    names = (await notifies()).map((n) => n.name);
    expect(names).not.toContain("BCovPing");

    const removedState = await call("remove_anim_notify_state", { assetPath: IDLE, notifyStateClass: "TimedParticleEffect" });
    expect(removedState.removedCount).toBe(1);
    expect((await notifies()).some((n) => n.notifyStateClass !== undefined)).toBe(false);
  }, 120_000);

  it("set_root_motion_settings writes the root motion flags", async () => {
    await call("set_root_motion_settings", {
      assetPath: IDLE, enableRootMotion: true, forceRootLock: true, rootMotionRootLock: "Zero",
    });
    const read = await readSequence(IDLE);
    expect(read.rootMotionEnabled).toBe(true);
    expect(read.forceRootLock).toBe(true);
    expect(read.rootMotionRootLock).toBe("Zero");
  }, 120_000);

  it("set_sequence_properties makes the sequence additive", async () => {
    expect((await readSequence(PROPS)).additiveType).toBe("None");
    await call("set_sequence_properties", {
      assetPaths: [PROPS],
      properties: { additiveAnimType: "LocalSpaceBase", refPoseType: "RefPose" },
    });
    const read = await readSequence(PROPS);
    expect(read.additiveType).toBe("LocalSpaceBase");
    expect(read.isAdditive).toBe(true);
  }, 120_000);

  it("set_sync_markers authors markers and drops one by name", async () => {
    const markers = async (): Promise<string> =>
      JSON.stringify(await call("get_asset_properties", { assetPath: WALK, propertyName: "AuthoredSyncMarkers" }));
    const written = await call("set_sync_markers", {
      assetPath: WALK, markers: [{ name: "BCovLeft", time: 0.1 }, { name: "BCovRight", time: 0.4 }],
    });
    expect(written.markerCount).toBe(2);
    let stored = await markers();
    expect(stored).toContain("BCovLeft");
    expect(stored).toContain("BCovRight");

    const dropped = await call("set_sync_markers", { assetPath: WALK, removeMarkers: ["BCovRight"], markerMode: "merge" });
    expect(dropped.markerCount).toBe(1);
    stored = await markers();
    expect(stored).toContain("BCovLeft");
    expect(stored).not.toContain("BCovRight");
  }, 120_000);

  it("measure_natural_speed reports a forward speed for the walk cycle", async () => {
    const measured = await call("measure_natural_speed", { assetPath: WALK, footBones: ["foot_l", "foot_r"] });
    expect(Number(measured.naturalSpeed)).toBeGreaterThan(0);
    expect(Number(measured.sampleCount)).toBeGreaterThan(1);
    expect((measured.footBones as Array<{ name: string }>).map((b) => b.name)).toEqual(["foot_l", "foot_r"]);
  }, 120_000);

  it("list_skeletal_meshes finds the mesh copied into the folder", async () => {
    const listed = await call("list_skeletal_meshes", { directory: DIR });
    const names = (listed.assets as Array<{ name: string; path: string }>).map((a) => a.name);
    expect(names).toContain("SKM_BCovTPP");
    expect(names.every((n) => n !== "TutorialTPP")).toBe(true);
  }, 120_000);

  it("remove_virtual_bone takes an added virtual bone off the skeleton", async () => {
    const added = await call("add_virtual_bone", {
      skeletonPath: SKELETON, sourceBone: "hand_l", targetBone: "hand_r", name: "BCovVB",
    });
    const vbName = String(added.virtualBoneName);
    const virtualBones = async (): Promise<string[]> =>
      ((await call("get_skeleton_info", { assetPath: SKELETON })).virtualBones as Array<{ name: string }>).map((v) => v.name);
    expect(await virtualBones()).toContain(vbName);

    const removed = await call("remove_virtual_bone", { skeletonPath: SKELETON, virtualBoneName: vbName });
    expect(removed.deleted).toBe(true);
    expect(await virtualBones()).not.toContain(vbName);
  }, 120_000);

  it("populate_blendspace sets the axis and samples, and set_blend_sample moves one", async () => {
    await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Walk_Fwd`, destinationPath: BS_WALK });
    await call("create_blendspace_1d", { name: "BS_BCov", skeletonPath: ENGINE_SKELETON, packagePath: DIR });
    const populated = await call("populate_blendspace", {
      assetPath: BLENDSPACE,
      axisHorizontal: "BCovSpeed",
      horizontalMin: 0,
      horizontalMax: 300,
      samples: [{ animationPath: IDLE, x: 0 }, { animationPath: BS_WALK, x: 300 }],
    });
    expect(populated.failed, JSON.stringify(populated.failed)).toBeUndefined();
    expect(populated.sampleCount).toBe(2);
    let read = await call("read_blendspace", { assetPath: BLENDSPACE });
    const axis = (read.axes as Array<{ displayName: string; max: number }>)[0];
    expect(axis.displayName).toBe("BCovSpeed");
    expect(axis.max).toBe(300);
    type Sample = { animation: string; sampleValue: { x: number } };
    const walkIndex = (read.samples as Sample[]).findIndex((s) => s.animation.includes("AS_BCovBlendWalk"));
    expect(walkIndex).toBeGreaterThanOrEqual(0);
    expect((read.samples as Sample[])[walkIndex].sampleValue.x).toBe(300);

    await call("set_blend_sample", { assetPath: BLENDSPACE, sampleIndex: walkIndex, x: 150 });
    read = await call("read_blendspace", { assetPath: BLENDSPACE });
    expect((read.samples as Sample[])[walkIndex].sampleValue.x).toBe(150);
    expect((read.samples as Sample[])[walkIndex].animation).toContain("AS_BCovBlendWalk");
  }, 120_000);
});
