// @live-editor headless
/**
 * Control Rig authoring on a copy of the engine tutorial mesh: blueprint
 * variables, and the LevelSequence edit session from begin through capture,
 * a keyed offset and the bake to a new AnimSequence. The bakes name the bound
 * mesh as the retarget source, and a Step clip round-trips key for key.
 * Refs #1223, #1642
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AnimA/ControlRig";
const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const MESH = `${DIR}/SKM_BCovA_Cr`;
const IDLE = `${DIR}/AS_BCovA_CrIdle`;
const CONTROL_RIG = `${DIR}/CR_BCovA`;
const SEQUENCE = `${DIR}/LS_BCovA_Edit`;
const BAKED = `${DIR}/AS_BCovA_Baked`;
const TAG = "BCovA_Edit";
const STEP = `${DIR}/AS_BCovA_CrStep`;
const STEP_SEQUENCE = `${DIR}/LS_BCovA_Step`;
const STEP_BAKED = `${DIR}/AS_BCovA_StepBaked`;
const STEP_TAG = "BCovA_Step";
const ASSETS = [STEP_BAKED, STEP_SEQUENCE, STEP, BAKED, SEQUENCE, CONTROL_RIG, IDLE, MESH];

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
type Control = { name: string; transformControl: boolean; animatable: boolean };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let control = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function controlTranslation(frame: number): Promise<Vec> {
  const read = await call("read_control_rig_edit", { sequencePath: SEQUENCE, bindingTag: TAG, controlNames: [control], frames: [frame] });
  const samples = (read.samples as Array<{ control: string; samples: Array<{ transform: { translation: Vec } }> }>)[0].samples;
  return samples[0].transform.translation;
}

async function pelvisAt(assetPath: string, frame: number): Promise<Vec> {
  const read = await call("read_bone_track", { assetPath, boneName: "pelvis", frames: [frame] });
  return (read.samples as Array<{ location: Vec }>)[0].location;
}

async function boneTrack(assetPath: string, boneName: string, frames: number[]): Promise<Vec[]> {
  const read = await call("read_bone_track", { assetPath, boneName, frames, space: "component" });
  return (read.samples as Array<{ location: Vec }>).map((s) => s.location);
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/TutorialTPP`, destinationPath: MESH });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: IDLE });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: STEP });
  await call("set_property", { objectPath: STEP, propertyName: "Interpolation", value: "Step" });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("control rig authoring", () => {
  it("list_control_rig_variables lists a variable added to the rig", async () => {
    await call("create_control_rig", { name: "CR_BCovA", packagePath: DIR, skeletalMeshPath: MESH });
    const before = await call("list_control_rig_variables", { assetPath: CONTROL_RIG });
    expect((before.variables as Array<{ name: string }>).map((v) => v.name)).not.toContain("BCovGain");

    await call("add_variable", { assetPath: CONTROL_RIG, name: "BCovGain", varType: "float" });
    const after = await call("list_control_rig_variables", { assetPath: CONTROL_RIG });
    expect((after.variables as Array<{ name: string }>).map((v) => v.name)).toContain("BCovGain");
    expect(after.variableCount).toBe(Number(before.variableCount) + 1);
  }, 180_000);

  it("begin_control_rig_edit opens an FK session over the source frames", async () => {
    const begun = await call("begin_control_rig_edit", {
      sequencePath: SEQUENCE, skeletalMeshPath: MESH, sourceAnimationPath: IDLE, bindingTag: TAG, endFrame: 10,
    });
    expect(begun.bindingTag).toBe(TAG);
    expect(begun.startFrame).toBe(0);
    expect(begun.endFrameExclusive).toBe(10);
    expect(begun.sourceInterpolation).toBe("linear");
    const controls = begun.controls as Control[];
    expect(begun.controlCount).toBe(controls.length);
    const pick = controls.find((c) => c.transformControl && c.animatable && c.name.toLowerCase().includes("pelvis"))
      ?? controls.find((c) => c.transformControl && c.animatable);
    expect(pick).toBeDefined();
    control = pick!.name;
    const asset = await call("read_asset", { assetPath: SEQUENCE });
    expect(asset.className).toBe("LevelSequence");
  }, 300_000);

  it("capture_control_rig_pose snapshots the named control's live transform", async () => {
    // Live capture reads only the focused Sequencer, so open the sequence first.
    const opened = await call("open_asset", { assetPath: SEQUENCE });
    expect(opened.assetClass).toBe("LevelSequence");
    let captured: Json;
    try {
      captured = await call("capture_control_rig_pose", { sequencePath: SEQUENCE, bindingTag: TAG, controlNames: [control] });
    } finally {
      await callBridge(bridge, "close_sequence", {});
    }
    expect(captured.capturedControlCount).toBe(1);
    const snapshot = captured.snapshot as Json;
    expect(snapshot.bindingTag).toBe(TAG);
    const entry = (snapshot.controls as Array<{ control: string; valueType: string; transform: { translation: Vec } }>)[0];
    expect(entry.control).toBe(control);
    expect(entry.valueType).toBe("transform");
    expect(Number.isFinite(entry.transform.translation.x)).toBe(true);
  }, 120_000);

  it("apply_control_rig_edits keys a translation offset that read_control_rig_edit reads back", async () => {
    const before = await controlTranslation(5);
    const untouched = await controlTranslation(0);
    await call("apply_control_rig_edits", {
      sequencePath: SEQUENCE, bindingTag: TAG,
      operations: [{ op: "offset", control, startFrame: 5, endFrame: 5, translationCm: { x: 10, y: 0, z: 0 } }],
    });
    const after = await controlTranslation(5);
    expect(Math.hypot(after.x - before.x, after.y - before.y, after.z - before.z)).toBeCloseTo(10, 1);
    const frame0 = await controlTranslation(0);
    expect(frame0.x).toBeCloseTo(untouched.x, 3);
  }, 180_000);

  it("bake_control_rig_edit writes the edited range to a new AnimSequence carrying the offset", async () => {
    const baked = await call("bake_control_rig_edit", { sequencePath: SEQUENCE, bindingTag: TAG, outputAssetPath: BAKED });
    expect(String(baked.outputAssetPath)).toContain("AS_BCovA_Baked");
    expect(Number(baked.sampledKeyCount)).toBeGreaterThan(1);

    const read = await call("read_anim_sequence", { assetPath: BAKED });
    expect(String(read.skeleton)).toContain("TutorialTPP_Skeleton");
    expect(read.numFrames).toBe(baked.sampledKeyCount);
    // The bake records the mesh's own bone lengths, so the mesh is the retarget source (#1642).
    expect(String(baked.retargetSourceAsset)).toContain("SKM_BCovA_Cr");
    expect(String(read.retargetSourceAsset)).toContain("SKM_BCovA_Cr");
    expect(read.interpolation).toBe("linear");
    if (control.toLowerCase().includes("pelvis")) {
      const source = await pelvisAt(IDLE, 5);
      const edited = await pelvisAt(BAKED, 5);
      const moved = Math.hypot(edited.x - source.x, edited.y - source.y, edited.z - source.z);
      expect(moved).toBeGreaterThan(5);
      expect(moved).toBeLessThan(15);
    }
  }, 300_000);

  it("a Step clip round-trips through an unedited session key for key (#1642)", async () => {
    const source = await call("read_anim_sequence", { assetPath: STEP });
    expect(source.interpolation).toBe("step");
    const begun = await call("begin_control_rig_edit", {
      sequencePath: STEP_SEQUENCE, skeletalMeshPath: MESH, sourceAnimationPath: STEP, bindingTag: STEP_TAG, endFrame: 10,
    });
    expect(begun.sourceInterpolation).toBe("step");
    const baked = await call("bake_control_rig_edit", { sequencePath: STEP_SEQUENCE, bindingTag: STEP_TAG, outputAssetPath: STEP_BAKED });
    expect(baked.interpolation).toBe("step");
    expect(String(baked.retargetSourceAsset)).toContain("SKM_BCovA_Cr");

    const frames = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
    for (const bone of ["pelvis", "hand_r", "foot_l"]) {
      const want = await boneTrack(STEP, bone, frames);
      const got = await boneTrack(STEP_BAKED, bone, frames);
      frames.forEach((frame, i) => {
        const off = Math.hypot(got[i].x - want[i].x, got[i].y - want[i].y, got[i].z - want[i].z);
        expect(off, `${bone} frame ${frame} is ${off} cm off the source key`).toBeLessThan(0.01);
      });
    }
  }, 300_000);
});
