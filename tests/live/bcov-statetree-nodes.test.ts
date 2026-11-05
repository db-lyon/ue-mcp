// @live-editor headless
/**
 * StateTree nodes: tasks, enter conditions, transitions and their conditions,
 * clearing a state, evaluators, global tasks, node-type discovery and a
 * Blueprint task's class, each read back through read_state or read. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/StateTree";
const ST = `${DIR}/ST_BCovNodes`;
const BP_TASK = `${DIR}/BP_BCovSTTask`;
const BP_TASK_CLASS = `${BP_TASK}.BP_BCovSTTask_C`;
const DELAY = "/Script/StateTreeModule.StateTreeDelayTask";
const COMPARE_INT = "/Script/StateTreeModule.StateTreeCompareIntCondition";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function addState(name: string, extra: Json = {}): Promise<string> {
  const res = await call("add_state_tree_state", { assetPath: ST, name, ...extra });
  return String(res.stateId);
}

async function readState(stateId: string): Promise<Json> {
  return call("read_state_tree_state", { assetPath: ST, stateId });
}

async function readTree(): Promise<Json> {
  return call("read_state_tree", { assetPath: ST });
}

const props = (node: Json, key: "instanceProperties" | "nodeProperties"): Json => (node[key] as Json) ?? {};

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: BP_TASK, force: true });
  await call("create_state_tree", { name: "ST_BCovNodes", packagePath: DIR });
  await call("create_blueprint", { assetPath: BP_TASK, parentClass: "/Script/StateTreeModule.StateTreeTaskBlueprintBase" });
  await call("compile_blueprint", { assetPath: BP_TASK });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
    await callBridge(bridge, "delete_asset", { assetPath: BP_TASK, force: true });
    disconnectBridge();
  }
});

describe("statetree state tasks and conditions", () => {
  it("tasks: add, set instance and node properties, remove", async () => {
    const id = await addState("BCov_Tasks");
    const added = await call("add_state_tree_task", { assetPath: ST, stateId: id, structType: DELAY, instanceProperties: { Duration: 4 } });
    expect(added.taskIndex).toBe(0);

    let task = (await readState(id)).tasks as Json[];
    expect(task).toHaveLength(1);
    expect(task[0].nodeId).toBe(added.nodeId);
    expect(task[0].structType).toBe("StateTreeDelayTask");
    expect(Number(props(task[0], "instanceProperties").Duration)).toBeCloseTo(4, 3);

    await call("set_state_tree_task_instance_property", { assetPath: ST, stateId: id, taskIndex: 0, propertyName: "Duration", value: "6.5" });
    await call("set_state_tree_task_property", { assetPath: ST, stateId: id, taskIndex: 0, propertyName: "bTaskEnabled", value: "false" });
    task = (await readState(id)).tasks as Json[];
    expect(Number(props(task[0], "instanceProperties").Duration)).toBeCloseTo(6.5, 3);
    expect(String(props(task[0], "nodeProperties").bTaskEnabled).toLowerCase()).toBe("false");

    const removed = await call("remove_state_tree_task", { assetPath: ST, stateId: id, taskIndex: 0 });
    expect(removed.structType).toBe("StateTreeDelayTask");
    expect((await readState(id)).tasks as Json[]).toHaveLength(0);
  }, 120_000);

  it("enter conditions: add with instance properties, remove", async () => {
    const id = await addState("BCov_Enter");
    const added = await call("add_state_tree_enter_condition", {
      assetPath: ST, stateId: id, structType: COMPARE_INT, instanceProperties: { Right: "7" }, operand: "Or",
    });
    expect(added.conditionIndex).toBe(0);
    let conds = (await readState(id)).enterConditions as Json[];
    expect(conds).toHaveLength(1);
    expect(conds[0].structType).toBe("StateTreeCompareIntCondition");
    expect(conds[0].operand).toBe("Or");
    expect(Number(props(conds[0], "instanceProperties").Right)).toBe(7);

    await call("remove_state_tree_enter_condition", { assetPath: ST, stateId: id, conditionIndex: 0 });
    conds = (await readState(id)).enterConditions as Json[];
    expect(conds).toHaveLength(0);
  }, 120_000);

  it("transitions and transition conditions: add, read back, remove", async () => {
    const from = await addState("BCov_From");
    const to = await addState("BCov_To");
    const added = await call("add_state_tree_transition", {
      assetPath: ST, stateId: from, trigger: "OnTick", transitionType: "GotoState", targetStateId: to,
      priority: "High", bDelayTransition: true, delayDuration: 2.5,
    });
    expect(added.transitionIndex).toBe(0);

    const node = ((await readTree()).subTrees as Json[]).find((s) => s.id === from) as Json;
    const trans = (node.transitions as Json[])[0];
    expect(trans.trigger).toBe("OnTick");
    expect(trans.transitionType).toBe("GotoState");
    expect(trans.targetStateId).toBe(to);
    let st = await readState(from);
    let t0 = (st.transitions as Json[])[0];
    expect(t0.priority).toBe("High");
    expect(t0.bDelayTransition).toBe(true);
    expect(Number(t0.delayDuration)).toBeCloseTo(2.5, 3);

    const cond = await call("add_state_tree_transition_condition", {
      assetPath: ST, stateId: from, transitionIndex: 0, structType: COMPARE_INT, instanceProperties: { Right: "3" },
    });
    expect(cond.conditionIndex).toBe(0);
    st = await readState(from);
    t0 = (st.transitions as Json[])[0];
    const tconds = t0.conditions as Json[];
    expect(tconds).toHaveLength(1);
    expect(Number(props(tconds[0], "instanceProperties").Right)).toBe(3);

    await call("remove_state_tree_transition_condition", { assetPath: ST, stateId: from, transitionIndex: 0, conditionIndex: 0 });
    t0 = ((await readState(from)).transitions as Json[])[0];
    expect(t0.conditions as Json[]).toHaveLength(0);

    await call("remove_state_tree_transition", { assetPath: ST, stateId: from, transitionIndex: 0 });
    expect((await readState(from)).transitions as Json[]).toHaveLength(0);
  }, 120_000);

  it("clear_state_tree_state_nodes empties tasks, enter conditions and transitions", async () => {
    const id = await addState("BCov_Clear");
    await call("add_state_tree_task", { assetPath: ST, stateId: id, structType: DELAY });
    await call("add_state_tree_enter_condition", { assetPath: ST, stateId: id, structType: COMPARE_INT });
    await call("add_state_tree_transition", { assetPath: ST, stateId: id, trigger: "OnStateCompleted", transitionType: "Succeeded" });

    const cleared = await call("clear_state_tree_state_nodes", { assetPath: ST, stateId: id });
    expect(cleared.cleared).toBe(true);
    expect(cleared.clearedTasks).toBe(1);
    expect(cleared.clearedEnterConditions).toBe(1);
    expect(cleared.clearedTransitions).toBe(1);

    const st = await readState(id);
    expect(st.tasks as Json[]).toHaveLength(0);
    expect(st.enterConditions as Json[]).toHaveLength(0);
    expect(st.transitions as Json[]).toHaveLength(0);
  }, 120_000);
});

describe("statetree tree-level nodes", () => {
  it("list_state_tree_node_types lists the engine delay task with its instance properties", async () => {
    const res = await call("list_state_tree_node_types", { assetPath: ST, nodeType: "task", filter: "Delay", schemaAllowedOnly: false });
    const tasks = (res.task as Json).nodeTypes as Json[];
    const delay = tasks.find((t) => t.structType === "StateTreeDelayTask");
    expect(delay, JSON.stringify(tasks.map((t) => t.structType))).toBeDefined();
    const instNames = (delay!.instanceProperties as Json[]).map((p) => p.name);
    expect(instNames).toContain("Duration");
    expect((res.task as Json).blueprintWrapperStruct).toBe("StateTreeBlueprintTaskWrapper");
  }, 120_000);

  it("evaluators: add, set instance and node properties, remove", async () => {
    // Any loaded evaluator struct with a numeric instance property will do.
    const types = await call("list_state_tree_node_types", { assetPath: ST, nodeType: "evaluator", schemaAllowedOnly: false });
    const candidates = ((types.evaluator as Json).nodeTypes as Json[]).filter((t) => t.instanceDataIsClass === false);
    let structPath = "";
    let propName = "";
    for (const t of candidates) {
      const p = ((t.instanceProperties as Json[]) ?? []).find((ip) => ip.type === "float" || ip.type === "int32");
      if (p) { structPath = String(t.structPath); propName = String(p.name); break; }
    }
    expect(structPath, "no loaded evaluator struct with a float or int32 instance property").not.toBe("");

    const added = await call("add_state_tree_evaluator", { assetPath: ST, structType: structPath });
    const nodeId = String(added.nodeId);
    const evalNode = async (): Promise<Json | undefined> =>
      ((await readTree()).evaluators as Json[]).find((e) => e.id === nodeId);
    expect(await evalNode()).toBeDefined();

    await call("set_state_tree_evaluator_instance_property", { assetPath: ST, nodeId, propertyName: propName, value: "5" });
    await call("set_state_tree_evaluator_property", { assetPath: ST, nodeId, propertyName: "Name", value: "BCovEval" });
    const ev = (await evalNode())!;
    expect(Number(props(ev, "instanceProperties")[propName])).toBe(5);
    expect(props(ev, "nodeProperties").Name).toBe("BCovEval");

    await call("remove_state_tree_evaluator", { assetPath: ST, nodeId });
    expect(await evalNode()).toBeUndefined();
  }, 120_000);

  it("global tasks: add, set instance and node properties, remove", async () => {
    const added = await call("add_state_tree_global_task", { assetPath: ST, structType: DELAY, instanceProperties: { Duration: 1 } });
    const nodeId = String(added.nodeId);
    const gtNode = async (): Promise<Json | undefined> =>
      ((await readTree()).globalTasks as Json[]).find((g) => g.id === nodeId);
    expect(Number(props((await gtNode())!, "instanceProperties").Duration)).toBeCloseTo(1, 3);

    await call("set_state_tree_global_task_instance_property", { assetPath: ST, nodeId, propertyName: "Duration", value: "9" });
    await call("set_state_tree_global_task_property", { assetPath: ST, nodeId, propertyName: "bTaskEnabled", value: "false" });
    const gt = (await gtNode())!;
    expect(Number(props(gt, "instanceProperties").Duration)).toBeCloseTo(9, 3);
    expect(String(props(gt, "nodeProperties").bTaskEnabled).toLowerCase()).toBe("false");

    await call("remove_state_tree_global_task", { assetPath: ST, nodeId });
    expect(await gtNode()).toBeUndefined();
  }, 120_000);

  it("set_state_tree_node_class gives a Blueprint task wrapper its class and an instance object", async () => {
    const id = await addState("BCov_BPTask");
    const added = await call("add_state_tree_task", { assetPath: ST, stateId: id, structType: "/Script/StateTreeModule.StateTreeBlueprintTaskWrapper" });
    const nodeId = String(added.nodeId);

    const res = await call("set_state_tree_node_class", { assetPath: ST, nodeId, nodeClass: BP_TASK_CLASS });
    expect(res.classProperty).toBe("TaskClass");
    expect(res.hasInstanceObject).toBe(true);

    const task = ((await readState(id)).tasks as Json[])[0];
    expect(task.nodeId).toBe(nodeId);
    expect(task.instanceClass).toBe(BP_TASK_CLASS);
    expect(String(props(task, "nodeProperties").TaskClass)).toContain("BP_BCovSTTask_C");
    expect(task.instanceMissing).toBeUndefined();
  }, 120_000);
});
