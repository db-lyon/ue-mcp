// @live-editor render
/**
 * Material compile results, texture sampler upkeep and static-switch instances,
 * in an editor that compiles shaders, checked against the pictures it renders.
 *
 * The material samples a texture behind a static switch, and the instance on
 * the mesh overrides that switch, so it draws with a shader permutation of its
 * own: the case that left render_mesh_studio waiting forever (#1992) and the
 * one a texture swap broke (#1441). Refs #1947, #1994, #1441, #1992
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { decodePng, encodePng } from "../../src/core/png.js";

const DIR = `${TEST_PREFIX}/MaterialCompile`;
const RED = `${DIR}/T_McRed`;
const GREEN = `${DIR}/T_McGreen`;
const MATERIAL = `${DIR}/M_McSwitch`;
const INSTANCE = `${DIR}/MI_McSwitch`;
const MESH = `${DIR}/SM_McSwitch`;
const BROKEN = `${DIR}/M_McBroken`;
const INSTANCE_COPY = `${DIR}/MI_McSwitchCopy`;
const MESH_COPY = `${DIR}/SM_McSwitchCopy`;
const ALL_ASSETS = [MESH_COPY, MESH, INSTANCE_COPY, INSTANCE, MATERIAL, BROKEN, GREEN, RED];

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;
let scratch: string;
let textureNode = -1;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok, `${method}: ${r.error}`).toBe(true);
  return body(r);
}

async function succeed(method: string, params: Json): Promise<Json> {
  const result = await call(method, params);
  expect(result.success, `${method}: ${JSON.stringify(result).slice(0, 800)}`).not.toBe(false);
  return result;
}

function solidPng(file: string, r: number, g: number, b: number): void {
  const width = 16, height = 16;
  const data = new Uint8Array(width * height * 4);
  for (let i = 0; i < data.length; i += 4) data.set([r, g, b, 255], i);
  fs.writeFileSync(file, encodePng({ width, height, data }));
}

/** Renders the mesh in the studio and returns the outcome row with the centre pixel's colour. */
async function studio(id: string, meshPath = MESH): Promise<{ row: Json; centre: number[] }> {
  const start = await succeed("render_mesh_studio", {
    outputDir: scratch, resolution: 64, fullyLoadTextures: false, captures: [{ id, meshPath }],
  });
  let report = start;
  const deadline = Date.now() + 120_000;
  while (report.state === "running" && Date.now() < deadline) {
    await new Promise((resolve) => setTimeout(resolve, 500));
    report = await call("get_mesh_studio_render", { jobId: start.jobId });
  }
  const row = (report.outcomes as Json[])[0];
  if (row.status !== "rendered") return { row, centre: [] };
  const image = decodePng(fs.readFileSync(row.file as string));
  const at = ((image.height >> 1) * image.width + (image.width >> 1)) * 4;
  return { row, centre: Array.from(image.data.slice(at, at + 3)) };
}

/** The material's graph as export_material_graph writes it, which lists a sampler type that is not the default. */
async function exported(): Promise<string> {
  return JSON.stringify(await succeed("export_material_graph", { assetPath: MATERIAL }));
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-material-compile-"));
  for (const assetPath of ALL_ASSETS) await callBridge(bridge, "delete_asset", { assetPath, force: true });
  solidPng(path.join(scratch, "T_McRed.png"), 255, 0, 0);
  solidPng(path.join(scratch, "T_McGreen.png"), 0, 255, 0);
  await succeed("import_texture", { filePath: path.join(scratch, "T_McRed.png"), name: "T_McRed", packagePath: DIR });
  await succeed("import_texture", { filePath: path.join(scratch, "T_McGreen.png"), name: "T_McGreen", packagePath: DIR });
  await succeed("create_material", { name: "M_McSwitch", packagePath: DIR });
  const built = await succeed("build_material_graph", {
    assetPath: MATERIAL,
    nodes: [
      { name: "Tex", class: "TextureSample", texturePath: `${RED}.T_McRed` },
      { name: "Black", class: "Constant3Vector", value: { r: 0, g: 0, b: 0 } },
      { name: "UseTex", class: "StaticSwitchParameter", parameterName: "UseTex" },
    ],
    connections: [{ from: "Tex", to: "UseTex", toInput: 0 }, { from: "Black", to: "UseTex", toInput: 1 }],
    propertyConnections: [{ property: "EmissiveColor", from: "UseTex" }],
    materialSettings: { ShadingModel: "MSM_Unlit" },
  });
  expect(built.compiled).toBe(true);
  textureNode = (built.nodes as Json[]).find((n) => n.name === "Tex")!.expressionIndex as number;
  await succeed("create_material_instance", { parentPath: MATERIAL, name: "MI_McSwitch", packagePath: DIR });
  await succeed("duplicate_asset", { sourcePath: "/Engine/BasicShapes/Cube.Cube", destinationPath: MESH });
  await succeed("set_mesh_material", { assetPath: MESH, materialPath: INSTANCE, slotIndex: 0 });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    for (const assetPath of ALL_ASSETS) await callBridge(bridge, "delete_asset", { assetPath, force: true });
    disconnectBridge();
  }
  if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
}, 120_000);

describe("static-switch instances", () => {
  it("set_material_static_switch compiles the instance's own permutation and says so", async () => {
    const result = await succeed("set_material_static_switch", { assetPath: INSTANCE, parameterName: "UseTex", value: true });
    expect(result).toMatchObject({ compileChecked: true, compiled: true, compileErrors: [] });
    expect(result.shaderState).toMatchObject({ staticPermutation: true, validShaderMap: true });
  }, 180_000);

  it("render_mesh_studio renders a mesh whose instance overrides a static switch", async () => {
    // A copy made by duplicate_asset has compiled none of its permutation's shaders, which is the
    // state every such instance is in until something draws it.
    await succeed("duplicate_asset", { sourcePath: INSTANCE, destinationPath: INSTANCE_COPY });
    await succeed("duplicate_asset", { sourcePath: "/Engine/BasicShapes/Cube.Cube", destinationPath: MESH_COPY });
    await succeed("set_mesh_material", { assetPath: MESH_COPY, materialPath: INSTANCE_COPY, slotIndex: 0 });
    const { row, centre } = await studio("switch_red", MESH_COPY);
    expect(row.status, JSON.stringify(row)).toBe("rendered");
    // The instance takes the texture branch: red, not the black branch or the default material's grey.
    expect(centre[0]).toBeGreaterThan(150);
    expect(centre[1]).toBeLessThan(80);
    expect(centre[2]).toBeLessThan(80);
  }, 180_000);

  it("set_expression_value swaps the texture, and the instance draws the new one", async () => {
    const result = await succeed("set_expression_value", { materialPath: MATERIAL, expressionIndex: textureNode, texturePath: `${GREEN}.T_McGreen` });
    expect(result).toMatchObject({ compileChecked: true, compiled: true, compileErrors: [] });
    const state = result.shaderState as Json;
    expect(state.validShaderMap).toBe(true);
    expect(state.childrenWithoutShaderMap).toEqual([]);
    expect(state.staticPermutationChildren).toBeGreaterThanOrEqual(1);
    const { row, centre } = await studio("switch_green");
    expect(row.status, JSON.stringify(row)).toBe("rendered");
    expect(centre[1]).toBeGreaterThan(150);
    expect(centre[0]).toBeLessThan(80);
  }, 180_000);
});

describe("texture settings and the materials sampling the texture", () => {
  it("a dry run lists the sampler type it would change and changes nothing", async () => {
    const result = await succeed("set_texture_settings", { assetPath: GREEN, sRGB: false, dryRun: true });
    const fixes = result.samplerFixes as Json[];
    expect(fixes).toEqual([expect.objectContaining({
      assetPath: expect.stringContaining("M_McSwitch"), from: "SAMPLERTYPE_Color", to: "SAMPLERTYPE_LinearColor", applied: false,
    })]);
    expect(result.recompiledMaterials).toBeUndefined();
    expect(await exported()).not.toContain("LinearColor");
  }, 120_000);

  it("turning a texture linear fixes the sampler, recompiles the material once and keeps it drawing", async () => {
    const result = await succeed("set_texture_settings", { assetPath: GREEN, sRGB: false, save: true });
    expect(result.samplerFixes).toEqual([expect.objectContaining({ from: "SAMPLERTYPE_Color", to: "SAMPLERTYPE_LinearColor", applied: true })]);
    const recompiled = result.recompiledMaterials as Json[];
    const row = recompiled.find((r) => String(r.materialPath).includes("M_McSwitch")) as Json;
    expect(row).toMatchObject({ samplerFixed: true, compileChecked: true, compiled: true, compileErrors: [] });
    expect((row.shaderState as Json).childrenWithoutShaderMap).toEqual([]);
    expect(await exported()).toContain("LinearColor");
    const validated = await succeed("validate_material", { assetPath: MATERIAL });
    expect(validated).toMatchObject({ compiled: true, valid: true });
    const { row: outcome, centre } = await studio("linear_green");
    expect(outcome.status, JSON.stringify(outcome)).toBe("rendered");
    expect(centre[1]).toBeGreaterThan(150);
  }, 240_000);

  it("with fixSamplers off, a material left on the wrong sampler fails the call and names itself", async () => {
    const result = await call("set_texture_settings", { assetPath: GREEN, sRGB: true, fixSamplers: false });
    expect(result.success).toBe(false);
    expect(String(result.error)).toContain("M_McSwitch");
    const row = (result.recompiledMaterials as Json[]).find((r) => String(r.materialPath).includes("M_McSwitch")) as Json;
    // The parent's own switch default never reaches the texture, so it compiles; the instance that
    // overrides the switch is what fails, and it is named with the compiler's text.
    expect(row).toMatchObject({ samplerFixed: false, compileChecked: true, compiled: false });
    // Both instances override the switch: the one on the mesh and its copy from the studio case.
    const failing = (row.instanceCompileErrors as Json[]).map((r) => String(r.instancePath)).sort();
    expect(failing).toEqual([expect.stringContaining("MI_McSwitch."), expect.stringContaining("MI_McSwitchCopy.")]);
    expect(JSON.stringify(row.instanceCompileErrors)).toMatch(/Sampler type is Linear Color, should be Color/);
    expect(result.samplerFixes).toEqual([expect.objectContaining({ applied: false, to: "SAMPLERTYPE_Color" })]);
    // Putting fixSamplers back on repairs it in the same call shape.
    const repaired = await succeed("set_texture_settings", { assetPath: GREEN, sRGB: true, save: true });
    expect((repaired.recompiledMaterials as Json[]).find((r) => String(r.materialPath).includes("M_McSwitch"))).toMatchObject({ compiled: true });
  }, 240_000);
});

describe("a material that does not compile", () => {
  it("fails each edit, recompile and the reads report it, in one shape", async () => {
    await succeed("create_material", { name: "M_McBroken", packagePath: DIR });
    const built = await call("build_material_graph", {
      assetPath: BROKEN,
      nodes: [{ name: "Bad", class: "Custom", code: "return mcp_undefined_symbol;", outputType: "float3" }],
      propertyConnections: [{ property: "EmissiveColor", from: "Bad" }],
    });
    // A graph edit keeps its success, since a graph is often finished over several calls, and says
    // in warning that the material does not compile, quoting the compiler.
    expect(built.success).not.toBe(false);
    expect(built).toMatchObject({ compileChecked: true, compiled: false });
    const errors = built.compileErrors as string[];
    expect(errors.length).toBeGreaterThan(0);
    expect(errors.join(" ")).toContain("mcp_undefined_symbol");
    expect(String(built.warning)).toContain("does not compile");
    const customIndex = (built.nodes as Json[]).find((n) => n.name === "Bad")!.expressionIndex as number;

    const recompiled = await call("recompile_material", { materialPath: BROKEN });
    expect(recompiled).toMatchObject({ success: false, compiled: false });
    expect((recompiled.compileErrors as string[]).join(" ")).toContain("mcp_undefined_symbol");

    const validated = await succeed("validate_material", { assetPath: BROKEN });
    expect(validated).toMatchObject({ valid: false, compiled: false });
    expect(validated.issues).toEqual(expect.arrayContaining([expect.objectContaining({ kind: "compile_error" })]));

    const stats = await succeed("get_material_shader_stats", { assetPath: BROKEN });
    expect(stats).toMatchObject({ compiled: false });
    expect(stats.compiledStats).toBeUndefined();

    const fixed = await succeed("set_custom_expression", { materialPath: BROKEN, expressionIndex: customIndex, code: "return float3(0, 1, 0);" });
    expect(fixed).toMatchObject({ compileChecked: true, compiled: true, compileErrors: [] });
    const fixedStats = await succeed("get_material_shader_stats", { assetPath: BROKEN });
    expect(fixedStats).toMatchObject({ compiled: true });
    expect((fixedStats.compiledStats as Json).pixelShaderInstructions).toBeGreaterThan(0);
  }, 240_000);
});
