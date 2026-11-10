// @live-editor headless
/**
 * Actor replacement and brush rebuilds through UEditorActorSubsystem (#1828): replace_actors_with_class and
 * replace_actors_with_asset in the open map, read back through the actor queries, and rebuild_brush on a
 * blocking volume, read back through its bounds. Every actor is labelled BCov_Replace* or carries the
 * replaced actors' place, and is deleted afterwards.
 *
 * ReplaceActorsWithClass and ReplaceActorsWithAsset arrived in UE 5.9; older editors reach the same actor
 * factory path, so the replace cases run on every engine. RebuildBrush has no older equivalent and is
 * asserted to refuse with unsupported_engine_version before 5.9.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const PREFIX = "BCov_Replace";
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const SPHERE = "/Engine/BasicShapes/Sphere.Sphere";

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let is59 = false;
const created: string[] = [];

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function refused(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method} should refuse: ${JSON.stringify(r.result)}`).toBe(false);
  return body(r);
}

async function cube(label: string, location: Vec): Promise<void> {
  await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label, staticMesh: CUBE, location });
}

const exists = async (label: string) => succeeded(await callBridge(bridge, "get_actor_details", { actorLabel: label }));

beforeAll(async () => {
  bridge = await getBridge();
  const caps = body(await callBridge(bridge, "get_bridge_capabilities", {}));
  const [major, minor] = String(caps.engineVersion).split(".").map(Number);
  is59 = major > 5 || (major === 5 && minor >= 9);
  await callBridge(bridge, "delete_actors", { labelPrefix: PREFIX });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "delete_actors", { labelPrefix: PREFIX });
  for (const path of created) await callBridge(bridge, "delete_actor", { actorPath: path });
  disconnectBridge();
}, 120_000);

describe("replacing actors", () => {
  const A = `${PREFIX}_A`;
  const B = `${PREFIX}_B`;
  let lights: Json[] = [];

  it("replace_actors_with_class replaces two cubes with point lights at their locations", async () => {
    await cube(A, { x: 0, y: 61000, z: 100 });
    await cube(B, { x: 400, y: 61000, z: 100 });
    const out = await call("replace_actors_with_class", { actorLabels: [A, B], className: "PointLight" });
    expect(out.requested).toBe(2);
    expect(out.replaced).toBe(2);
    lights = out.newActors as Json[];
    for (const light of lights) {
      expect(String(light.class)).toBe("/Script/Engine.PointLight");
      created.push(String(light.actorPath));
    }
    expect(out.rollbackPossible).toBe(false);
    expect(await exists(A)).toBe(false);
    expect(await exists(B)).toBe(false);
    const xs: number[] = [];
    for (const light of lights) xs.push(((await call("get_actor_details", { actorPath: light.actorPath })).location as Vec).x);
    expect(xs.sort((p, q) => p - q).map((x) => Math.round(x))).toEqual([0, 400]);
  }, 60_000);

  it("replace_actors_with_asset replaces the lights with sphere mesh actors", async () => {
    const out = await call("replace_actors_with_asset", { actorLabels: lights.map((l) => String(l.actorPath)), assetPath: SPHERE });
    expect(out.replaced).toBe(2);
    for (const actor of out.newActors as Json[]) {
      expect(String(actor.class)).toBe("/Script/Engine.StaticMeshActor");
      created.push(String(actor.actorPath));
    }
    for (const light of lights) {
      expect(succeeded(await callBridge(bridge, "get_actor_details", { actorPath: light.actorPath }))).toBe(false);
    }
  }, 60_000);

  it("replace refuses an actor that is not there, a class that is not one and an asset that is not there", async () => {
    const missing = await refused("replace_actors_with_class", { actorLabels: [`${PREFIX}_Nobody`], className: "PointLight" });
    expect(String(missing.error)).toContain(`${PREFIX}_Nobody`);
    await cube(`${PREFIX}_C`, { x: 800, y: 61000, z: 100 });
    const noClass = await refused("replace_actors_with_class", { actorLabels: [`${PREFIX}_C`], className: "BCovNoSuchActorClass" });
    expect(String(noClass.error)).toContain("BCovNoSuchActorClass");
    const noAsset = await refused("replace_actors_with_asset", { actorLabels: [`${PREFIX}_C`], assetPath: "/Game/MCPTest/BCov/NoSuchMesh" });
    expect(String(noAsset.error)).toContain("NoSuchMesh");
    expect(await exists(`${PREFIX}_C`)).toBe(true);
  }, 60_000);
});

describe("rebuilding a brush", () => {
  const VOLUME = `${PREFIX}_Volume`;

  it("rebuild_brush rebuilds a blocking volume from a cube builder without moving it", async () => {
    if (!is59) {
      const out = await refused("rebuild_brush", { actorLabel: VOLUME, builderClass: "CubeBuilder" });
      expect(out.errorCode).toBe("unsupported_engine_version");
      expect(String(out.error)).toContain("UEditorActorSubsystem::RebuildBrush requires Unreal Engine 5.9 or newer");
      return;
    }
    await call("spawn_volume", { volumeType: "BlockingVolume", label: VOLUME, location: { x: 0, y: 62000, z: 200 }, extent: { x: 100, y: 100, z: 100 } });
    const before = await call("get_actor_bounds", { actorLabel: VOLUME });
    const out = await call("rebuild_brush", { actorLabel: VOLUME, builderClass: "CubeBuilder", builderProperties: { X: 600, Y: 400, Z: 200 } });
    expect(String(out.builderClass)).toContain("CubeBuilder");
    expect(Number((out.builderProperties as Json).X)).toBeCloseTo(600, 0);
    const after = await call("get_actor_bounds", { actorLabel: VOLUME });
    expect((after.extent as Vec).x).toBeCloseTo(300, 0);
    expect((after.extent as Vec).y).toBeCloseTo(200, 0);
    expect((after.extent as Vec).z).toBeCloseTo(100, 0);
    expect((after.origin as Vec).x).toBeCloseTo((before.origin as Vec).x, 0);
    expect((after.origin as Vec).z).toBeCloseTo((before.origin as Vec).z, 0);

    const again = await call("rebuild_brush", { actorLabel: VOLUME, builderProperties: { X: 200 } });
    expect((again.rollback as Json).method).toBe("rebuild_brush");
    expect(Number((((again.rollback as Json).payload as Json).builderProperties as Json).X)).toBeCloseTo(600, 0);
    expect(((await call("get_actor_bounds", { actorLabel: VOLUME })).extent as Vec).x).toBeCloseTo(100, 0);
  }, 60_000);
});
