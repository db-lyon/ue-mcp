import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
const bpPath = `${TEST_PREFIX}/BP_SmokeTest`;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: bpPath });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/BPI_SmokeTest` });
  disconnectBridge();
});

describe("blueprint — read helpers", () => {
  it("search_node_types", async () => {
    const r = await callBridge(bridge, "search_node_types", { query: "PrintString" });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_node_types", async () => {
    const r = await callBridge(bridge, "list_node_types", { category: "Utilities" });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("blueprint — full lifecycle", () => {
  it("create_blueprint", async () => {
    const r = await callBridge(bridge, "create_blueprint", { path: bpPath, parentClass: "Actor" });
    expect(r.ok, r.error).toBe(true);
  });

  it("read_blueprint", async () => {
    const r = await callBridge(bridge, "read_blueprint", { path: bpPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("add_variable", async () => {
    const r = await callBridge(bridge, "add_variable", { path: bpPath, name: "Health", type: "Float" });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_blueprint_variables", async () => {
    const r = await callBridge(bridge, "list_blueprint_variables", { path: bpPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("set_variable_properties", async () => {
    const r = await callBridge(bridge, "set_variable_properties", {
      path: bpPath, name: "Health",
      instanceEditable: true, category: "Stats", tooltip: "Player health",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_function", async () => {
    const r = await callBridge(bridge, "create_function", { path: bpPath, functionName: "TakeDamage" });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_blueprint_functions", async () => {
    const r = await callBridge(bridge, "list_blueprint_functions", { path: bpPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("add_node (PrintString)", async () => {
    const r = await callBridge(bridge, "add_node", {
      path: bpPath, graphName: "EventGraph",
      nodeClass: "K2Node_CallFunction",
      nodeParams: { FunctionReference: { MemberName: "PrintString" } },
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("read_blueprint_graph", async () => {
    const r = await callBridge(bridge, "read_blueprint_graph", { path: bpPath, graphName: "EventGraph" });
    expect(r.ok, r.error).toBe(true);
  });

  it("add_component (SceneComponent)", async () => {
    const r = await callBridge(bridge, "add_component", {
      path: bpPath, componentClass: "SceneComponent", componentName: "MyScene",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("add_event_dispatcher", async () => {
    const r = await callBridge(bridge, "add_event_dispatcher", { blueprintPath: bpPath, name: "OnHealthChanged" });
    expect(r.ok, r.error).toBe(true);
  });

  it("compile_blueprint", async () => {
    const r = await callBridge(bridge, "compile_blueprint", { path: bpPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("rename_function", async () => {
    const r = await callBridge(bridge, "rename_function", {
      path: bpPath, oldName: "TakeDamage", newName: "ApplyDamage",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("delete_function", async () => {
    const r = await callBridge(bridge, "delete_function", { path: bpPath, functionName: "ApplyDamage" });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("blueprint — interface", () => {
  it("create_blueprint_interface", async () => {
    const r = await callBridge(bridge, "create_blueprint_interface", { path: `${TEST_PREFIX}/BPI_SmokeTest` });
    expect(r.ok, r.error).toBe(true);
  });

  it("add_blueprint_interface", async () => {
    const r = await callBridge(bridge, "add_blueprint_interface", {
      blueprintPath: bpPath,
      interfacePath: `${TEST_PREFIX}/BPI_SmokeTest`,
    });
    expect(r.ok, r.error).toBe(true);
  });
});
