import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
const matPath = `${TEST_PREFIX}/M_SmokeTest`;
const miPath = `${TEST_PREFIX}/MI_SmokeTest`;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: miPath });
  await callBridge(bridge, "delete_asset", { assetPath: matPath });
  disconnectBridge();
});

describe("material — read helpers", () => {
  it("list_expression_types", async () => {
    const r = await callBridge(bridge, "list_expression_types");
    expect(r.ok, r.error).toBe(true);
  });
});

describe("material — full lifecycle", () => {
  it("create_material", async () => {
    const r = await callBridge(bridge, "create_material", { name: "M_SmokeTest", packagePath: TEST_PREFIX });
    expect(r.ok, r.error).toBe(true);
  });

  it("read_material", async () => {
    const r = await callBridge(bridge, "read_material", { assetPath: matPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("set_material_shading_model", async () => {
    const r = await callBridge(bridge, "set_material_shading_model", {
      assetPath: matPath, shadingModel: "DefaultLit",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("set_material_base_color", async () => {
    const r = await callBridge(bridge, "set_material_base_color", {
      assetPath: matPath, color: { r: 1.0, g: 0.2, b: 0.2, a: 1.0 },
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("add_material_expression (Constant3Vector)", async () => {
    const r = await callBridge(bridge, "add_material_expression", {
      materialPath: matPath, expressionType: "Constant3Vector",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_material_expressions", async () => {
    const r = await callBridge(bridge, "list_material_expressions", { materialPath: matPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_material_parameters", async () => {
    const r = await callBridge(bridge, "list_material_parameters", { assetPath: matPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("recompile_material", async () => {
    const r = await callBridge(bridge, "recompile_material", { materialPath: matPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_material_instance", async () => {
    const r = await callBridge(bridge, "create_material_instance", {
      parentPath: matPath, name: "MI_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });
});
