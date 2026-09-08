/**
 * Addressing a Blueprint node by GUID when writing (#996).
 *
 * `get_connections` and `find_nodes` report a node GUID precisely so wiring can
 * be verified and re-targeted where a title cannot pick one node from another.
 * That is only worth anything if the write actions can receive those GUIDs.
 *
 * The C++ has accepted them all along - `connect_pins` reads
 * `sourceNodeId`/`targetNodeId` and `delete_node` reads `nodeId`, both through
 * `RequireStringAlt` - but the TS schema never declared the first two, and
 * `delete_node`'s mapParams forwarded only `nodeName`. So the surface stripped
 * them and the round trip did not close.
 *
 * The fixture builds TWO identically titled getters on purpose. Every
 * assertion below is about telling those two apart, which is exactly what a
 * title cannot do and the whole reason the GUID is reported.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

const BP = `${TEST_PREFIX}/BP_GuidAddressing`;
const VARIABLE = "Health";

type Node = { nodeId?: string; nodeTitle?: string; nodeClass?: string };
type Edge = { fromNodeId?: string; toNodeId?: string; fromPin?: string; toPin?: string; kind?: string };

let bridge: EditorBridge;
let fixtureError = "";
/** The two getters, distinguishable only by GUID. */
let getters: Node[] = [];
let setter: Node | undefined;

async function nodes(nodeClass: string): Promise<Node[]> {
  const found = await callBridge(bridge, "search_blueprint_nodes", {
    assetPath: BP,
    nodeClasses: [nodeClass],
  });
  expect(found.ok, found.error).toBe(true);
  return (resultArray(found.result, "nodes") ?? []) as Node[];
}

async function dataEdges(): Promise<Edge[]> {
  const read = await callBridge(bridge, "get_blueprint_connections", { assetPath: BP, kind: "data" });
  // A bare `?? []` here would turn an Unknown-method error into "no edges",
  // which is how a stale plugin once read as a broken feature.
  expect(read.ok, read.error).toBe(true);
  return (resultArray(read.result, "connections") ?? []) as Edge[];
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });

  const created = await callBridge(bridge, "create_blueprint", { path: BP, parentClass: "Actor" });
  expect(created.ok, created.error).toBe(true);
  const variable = await callBridge(bridge, "add_variable", { path: BP, name: VARIABLE, varType: "float" });
  expect(variable.ok, variable.error).toBe(true);

  for (const nodeClass of ["GetVar", "GetVar", "SetVar"]) {
    const node = await callBridge(bridge, "add_node", {
      path: BP, graphName: "EventGraph", nodeClass,
      nodeParams: { variableName: VARIABLE },
    });
    if (!node.ok) { fixtureError = `add ${nodeClass} failed: ${node.error}`; return; }
  }

  getters = await nodes("K2Node_VariableGet");
  setter = (await nodes("K2Node_VariableSet"))[0];
  if (getters.length !== 2 || !setter) {
    fixtureError = `expected 2 getters and a setter, got ${getters.length} and ${setter ? 1 : 0}`;
  }
});

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    disconnectBridge();
  }
});

describe("the fixture is actually ambiguous by title (#996)", () => {
  it("has two getters with the same title and different GUIDs", () => {
    expect(fixtureError, fixtureError).toBe("");
    expect(getters[0].nodeTitle).toBe(getters[1].nodeTitle);
    expect(getters[0].nodeId).not.toBe(getters[1].nodeId);
  });
});

describe("connect_pins accepts a node GUID (#996)", () => {
  it("wires the second getter, which no title could name", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const wired = await callBridge(bridge, "connect_pins", {
      assetPath: BP,
      graphName: "EventGraph",
      sourceNodeId: getters[1].nodeId,
      sourcePin: VARIABLE,
      targetNodeId: setter!.nodeId,
      targetPin: VARIABLE,
    });
    expect(wired.ok, wired.error).toBe(true);

    // The round trip: the edge reader has to name the getter that was asked
    // for, not merely some getter. A title-addressed wire could not express
    // the request in the first place.
    const edge = (await dataEdges()).find((e) => e.toNodeId === setter!.nodeId);
    expect(edge, "no data edge into the setter").toBeTruthy();
    expect(edge!.fromNodeId).toBe(getters[1].nodeId);
    expect(edge!.fromNodeId).not.toBe(getters[0].nodeId);
  });
});

describe("delete_node accepts a node GUID (#996)", () => {
  it("deletes the getter it was given and leaves the other", async () => {
    expect(fixtureError, fixtureError).toBe("");
    // Delete the UNWIRED one. If the GUID were ignored and a title used, there
    // would be no way to say which, and the wired edge could vanish instead.
    const deleted = await callBridge(bridge, "delete_node", {
      assetPath: BP,
      graphName: "EventGraph",
      nodeId: getters[0].nodeId,
    });
    expect(deleted.ok, deleted.error).toBe(true);

    const left = await nodes("K2Node_VariableGet");
    expect(left).toHaveLength(1);
    expect(left[0].nodeId).toBe(getters[1].nodeId);

    // And the wire the survivor carried is still there.
    const edge = (await dataEdges()).find((e) => e.toNodeId === setter!.nodeId);
    expect(edge?.fromNodeId).toBe(getters[1].nodeId);
  });
});
