// @live-editor headless
/**
 * Blackboard, BehaviorTree and EQS authoring, each write read back through the
 * matching read action: keys and parent chains, tree blackboard binding, graph
 * nodes added, moved, edited and removed, and EQS tests and options removed.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Gameplay/AI";
const BB_PARENT = `${DIR}/BB_BCovParent`;
const BB_CHILD = `${DIR}/BB_BCovChild`;
const BT = `${DIR}/BT_BCov`;
const BT_PRE = `${DIR}/BT_BCovPreBound`;
const EQS = `${DIR}/EQS_BCov`;

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Row[] => (Array.isArray(v) ? (v as Row[]) : []);
const names = (v: unknown): string[] => rows(v).map((k) => String(k.name));

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function wipe(): Promise<void> {
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await wipe();
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await wipe();
    disconnectBridge();
  }
});

describe("blackboard authoring", () => {
  it("create_blackboard, add_blackboard_key, set_blackboard_parent and remove_blackboard_key read back through read_blackboard", async () => {
    const parent = await call("create_blackboard", { name: "BB_BCovParent", packagePath: DIR });
    expect(String(parent.path)).toContain("BB_BCovParent");
    await call("create_blackboard", { name: "BB_BCovChild", packagePath: DIR });

    await call("add_blackboard_key", { blackboardPath: BB_PARENT, keyName: "Shared", keyType: "Bool" });
    const added = await call("add_blackboard_key", {
      blackboardPath: BB_CHILD, keyName: "TargetActor", keyType: "Object", baseClass: "/Script/Engine.Actor",
    });
    expect(String(added.baseClass)).toContain("Actor");
    await call("add_blackboard_key", { blackboardPath: BB_CHILD, keyName: "Speed", keyType: "Float" });
    await call("add_blackboard_key", { blackboardPath: BB_CHILD, keyName: "Shared", keyType: "Bool" });

    let child = await call("read_blackboard", { blackboardPath: BB_CHILD });
    expect(names(child.ownKeys)).toEqual(expect.arrayContaining(["TargetActor", "Speed", "Shared"]));
    const target = rows(child.ownKeys).find((k) => k.name === "TargetActor");
    expect(String(target?.type)).toContain("Object");
    expect(child.parentPath).toBe("None");

    await call("set_blackboard_parent", { blackboardPath: BB_CHILD, parentPath: BB_PARENT });
    child = await call("read_blackboard", { blackboardPath: BB_CHILD });
    expect(String(child.parentPath)).toContain("BB_BCovParent");
    expect(names(child.inheritedKeys)).toContain("Shared");
    // The duplicate the parent now supplies is pruned from the child's own keys.
    expect(names(child.ownKeys)).not.toContain("Shared");

    await call("remove_blackboard_key", { blackboardPath: BB_CHILD, keyName: "Speed" });
    child = await call("read_blackboard", { blackboardPath: BB_CHILD });
    expect(names(child.ownKeys)).not.toContain("Speed");
    expect(names(child.ownKeys)).toContain("TargetActor");
  }, 120_000);
});

describe("behavior tree authoring", () => {
  it("create_behavior_tree and set_behavior_tree_blackboard bind a blackboard get_behavior_tree_info reports", async () => {
    const pre = await call("create_behavior_tree", { name: "BT_BCovPreBound", packagePath: DIR, blackboardPath: BB_CHILD });
    expect(String(pre.blackboardPath)).toContain("BB_BCovChild");
    const preInfo = await call("get_behavior_tree_info", { assetPath: BT_PRE });
    expect(String(preInfo.blackboardAsset)).toContain("BB_BCovChild");

    await call("create_behavior_tree", { name: "BT_BCov", packagePath: DIR });
    const before = await call("get_behavior_tree_info", { assetPath: BT });
    expect(before.blackboardAsset).toBeUndefined();

    await call("set_behavior_tree_blackboard", { behaviorTreePath: BT, blackboardPath: BB_CHILD });
    const after = await call("get_behavior_tree_info", { assetPath: BT });
    expect(String(after.blackboardAsset)).toContain("BB_BCovChild");
    expect(names(after.blackboardKeys)).toContain("TargetActor");

    const listed = await call("list_behavior_trees", { limit: 2000 });
    const paths = rows(listed.behaviorTrees).map((t) => String(t.path));
    expect(paths.some((p) => p.startsWith(BT))).toBe(true);
    expect(paths.some((p) => p.startsWith(BT_PRE))).toBe(true);
  }, 120_000);

  it("add_bt_node, move_bt_node and remove_bt_node reshape the graph list_bt_graph_nodes and read_behavior_tree_graph report", async () => {
    const empty = await call("list_bt_graph_nodes", { assetPath: BT });
    expect(empty.nodeCount).toBe(0);

    const selector = await call("add_bt_node", { assetPath: BT, nodeClass: "BTComposite_Selector" });
    expect(selector.category).toBe("composite");
    const sel = String(selector.guid);
    const waitA = String((await call("add_bt_node", { assetPath: BT, nodeClass: "BTTask_Wait", parent: sel, nodeName: "WaitA" })).guid);
    const waitB = String((await call("add_bt_node", { assetPath: BT, nodeClass: "BTTask_Wait", parent: sel, nodeName: "WaitB" })).guid);
    const moveTo = await call("add_bt_node", {
      assetPath: BT, nodeClass: "BTTask_MoveTo", parent: sel, nodeName: "MoveToTarget",
      blackboardKeys: { BlackboardKey: "TargetActor" },
    });
    const moveGuid = String(moveTo.guid);

    const byGuid = async (): Promise<Map<string, Row>> => {
      const listed = await call("list_bt_graph_nodes", { assetPath: BT });
      return new Map(rows(listed.nodes).map((n) => [String(n.guid), n]));
    };
    let nodes = await byGuid();
    expect(nodes.get(waitA)?.parentGuid).toBe(sel);
    expect(nodes.get(waitA)?.indexInParent).toBe(0);
    expect(nodes.get(waitB)?.indexInParent).toBe(1);
    expect(nodes.get(moveGuid)?.class).toBe("BTTask_MoveTo");

    let graph = await call("read_behavior_tree_graph", { assetPath: BT });
    let root = graph.root as Row;
    expect(root.class).toBe("BTComposite_Selector");
    let children = rows(root.children).map((c) => (c.child as Row | undefined)?.nodeName);
    expect(children).toEqual(["WaitA", "WaitB", "MoveToTarget"]);
    const move = rows(root.children)[2].child as Row;
    expect((move.blackboardKey as Row).selectedKeyName).toBe("TargetActor");

    const moved = await call("move_bt_node", { assetPath: BT, node: waitB, index: 0 });
    expect(moved.previousIndex).toBe(1);
    nodes = await byGuid();
    expect(nodes.get(waitB)?.indexInParent).toBe(0);
    expect(nodes.get(waitA)?.indexInParent).toBe(1);
    graph = await call("read_behavior_tree_graph", { assetPath: BT });
    children = rows((graph.root as Row).children).map((c) => (c.child as Row | undefined)?.nodeName);
    expect(children).toEqual(["WaitB", "WaitA", "MoveToTarget"]);

    const removed = await call("remove_bt_node", { assetPath: BT, node: moveGuid });
    expect(removed.removedCount).toBe(1);
    nodes = await byGuid();
    expect(nodes.has(moveGuid)).toBe(false);
    graph = await call("read_behavior_tree_graph", { assetPath: BT });
    root = graph.root as Row;
    expect(rows(root.children)).toHaveLength(2);
  }, 180_000);

  it("set_bt_node_property and set_bt_task_property write values read_bt_node_properties reads back", async () => {
    await call("set_bt_node_property", { assetPath: BT, nodeName: "WaitA", property: "WaitTime", value: 7.5 });
    const a = await call("read_bt_node_properties", { assetPath: BT, nodeName: "WaitA", propertyNames: ["WaitTime"] });
    expect(a.matchCount).toBe(1);
    const aProps = (rows(a.nodes)[0].properties ?? {}) as Row;
    expect(JSON.stringify(aProps.WaitTime)).toContain("7.5");

    await call("set_bt_task_property", { assetPath: BT, nodeName: "WaitB", property: "RandomDeviation", value: 1.25 });
    const b = await call("read_bt_node_properties", { assetPath: BT, nodeName: "WaitB", propertyNames: ["RandomDeviation"] });
    const bProps = (rows(b.nodes)[0].properties ?? {}) as Row;
    expect(JSON.stringify(bProps.RandomDeviation)).toContain("1.25");
    // The other task kept its own value.
    const again = await call("read_bt_node_properties", { assetPath: BT, nodeName: "WaitA", propertyNames: ["RandomDeviation"] });
    expect(JSON.stringify((rows(again.nodes)[0].properties as Row).RandomDeviation)).not.toContain("1.25");
  }, 120_000);

  it("list_bt_tasks inventories the tree's tasks and filters by class", async () => {
    const all = await call("list_bt_tasks", { assetPath: BT });
    expect(all.assetCount).toBe(1);
    expect(rows(all.tasks).map((t) => t.nodeName).sort()).toEqual(["WaitA", "WaitB"]);
    const none = await call("list_bt_tasks", { assetPath: BT, taskClass: "BTTask_MoveTo" });
    expect(rows(none.tasks)).toHaveLength(0);
    const swept = await call("list_bt_tasks", { directory: DIR, taskClass: "BTTask_Wait" });
    expect(rows(swept.tasks).every((t) => String(t.assetPath).startsWith(BT))).toBe(true);
    expect(rows(swept.tasks)).toHaveLength(2);
  }, 120_000);

  it("list_bt_node_classes reports the classes add_bt_node placed, by kind", async () => {
    const tasks = await call("list_bt_node_classes", { kind: "task", limit: 2000 });
    expect(names(tasks.tasks)).toEqual(expect.arrayContaining(["BTTask_Wait", "BTTask_MoveTo"]));
    expect(tasks.composites).toBeUndefined();
    const composites = await call("list_bt_node_classes", { kind: "composite", limit: 2000 });
    expect(names(composites.composites)).toContain("BTComposite_Selector");
    expect(rows(composites.composites).every((c) => c.kind === "composite")).toBe(true);
  }, 120_000);
});

describe("EQS authoring", () => {
  it("remove_eqs_test and remove_eqs_option shrink the query read_eqs_query reports, and list_eqs_queries lists it", async () => {
    await call("create_eqs_query", { name: "EQS_BCov", packagePath: DIR });
    await call("add_eqs_generator", { queryPath: EQS, generatorClass: "SimpleGrid" });
    await call("add_eqs_test", { queryPath: EQS, testClass: "Distance", purpose: "score" });
    await call("add_eqs_test", { queryPath: EQS, testClass: "Dot", purpose: "score" });

    let read = await call("read_eqs_query", { queryPath: EQS });
    expect(read.optionCount).toBe(1);
    expect(rows(rows(read.options)[0].tests)).toHaveLength(2);

    await call("remove_eqs_test", { queryPath: EQS, testIndex: 0 });
    read = await call("read_eqs_query", { queryPath: EQS });
    const tests = rows(rows(read.options)[0].tests);
    expect(tests).toHaveLength(1);
    expect(String(tests[0].class)).toContain("Dot");

    await call("remove_eqs_option", { queryPath: EQS, optionIndex: 0 });
    read = await call("read_eqs_query", { queryPath: EQS });
    expect(read.optionCount).toBe(0);

    const listed = await call("list_eqs_queries");
    expect(rows(listed.eqsQueries).some((q) => String(q.path).startsWith(EQS))).toBe(true);
  }, 120_000);
});
