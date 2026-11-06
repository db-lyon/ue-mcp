// @live-editor headless
/**
 * get_skinned_vertices on a placed, animated mesh actor with a second skeletal
 * mesh component that follows the first through its leader pose: the follower
 * reads as drawn, vertices on one bone sit where that bone's live transform
 * carries them from the bind pose, world space is component space carried by
 * the component transform, an unanimated copy matches the source geometry, and
 * the selectors, pages and dump hold.
 * Refs #1452
 */
import { existsSync, readFileSync, rmSync } from "node:fs";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const MESH = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP";
const WALK = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Walk_Fwd";
const LABEL = "BCov_SkinnedVertices";
const REF = "BCov_SkinnedVertices_Ref";
const FOLLOWER = "BCovFollower";
const AT = { x: 1600, y: 7200, z: 300 };
const YAW = 90;

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
type Box = { min: Vec; max: Vec; center: Vec; extent: Vec };
type Influence = { boneName: string; weight: number };
type Vertex = { vertexIndex: number; position?: Vec; selectedWeight?: number; influences?: Influence[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let leaderName = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

function distance(a: Vec, b: Vec): number {
  return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

/** Yaw by YAW degrees about Z, then translate by AT: the actor's transform. */
function toWorld(p: Vec): Vec {
  const r = (YAW * Math.PI) / 180;
  return { x: AT.x + p.x * Math.cos(r) - p.y * Math.sin(r), y: AT.y + p.x * Math.sin(r) + p.y * Math.cos(r), z: AT.z + p.z };
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
  await callBridge(bridge, "delete_actor", { actorLabel: REF });
  await call("spawn_skeletal_mesh_actor", { skeletalMesh: MESH, label: REF, location: { x: AT.x, y: AT.y + 600, z: AT.z } });
  await call("spawn_skeletal_mesh_actor", {
    skeletalMesh: MESH, label: LABEL, location: AT, rotation: { pitch: 0, yaw: YAW, roll: 0 }, animSequence: WALK, loop: true,
  });
  await call("preview_animation", { actorLabel: LABEL, enabled: true });
  const first = await call("get_skinned_vertices", { actorLabel: LABEL, limit: 1 });
  leaderName = String(first.componentName);
  await call("add_component_to_actor", { actorLabel: LABEL, componentClass: "SkeletalMeshComponent", componentName: FOLLOWER });
  await call("set_component_skeletal_mesh", { actorLabel: LABEL, componentName: FOLLOWER, skeletalMesh: MESH });
  await call("rebind_leader_pose", { actorLabel: LABEL, bodyComponent: leaderName });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
    await callBridge(bridge, "delete_actor", { actorLabel: REF });
    disconnectBridge();
  }
}, 120_000);

describe("get_skinned_vertices", () => {
  it("reads a leader-pose follower as drawn, matching its leader vertex for vertex", async () => {
    const leader = await call("get_skinned_vertices", { actorLabel: LABEL, componentName: leaderName, include: ["positions"], limit: 20000 });
    const follower = await call("get_skinned_vertices", { actorLabel: LABEL, componentName: FOLLOWER, include: ["positions"], limit: 20000 });
    expect(leader.leaderPoseComponent).toBeUndefined();
    expect((follower.leaderPoseComponent as Json).componentName).toBe(leaderName);
    expect(follower.skinnedAsset).toBe(leader.skinnedAsset);
    expect(follower.lodIndex).toBe(leader.lodIndex);
    expect(follower.vertexCount).toBe(leader.vertexCount);
    expect(Number(leader.vertexCount)).toBeGreaterThan(100);
    expect(leader.hasMore).toBe(false);
    const a = leader.vertices as Vertex[];
    const b = follower.vertices as Vertex[];
    expect(b).toHaveLength(a.length);
    let worst = 0;
    for (let i = 0; i < a.length; i += 1) {
      expect(b[i].vertexIndex).toBe(a[i].vertexIndex);
      worst = Math.max(worst, distance(a[i].position!, b[i].position!));
    }
    expect(worst).toBeLessThan(0.01);
  }, 120_000);

  it("carries each vertex on one bone from the bind pose by that bone's live transform", async () => {
    // A vertex skinned only to the head moves rigidly with it, so its distance
    // to the head and to every other such vertex is the bind-pose distance.
    // Holds for whatever pose the editor evaluated, animated or not.
    const posed = await call("get_skinned_vertices", { actorLabel: LABEL, boneNames: ["head"], minWeight: 0.999, limit: 20000 });
    const bind = await call("get_skinned_vertices", { actorLabel: REF, boneNames: ["head"], minWeight: 0.999, limit: 20000 });
    const posedHead = await call("get_live_bone_transforms", { actorLabel: LABEL, boneNames: ["head"], space: "component", world: "editor" });
    const bindHead = await call("get_live_bone_transforms", { actorLabel: REF, boneNames: ["head"], space: "component", world: "editor" });
    const p = (posed.vertices as Vertex[]).slice(0, 25);
    const b = (bind.vertices as Vertex[]).slice(0, 25);
    expect(p.length).toBeGreaterThan(3);
    expect(p.map((v) => v.vertexIndex)).toEqual(b.map((v) => v.vertexIndex));
    const ph = (posedHead.bones as Array<{ location: Vec }>)[0].location;
    const bh = (bindHead.bones as Array<{ location: Vec }>)[0].location;
    for (let i = 0; i < p.length; i += 1) {
      expect(Math.abs(distance(p[i].position!, ph) - distance(b[i].position!, bh))).toBeLessThan(0.05);
      for (let j = i + 1; j < p.length; j += 1) {
        expect(Math.abs(distance(p[i].position!, p[j].position!) - distance(b[i].position!, b[j].position!))).toBeLessThan(0.05);
      }
    }
  }, 120_000);

  it("puts world space at component space carried by the component transform", async () => {
    const component = await call("get_skinned_vertices", { actorLabel: LABEL, include: ["positions"], limit: 50 });
    const world = await call("get_skinned_vertices", { actorLabel: LABEL, include: ["positions"], limit: 50, space: "world" });
    expect(component.space).toBe("component");
    expect(world.space).toBe("world");
    expect(distance((world.componentTransform as { location: Vec }).location, AT)).toBeLessThan(0.5);
    const c = component.vertices as Vertex[];
    const w = world.vertices as Vertex[];
    expect(w.map((v) => v.vertexIndex)).toEqual(c.map((v) => v.vertexIndex));
    for (let i = 0; i < c.length; i += 1) expect(distance(w[i].position!, toWorld(c[i].position!))).toBeLessThan(0.05);
  }, 120_000);

  it("matches the source geometry at the reference pose", async () => {
    const posed = await call("get_skinned_vertices", { actorLabel: REF, lodIndex: 0, limit: 1 });
    const source = await call("read_skeletal_mesh_vertices", { assetPath: MESH, limit: 1 });
    const p = posed.selectedBounds as Box;
    const s = source.selectedBounds as Box;
    expect(distance(p.min, s.min)).toBeLessThan(0.5);
    expect(distance(p.max, s.max)).toBeLessThan(0.5);
  }, 120_000);

  it("selects by bone, box and nearPoint, and refuses a bone the mesh lacks", async () => {
    const byBone = await call("get_skinned_vertices", { actorLabel: LABEL, boneNames: ["head"], minWeight: 0.5, limit: 20000 });
    const headVertices = byBone.vertices as Vertex[];
    expect(byBone.selectedBones).toEqual(["head"]);
    expect(headVertices.length).toBeGreaterThan(0);
    for (const v of headVertices) {
      expect(v.selectedWeight!).toBeGreaterThanOrEqual(0.5);
      expect(v.influences!.some((i) => i.boneName === "head")).toBe(true);
    }

    const head = byBone.selectedBounds as Box;
    const inBox = await call("get_skinned_vertices", { actorLabel: LABEL, box: { min: head.min, max: head.max }, include: ["positions"], limit: 20000 });
    const boxIds = new Set((inBox.vertices as Vertex[]).map((v) => v.vertexIndex));
    for (const v of headVertices) expect(boxIds.has(v.vertexIndex)).toBe(true);

    const center = toWorld(head.center);
    const near = await call("get_skinned_vertices", { actorLabel: LABEL, space: "world", nearPoint: center, radius: 5, include: ["positions"], limit: 20000 });
    for (const v of near.vertices as Vertex[]) expect(distance(v.position!, center)).toBeLessThanOrEqual(5.001);
    const wide = await call("get_skinned_vertices", { actorLabel: LABEL, space: "world", nearPoint: center, radius: 100000, limit: 1 });
    expect(wide.selectedVertexCount).toBe(wide.vertexCount);

    const refused = await callBridge(bridge, "get_skinned_vertices", { actorLabel: LABEL, boneNames: ["NoSuchBone_1452"] });
    expect(succeeded(refused)).toBe(false);
    expect(JSON.stringify(refused.result ?? refused.error)).toContain("is not a bone of");
  }, 120_000);

  it("pages with startIndex, returns each triangle once and dumps the whole selection", async () => {
    const whole = await call("get_skinned_vertices", { actorLabel: LABEL, boneNames: ["head"], include: ["triangles"], limit: 20000 });
    const total = Number(whole.selectedVertexCount);
    const triangleCount = Number(whole.selectedTriangleCount);
    expect(triangleCount).toBeGreaterThan(0);
    expect((whole.triangles as unknown[]).length).toBe(triangleCount);

    const limit = Math.max(1, Math.ceil(total / 3));
    const seenTriangles = new Set<number>();
    const seenVertices: number[] = [];
    for (let startIndex = 0; ; ) {
      const page = await call("get_skinned_vertices", { actorLabel: LABEL, boneNames: ["head"], include: ["triangles"], startIndex, limit });
      expect(page.startIndex).toBe(startIndex);
      seenVertices.push(...(page.vertices as Vertex[]).map((v) => v.vertexIndex));
      for (const t of page.triangles as Array<{ triangleIndex: number }>) {
        expect(seenTriangles.has(t.triangleIndex)).toBe(false);
        seenTriangles.add(t.triangleIndex);
      }
      if (!page.hasMore) break;
      startIndex = Number(page.nextStartIndex);
    }
    expect(seenVertices).toHaveLength(total);
    expect(seenTriangles.size).toBe(triangleCount);

    const dumped = await call("get_skinned_vertices", { actorLabel: LABEL, boneNames: ["head"], dumpToFile: true, outputPath: "UE_MCP/Tests/skinned_vertices_1452.json" });
    const file = String(dumped.outputPath);
    try {
      expect(existsSync(file)).toBe(true);
      const written = JSON.parse(readFileSync(file, "utf8")) as Json;
      expect((written.vertices as unknown[]).length).toBe(total);
      expect(dumped.vertices).toBeUndefined();
    } finally {
      rmSync(file, { force: true });
    }
  }, 120_000);
});
