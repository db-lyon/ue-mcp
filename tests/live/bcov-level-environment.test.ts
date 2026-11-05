// @live-editor headless
/**
 * Level lights, volumes, post process, fog, runtime virtual texture volumes and water read back through
 * another action. Every actor is labelled BCov_Level_Env* in the open map and deleted afterwards. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const PREFIX = "BCov_Level_Env";
const GRID_MATERIAL = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial";

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** UPROPERTY values of the first component of a class, as get_component_details exports them. */
async function componentValues(label: string, componentClass: string, names: string[]): Promise<Json> {
  const tree = (await call("get_component_tree", { actorLabel: label })).components as Json[];
  const comp = tree.find((c) => c.class === componentClass);
  expect(comp, `${label} has no ${componentClass}`).toBeDefined();
  const out = await call("get_component_details", { actorLabel: label, componentName: comp!.name, includeValues: true, propertyNames: names });
  return (out.component as Json).values as Json;
}

const extentOf = async (label: string) => (await call("get_actor_bounds", { actorLabel: label })).extent as Vec;

async function cleanup(): Promise<void> {
  await callBridge(bridge, "delete_actors", { labelPrefix: PREFIX });
}

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await cleanup();
    disconnectBridge();
  }
}, 120_000);

describe("lights", () => {
  const LIGHT = `${PREFIX}_Point`;

  it("spawn_light places a point light at the requested location", async () => {
    const out = await call("spawn_light", { lightType: "point", label: LIGHT, location: { x: 0, y: 50000, z: 300 }, intensity: 5000 });
    expect(out.actorLabel).toBe(LIGHT);
    const found = await call("get_actors_by_class", { className: "PointLight", labelPrefix: LIGHT });
    expect(found.count).toBe(1);
    expect(((found.actors as Json[])[0].location as Vec).z).toBeCloseTo(300, 0);
    expect(Number((await componentValues(LIGHT, "PointLightComponent", ["Intensity"])).Intensity)).toBeCloseTo(5000, 0);
  }, 120_000);

  it("set_light_properties writes intensity and colour", async () => {
    await call("set_light_properties", { actorLabel: LIGHT, intensity: 8000, color: { r: 255, g: 0, b: 0 } });
    const values = await componentValues(LIGHT, "PointLightComponent", ["Intensity", "LightColor"]);
    expect(Number(values.Intensity)).toBeCloseTo(8000, 0);
    expect(String(values.LightColor)).toContain("R=255");
    expect(String(values.LightColor)).toContain("G=0");
  }, 120_000);
});

describe("volumes and post process", () => {
  const BLOCK = `${PREFIX}_Block`;
  const PP = `${PREFIX}_PostProcess`;

  it("spawn_volume places a blocking volume list_volumes lists with the requested extent", async () => {
    await call("spawn_volume", { volumeType: "BlockingVolume", label: BLOCK, location: { x: 0, y: 51000, z: 100 }, extent: { x: 100, y: 100, z: 100 } });
    const listed = await call("list_volumes", { volumeType: "BlockingVolume" });
    expect((listed.volumes as Json[]).map((v) => v.label)).toContain(BLOCK);
    expect((await extentOf(BLOCK)).x).toBeCloseTo(100, 0);
  }, 120_000);

  it("set_volume_properties resizes the brush and writes a property", async () => {
    await call("spawn_volume", { volumeType: "PostProcessVolume", label: PP, location: { x: 1000, y: 51000, z: 100 } });
    const out = await call("set_volume_properties", { actorLabel: PP, properties: { BrushExtent: { x: 300, y: 200, z: 100 }, Priority: 7 } });
    expect(out.changes).toEqual(expect.arrayContaining(["BrushExtent", "Priority"]));
    const extent = await extentOf(PP);
    expect(extent.x).toBeCloseTo(300, 0);
    expect(extent.y).toBeCloseTo(200, 0);
    const props = (await call("get_actor_details", { actorLabel: PP, includeProperties: true, propertyName: "Priority" })).properties as Json[];
    expect(Number(props[0].value)).toBeCloseTo(7, 3);
  }, 120_000);

  it("add_post_process_blendable adds the material to the volume's blendables", async () => {
    const out = await call("add_post_process_blendable", { actorLabel: PP, materialPath: GRID_MATERIAL, weight: 0.5 });
    expect(out.blendableCount).toBe(1);
    const read = await call("get_post_process_settings", { actorLabel: PP, names: ["WeightedBlendables"] });
    const row = (read.settings as Json[]).find((s) => s.name === "WeightedBlendables");
    expect(String(row?.value)).toContain("WorldGridMaterial");
  }, 120_000);
});

describe("fog and virtual texture volumes", () => {
  const FOG = `${PREFIX}_Fog`;
  const RVT = `${PREFIX}_RVT`;

  it("set_fog_properties writes the fog component", async () => {
    await call("place_actor", { actorClass: "/Script/Engine.ExponentialHeightFog", label: FOG, location: { x: 0, y: 52000, z: 0 } });
    await call("set_fog_properties", { actorLabel: FOG, fogDensity: 0.05, fogHeightFalloff: 0.5, startDistance: 250 });
    const values = await componentValues(FOG, "ExponentialHeightFogComponent", ["FogDensity", "FogHeightFalloff", "StartDistance"]);
    expect(Number(values.FogDensity)).toBeCloseTo(0.05, 4);
    expect(Number(values.FogHeightFalloff)).toBeCloseTo(0.5, 4);
    expect(Number(values.StartDistance)).toBeCloseTo(250, 1);
  }, 120_000);

  it("get_runtime_virtual_texture_summary lists a placed runtime virtual texture volume", async () => {
    await call("place_actor", { actorClass: "/Script/Engine.RuntimeVirtualTextureVolume", label: RVT, location: { x: 500, y: 52000, z: 0 } });
    const out = await call("get_runtime_virtual_texture_summary");
    const row = (out.volumes as Json[]).find((v) => v.label === RVT);
    expect(row).toBeDefined();
    expect(row!.class).toBe("RuntimeVirtualTextureVolume");
    expect(Number(out.volumeCount)).toBeGreaterThanOrEqual(1);
  }, 120_000);
});

describe("water", () => {
  const ZONE = `${PREFIX}_WaterZone`;
  const LAKE = `${PREFIX}_Lake`;
  const xy = (v: unknown, axis: "x" | "y") => Number((v as Json)[axis] ?? (v as Json)[axis.toUpperCase()]);

  beforeAll(async () => {
    await call("place_actor", { actorClass: "/Script/Water.WaterZone", label: ZONE, location: { x: 0, y: 60000, z: 0 } });
    await call("place_actor", { actorClass: "/Script/Water.WaterBodyLake", label: LAKE, location: { x: 0, y: 60000, z: 0 } });
  }, 120_000);

  it("get_water_state describes the placed zone and lake", async () => {
    const zone = await call("get_water_state", { actorLabel: ZONE });
    expect(zone.zoneCount).toBe(1);
    expect((zone.zones as Json[])[0].actorLabel).toBe(ZONE);
    const lake = await call("get_water_state", { actorLabel: LAKE });
    expect(lake.bodyCount).toBe(1);
    expect((lake.bodies as Json[])[0].componentClass).toContain("WaterBody");
  }, 120_000);

  it("set_water_body_property writes a WaterBodyComponent property", async () => {
    await call("set_water_body_property", { actorLabel: LAKE, propertyName: "ShapeDilation", value: 2048 });
    const lake = await call("get_water_state", { actorLabel: LAKE });
    expect(Number(((lake.bodies as Json[])[0].properties as Json).ShapeDilation)).toBeCloseTo(2048, 1);
  }, 120_000);

  it("rebuild_water_zone resizes the zone and its water mesh tiles", async () => {
    await call("rebuild_water_zone", { actorLabel: ZONE, zoneExtent: { x: 12800, y: 12800 }, tileSize: 400 });
    const zone = (await call("get_water_state", { actorLabel: ZONE })).zones as Json[];
    const extent = (zone[0].properties as Json).ZoneExtent;
    expect(xy(extent, "x")).toBeCloseTo(12800, 0);
    expect(xy(extent, "y")).toBeCloseTo(12800, 0);
    expect(Number((zone[0].waterMesh as Json).TileSize)).toBeCloseTo(400, 0);
  }, 180_000);
});
