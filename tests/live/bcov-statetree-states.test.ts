// @live-editor headless
/**
 * StateTree state authoring: the state hierarchy, state properties, the colour
 * palette, links, moves, state and root parameters and utility considerations,
 * each read back through list_states, read or read_state. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/StateTree";
const ST = `${DIR}/ST_BCovStates`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

interface StateRow { name: string; id: string; path: string; type: string }

async function states(): Promise<StateRow[]> {
  const res = await call("list_state_tree_states", { assetPath: ST });
  return res.states as StateRow[];
}

async function readState(stateId: string): Promise<Json> {
  return call("read_state_tree_state", { assetPath: ST, stateId });
}

/** Depth-first search of read_state_tree's subTrees for one state id. */
function findInTree(nodes: Json[], id: string): Json | undefined {
  for (const n of nodes) {
    if (n.id === id) return n;
    const hit = findInTree((n.children as Json[]) ?? [], id);
    if (hit) return hit;
  }
  return undefined;
}

async function addState(name: string, extra: Json = {}): Promise<string> {
  const res = await call("add_state_tree_state", { assetPath: ST, name, ...extra });
  return String(res.stateId);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
  await call("create_state_tree", { name: "ST_BCovStates", packagePath: DIR });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
    disconnectBridge();
  }
});

describe("statetree state hierarchy", () => {
  it("add_state_tree_state adds a child that list_state_tree_states and read_state_tree report", async () => {
    const before = await states();
    expect(before.length).toBeGreaterThan(0);
    const rootPath = before[0].path;

    const added = await call("add_state_tree_state", {
      assetPath: ST, statePath: rootPath, name: "BCov_Child", stateType: "Group", selectionBehavior: "TrySelectChildrenAtRandom",
    });
    expect(added.statePath).toBe(`${rootPath}.BCov_Child`);

    const after = await states();
    const row = after.find((s) => s.id === added.stateId);
    expect(row?.path).toBe(`${rootPath}.BCov_Child`);
    expect(row?.type).toBe("Group");

    const tree = await call("read_state_tree", { assetPath: ST });
    expect(typeof tree.schemaClass).toBe("string");
    const node = findInTree(tree.subTrees as Json[], String(added.stateId));
    expect(node?.selectionBehavior).toBe("TrySelectChildrenAtRandom");
    expect(node?.type).toBe("Group");
  }, 120_000);

  it("set_state_tree_state_property writes name, weight, description and bEnabled, read back by read_state_tree_state", async () => {
    const id = await addState("BCov_Props");
    await call("set_state_tree_state_property", { assetPath: ST, stateId: id, propertyName: "weight", value: "2.5" });
    await call("set_state_tree_state_property", { assetPath: ST, stateId: id, propertyName: "description", value: "bcov description" });
    await call("set_state_tree_state_property", { assetPath: ST, stateId: id, propertyName: "bEnabled", value: "false" });
    await call("set_state_tree_state_property", { assetPath: ST, stateId: id, propertyName: "name", value: "BCov_Renamed" });

    const st = await readState(id);
    expect(Number(st.weight)).toBeCloseTo(2.5, 3);
    expect(st.description).toBe("bcov description");
    expect(st.bEnabled).toBe(false);
    expect(st.name).toBe("BCov_Renamed");
    expect(st.statePath).toBe("BCov_Renamed");
  }, 120_000);

  it("add_state_tree_color adds a palette entry that list_state_tree_colors lists and a state can use", async () => {
    const added = await call("add_state_tree_color", { assetPath: ST, displayName: "BCovRed", color: "(R=1.0,G=0.0,B=0.0,A=1.0)" });
    const colors = (await call("list_state_tree_colors", { assetPath: ST })).colors as Json[];
    const entry = colors.find((c) => c.displayName === "BCovRed");
    expect(entry?.id).toBe(added.colorId);
    expect(String(entry?.color)).toMatch(/^\(R=1\.0+,G=0\.0+,B=0\.0+,A=1\.0+\)$/);

    const id = await addState("BCov_Coloured");
    await call("set_state_tree_state_property", { assetPath: ST, stateId: id, propertyName: "color", value: "BCovRed" });
    const tree = await call("read_state_tree", { assetPath: ST });
    const node = findInTree(tree.subTrees as Json[], id);
    expect(node?.color).toBe("BCovRed");
    expect(node?.colorId).toBe(added.colorId);
  }, 120_000);

  it("move_state_tree_state reorders siblings and moves a state to the root", async () => {
    const parent = await addState("BCov_MoveParent");
    const a = await addState("BCov_A", { stateId: parent });
    const b = await addState("BCov_B", { stateId: parent });
    const c = await addState("BCov_C", { stateId: parent });

    const moved = await call("move_state_tree_state", { assetPath: ST, stateId: c, insertIndex: 0 });
    expect(moved.index).toBe(0);
    let order = (await states()).filter((s) => [a, b, c].includes(s.id)).map((s) => s.id);
    expect(order).toEqual([c, a, b]);

    await call("move_state_tree_state", { assetPath: ST, stateId: b, toRoot: true });
    const rowB = (await states()).find((s) => s.id === b);
    expect(rowB?.path).toBe("BCov_B");
    const parentState = await readState(parent);
    expect(parentState.childCount).toBe(2);
    order = (await states()).filter((s) => [a, c].includes(s.id)).map((s) => s.id);
    expect(order).toEqual([c, a]);
  }, 120_000);

  it("remove_state_tree_state drops the state from list_state_tree_states", async () => {
    const id = await addState("BCov_Doomed");
    expect((await states()).some((s) => s.id === id)).toBe(true);
    const res = await call("remove_state_tree_state", { assetPath: ST, stateId: id });
    expect(res.removed).toBe(true);
    expect((await states()).some((s) => s.id === id)).toBe(false);
  }, 120_000);

  it("set_state_tree_state_link links a state to an in-asset Subtree and clears it again", async () => {
    const sub = await addState("BCov_Sub", { stateType: "Subtree" });
    const linker = await addState("BCov_Linker");

    const linked = await call("set_state_tree_state_link", { assetPath: ST, stateId: linker, linkType: "subtree", targetStateId: sub });
    expect(linked.targetStateId).toBe(sub);
    let st = await readState(linker);
    expect(st.type).toBe("Linked");
    expect((st.link as Json).linkedSubtreeStateId).toBe(sub);
    expect((st.link as Json).linkedSubtreeStatePath).toBe("BCov_Sub");

    await call("set_state_tree_state_link", { assetPath: ST, stateId: linker, linkType: "none" });
    st = await readState(linker);
    expect(st.type).toBe("State");
    expect((st.link as Json).linkedSubtreeStateId).toBe("");
  }, 120_000);
});

describe("statetree parameters and considerations", () => {
  it("state parameters: add, list, set and remove", async () => {
    const id = await addState("BCov_Params");
    const listParams = async (): Promise<Json[]> =>
      (await call("list_state_tree_state_parameters", { assetPath: ST, stateId: id })).parameters as Json[];

    await call("add_state_tree_state_parameter", { assetPath: ST, stateId: id, paramName: "BCovSpeed", paramType: "Float" });
    let param = (await listParams()).find((p) => p.name === "BCovSpeed");
    expect(param?.type).toBe("Float");
    expect(Number(param?.value)).toBe(0);

    await call("set_state_tree_state_parameter", { assetPath: ST, stateId: id, paramName: "BCovSpeed", value: "3.5" });
    param = (await listParams()).find((p) => p.name === "BCovSpeed");
    expect(Number(param?.value)).toBeCloseTo(3.5, 3);

    const removed = await call("remove_state_tree_state_parameter", { assetPath: ST, stateId: id, paramName: "BCovSpeed" });
    expect(removed.alreadyRemoved).toBeUndefined();
    expect((await listParams()).some((p) => p.name === "BCovSpeed")).toBe(false);
  }, 120_000);

  it("set_state_tree_root_parameters adds root parameters read_state_tree reports", async () => {
    const res = await call("set_state_tree_root_parameters", {
      assetPath: ST, parameters: [{ name: "BCovRootSpeed", type: "float" }, { name: "BCovRootCount", type: "int32" }],
    });
    expect(res.parameterCount).toBe(2);
    const tree = await call("read_state_tree", { assetPath: ST });
    const root = tree.rootParameters as Json;
    expect(Object.keys(root)).toEqual(expect.arrayContaining(["BCovRootSpeed", "BCovRootCount"]));
  }, 120_000);

  it("add_state_tree_consideration and remove_state_tree_consideration edit a child's utility considerations", async () => {
    const parent = await addState("BCov_Utility", { selectionBehavior: "TrySelectChildrenWithHighestUtility" });
    const child = await addState("BCov_Scored", { stateId: parent });

    const added = await call("add_state_tree_consideration", {
      assetPath: ST, stateId: child, structType: "/Script/StateTreeModule.StateTreeConstantConsideration", instanceProperties: { Constant: 0.75 },
    });
    expect(added.considerationIndex).toBe(0);
    expect(added.parentScoresByUtility).toBe(true);
    let st = await readState(child);
    const cons = st.considerations as Json[];
    expect(cons).toHaveLength(1);
    expect(cons[0].structType).toBe("StateTreeConstantConsideration");
    expect(Number((cons[0].instanceProperties as Json).Constant)).toBeCloseTo(0.75, 3);

    await call("remove_state_tree_consideration", { assetPath: ST, stateId: child, considerationIndex: 0 });
    st = await readState(child);
    expect(st.considerations as Json[]).toHaveLength(0);
  }, 120_000);
});
