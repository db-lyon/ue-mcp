// @live-editor headless
/**
 * Live skeletal reads on a tutorial mesh actor the test spawns at a known
 * location: the bone list, one bone's transform, and the evaluated pose.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const MESH = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP";
const LABEL = "BCov_AnimA_Live";
const AT = { x: 1200, y: 6400, z: 300 };

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

function expectNear(actual: Vec, expected: Vec, tolerance = 0.5): void {
  expect(Math.hypot(actual.x - expected.x, actual.y - expected.y, actual.z - expected.z), JSON.stringify(actual)).toBeLessThan(tolerance);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
  await call("spawn_skeletal_mesh_actor", { skeletalMesh: MESH, label: LABEL, location: AT });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
    disconnectBridge();
  }
}, 120_000);

describe("live skeletal reads", () => {
  it("list_bones lists the placed mesh's whole reference skeleton with parents", async () => {
    const info = await call("get_skeleton_info", { assetPath: MESH });
    const listed = await call("list_bones", { actorLabel: LABEL, world: "editor", limit: 5000 });
    expect(listed.boneCount).toBe(info.boneCount);
    const bones = listed.bones as Array<{ name: string; parentName?: string }>;
    expect(bones).toHaveLength(Number(info.boneCount));
    expect(bones.find((b) => b.name === "pelvis")?.parentName).toBe("Root");
  }, 120_000);

  it("get_bone_transform puts the root bone at the actor's location in world space", async () => {
    const root = await call("get_bone_transform", { actorLabel: LABEL, boneName: "Root", world: "editor", space: "world" });
    expect(root.targetType).toBe("bone");
    expectNear(root.location as Vec, AT);
    const pelvis = await call("get_bone_transform", { actorLabel: LABEL, boneName: "pelvis", world: "editor", space: "world" });
    expect((pelvis.location as Vec).z).toBeGreaterThan(AT.z + 50);
  }, 120_000);

  it("get_live_bone_transforms reads the evaluated pose, world space offset by the actor and component space not", async () => {
    const world = await call("get_live_bone_transforms", { actorLabel: LABEL, boneNames: ["Root", "pelvis"], space: "world", world: "editor" });
    const worldBones = world.bones as Array<{ name: string; location: Vec }>;
    expect(worldBones.map((b) => b.name)).toEqual(["Root", "pelvis"]);
    expectNear(worldBones[0].location, AT);
    expectNear((world.componentTransform as { location: Vec }).location, AT);

    const component = await call("get_live_bone_transforms", { actorLabel: LABEL, boneNames: ["pelvis"], space: "component", world: "editor" });
    const pelvisComponent = (component.bones as Array<{ location: Vec }>)[0].location;
    const pelvisWorld = worldBones[1].location;
    expectNear({ x: pelvisWorld.x - AT.x, y: pelvisWorld.y - AT.y, z: pelvisWorld.z - AT.z }, pelvisComponent, 1);
  }, 120_000);
});
