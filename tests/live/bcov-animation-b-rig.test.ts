// @live-editor headless
/**
 * IK Rig, IK Retargeter and Control Rig reads and edits, each read back: rig
 * mesh, retargeter rig assignment, retarget pose reset, Control Rig hierarchy
 * and graph, and a Control Rig edit session's control values. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const CUBE = "/Engine/EngineMeshes/SkeletalCube";
const DIR = `${TEST_PREFIX}/BCov/AnimB/Rig`;
const CUBE_COPY = `${DIR}/SKM_BCovCube`;
const RIG_SRC = `${DIR}/IK_BCovSrc`;
const RIG_TGT = `${DIR}/IK_BCovTgt`;
const RIG_ALT = `${DIR}/IK_BCovAlt`;
const RETARGETER = `${DIR}/RTG_BCov`;
const CONTROL_RIG = `${DIR}/CR_BCov`;
// A fresh rig has an empty graph and no action authors RigVM nodes, so the
// graph read uses a copy of a shipped module rig.
const GRAPH_RIG = `${DIR}/CR_BCovChain`;
const IDLE = `${DIR}/AS_BCovIdle`;
const MESH = `${DIR}/SKM_BCovTPP`;
const EDIT_SEQUENCE = `${DIR}/LS_BCovEdit`;
const BINDING = "BCovEdit";
const ASSETS = [EDIT_SEQUENCE, CONTROL_RIG, GRAPH_RIG, RETARGETER, RIG_SRC, RIG_TGT, RIG_ALT, CUBE_COPY, IDLE, MESH];

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

async function currentSourcePose(): Promise<Pose> {
  const read = await call("read_ik_retargeter", { assetPath: RETARGETER });
  const pose = (read.sourcePoses as Pose[]).find((p) => p.name === read.currentSourcePose);
  expect(pose, JSON.stringify(read.sourcePoses)).toBeDefined();
  return pose!;
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: CUBE, destinationPath: CUBE_COPY });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: IDLE });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/TutorialTPP`, destinationPath: MESH });
  for (const name of ["IK_BCovSrc", "IK_BCovTgt", "IK_BCovAlt"]) {
    await call("create_ik_rig", {
      name, packagePath: DIR, skeletalMeshPath: CUBE,
      chains: [{ name: "Spine", startBone: "Bone01", endBone: "Bone02" }],
    });
  }
  await call("create_ik_retargeter", { name: "RTG_BCov", packagePath: DIR, sourceRig: RIG_SRC, targetRig: RIG_TGT });
  await call("create_control_rig", { skeletalMeshPath: CUBE, name: "CR_BCov", packagePath: DIR });
  await call("duplicate_asset", { sourcePath: "/ControlRigModules/Modules/Chain", destinationPath: GRAPH_RIG });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
});

describe("IK rig and retargeter", () => {
  it("read_ik_rig reports the chain and the source mesh", async () => {
    const read = await call("read_ik_rig", { assetPath: RIG_SRC });
    const chain = (read.retargetChains as Array<{ name: string; startBone: string; endBone: string }>).find((c) => c.name === "Spine");
    expect(chain?.startBone).toBe("Bone01");
    expect(chain?.endBone).toBe("Bone02");
    expect((read.bones as Array<{ name: string }>).map((b) => b.name)).toContain("Bone01");
    expect(String(read.previewMesh)).toContain("SkeletalCube");
  }, 120_000);

  it("set_ik_rig_mesh points the rig at another mesh", async () => {
    const set = await call("set_ik_rig_mesh", { rigPath: RIG_ALT, meshPath: CUBE_COPY });
    expect(set.applied).toBe(true);
    expect(String(set.previousSkeletalMesh)).toContain("SkeletalCube");
    const read = await call("read_ik_rig", { assetPath: RIG_ALT });
    expect(String(read.previewMesh)).toContain("SKM_BCovCube");
  }, 120_000);

  it("reset_retarget_pose clears the offsets on the current pose", async () => {
    const pose = await currentSourcePose();
    const half = Math.PI / 8;
    await call("configure_ik_retargeter", {
      retargeterPath: RETARGETER,
      ensureDefaultOps: false,
      pose: {
        side: "source",
        name: pose.name,
        rotationOffsets: [{ bone: "Bone02", rotationQuaternion: { x: 0, y: 0, z: Math.sin(half), w: Math.cos(half) } }],
      },
    });
    const offset = (await currentSourcePose()).rotationOffsets.find((o) => o.bone === "Bone02");
    expect(offset?.rotationQuaternion.z).toBeCloseTo(Math.sin(half), 3);

    const reset = await call("reset_retarget_pose", { retargeterPath: RETARGETER, side: "source" });
    expect(reset.pose).toBe(pose.name);
    const after = (await currentSourcePose()).rotationOffsets.find((o) => o.bone === "Bone02");
    expect(after === undefined || Math.abs(after.rotationQuaternion.w) > 0.9999, JSON.stringify(after)).toBe(true);
  }, 120_000);

  it("set_ik_retargeter_rig assigns a different target rig", async () => {
    const set = await call("set_ik_retargeter_rig", { retargeterPath: RETARGETER, rigPath: RIG_ALT, side: "target" });
    expect(String(set.previousIkRig)).toContain("IK_BCovTgt");
    const read = await call("read_ik_retargeter", { assetPath: RETARGETER });
    expect(String(read.targetRig)).toContain("IK_BCovAlt");
    expect(String(read.sourceRig)).toContain("IK_BCovSrc");
  }, 120_000);
});

describe("control rig", () => {
  it("read_control_rig_hierarchy lists the bones imported from the mesh", async () => {
    const read = await call("read_control_rig_hierarchy", { assetPath: CONTROL_RIG });
    const elements = read.elements as Array<{ name: string; type: string }>;
    expect(read.elementCount).toBe(elements.length);
    const bone = elements.find((e) => e.name === "Bone01");
    expect(bone, JSON.stringify(elements.slice(0, 10))).toBeDefined();
    expect(bone!.type).toMatch(/bone/i);
  }, 120_000);

  it("read_control_rig_graph reports the rig's models and honours includePins", async () => {
    const full = await call("read_control_rig_graph", { assetPath: GRAPH_RIG });
    expect(Number(full.modelCount)).toBeGreaterThan(0);
    expect(Number(full.totalNodes)).toBeGreaterThan(0);
    type Model = { nodes: Array<{ class: string; pins?: unknown[] }> };
    const withPins = (full.models as Model[]).flatMap((m) => m.nodes);
    expect(withPins.some((n) => Array.isArray(n.pins))).toBe(true);

    const bare = await call("read_control_rig_graph", { assetPath: GRAPH_RIG, includePins: false });
    expect(bare.totalNodes).toBe(full.totalNodes);
    expect((bare.models as Model[]).flatMap((m) => m.nodes).every((n) => n.pins === undefined)).toBe(true);
  }, 120_000);

  it("read_control_rig_edit reads a control's value and sees an applied offset", async () => {
    const session = await call("begin_control_rig_edit", {
      sequencePath: EDIT_SEQUENCE, skeletalMeshPath: MESH, sourceAnimationPath: IDLE,
      rigMode: "fk", startFrame: 0, endFrame: 10, bindingTag: BINDING,
    });
    const controls = session.controls as Array<{ name: string; transformControl: boolean; animatable: boolean }>;
    const control = controls.find((c) => c.transformControl && c.animatable)?.name;
    expect(control, JSON.stringify(controls.slice(0, 10))).toBeTruthy();

    type Read = { samples: Array<{ control: string; samples: Array<{ frame: number; transform: { translation: { z: number } } }> }> };
    const zAt0 = async (): Promise<number> => {
      const read = (await call("read_control_rig_edit", {
        sequencePath: EDIT_SEQUENCE, bindingTag: BINDING, controlNames: [control], frames: [0],
      })) as unknown as Read;
      expect(read.samples[0].control).toBe(control);
      expect(read.samples[0].samples[0].frame).toBe(0);
      return read.samples[0].samples[0].transform.translation.z;
    };
    const before = await zAt0();
    await call("apply_control_rig_edits", {
      sequencePath: EDIT_SEQUENCE, bindingTag: BINDING,
      operations: [{ op: "offset", control, startFrame: 0, endFrame: 0, translationCm: { x: 0, y: 0, z: 10 } }],
    });
    expect((await zAt0()) - before).toBeCloseTo(10, 1);
  }, 180_000);
});
