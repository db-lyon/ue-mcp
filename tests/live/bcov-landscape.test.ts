// @live-editor render
/**
 * Landscape actions against a small landscape the file creates and deletes:
 * creation and listings, heights, point and region queries, heightmap IO,
 * sculpting, erosion, paint layers, holes and geo projection. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Landscape`;
const LABEL = "BCov_Landscape_Terrain";
const MATERIAL = "/Engine/EngineMaterials/WorldGridMaterial";
const EXPORT_FILE = "BCov/bcov_landscape_export.r16";
// 2x2 components of 15 quads: vertices 0..30 on each axis, 100 cm apart.
const BX = 400000;
const BY = 400000;
const RAW_PER_CM = 1.28; // Z scale 100: one raw unit is 1/128 * 100 cm.
const MID = 32768;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const at = (qx: number, qy: number) => ({ x: BX + 100 * qx, y: BY + 100 * qy });

let bridge: EditorBridge;
let actorPath = "";
let landscapeName = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** Merge the edit layers, then read until the merged surface shows the write. */
async function settle(read: () => Promise<Json>, done: (b: Json) => boolean): Promise<Json> {
  let last: Json = {};
  for (let i = 0; i < 10; i++) {
    await call("merge_landscape_edit_layers", { actorPath });
    last = await read();
    if (done(last)) return last;
    await sleep(300);
  }
  return last;
}

const rawAt = (qx: number, qy: number) => call("get_landscape_height_at_point", { actorPath, point: at(qx, qy) });

async function deleteLeftovers(): Promise<void> {
  for (let i = 0; i < 3; i++) {
    const r = await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
    if (!succeeded(r)) break;
  }
  for (const name of ["LI_BCovRock", "LI_BCovGrass"]) {
    await callBridge(bridge, "delete_asset", { assetPath: `${DIR}/${name}`, force: true });
  }
}

beforeAll(async () => {
  bridge = await getBridge();
  await deleteLeftovers();
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await deleteLeftovers();
    disconnectBridge();
  }
}, 120_000);

describe("landscape creation and listings", () => {
  it("create_landscape spawns a landscape the info, component, proxy and layer listings report", async () => {
    const proxiesBefore = await call("list_landscape_proxies", { limit: 2000 });
    const infoBefore = await call("get_landscape_info", {});

    const created = await call("create_landscape", {
      label: LABEL, location: { x: BX, y: BY, z: 0 },
      componentCountX: 2, componentCountY: 2, subsectionSizeQuads: 15, numSubsections: 1,
    });
    expect(created.sizeX).toBe(31);
    expect(created.sizeY).toBe(31);
    actorPath = String(created.actorPath);
    landscapeName = actorPath.split(".").pop() ?? "";
    expect(landscapeName).not.toBe("");

    const info = await call("get_landscape_info", {});
    expect(Number(info.count)).toBe(Number(infoBefore.count ?? 0) + 1);
    const mine = (info.landscapes as Json[]).find((l) => l.name === landscapeName);
    expect(mine, JSON.stringify(info.landscapes)).toBeDefined();
    expect(mine!.componentCount).toBe(4);
    expect(Number(mine!.locationX)).toBeCloseTo(BX, 0);

    // Component indices run across every landscape in the level; find this one's four.
    const total = (info.landscapes as Json[]).reduce((n, l) => n + Number(l.componentCount ?? 0), 0);
    const bases = new Set<string>();
    for (let i = total - 1; i >= 0 && bases.size < 4; i--) {
      const comp = await call("get_landscape_component", { componentIndex: i });
      const x = Number(comp.locationX);
      const y = Number(comp.locationY);
      if (x >= BX - 1 && x < BX + 3000 && y >= BY - 1 && y < BY + 3000) {
        expect(comp.componentSizeQuads).toBe(15);
        expect(comp.subSections).toBe(1);
        bases.add(`${comp.sectionBaseX},${comp.sectionBaseY}`);
      }
    }
    expect([...bases].sort()).toEqual(["0,0", "0,15", "15,0", "15,15"]);

    const proxies = await call("list_landscape_proxies", { limit: 2000 });
    expect(proxies.parentLandscapes).toBe(Number(proxiesBefore.parentLandscapes) + 1);

    const edit = await call("list_landscape_edit_layers", { actorPath });
    expect(edit.actorPath).toBe(actorPath);
    expect(Number(edit.count)).toBeGreaterThanOrEqual(1);
    expect(typeof (edit.editLayers as Json[])[0].name).toBe("string");

    // No action places spline control points, so a new landscape owns none.
    const splines = await call("list_landscape_splines", {});
    const points = splines.controlPoints as Json[];
    expect(splines.count).toBe(points.length);
    expect(points.filter((p) => p.landscapeName === landscapeName)).toHaveLength(0);
  }, 180_000);

  it("set_landscape_material assigns the material the usage summary reports", async () => {
    const set = await call("set_landscape_material", { landscapeName: actorPath, materialPath: MATERIAL });
    expect(set.componentsUpdated).toBe(4);
    const summary = await call("get_landscape_material_usage_summary", {});
    const row = (summary.proxies as Json[]).find((p) => p.path === actorPath);
    expect(row, JSON.stringify(summary.proxies)).toBeDefined();
    expect(String(row!.landscapeMaterial)).toContain("WorldGridMaterial");
    expect(row!.landscapeComponentCount).toBe(4);
    expect(summary.uniqueLandscapeMaterials as string[]).toEqual(expect.arrayContaining([expect.stringContaining("WorldGridMaterial")]));
  }, 120_000);
});

describe("landscape heights", () => {
  it("set_landscape_height_region raises a plateau that merge, region, point, normal, slope and analysis reads report", async () => {
    const set = await call("set_landscape_height_region", {
      actorPath, region: { minX: 5, minY: 5, maxX: 9, maxY: 9 }, height: 500,
    });
    expect(set.verticesChanged).toBe(25);
    expect(Number((set.heightAfter as Json).minZ)).toBeCloseTo(500, 0);

    const merged = await call("merge_landscape_edit_layers", { actorPath });
    expect(merged.updatedNow).toBe(true);
    expect((merged.editLayers as Json[]).length).toBeGreaterThanOrEqual(1);

    const point = await settle(() => rawAt(7, 7), (b) => b.rawHeight === MID + 500 * RAW_PER_CM);
    expect(point.rawHeight).toBe(MID + 640);
    expect(Number(point.height)).toBeCloseTo(500, 0);

    const region = await call("get_landscape_height_region", { actorPath, region: { minX: 5, minY: 5, maxX: 9, maxY: 9 } });
    expect((region.height as Json).minRaw).toBe(MID + 640);
    expect((region.height as Json).maxRaw).toBe(MID + 640);
    expect(region.heights as number[]).toHaveLength(25);

    const flatNormal = await call("get_landscape_normal_at_point", { actorPath, point: at(7, 7) });
    expect(Number((flatNormal.normal as Json).z)).toBeGreaterThan(0.999);
    // At the plateau edge the ground falls away toward +X, so the normal leans +X.
    const edgeNormal = await call("get_landscape_normal_at_point", { actorPath, point: at(9, 7) });
    expect(Number((edgeNormal.normal as Json).x)).toBeGreaterThan(0.5);

    // 500 cm drop over the 200 cm central difference: atan(2.5), about 68 degrees.
    const slope = await call("get_landscape_slope_at_point", { actorPath, point: at(9, 7) });
    expect(Number(slope.slopeDegrees)).toBeGreaterThan(60);
    expect(Number(slope.slopeDegrees)).toBeLessThan(75);
    expect(Number(slope.gradePercent)).toBeCloseTo(250, -1);

    const map = await call("get_landscape_slope_map", { actorPath, region: { minX: 4, minY: 4, maxX: 10, maxY: 10 } });
    const slopes = map.slopeDegrees as number[];
    expect(slopes).toHaveLength(49);
    expect(slopes[3 * 7 + 3]).toBeLessThan(1);
    expect(Number(map.maxSlopeDegrees)).toBeGreaterThan(45);

    const analysis = await call("analyze_landscape_terrain", { actorPath, region: { minX: 0, minY: 0, maxX: 30, maxY: 30 } });
    expect(Number((analysis.height as Json).maxZ)).toBeCloseTo(500, 0);
    expect(Number((analysis.height as Json).minZ)).toBeCloseTo(0, 0);
    expect(Number(analysis.maxSlopeDegrees)).toBeGreaterThan(45);
    expect(Number(analysis.flatVertexCount)).toBeGreaterThan(0);
    expect(Number(analysis.flatVertexCount)).toBeLessThan(31 * 31);
  }, 180_000);

  it("export_landscape_heightmap writes the plateau and import_landscape_heightmap puts it back after a flatten", async () => {
    const region = { minX: 0, minY: 0, maxX: 12, maxY: 12 };
    const exported = await call("export_landscape_heightmap", { actorPath, filePath: EXPORT_FILE, region });
    expect(exported.width).toBe(13);
    expect(exported.height).toBe(13);
    expect(exported.bytesWritten).toBe(13 * 13 * 2);
    expect((exported.heightRange as Json).maxRaw).toBe(MID + 640);

    const flattened = await call("set_landscape_height_region", { actorPath, region, rawHeight: MID });
    expect(flattened.verticesChanged).toBe(25);
    const flat = await settle(() => rawAt(7, 7), (b) => b.rawHeight === MID);
    expect(flat.rawHeight).toBe(MID);

    const imported = await call("import_landscape_heightmap", { actorPath, filePath: EXPORT_FILE, region, width: 13, height: 13 });
    expect(imported.sourceWidth).toBe(13);
    expect(imported.verticesChanged).toBe(25);
    expect((imported.heightAfter as Json).maxRaw).toBe(MID + 640);
    const back = await settle(() => rawAt(7, 7), (b) => b.rawHeight === MID + 640);
    expect(back.rawHeight).toBe(MID + 640);
  }, 180_000);

  it("sculpt_landscape raises a brush that sculpt_landscape_region flattens again", async () => {
    const sculpted = await call("sculpt_landscape", {
      actorPath, center: at(22, 22), radius: 300, mode: "raise", amount: 200, falloff: 0,
    });
    expect(Number(sculpted.verticesChanged)).toBeGreaterThan(0);
    const raised = await settle(() => rawAt(22, 22), (b) => b.rawHeight === MID + 256);
    expect(Number(raised.height)).toBeCloseTo(200, 0);

    const flattened = await call("sculpt_landscape_region", {
      actorPath, operator: "flatten", region: { minX: 18, minY: 18, maxX: 26, maxY: 26 },
      shape: "rect", falloff: 0, strength: 1, targetHeight: 0,
    });
    expect(Number(flattened.verticesChanged)).toBeGreaterThan(0);
    expect((flattened.heightAfter as Json).maxRaw).toBe(MID);
    const flat = await settle(() => rawAt(22, 22), (b) => b.rawHeight === MID);
    expect(flat.rawHeight).toBe(MID);
  }, 180_000);

  it("apply_landscape_erosion slumps a spike toward its neighbours", async () => {
    await call("set_landscape_height_region", { actorPath, region: { minX: 14, minY: 4, maxX: 14, maxY: 4 }, rawHeight: 40000 });
    const spike = await settle(() => rawAt(14, 4), (b) => b.rawHeight === 40000);
    expect(spike.rawHeight).toBe(40000);

    const eroded = await call("apply_landscape_erosion", {
      actorPath, erosionType: "thermal", region: { minX: 10, minY: 0, maxX: 18, maxY: 8 },
      iterations: 10, talusAngle: 30, strength: 0.5,
    });
    expect((eroded.heightBefore as Json).maxRaw).toBe(40000);
    expect(Number((eroded.heightAfter as Json).maxRaw)).toBeLessThan(40000);
    expect(Number(eroded.verticesChanged)).toBeGreaterThan(1);
    const after = await settle(() => rawAt(14, 4), (b) => Number(b.rawHeight) < 40000);
    expect(Number(after.rawHeight)).toBeLessThan(40000);
  }, 180_000);
});

describe("landscape paint layers", () => {
  it("create_landscape_layer_info makes the asset add_landscape_layer_info registers, and the layer queries see it", async () => {
    const created = await call("create_landscape_layer_info", { layerName: "BCovRock", packagePath: DIR, hardness: 0.7 });
    const rockPath = String(created.path);
    expect(rockPath).toBe(`${DIR}/LI_BCovRock.LI_BCovRock`);

    // The asset already exists, so registering it reuses it instead of making another.
    const rock = await call("add_landscape_layer_info", { layerName: "BCovRock", landscapeName: actorPath, packagePath: DIR });
    expect(rock.layerInfoAssetCreated).toBe(false);
    expect(rock.path).toBe(rockPath);

    const grass = await call("add_landscape_layer_info", { layerName: "BCovGrass", landscapeName: actorPath, packagePath: DIR });
    expect(grass.layerInfoAssetCreated).toBe(true);
    expect(grass.path).toBe(`${DIR}/LI_BCovGrass.LI_BCovGrass`);

    const exists = await call("landscape_layer_exists", { actorPath, layerName: "BCovRock" });
    expect(exists.exists).toBe(true);
    expect(exists.hasLayerInfo).toBe(true);
    expect(exists.layerInfoPath).toBe(rockPath);

    const listed = await call("list_landscape_layers", {});
    const mine = (listed.layers as Json[]).filter((l) => l.landscapeName === landscapeName).map((l) => l.name);
    expect(mine).toEqual(expect.arrayContaining(["BCovRock", "BCovGrass"]));
  }, 120_000);

  it("paint_landscape_layer and set_landscape_layer_weight_region write weights the region read returns", async () => {
    const painted = await call("paint_landscape_layer", {
      actorPath, layerName: "BCovGrass", center: at(5, 25), radius: 200, strength: 1, falloff: 0,
    });
    expect(Number(painted.verticesChanged)).toBeGreaterThan(0);
    const brush = await call("get_landscape_layer_weight_region", {
      actorPath, layerName: "BCovGrass", region: { minX: 5, minY: 25, maxX: 5, maxY: 25 }, editLayerIndex: 0,
    });
    expect(brush.maxWeight).toBe(255);

    const region = { minX: 20, minY: 2, maxX: 22, maxY: 4 };
    const set = await call("set_landscape_layer_weight_region", { actorPath, layerName: "BCovGrass", region, weight: 0.5 });
    expect(set.verticesChanged).toBe(9);
    const read = await call("get_landscape_layer_weight_region", { actorPath, layerName: "BCovGrass", region, editLayerIndex: 0 });
    const weights = read.weights as number[];
    expect(weights).toHaveLength(9);
    for (const w of weights) {
      expect(w).toBeGreaterThanOrEqual(127);
      expect(w).toBeLessThanOrEqual(128);
    }
    expect(read.paintedVertexCount).toBe(9);
  }, 120_000);

  it("remove_landscape_layer unregisters a layer the existence check and listing then miss", async () => {
    const removed = await call("remove_landscape_layer", { actorPath, layerName: "BCovRock" });
    expect(removed.removed).toBe(true);
    expect(removed.layersAfter as string[]).not.toContain("BCovRock");

    const exists = await call("landscape_layer_exists", { actorPath, layerName: "BCovRock" });
    expect(exists.exists).toBe(false);
    const listed = await call("list_landscape_layers", {});
    const mine = (listed.layers as Json[]).filter((l) => l.landscapeName === landscapeName).map((l) => l.name);
    expect(mine).not.toContain("BCovRock");
    expect(mine).toContain("BCovGrass");
  }, 120_000);
});

describe("landscape holes", () => {
  it("set_landscape_holes punches and fills a region that get_landscape_holes reads back", async () => {
    // The engine keeps visibility data only where the landscape material has a Landscape Visibility Mask node,
    // so without one the action refuses rather than reporting holes that are not there.
    const refused = await callBridge(bridge, "set_landscape_holes", { actorPath, region: { minX: 25, minY: 10, maxX: 27, maxY: 12 }, hole: true });
    expect((refused.result as Json | undefined)?.success).toBe(false);
    expect(String((refused.result as Json | undefined)?.error)).toContain("Landscape Visibility Mask");
    const holeMaterial = `${DIR}/M_BCovHoles`;
    await call("create_material", { name: "M_BCovHoles", packagePath: DIR });
    await call("set_material_blend_mode", { assetPath: holeMaterial, blendMode: "Masked" });
    const mask = await call("add_material_expression", { materialPath: holeMaterial, expressionType: "LandscapeVisibilityMask" });
    await call("connect_to_material_property", { materialPath: holeMaterial, expressionName: String(mask.expressionName ?? "MaterialExpressionLandscapeVisibilityMask_0"), property: "OpacityMask" });
    await call("set_landscape_material", { materialPath: holeMaterial, landscapeName: actorPath });

    const region = { minX: 25, minY: 10, maxX: 27, maxY: 12 };
    const punched = await call("set_landscape_holes", { actorPath, region, hole: true });
    expect(punched.holeVertexCountAfter).toBe(9);
    expect(punched.verticesChanged).toBe(9);
    const holes = await settle(() => call("get_landscape_holes", { actorPath, region }), (b) => b.holeVertexCount === 9);
    expect(holes.holeVertexCount).toBe(9);
    const point = await call("get_landscape_holes", { actorPath, point: at(26, 11) });
    expect(point.isHole).toBe(true);

    const filled = await call("set_landscape_holes", { actorPath, region, hole: false });
    expect(filled.verticesChanged).toBe(9);
    const none = await settle(() => call("get_landscape_holes", { actorPath, region }), (b) => b.holeVertexCount === 0);
    expect(none.holeVertexCount).toBe(0);
  }, 120_000);
});

describe("real-world planning", () => {
  it("plan_real_world_landscape sizes a 1 km tile at one metre per quad and a 100 m elevation band", async () => {
    const plan = await call("plan_real_world_landscape", {
      realWorldSizeMeters: { x: 1000, y: 1000 }, minElevationMeters: 0, maxElevationMeters: 100,
      metersPerQuad: 1, location: { x: 0, y: 0, z: 0 },
    });
    expect(plan.groundSizeMetersX).toBe(1000);
    expect(Number(plan.metersPerQuadX)).toBeGreaterThan(0.95);
    expect(Number(plan.metersPerQuadX)).toBeLessThan(1.05);
    expect(Number(plan.minWorldZ)).toBeCloseTo(0, 3);
    expect(Number(plan.maxWorldZ)).toBeCloseTo(10000, 3);
    const create = plan.create as Json;
    const resolution = plan.resolution as Json;
    expect(create.componentCountX).toBe(resolution.componentCountX);
    expect(Number((create.scale as Json).x)).toBeCloseTo(Number(plan.metersPerQuadX) * 100, 3);
    // 10000 cm of relief over 65535 raw units of 1/128 cm needs a Z scale of at least 19.53.
    expect(Number((create.scale as Json).z)).toBeGreaterThanOrEqual(19.53);
  }, 120_000);

  it("project_geo_coordinates maps the box corners onto the landscape corners and back", async () => {
    const boundsLatLon = { minLat: 46.0, minLon: 7.0, maxLat: 46.01, maxLon: 7.01 };
    const projected = await call("project_geo_coordinates", {
      actorPath, boundsLatLon,
      points: [
        { name: "nw", lat: 46.01, lon: 7.0 },
        { name: "se", lat: 46.0, lon: 7.01 },
        { name: "mid", x: BX + 1500, y: BY + 1500 },
      ],
    });
    const rows = projected.points as Json[];
    const nw = rows.find((r) => r.name === "nw")!;
    const se = rows.find((r) => r.name === "se")!;
    const mid = rows.find((r) => r.name === "mid")!;
    expect(Number(nw.x)).toBeCloseTo(BX, 0);
    expect(Number(nw.y)).toBeCloseTo(BY, 0);
    expect(nw.heightSampled).toBe(true);
    expect(Number(nw.z)).toBeCloseTo(0, 0);
    expect(Number(se.x)).toBeCloseTo(BX + 3000, 0);
    expect(Number(se.y)).toBeCloseTo(BY + 3000, 0);
    expect(mid.direction).toBe("worldToGeo");
    expect(Number(mid.lat)).toBeCloseTo(46.005, 6);
    expect(Number(mid.lon)).toBeCloseTo(7.005, 6);
    expect(projected.outOfBoundsCount).toBe(0);
  }, 120_000);
});
