// @live-editor headless
/**
 * StateTree node authoring by ID and binding introspection: reordering nodes,
 * reading one node, its typed flags, writing node and instance data, binding
 * discovery and checks, and promote to parameter. Each effect is read back
 * through read_state, read_node, the parameter list or the binding list.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/StateTree";
const ST = `${DIR}/ST_BCovAuthoring`;
const DELAY = "/Script/StateTreeModule.StateTreeDelayTask";
const COMPARE_INT = "/Script/StateTreeModule.StateTreeCompareIntCondition";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const guid = (g: unknown): string => String(g).replace(/-/g, "").toLowerCase();

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function refused(method: string, params: Json): Promise<string> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method} should have been refused: ${JSON.stringify(r.result)}`).toBe(false);
  return String(r.error ?? body(r).error ?? JSON.stringify(r.result));
}

async function addState(name: string, extra: Json = {}): Promise<string> {
  return String((await call("add_state_tree_state", { assetPath: ST, name, ...extra })).stateId);
}

const readState = (stateId: string): Promise<Json> => call("read_state_tree_state", { assetPath: ST, stateId });
const readNode = (nodeId: string): Promise<Json> => call("read_state_tree_node", { assetPath: ST, nodeId });

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
  await call("create_state_tree", { name: "ST_BCovAuthoring", packagePath: DIR });
  await call("set_state_tree_root_parameters", {
    assetPath: ST, parameters: [{ name: "BCovSpeed", type: "float" }, { name: "BCovLabel", type: "name" }],
  });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
    disconnectBridge();
  }
});

describe("statetree nodes by id", () => {
  it("move_state_tree_node reorders tasks and enter conditions and keeps their ids", async () => {
    const id = await addState("BCov_Move");
    const a = String((await call("add_state_tree_task", { assetPath: ST, stateId: id, structType: DELAY })).nodeId);
    const b = String((await call("add_state_tree_task", { assetPath: ST, stateId: id, structType: DELAY })).nodeId);
    const c1 = String((await call("add_state_tree_enter_condition", { assetPath: ST, stateId: id, structType: COMPARE_INT })).nodeId);
    const c2 = String((await call("add_state_tree_enter_condition", { assetPath: ST, stateId: id, structType: COMPARE_INT })).nodeId);

    const moved = await call("move_state_tree_node", { assetPath: ST, nodeId: b, newIndex: 0 });
    expect(moved.moved).toBe(true);
    expect(moved.previousIndex).toBe(1);
    expect(moved.container).toBe("tasks");
    expect((moved.rollback as Json).method).toBe("move_state_tree_node");
    await call("move_state_tree_node", { assetPath: ST, nodeId: c1, newIndex: 1 });

    const st = await readState(id);
    expect((st.tasks as Json[]).map((t) => t.nodeId)).toEqual([b, a]);
    expect((st.enterConditions as Json[]).map((t) => t.nodeId)).toEqual([c2, c1]);

    await refused("move_state_tree_node", { assetPath: ST, nodeId: a, newIndex: 5 });
  }, 120_000);

  it("read_state_tree_node reads a task where it lives, with its data and flags", async () => {
    const id = await addState("BCov_Read");
    const task = String((await call("add_state_tree_task", {
      assetPath: ST, stateId: id, structType: DELAY, instanceProperties: { Duration: 2.5 },
    })).nodeId);

    const node = await readNode(task);
    expect(node.kind).toBe("task");
    expect(node.container).toBe("tasks");
    expect(node.index).toBe(0);
    expect(node.stateId).toBe(id);
    expect(node.structType).toBe("StateTreeDelayTask");
    expect(Number((node.instanceProperties as Json).Duration)).toBeCloseTo(2.5, 3);
    expect(node.taskEnabled).toBe(true);
    expect(typeof node.consideredForCompletion).toBe("boolean");
    expect(String(node.instanceStructId)).not.toBe("");

    // The tree-wide read lists the editor data extensions alongside the nodes.
    const tree = await call("read_state_tree", { assetPath: ST });
    expect(Array.isArray(tree.extensions)).toBe(true);
  }, 120_000);

  it("set_state_tree_node_flags writes taskEnabled, consideredForCompletion and a condition's evaluationMode", async () => {
    const id = await addState("BCov_Flags");
    const task = String((await call("add_state_tree_task", { assetPath: ST, stateId: id, structType: DELAY })).nodeId);
    const cond = String((await call("add_state_tree_enter_condition", { assetPath: ST, stateId: id, structType: COMPARE_INT })).nodeId);

    const off = await call("set_state_tree_node_flags", { assetPath: ST, nodeId: task, taskEnabled: false });
    expect(((off.rollback as Json).payload as Json).taskEnabled).toBe(true);
    expect((await readNode(task)).taskEnabled).toBe(false);

    // A disabled task's completion flag cannot change, as in the editor.
    await refused("set_state_tree_node_flags", { assetPath: ST, nodeId: task, consideredForCompletion: false });

    await call("set_state_tree_node_flags", { assetPath: ST, nodeId: task, taskEnabled: true, consideredForCompletion: false });
    let node = await readNode(task);
    expect(node.taskEnabled).toBe(true);
    expect(node.consideredForCompletion).toBe(false);

    await call("set_state_tree_node_flags", { assetPath: ST, nodeId: cond, evaluationMode: "ForcedTrue" });
    node = await readNode(cond);
    expect(node.kind).toBe("condition");
    expect(node.evaluationMode).toBe("ForcedTrue");

    // Task flags on a condition are refused.
    await refused("set_state_tree_node_flags", { assetPath: ST, nodeId: cond, taskEnabled: false });
  }, 120_000);

  it("set_state_tree_node_data writes a condition's instance and node data", async () => {
    const id = await addState("BCov_Data");
    const cond = String((await call("add_state_tree_enter_condition", { assetPath: ST, stateId: id, structType: COMPARE_INT })).nodeId);

    const res = await call("set_state_tree_node_data", { assetPath: ST, nodeId: cond, propertyName: "Right", value: "7" });
    expect((res.rollback as Json).method).toBe("set_state_tree_node_data");
    await call("set_state_tree_node_data", { assetPath: ST, nodeId: cond, target: "node", propertyName: "bInvert", value: "true" });

    const node = await readNode(cond);
    expect(Number((node.instanceProperties as Json).Right)).toBe(7);
    expect(String((node.nodeProperties as Json).bInvert).toLowerCase()).toBe("true");

    await refused("set_state_tree_node_data", { assetPath: ST, nodeId: cond, propertyName: "NoSuchProperty", value: "1" });
  }, 120_000);
});

describe("statetree binding introspection", () => {
  let stateId = "";
  let taskInstanceId = "";
  let speedOption: Json | undefined;

  beforeAll(async () => {
    stateId = await addState("BCov_Bind");
    const task = String((await call("add_state_tree_task", {
      assetPath: ST, stateId, structType: DELAY, instanceProperties: { Duration: 3.5 },
    })).nodeId);
    taskInstanceId = String((await readNode(task)).instanceStructId);
  }, 120_000);

  it("list_state_tree_bindable_properties lists a task's properties and the sources compatible with one", async () => {
    const props = await call("list_state_tree_bindable_properties", { assetPath: ST, structId: taskInstanceId });
    const duration = (props.properties as Json[]).find((p) => p.path === "Duration");
    expect(duration?.type).toBe("float");

    const opts = await call("list_state_tree_bindable_properties", { assetPath: ST, structId: taskInstanceId, targetPath: "Duration" });
    expect(opts.targetType).toBe("float");
    const options = opts.options as Json[];
    speedOption = options.find((o) => o.sourcePath === "BCovSpeed");
    expect(speedOption, JSON.stringify(options)).toBeDefined();
    expect(speedOption!.compatibility).toBe("compatible");
    // A Name parameter cannot be copied into a float.
    expect(options.some((o) => o.sourcePath === "BCovLabel")).toBe(false);
  }, 120_000);

  it("check_state_tree_binding grades a pair and resolves a path", async () => {
    expect(speedOption).toBeDefined();
    const rootId = String(speedOption!.sourceStructId);

    const ok = await call("check_state_tree_binding", {
      assetPath: ST, targetStructId: taskInstanceId, targetPath: "Duration", sourceStructId: rootId, sourcePath: "BCovSpeed",
    });
    expect(ok.canBind).toBe(true);
    expect(ok.inScope).toBe(true);
    expect(ok.compatibility).toBe("compatible");

    const bad = await call("check_state_tree_binding", {
      assetPath: ST, targetStructId: taskInstanceId, targetPath: "Duration", sourceStructId: rootId, sourcePath: "BCovLabel",
    });
    expect(bad.canBind).toBe(false);
    expect(bad.compatibility).toBe("incompatible");
    expect((bad.reasons as string[]).length).toBeGreaterThan(0);

    const missing = await call("check_state_tree_binding", { assetPath: ST, targetStructId: taskInstanceId, targetPath: "NoSuchProperty" });
    expect(missing.resolved).toBe(false);

    // Checking writes nothing.
    const bindings = (await call("list_state_tree_bindings", { assetPath: ST, structId: taskInstanceId })).bindings as Json[];
    expect(bindings).toHaveLength(0);
  }, 120_000);

  it("promote_state_tree_property_to_parameter creates a state parameter with the value and binds the target to it", async () => {
    const res = await call("promote_state_tree_property_to_parameter", {
      assetPath: ST, targetStructId: taskInstanceId, targetPath: "Duration", parameterName: "BCovPromoted", stateId,
    });
    expect(res.scope).toBe("state");
    const name = String(res.parameterName);
    expect(name.startsWith("BCovPromoted")).toBe(true);

    const params = (await call("list_state_tree_state_parameters", { assetPath: ST, stateId })).parameters as Json[];
    const param = params.find((p) => p.name === name);
    expect(param, JSON.stringify(params)).toBeDefined();
    expect(Number(param!.value)).toBeCloseTo(3.5, 3);

    const bindings = (await call("list_state_tree_bindings", { assetPath: ST, structId: taskInstanceId })).bindings as Json[];
    const bound = bindings.find((b) => b.targetPath === "Duration");
    expect(bound?.sourcePath).toBe(name);
    expect(guid(bound?.sourceStructId)).toBe(guid(res.parameterStructId));
  }, 120_000);
});
