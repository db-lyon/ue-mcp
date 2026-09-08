/**
 * `blueprint(get_connections)`, against a real editor (#996, gap 2).
 *
 * The report's point is that a data edge was unreadable: `read_graph` says a
 * pin is connected true or false and never to what, and `read_graph_summary`
 * carries exec edges but no data edges. So the fixture wires a real data edge
 * and the assertions are about naming its two ends - which is the thing that
 * was missing, not whether an edge count is nonzero.
 *
 * Node GUIDs matter here for the reason the report gives: a graph with several
 * same-titled nodes cannot be rewired by title. The fixture deliberately puts
 * two identically titled nodes in the graph so "addressed by GUID" is asserted
 * rather than assumed.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

const BP = `${TEST_PREFIX}/BP_Connections`;
const VARIABLE = "Health";

type Edge = {
  kind?: string;
  graphName?: string;
  fromNodeId?: string;
  fromNodeTitle?: string;
  fromPin?: string;
  toNodeId?: string;
  toNodeTitle?: string;
  toPin?: string;
  pinCategory?: string;
};

let bridge: EditorBridge;
/** Set when the wiring could not be built, so every case reports why. */
let fixtureError = "";

async function connections(params: Record<string, unknown> = {}): Promise<Edge[]> {
  const read = await callBridge(bridge, "get_blueprint_connections", { assetPath: BP, ...params });
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

  // Two getters of the same variable: identically titled, which is exactly the
  // ambiguity the report says makes title-addressing useless.
  for (const _ of [0, 1]) {
    const node = await callBridge(bridge, "add_node", {
      path: BP, graphName: "EventGraph", nodeClass: "GetVar",
      nodeParams: { variableName: VARIABLE },
    });
    if (!node.ok) { fixtureError = `add GetVar failed: ${node.error}`; return; }
  }
  const setter = await callBridge(bridge, "add_node", {
    path: BP, graphName: "EventGraph", nodeClass: "SetVar",
    nodeParams: { variableName: VARIABLE },
  });
  if (!setter.ok) { fixtureError = `add SetVar failed: ${setter.error}`; return; }

  // A real data edge: a getter's output into the setter's value input.
  const wired = await callBridge(bridge, "connect_pins", {
    assetPath: BP,
    graphName: "EventGraph",
    sourceNode: `Get ${VARIABLE}`,
    sourcePin: VARIABLE,
    targetNode: `Set ${VARIABLE}`,
    targetPin: VARIABLE,
  });
  if (!wired.ok) fixtureError = `connect_pins failed: ${wired.error}`;
});

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    disconnectBridge();
  }
});

describe("reading data edges (#996 gap 2)", () => {
  it("names both ends of a data edge", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const data = (await connections({ kind: "data" })).filter((e) => e.graphName === "EventGraph");
    expect(data.length).toBeGreaterThan(0);

    const edge = data[0];
    // The whole gap: read_graph said "connected: true" and stopped here.
    expect(edge.fromPin, "no source pin named").toBeTruthy();
    expect(edge.toPin, "no target pin named").toBeTruthy();
    expect(edge.fromNodeId, "no source node GUID").toBeTruthy();
    expect(edge.toNodeId, "no target node GUID").toBeTruthy();
    expect(edge.kind).toBe("data");
  });

  it("carries the pin type, which a title cannot", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const data = (await connections({ kind: "data" })).filter((e) => e.graphName === "EventGraph");
    // Whether a re-target is even legal is a question about the pin type.
    expect(data.some((e) => (e.pinCategory ?? "").length > 0)).toBe(true);
  });

  it("distinguishes the two same-titled nodes by GUID", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const nodes = await callBridge(bridge, "search_blueprint_nodes", {
      assetPath: BP,
      nodeClasses: ["K2Node_VariableGet"],
    });
    expect(nodes.ok, nodes.error).toBe(true);
    const getters = (resultArray(nodes.result, "nodes") ?? []) as Array<{ nodeId?: string; nodeTitle?: string }>;
    expect(getters.length).toBe(2);

    // Same title, different identity. Without the GUID an agent cannot say
    // which of the two a given edge came from, which is the report's point.
    expect(new Set(getters.map((g) => g.nodeTitle)).size).toBe(1);
    expect(new Set(getters.map((g) => g.nodeId)).size).toBe(2);

    const data = (await connections({ kind: "data" })).filter((e) => e.graphName === "EventGraph");
    const sourceIds = new Set(data.map((e) => e.fromNodeId));
    const wiredGetter = getters.filter((g) => sourceIds.has(g.nodeId));
    expect(wiredGetter, "the edge does not name which getter it came from").toHaveLength(1);
  });
});

describe("filtering and addressing (#996 gap 2)", () => {
  it("returns only exec edges when asked, and only data edges when asked", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const exec = await connections({ kind: "exec" });
    const data = await connections({ kind: "data" });
    expect(exec.every((e) => e.kind === "exec")).toBe(true);
    expect(data.every((e) => e.kind === "data")).toBe(true);
  });

  it("reports each edge once rather than from both ends", async () => {
    expect(fixtureError, fixtureError).toBe("");
    const all = await connections();
    const ids = all.map((e) => `${e.fromNodeId}.${e.fromPin}->${e.toNodeId}.${e.toPin}`);
    expect(new Set(ids).size).toBe(ids.length);
  });

  it("refuses a graph name the Blueprint does not have", async () => {
    const read = await callBridge(bridge, "get_blueprint_connections", {
      assetPath: BP,
      graphName: "NoSuchGraphAnywhere",
    });
    const message = String(read.error ?? JSON.stringify(read.result));
    // Answering "no edges" would read as a wired-up graph with nothing in it.
    expect(message).toMatch(/no graph named/i);
  });
});
