// @live-editor headless
/**
 * level(audit_performance) over a box built for it: a row of separately
 * placed cubes (instancing candidates, shadow casters, Nanite candidates at a
 * low threshold), one large opaque slab (a lightmap density outlier), a stack
 * of translucent cubes and a large translucent sheet (overdraw), and five
 * overlapping stationary point lights (shadowed-light overlap past the four
 * stationary shadow channels).
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const PREFIX = "PerfAudit_";
const CUBE = "/Engine/BasicShapes/Cube";
const DIR = `${TEST_PREFIX}/PerfAudit`;
const MATERIAL = `${DIR}/M_PerfAudit_Translucent`;
const ORIGIN = { x: 90000, y: 90000 };
const REGION = {
  regionMin: { x: ORIGIN.x - 1000, y: ORIGIN.y - 1000, z: -1000 },
  regionMax: { x: ORIGIN.x + 6000, y: ORIGIN.y + 6000, z: 3000 },
};
const ROW = Array.from({ length: 12 }, (_, i) => `${PREFIX}Cube${i}`);
const SLAB = `${PREFIX}Slab`;
const STACK = [0, 1, 2].map((i) => `${PREFIX}Glass${i}`);
const SHEET = `${PREFIX}Sheet`;
const LIGHTS = Array.from({ length: 5 }, (_, i) => `${PREFIX}Light${i}`);
const ALL = [...ROW, SLAB, ...STACK, SHEET, ...LIGHTS];
const CUBE_COMPONENTS = ROW.length + 1 + STACK.length + 1;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const obj = (v: unknown): Json => (v && typeof v === "object" ? (v as Json) : {});
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);

let bridge: EditorBridge;
let scratch = "";
// Primitives whose bounds span the whole map (the default map's sky sphere)
// fall in every box; measured before the fixture is placed and added back.
const FAR = { regionMin: { x: -990000, y: -990000, z: -990000 }, regionMax: { x: -980000, y: -980000, z: -980000 } };
let ambient = { primitives: 0, triangles: 0 };
let ambientFar = 0;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function wipe(): Promise<void> {
  for (const label of ALL) await callBridge(bridge, "delete_actor", { actorLabel: label });
  await callBridge(bridge, "delete_asset", { assetPath: MATERIAL, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "perf-audit-"));
  await wipe();

  const before = obj((await call("audit_level_performance", { ...REGION })).totals);
  ambient = { primitives: Number(before.primitives), triangles: Number(before.lod0Triangles) };
  ambientFar = Number(obj((await call("audit_level_performance", FAR)).totals).primitives);

  await call("create_material", { name: "M_PerfAudit_Translucent", packagePath: DIR });
  await call("set_material_blend_mode", { assetPath: MATERIAL, blendMode: "Translucent" });

  for (const [i, label] of ROW.entries()) {
    await call("place_actor", {
      actorClass: "StaticMeshActor", staticMesh: CUBE, label,
      location: { x: ORIGIN.x + i * 300, y: ORIGIN.y, z: 100 },
    });
  }
  await call("place_actor", {
    actorClass: "StaticMeshActor", staticMesh: CUBE, label: SLAB,
    location: { x: ORIGIN.x + 2500, y: ORIGIN.y + 4000, z: 0 }, scale: { x: 40, y: 40, z: 0.2 },
  });
  for (const label of STACK) {
    await call("place_actor", {
      actorClass: "StaticMeshActor", staticMesh: CUBE, label, material: MATERIAL,
      location: { x: ORIGIN.x + 1000, y: ORIGIN.y + 1500, z: 200 }, scale: { x: 2, y: 2, z: 2 },
    });
  }
  await call("place_actor", {
    actorClass: "StaticMeshActor", staticMesh: CUBE, label: SHEET, material: MATERIAL,
    location: { x: ORIGIN.x + 4000, y: ORIGIN.y + 1500, z: 300 }, scale: { x: 15, y: 15, z: 0.1 },
  });
  for (const label of LIGHTS) {
    await call("place_actor", { actorClass: "PointLight", label, location: { x: ORIGIN.x + 1500, y: ORIGIN.y + 500, z: 400 } });
    await call("set_light_properties", { actorLabel: label, mobility: "Stationary" });
  }
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await wipe();
  } finally {
    if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
    disconnectBridge();
  }
}, 300_000);

describe("level(audit_performance)", () => {
  it("ranks triangles, instancing, shadow casters, Nanite candidates and lights inside the box", async () => {
    const audit = await call("audit_level_performance", { ...REGION, gridSize: 2000, heavyTriangleThreshold: 10 });
    expect(obj(audit.totals).primitives).toBe(ambient.primitives + CUBE_COMPONENTS);

    const cube = rows(obj(audit.trianglesByMesh).rows).find((r) => String(r.mesh).startsWith(CUBE));
    expect(cube, JSON.stringify(audit.trianglesByMesh)).toBeDefined();
    expect(cube!.placements).toBe(CUBE_COMPONENTS);
    // The engine cube's LOD0 triangle count is not fixed across engine versions (48 on 5.9).
    const cubeTriangles = Number(cube!.lod0Triangles);
    expect(cubeTriangles).toBeGreaterThanOrEqual(12);
    expect(cube!.totalTriangles).toBe(cubeTriangles * CUBE_COMPONENTS);
    expect(obj(audit.totals).lod0Triangles).toBe(ambient.triangles + cubeTriangles * CUBE_COMPONENTS);

    const instancing = rows(obj(audit.instancingCandidates).rows).find((r) => String(r.mesh).startsWith(CUBE));
    expect(instancing).toBeDefined();
    expect(instancing!.components).toBe(CUBE_COMPONENTS);
    expect(Number(instancing!.separateDraws)).toBeGreaterThanOrEqual(CUBE_COMPONENTS);

    const shadows = obj(audit.dynamicShadowCasters);
    expect(Number(shadows.count)).toBeGreaterThanOrEqual(ROW.length);
    expect(rows(shadows.costliest).length).toBeGreaterThan(0);

    // The translucent cubes share the mesh, so Nanite is blocked and the result says why.
    const nanite = rows(obj(audit.naniteCandidates).rows).find((r) => String(r.mesh).startsWith(CUBE));
    expect(nanite).toBeDefined();
    expect(nanite!.eligible).toBe(false);
    expect(JSON.stringify(nanite!.blockers)).toContain("translucent");

    const lights = obj(audit.lights);
    expect(lights.shadowedLocalLights).toBe(LIGHTS.length);
    expect(lights.maxOverlap).toBe(LIGHTS.length - 1);
    expect(lights.stationaryOverChannelLimit).toBe(LIGHTS.length);
    expect(Number(obj(audit.regions).maxShadowedLightsInCell)).toBeGreaterThanOrEqual(LIGHTS.length);
  }, 120_000);

  it("finds the stacked and the large translucent surfaces", async () => {
    const audit = await call("audit_level_performance", { ...REGION });
    const overdraw = obj(audit.translucentOverdraw);
    expect(overdraw.translucentPrimitives).toBe(STACK.length + 1);
    const byLabel = new Map(rows(overdraw.rows).map((r) => [String(r.actorLabel), r]));
    for (const label of STACK) {
      expect(byLabel.get(label), JSON.stringify(overdraw)).toBeDefined();
      expect(byLabel.get(label)!.stackDepth).toBe(STACK.length);
      expect(rows(byLabel.get(label)!.reasons)).toContain("stacked");
    }
    const sheet = byLabel.get(SHEET);
    expect(sheet).toBeDefined();
    expect(Number(sheet!.areaM2)).toBeGreaterThanOrEqual(100);
    expect(rows(sheet!.reasons)).toContain("large");
  }, 120_000);

  it("calls out the slab's lightmap density against the rest of the box", async () => {
    const audit = await call("audit_level_performance", { ...REGION });
    const lightmap = obj(audit.lightmapResolutionOutliers);
    expect(Number(lightmap.evaluated)).toBeGreaterThanOrEqual(ROW.length);
    const slab = rows(lightmap.rows).find((r) => r.actorLabel === SLAB);
    expect(slab, JSON.stringify(lightmap)).toBeDefined();
    expect(slab!.direction).toBe("low");
    expect(Number(slab!.ratioToMedian)).toBeLessThan(0.25);
    expect(typeof obj(audit.texelDensityOutliers).evaluated).toBe("number");
  }, 120_000);

  it("bounds every list by maxResults, and an empty box audits nothing", async () => {
    const capped = await call("audit_level_performance", { ...REGION, maxResults: 1, heavyTriangleThreshold: 10 });
    expect(rows(obj(capped.dynamicShadowCasters).costliest)).toHaveLength(1);
    expect(obj(capped.dynamicShadowCasters).truncated).toBe(true);
    expect(rows(obj(capped.lights).rows)).toHaveLength(1);

    const empty = await call("audit_level_performance", FAR);
    expect(obj(empty.totals).primitives).toBe(ambientFar);
    expect(rows(obj(empty.trianglesByMesh).rows).some((r) => String(r.mesh).startsWith(CUBE))).toBe(false);
    expect(obj(empty.lights).shadowedLocalLights).toBe(0);
  }, 120_000);

  it("writes every row to a file with dumpToFile", async () => {
    const out = path.join(scratch, "audit.json");
    const dumped = await call("audit_level_performance", { ...REGION, dumpToFile: true, outputPath: out });
    expect(dumped.dumpedToFile).toBe(true);
    expect(path.resolve(String(dumped.outputPath))).toBe(path.resolve(out));
    const onDisk = JSON.parse(fs.readFileSync(out, "utf8")) as Json;
    expect(obj(onDisk.totals).primitives).toBe(ambient.primitives + CUBE_COMPONENTS);
    expect(rows(obj(onDisk.trianglesByMesh).rows).length).toBeGreaterThan(0);
  }, 120_000);

  it("refuses half a region", async () => {
    const half = await callBridge(bridge, "audit_level_performance", { regionMin: REGION.regionMin });
    expect(succeeded(half)).toBe(false);
  }, 60_000);
});
