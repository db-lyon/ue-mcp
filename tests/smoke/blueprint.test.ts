import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
const bpPath = `${TEST_PREFIX}/BP_SmokeTest`;
const testAssets = [bpPath, `${TEST_PREFIX}/BPI_SmokeTest`];

beforeAll(async () => {
  bridge = await getBridge();
});
afterAll(async () => {
  for (const assetPath of testAssets) {
    await callBridge(bridge, "delete_asset", { assetPath }).catch(() => {});
  }
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

  it("connect_pins replaces links without losing them on failure", async () => {
    const addNode = async (nodeClass: string, nodeParams?: Record<string, unknown>) => {
      const response = await callBridge(bridge, "add_node", {
        path: bpPath, graphName: "EventGraph", nodeClass, nodeParams,
      });
      expect(response.ok, response.error).toBe(true);
      return (response.result as { nodeId: string }).nodeId;
    };

    const sourceA = await addNode("CustomEvent", { eventName: "MCPReplaceSourceA" });
    const sourceB = await addNode("CustomEvent", { eventName: "MCPReplaceSourceB" });
    const branch = await addNode("Branch");
    const connect = (sourceNode: string, sourcePin: string, extra: Record<string, unknown> = {}) =>
      callBridge(bridge, "connect_pins", {
        path: bpPath,
        graphName: "EventGraph",
        sourceNode,
        sourcePin,
        targetNode: branch,
        targetPin: "execute",
        ...extra,
      });

    const initial = await connect(sourceA, "then");
    expect(initial.ok, initial.error).toBe(true);

    const replaced = await connect(sourceB, "then", { breakExistingTarget: true });
    expect(replaced.ok, replaced.error).toBe(true);
    expect((replaced.result as { brokenTargetLinks: number }).brokenTargetLinks).toBe(1);

    const idempotent = await connect(sourceB, "then", { breakExistingTarget: true });
    expect(idempotent.ok, idempotent.error).toBe(true);
    expect((idempotent.result as { existed: boolean }).existed).toBe(true);
    expect((idempotent.result as { brokenTargetLinks: number }).brokenTargetLinks).toBe(0);

    const rejected = await connect(branch, "Condition", { breakExistingTarget: true });
    expect(rejected.ok, rejected.error).toBe(true);
    expect((rejected.result as { success: boolean }).success).toBe(false);

    const restored = await connect(sourceB, "then");
    expect(restored.ok, restored.error).toBe(true);
    expect((restored.result as { existed: boolean }).existed).toBe(true);
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

  it("add_function_parameter (input)", async () => {
    const r = await callBridge(bridge, "add_function_parameter", {
      path: bpPath, functionName: "TakeDamage",
      parameterName: "Amount", parameterType: "Float",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("add_function_parameter (output)", async () => {
    const r = await callBridge(bridge, "add_function_parameter", {
      path: bpPath, functionName: "TakeDamage",
      parameterName: "Survived", parameterType: "Bool", isOutput: true,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("set_variable_default", async () => {
    const r = await callBridge(bridge, "set_variable_default", {
      path: bpPath, name: "Health", value: "100.0",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("compile_blueprint", async () => {
    const r = await callBridge(bridge, "compile_blueprint", { path: bpPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("export_nodes_t3d / import_nodes_t3d round-trip (#130)", async () => {
    const exportRes = await callBridge(bridge, "export_nodes_t3d", {
      path: bpPath, graphName: "EventGraph",
    });
    expect(exportRes.ok, exportRes.error).toBe(true);
    const t3d = (exportRes.result as { t3d: string }).t3d;
    expect(typeof t3d).toBe("string");
    expect(t3d.length).toBeGreaterThan(0);

    const importRes = await callBridge(bridge, "import_nodes_t3d", {
      path: bpPath, graphName: "EventGraph", t3d, posX: 800, posY: 200,
    });
    expect(importRes.ok, importRes.error).toBe(true);
    const ids = (importRes.result as { nodeIds: string[]; count: number }).nodeIds;
    expect(Array.isArray(ids)).toBe(true);
    expect(ids.length).toBeGreaterThan(0);

    for (const nodeId of ids) {
      await callBridge(bridge, "delete_node", {
        path: bpPath, graphName: "EventGraph", nodeName: nodeId,
      }).catch(() => {});
    }
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

  it("delete_variable", async () => {
    const r = await callBridge(bridge, "delete_variable", { path: bpPath, name: "Health" });
    expect(r.ok, r.error).toBe(true);
  });

  it("remove_component", async () => {
    const r = await callBridge(bridge, "remove_component", {
      path: bpPath, componentName: "MyScene",
    });
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
