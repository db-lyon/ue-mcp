// @live-editor render
/**
 * Material asset settings and graph edits, each read back through read_material,
 * list_material_expressions, export_material_graph or the asset property reader.
 * Covers simple/built materials, wiring, validation, stats, preview and transactions.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Material/Graph`;
const M_PROPS = `${DIR}/M_BCov_Props`;
const M_WIRE = `${DIR}/M_BCov_Wire`;
const M_SIMPLE = `${DIR}/M_BCov_Simple`;
const M_TEX = `${DIR}/M_BCov_Tex`;
const M_VALIDATE = `${DIR}/M_BCov_Validate`;
const M_STATS = `${DIR}/M_BCov_Stats`;
const M_IMPORT = `${DIR}/M_BCov_Import`;
const M_DUP = `${DIR}/M_BCov_Dup`;
const M_BUILT = `${DIR}/M_BCov_Built`;
const MI_BUILT = `${DIR}/MI_BCov_Built`;
const TEXTURE = "/Engine/EngineResources/DefaultTexture";
const PREVIEW_FILE = path.join(os.tmpdir(), "bcov-material-preview.png").replace(/\\/g, "/");
const ALL_ASSETS = [MI_BUILT, M_BUILT, M_DUP, M_IMPORT, M_STATS, M_VALIDATE, M_TEX, M_SIMPLE, M_WIRE, M_PROPS];

type Json = Record<string, unknown>;
type Expr = { index: number; class: string; value?: unknown; texturePath?: string; inputs?: Array<Json> };
type Conn = { expressionClass: string; expressionIndex?: number } | null;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function readMaterial(assetPath: string): Promise<{ expressions: Expr[]; connections: Record<string, Conn>; raw: Json }> {
  const raw = await call("read_material", { assetPath });
  return { expressions: raw.expressions as Expr[], connections: raw.connections as Record<string, Conn>, raw };
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const assetPath of ALL_ASSETS) await callBridge(bridge, "delete_asset", { assetPath, force: true });
  for (const name of ["M_BCov_Props", "M_BCov_Wire", "M_BCov_Tex", "M_BCov_Validate", "M_BCov_Stats", "M_BCov_Import"]) {
    await call("create_material", { name, packagePath: DIR });
  }
}, 180_000);

afterAll(async () => {
  if (bridge) {
    for (const assetPath of ALL_ASSETS) await callBridge(bridge, "delete_asset", { assetPath, force: true });
    try { fs.rmSync(PREVIEW_FILE, { force: true }); } catch { /* already gone */ }
    disconnectBridge();
  }
}, 120_000);

describe("material settings", () => {
  it("list_expression_types pages the curated expression classes", async () => {
    const page = await call("list_expression_types", { limit: 5 });
    const types = page.expressionTypes as string[];
    expect(types).toHaveLength(5);
    expect(types[0]).toBe("MaterialExpressionConstant");
    const all = await call("list_expression_types", { limit: 1000 });
    expect(all.expressionTypes as string[]).toContain("MaterialExpressionLinearInterpolate");
  }, 60_000);

  it("set_material_base_color wires a Constant3Vector into BaseColor", async () => {
    await call("set_material_base_color", { assetPath: M_PROPS, color: { r: 0.25, g: 0.5, b: 0.75 } });
    const { expressions, connections } = await readMaterial(M_PROPS);
    const base = connections.BaseColor;
    expect(base?.expressionClass).toBe("MaterialExpressionConstant3Vector");
    const node = expressions.find((e) => e.index === base?.expressionIndex);
    expect((node?.value as { r: number }).r).toBeCloseTo(0.25, 3);
    expect((node?.value as { b: number }).b).toBeCloseTo(0.75, 3);
  }, 120_000);

  it("set_material_shading_model and set_material_blend_mode read back from read_material", async () => {
    await call("set_material_shading_model", { assetPath: M_PROPS, shadingModel: "Unlit" });
    await call("set_material_blend_mode", { assetPath: M_PROPS, blendMode: "Translucent" });
    const { raw } = await readMaterial(M_PROPS);
    expect(raw.shadingModel).toBe("Unlit");
    expect(String(raw.blendMode)).toContain("Translucent");
  }, 120_000);

  it("set_material_domain changes MaterialDomain on the asset", async () => {
    await call("set_material_domain", { assetPath: M_PROPS, materialDomain: "PostProcess" });
    const prop = await call("get_asset_properties", { assetPath: M_PROPS, propertyName: "MaterialDomain" });
    expect(String(prop.value)).toContain("PostProcess");
    await call("set_material_domain", { assetPath: M_PROPS, materialDomain: "Surface" });
    const back = await call("get_asset_properties", { assetPath: M_PROPS, propertyName: "MaterialDomain" });
    expect(String(back.value)).toContain("Surface");
  }, 120_000);

  it("create_material_simple wires constants into BaseColor, Roughness and Metallic", async () => {
    await call("create_material_simple", {
      name: "M_BCov_Simple", packagePath: DIR, baseColor: { r: 0.1, g: 0.2, b: 0.3 }, roughness: 0.6, metallic: 1,
    });
    const { expressions, connections } = await readMaterial(M_SIMPLE);
    const rough = connections.Roughness;
    expect(rough?.expressionClass).toBe("MaterialExpressionConstant");
    expect(Number(expressions.find((e) => e.index === rough?.expressionIndex)?.value)).toBeCloseTo(0.6, 3);
    expect(connections.Metallic?.expressionClass).toBe("MaterialExpressionConstant");
    const base = expressions.find((e) => e.index === connections.BaseColor?.expressionIndex);
    expect((base?.value as { g: number }).g).toBeCloseTo(0.2, 3);
  }, 120_000);

  it("connect_texture_to_material adds a TextureSample on the property", async () => {
    await call("connect_texture_to_material", { materialPath: M_TEX, texturePath: TEXTURE, property: "BaseColor" });
    const { expressions, connections } = await readMaterial(M_TEX);
    expect(connections.BaseColor?.expressionClass).toBe("MaterialExpressionTextureSample");
    const sample = expressions.find((e) => e.index === connections.BaseColor?.expressionIndex);
    expect(sample?.texturePath).toContain("DefaultTexture");
  }, 120_000);
});

describe("material graph edits", () => {
  let constantIndex = -1;

  it("build_material_graph creates named nodes and wires a property", async () => {
    const built = await call("build_material_graph", {
      assetPath: M_WIRE,
      nodes: [
        { name: "K", class: "Constant", value: 0.25 },
        { name: "Mul", class: "Multiply" },
      ],
      propertyConnections: [{ property: "BaseColor", from: "Mul" }],
    });
    expect(built.expressionsCreated).toBe(2);
    const { expressions, connections } = await readMaterial(M_WIRE);
    expect(expressions.map((e) => e.class)).toEqual(expect.arrayContaining(["MaterialExpressionConstant", "MaterialExpressionMultiply"]));
    expect(connections.BaseColor?.expressionClass).toBe("MaterialExpressionMultiply");
  }, 120_000);

  it("connect_material_expressions wires one node into another's input", async () => {
    await call("connect_material_expressions", { materialPath: M_WIRE, sourceExpression: "K", targetExpression: "Mul", targetInput: "A" });
    const { expressions } = await readMaterial(M_WIRE);
    const mul = expressions.find((e) => e.class === "MaterialExpressionMultiply");
    const inputA = mul?.inputs?.find((i) => i.inputName === "A");
    expect(inputA?.connectedExpressionClass).toBe("MaterialExpressionConstant");
  }, 120_000);

  it("set_expression_value changes a Constant's value", async () => {
    const listed = await call("list_material_expressions", { materialPath: M_WIRE, limit: 100 });
    const row = (listed.expressions as Array<{ nodeId: string; name: string }>).find((e) => e.name === "K");
    expect(row).toBeDefined();
    constantIndex = Number(row!.nodeId);
    await call("set_expression_value", { materialPath: M_WIRE, expressionIndex: constantIndex, value: 0.75 });
    const { expressions } = await readMaterial(M_WIRE);
    expect(Number(expressions.find((e) => e.index === constantIndex)?.value)).toBeCloseTo(0.75, 3);
  }, 120_000);

  it("disconnect_material_property clears the property input", async () => {
    await call("disconnect_material_property", { materialPath: M_WIRE, property: "BaseColor" });
    const { connections } = await readMaterial(M_WIRE);
    expect(connections.BaseColor).toBeNull();
  }, 120_000);

  it("delete_material_expression removes the node and the wire into it", async () => {
    await call("delete_material_expression", { materialPath: M_WIRE, expressionName: "K" });
    const { expressions } = await readMaterial(M_WIRE);
    expect(expressions.some((e) => e.class === "MaterialExpressionConstant")).toBe(false);
    const mul = expressions.find((e) => e.class === "MaterialExpressionMultiply");
    expect(mul?.inputs?.find((i) => i.inputName === "A")?.connectedExpressionClass).toBeUndefined();
  }, 120_000);

  it("set_custom_expression rebuilds a Custom node's code and input pins", async () => {
    const added = await call("add_material_expression", { materialPath: M_WIRE, expressionType: "Custom" });
    const index = Number(added.expressionIndex);
    const set = await call("set_custom_expression", {
      materialPath: M_WIRE, expressionIndex: index, code: "return Alpha * Beta;", inputs: ["Alpha", "Beta"], outputType: "float1",
    });
    expect(set.code).toBe("return Alpha * Beta;");
    const listed = await call("list_material_expressions", { materialPath: M_WIRE, includeInputs: true, limit: 100 });
    const row = (listed.expressions as Array<{ nodeId: string; inputs: Array<{ inputName: string }> }>).find((e) => e.nodeId === String(index));
    expect(row?.inputs.map((i) => i.inputName)).toEqual(["Alpha", "Beta"]);
  }, 120_000);
});

describe("whole-material operations", () => {
  it("validate_material reports an orphan node until it is removed", async () => {
    await call("build_material_graph", {
      assetPath: M_VALIDATE,
      nodes: [
        { name: "Orphan", class: "Constant", value: 1 },
        { name: "Col", class: "Constant3Vector", value: { r: 1, g: 0, b: 0 } },
      ],
      propertyConnections: [{ property: "BaseColor", from: "Col" }],
    });
    const before = await call("validate_material", { assetPath: M_VALIDATE });
    expect(before.valid).toBe(false);
    const issues = before.issues as Array<{ kind: string; class?: string }>;
    expect(issues.some((i) => i.kind === "orphan_expression" && i.class === "MaterialExpressionConstant")).toBe(true);
    await call("delete_material_expression", { materialPath: M_VALIDATE, expressionName: "Orphan" });
    const after = await call("validate_material", { assetPath: M_VALIDATE });
    expect(after.valid).toBe(true);
    expect(after.reachableCount).toBe(1);
  }, 120_000);

  it("get_material_shader_stats counts the parameters the graph declares", async () => {
    await call("build_material_graph", {
      assetPath: M_STATS,
      nodes: [
        { name: "RoughA", class: "ScalarParameter", parameterName: "RoughA", defaultValue: 0.5 },
        { name: "RoughB", class: "ScalarParameter", parameterName: "RoughB", defaultValue: 0.5 },
        { name: "Mul", class: "Multiply" },
        { name: "Tint", class: "VectorParameter", parameterName: "Tint" },
        { name: "Tex", class: "TextureSample", texturePath: TEXTURE },
      ],
      connections: [
        { from: "RoughA", to: "Mul", toInput: "A" },
        { from: "RoughB", to: "Mul", toInput: "B" },
      ],
      propertyConnections: [
        { property: "Roughness", from: "Mul" },
        { property: "BaseColor", from: "Tint" },
        { property: "EmissiveColor", from: "Tex" },
      ],
    });
    const stats = await call("get_material_shader_stats", { assetPath: M_STATS });
    expect(stats.scalarParameterCount).toBe(2);
    expect(stats.vectorParameterCount).toBe(1);
    expect(stats.referencedTextureCount).toBe(1);
  }, 120_000);

  it("import_material_graph adds nodes that export_material_graph reads back", async () => {
    await call("import_material_graph", {
      assetPath: M_IMPORT,
      nodes: [
        { name: "Tint", class: "VectorParameter", parameterName: "Tint" },
        { name: "Half", class: "Constant", value: 0.5 },
        { name: "Mul", class: "Multiply" },
      ],
      connections: [
        { from: "Tint", to: "Mul", toInput: "A" },
        { from: "Half", to: "Mul", toInput: "B" },
      ],
      propertyConnections: [{ property: "BaseColor", from: "Mul" }],
    });
    const exported = await call("export_material_graph", { assetPath: M_IMPORT });
    const nodes = exported.nodes as Array<{ class: string; value?: number }>;
    expect(nodes).toHaveLength(3);
    expect(nodes.map((n) => n.class)).toEqual(expect.arrayContaining(["MaterialExpressionVectorParameter", "MaterialExpressionMultiply"]));
    expect(nodes.find((n) => n.class === "MaterialExpressionConstant")?.value).toBeCloseTo(0.5, 3);
    expect(exported.connections as unknown[]).toHaveLength(2);
    const props = exported.propertyConnections as Array<{ property: string }>;
    expect(props.map((p) => p.property)).toContain("BaseColor");
  }, 120_000);

  it("duplicate_material copies the graph to a new asset", async () => {
    const dup = await call("duplicate_material", { sourcePath: M_IMPORT, destinationPath: M_DUP });
    expect(String(dup.destinationPath)).toContain("M_BCov_Dup");
    const { raw, connections } = await readMaterial(M_DUP);
    expect(raw.expressionCount).toBe(3);
    expect(connections.BaseColor?.expressionClass).toBe("MaterialExpressionMultiply");
  }, 120_000);

  it("build_material creates a parameterised material and an instance with overrides", async () => {
    await call("build_material", {
      name: "M_BCov_Built",
      packagePath: DIR,
      parameters: [
        { name: "Tint", type: "vector", default: { r: 0, g: 1, b: 0, a: 1 } },
        { name: "Rough", type: "scalar", default: 0.4 },
      ],
      propertyConnections: [
        { property: "BaseColor", from: "Tint" },
        { property: "Roughness", from: "Rough" },
      ],
      instances: [{ name: "MI_BCov_Built", packagePath: DIR, parameters: { Rough: 0.9 } }],
    });
    const { connections } = await readMaterial(M_BUILT);
    expect(connections.BaseColor?.expressionClass).toBe("MaterialExpressionVectorParameter");
    expect(connections.Roughness?.expressionClass).toBe("MaterialExpressionScalarParameter");
    const mi = await call("read_material_instance", { assetPath: MI_BUILT });
    const rough = (mi.scalarOverrides as Array<{ name: string; value: number }>).find((p) => p.name === "Rough");
    expect(rough?.value).toBeCloseTo(0.9, 3);
  }, 180_000);

  it("render_material_preview writes a PNG of the material", async () => {
    try { fs.rmSync(PREVIEW_FILE, { force: true }); } catch { /* none yet */ }
    const out = await call("render_material_preview", { assetPath: M_SIMPLE, outputPath: PREVIEW_FILE, width: 64, height: 64 });
    expect(out.width).toBe(64);
    expect(fs.existsSync(PREVIEW_FILE)).toBe(true);
    const bytes = fs.readFileSync(PREVIEW_FILE);
    expect(bytes.subarray(1, 4).toString("ascii")).toBe("PNG");
  }, 120_000);

  it("begin and end_material_transaction record one named undo step", async () => {
    const label = `BCov material txn ${Date.now()}`;
    const begun = await call("begin_material_transaction", { label });
    expect(begun.label).toBe(label);
    await call("set_material_blend_mode", { assetPath: M_PROPS, blendMode: "Masked" });
    const ended = await call("end_material_transaction", {});
    expect(ended.committed).toBe(true);
    const history = await call("get_transaction_history", { maxEntries: 5 });
    const titles = (history.transactions as Array<{ title: string }>).map((t) => t.title);
    expect(titles).toContain(label);
  }, 120_000);
});
