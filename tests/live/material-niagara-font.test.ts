// @live-editor render
/**
 * Niagara sim cache capture and read (#1394), material orphan cleanup (#1403)
 * and font import (#1377), each read back from the editor.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/MatNiaFont`;
const SYSTEM = `${DIR}/NS_SimCache`;
const MATERIAL = `${DIR}/M_Orphans`;
const FONT_FILE = "C:/Windows/Fonts/arial.ttf";
const FONT_FILE_BOLD = "C:/Windows/Fonts/arialbd.ttf";
const LABEL = "NS_SimCacheProbe";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
  for (const path of [SYSTEM, MATERIAL, `${DIR}/arial`, `${DIR}/arialbd`, `${DIR}/arial_Font`, `${DIR}/F_Probe`]) {
    await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  }
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
    disconnectBridge();
  }
});

describe("niagara sim cache (#1394)", () => {
  it("captures particle frames off a placed system and reads named attributes back", async () => {
    await call("create_niagara_system", { name: "NS_SimCache", packagePath: DIR });
    await call("add_emitter_to_system", { systemPath: SYSTEM, emitterPath: "/Niagara/DefaultAssets/Templates/Emitters/Fountain" });
    const spawned = await call("spawn_niagara_at_location", { systemPath: SYSTEM, label: LABEL, location: { x: 0, y: 0, z: 100 } });
    expect(spawned.actorClass).toBe("NiagaraActor");
    // A system authored this session stays inactive until it is compiled.
    await call("compile_niagara_system", { systemPath: SYSTEM });
    await call("reactivate_niagara", { actorLabel: LABEL });

    const captured = await call("capture_niagara_sim_cache", { actorLabel: LABEL, frames: 3, advanceSimulation: true, tickDelta: 0.1 });
    expect(captured.frames).toBe(3);
    const emitter = (captured.emitters as Array<{ numInstancesPerFrame: number[] }>)[0];
    expect(Math.max(...emitter.numInstancesPerFrame)).toBeGreaterThan(0);

    const read = await call("read_niagara_sim_cache", {
      simCachePath: captured.simCachePath, attributes: ["Particles.Position", "Age"], limit: 4,
    });
    const attributes = read.attributes as Array<{ found: boolean; count: number; values: unknown[] }>;
    expect(attributes.map((a) => a.found)).toEqual([true, true]);
    expect(attributes[0].count).toBe(read.numInstances);
    expect(attributes[0].values.length).toBeLessThanOrEqual(4);

    const missing = await callBridge(bridge, "read_niagara_sim_cache", { simCachePath: captured.simCachePath, attributes: ["Nope"] });
    expect(succeeded(missing)).toBe(false);
    expect(JSON.stringify(missing.result)).toContain("layout");
  }, 180_000);
});

describe("material orphans (#1403)", () => {
  it("delete_orphans lists the unreachable nodes on a dry run and deletes exactly those", async () => {
    await call("create_material", { name: "M_Orphans", packagePath: DIR });
    await call("add_material_expression", { materialPath: MATERIAL, expressionType: "Sine", name: "Loose" });
    const dry = await call("delete_orphan_expressions", { assetPath: MATERIAL, dryRun: true });
    expect(dry.orphanCount).toBe(1);
    expect(dry.deletedCount).toBe(0);
    const wet = await call("delete_orphan_expressions", { assetPath: MATERIAL });
    expect(wet.deletedCount).toBe(1);
    const graph = await call("read_material_graph", { assetPath: MATERIAL });
    expect((graph.expressions as unknown[]).length).toBe(0);
  }, 120_000);
});

describe("fonts (#1377)", () => {
  it("imports a font face with no dialog and builds a composite font from two faces", async (ctx) => {
    if (!fs.existsSync(FONT_FILE) || !fs.existsSync(FONT_FILE_BOLD)) ctx.skip("system Arial fonts are not installed");
    const face = await call("import_font", { filePath: FONT_FILE, packagePath: DIR, createFont: true });
    expect((face.subFaces as string[]).length).toBeGreaterThan(0);
    expect(String(face.fontPath)).toContain("arial_Font");
    await call("import_font", { filePath: FONT_FILE_BOLD, packagePath: DIR });

    const font = await call("create_font", {
      name: "F_Probe", packagePath: DIR, fontFaces: [`${DIR}/arial`, `${DIR}/arialbd`], typefaceNames: ["Regular", "Bold"],
    });
    expect((font.typefaces as Array<{ name: string }>).map((t) => t.name)).toEqual(["Regular", "Bold"]);
    const props = await call("get_asset_properties", { assetPath: `${DIR}/F_Probe`, propertyName: "CompositeFont" });
    expect(JSON.stringify(props)).toContain("arialbd");
  }, 180_000);
});
