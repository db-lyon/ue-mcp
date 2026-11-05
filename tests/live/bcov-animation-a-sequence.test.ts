// @live-editor headless
/**
 * AnimSequence authoring on sequences created against the engine tutorial
 * skeleton: creation, curves, notifies, keyframe and root-motion bakes, a
 * modifier, the analysis pass, reference-pose reads and an empty composite.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AnimA/Seq";
const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const SKELETON = `${ENGINE}/TutorialTPP_Skeleton`;
const ENGINE_MESH = `${ENGINE}/TutorialTPP`;
const SEQ = `${DIR}/AS_BCovA_Seq`;
const ROOT_SEQ = `${DIR}/AS_BCovA_Root`;
const COMPOSITE = `${DIR}/AC_BCovA`;
const ASSETS = [SEQ, ROOT_SEQ, COMPOSITE];

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
type Notify = { name: string; triggerTime: number; endTime?: number; notifyStateClass?: string };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function locationAt(assetPath: string, boneName: string, frame: number): Promise<Vec> {
  const read = await call("read_bone_track", { assetPath, boneName, frames: [frame] });
  return (read.samples as Array<{ location: Vec }>)[0].location;
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("animation sequence authoring", () => {
  it("create_sequence makes a sequence with the requested frames, and add_curve plus both notify kinds land on it", async () => {
    const created = await call("create_sequence", { name: "AS_BCovA_Seq", skeletonPath: SKELETON, packagePath: DIR, numFrames: 10, frameRate: 30 });
    expect(String(created.path)).toContain("AS_BCovA_Seq");

    const fresh = await call("read_anim_sequence", { assetPath: SEQ });
    expect(fresh.numFrames).toBe(11);
    expect(String(fresh.skeleton)).toContain("TutorialTPP_Skeleton");
    expect(fresh.curveNames).toEqual([]);

    await call("add_curve", { assetPath: SEQ, curveName: "BCovCurve" });
    await call("add_anim_notify", { assetPath: SEQ, notifyName: "BCovStep", triggerTime: 0.1 });
    await call("add_anim_notify_state", {
      assetPath: SEQ, notifyName: "BCovWindow", notifyStateClass: "TimedParticleEffect", triggerTime: 0.05, duration: 0.2,
    });

    const read = await call("read_anim_sequence", { assetPath: SEQ });
    expect(read.curveNames).toContain("BCovCurve");
    const notifies = read.notifies as Notify[];
    const step = notifies.find((n) => n.name === "BCovStep");
    expect(step?.triggerTime).toBeCloseTo(0.1, 2);
    const window = notifies.find((n) => n.name === "BCovWindow");
    expect(window?.notifyStateClass).toBe("AnimNotifyState_TimedParticleEffect");
    expect(window?.triggerTime).toBeCloseTo(0.05, 2);
    expect(window?.endTime).toBeCloseTo(0.25, 2);
  }, 120_000);

  it("compare_curves_to_morph_targets reports the added curve as one with no morph target", async () => {
    const compared = await call("compare_curves_to_morph_targets", { animPath: SEQ, skeletalMeshPath: ENGINE_MESH });
    expect(compared.curveKind).toBe("AnimSequence");
    expect(compared.curves).toContain("BCovCurve");
    expect(compared.curvesWithoutMorph).toContain("BCovCurve");
    expect(compared.matched).not.toContain("BCovCurve");
  }, 120_000);

  it("create_anim_composite makes an AnimComposite on the skeleton", async () => {
    const created = await call("create_anim_composite", { name: "AC_BCovA", skeletonPath: SKELETON, packagePath: DIR });
    expect(String(created.path)).toContain("AC_BCovA");
    const asset = await call("read_asset", { assetPath: COMPOSITE });
    expect(asset.className).toBe("AnimComposite");
    const skeleton = await call("read_asset_properties", {
      assetPath: COMPOSITE, propertyName: "Skeleton", includeValues: true, valueFormat: "json",
    });
    expect(JSON.stringify(skeleton.value)).toContain("TutorialTPP_Skeleton");
  }, 120_000);

  it("get_bone_transforms reads the reference pose, and bake_keyframes_batch writes a pelvis track the bone track reads back", async () => {
    const info = await call("get_skeleton_info", { assetPath: SKELETON });
    const ref = await call("get_bone_transforms", { skeletonPath: SKELETON, boneNames: ["Root", "pelvis"] });
    const bones = ref.bones as Array<{ name: string; index: number; parentIndex: number; location: Vec }>;
    expect(bones.map((b) => b.name)).toEqual(["Root", "pelvis"]);
    expect(bones[1].parentIndex).toBe(bones[0].index);
    const pelvis = bones[1].location;
    expect(Math.hypot(pelvis.x, pelvis.y, pelvis.z)).toBeGreaterThan(50);
    const all = await call("get_bone_transforms", { skeletonPath: SKELETON });
    expect(all.boneCount).toBe(info.boneCount);

    await call("create_sequence", { name: "AS_BCovA_Root", skeletonPath: SKELETON, packagePath: DIR, numFrames: 10, frameRate: 30 });
    const keyframes = Array.from({ length: 11 }, (_, i) => ({ location: { x: pelvis.x + 10 * i, y: pelvis.y, z: pelvis.z } }));
    const baked = await call("bake_keyframes_batch", { assetPath: ROOT_SEQ, tracks: [{ bone: "pelvis", keyframes }] });
    expect(baked.bonesBaked).toBe(1);

    const start = await locationAt(ROOT_SEQ, "pelvis", 0);
    const end = await locationAt(ROOT_SEQ, "pelvis", 10);
    expect(start.x).toBeCloseTo(pelvis.x, 2);
    expect(end.x - start.x).toBeCloseTo(100, 2);
    expect(end.z).toBeCloseTo(pelvis.z, 2);
  }, 180_000);

  it("bake_root_motion_from_bone moves the pelvis travel onto the root and analyze_animation measures it", async () => {
    const pelvisStart = await locationAt(ROOT_SEQ, "pelvis", 0);
    const baked = await call("bake_root_motion_from_bone", { assetPath: ROOT_SEQ, sourceBone: "pelvis", axes: ["x"] });
    expect((baked.totalDelta as Vec).x).toBeCloseTo(100, 2);

    expect((await locationAt(ROOT_SEQ, "Root", 10)).x).toBeCloseTo(100, 2);
    expect((await locationAt(ROOT_SEQ, "pelvis", 10)).x).toBeCloseTo(pelvisStart.x, 2);
    const read = await call("read_anim_sequence", { assetPath: ROOT_SEQ });
    expect(read.rootMotionEnabled).toBe(true);

    await call("add_anim_notify", { assetPath: ROOT_SEQ, notifyName: "BCovAnalyzed", triggerTime: 0.2 });
    const analysis = await call("analyze_animation", { assetPath: ROOT_SEQ, boneNames: ["Root", "pelvis"], frames: [0, 10] });
    expect(analysis.sampleCount).toBe(2);
    expect((analysis.samples as unknown[]).length).toBe(2);
    const summary = analysis.summary as Json;
    expect(summary.hasRootMotion).toBe(true);
    expect(Number(summary.rootDisplacementCm)).toBeCloseTo(100, 1);
    expect((analysis.notifies as Array<{ name: string }>).map((n) => n.name)).toContain("BCovAnalyzed");
  }, 180_000);

  it("apply_animation_modifier registers the modifier and bakes its curve, and list_anim_modifiers lists it", async () => {
    const before = await call("list_anim_modifiers", { assetPath: ROOT_SEQ });
    expect(before.count).toBe(0);

    const applied = await call("apply_animation_modifier", {
      assetPath: ROOT_SEQ, modifierClass: "DistanceCurveModifier", props: { CurveName: "BCovDistance" },
    });
    expect(applied.modifierClass).toBe("DistanceCurveModifier");
    expect(applied.appliedProps).toContain("CurveName");

    const listed = await call("list_anim_modifiers", { assetPath: ROOT_SEQ });
    expect((listed.modifiers as Array<{ class: string }>).map((m) => m.class)).toContain("DistanceCurveModifier");
    const read = await call("read_anim_sequence", { assetPath: ROOT_SEQ });
    expect(read.curveNames).toContain("BCovDistance");
  }, 180_000);
});
