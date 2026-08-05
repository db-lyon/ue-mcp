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

describe("material - read helpers", () => {
  it("list_expression_types", async () => {
    const r = await callBridge(bridge, "list_expression_types");
    expect(r.ok, r.error).toBe(true);
  });
});

describe("material - full lifecycle", () => {
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

describe("material - expression connections in read (#44)", () => {
  const connMatPath = `${TEST_PREFIX}/M_ConnTest`;

  afterAll(async () => {
    await callBridge(bridge, "delete_asset", { assetPath: connMatPath });
  });

  it("read returns expression-to-expression inputs", async () => {
    // Create a material with two connected expressions
    await callBridge(bridge, "create_material", { name: "M_ConnTest", packagePath: TEST_PREFIX });
    const addMul = await callBridge(bridge, "add_material_expression", {
      materialPath: connMatPath, expressionType: "Multiply",
    });
    expect(addMul.ok, addMul.error).toBe(true);

    const addConst = await callBridge(bridge, "add_material_expression", {
      materialPath: connMatPath, expressionType: "Constant",
    });
    expect(addConst.ok, addConst.error).toBe(true);

    // Wire Constant -> Multiply input A
    const conn = await callBridge(bridge, "connect_material_expressions", {
      materialPath: connMatPath,
      sourceExpression: "Constant",
      targetExpression: "Multiply",
      targetInput: "A",
    });
    expect(conn.ok, conn.error).toBe(true);

    // Read and verify internal connections appear
    const read = await callBridge(bridge, "read_material", { assetPath: connMatPath });
    expect(read.ok, read.error).toBe(true);
    const result = read.result as Record<string, unknown>;
    const expressions = result.expressions as Array<Record<string, unknown>>;
    const multiply = expressions.find((e) => (e.class as string)?.includes("Multiply"));
    expect(multiply).toBeDefined();
    expect(multiply!.inputs).toBeDefined();
    const inputs = multiply!.inputs as Array<Record<string, unknown>>;
    const connectedInput = inputs.find((i) => i.connectedExpressionIndex !== undefined);
    expect(connectedInput).toBeDefined();
  });
});

describe("material - delete_expression cleans up references (#44)", () => {
  const delMatPath = `${TEST_PREFIX}/M_DelTest`;

  afterAll(async () => {
    await callBridge(bridge, "delete_asset", { assetPath: delMatPath });
  });

  it("deleting wired expression does not leave ghost references", async () => {
    await callBridge(bridge, "create_material", { name: "M_DelTest", packagePath: TEST_PREFIX });

    // Add Multiply, Constant, wire them together
    await callBridge(bridge, "add_material_expression", {
      materialPath: delMatPath, expressionType: "Multiply",
    });
    await callBridge(bridge, "add_material_expression", {
      materialPath: delMatPath, expressionType: "Constant",
    });
    await callBridge(bridge, "connect_material_expressions", {
      materialPath: delMatPath,
      sourceExpression: "Constant",
      targetExpression: "Multiply",
      targetInput: "A",
    });

    // Delete the Constant (source)
    const del = await callBridge(bridge, "delete_material_expression", {
      materialPath: delMatPath, expressionName: "Constant",
    });
    expect(del.ok, del.error).toBe(true);

    // Read the graph - Multiply should still exist with no connected input
    const read = await callBridge(bridge, "read_material", { assetPath: delMatPath });
    expect(read.ok, read.error).toBe(true);
    const result = read.result as Record<string, unknown>;
    const expressions = result.expressions as Array<Record<string, unknown>>;
    const multiply = expressions.find((e) => (e.class as string)?.includes("Multiply"));
    expect(multiply).toBeDefined();
    const inputs = multiply!.inputs as Array<Record<string, unknown>>;
    // Input A should have no connected expression (cleaned up)
    const inputA = inputs?.find((i) => (i.inputName as string) === "A");
    if (inputA) {
      expect(inputA.connectedExpressionIndex).toBeUndefined();
    }

    // Verify recompile doesn't crash
    const recompile = await callBridge(bridge, "recompile_material", { materialPath: delMatPath });
    expect(recompile.ok, recompile.error).toBe(true);
  });
});
