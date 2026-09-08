/**
 * `blueprint(find_nodes)`, against a real editor (#998/#1015).
 *
 * Three things have to hold, and none of them is observable without a real
 * Blueprint with real nodes in more than one graph:
 *
 *   the walk reaches every graph the Blueprint owns rather than the one named
 *   graph read_graph and epic_find_nodes take, which is the gap both reports
 *   describe;
 *   a reported graph selector means what list_graphs says it means, because
 *   that selector is what the caller feeds back into read_graph;
 *   and the filters are alternatives, so one pass answers a question about a
 *   variable and a question about a title together.
 *
 * The fixture puts nodes in the event graph and in a function graph. It does
 * not build a collapsed subgraph, because nothing in the surface collapses
 * nodes into one; the nested-graph path is covered by search_call_sites'
 * suite, which walks the same GetAllGraphs enumeration.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

const BP = `${TEST_PREFIX}/BP_FindNodes`;
const VARIABLE = "bIsAiming";
const OTHER_VARIABLE = "SprintSpeed";
const FUNCTION_GRAPH = "ReadAimState";

type Hit = {
  graphName?: string;
  graphSelector?: string;
  nodeTitle?: string;
  nodeClass?: string;
  memberName?: string;
  access?: string;
  matchedOn?: string;
  nestedGraph?: boolean;
};

const hits = (result: unknown): Hit[] => (resultArray(result, "nodes") ?? []) as Hit[];

let bridge: EditorBridge;

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });

  const created = await callBridge(bridge, "create_blueprint", {
    path: BP,
    parentClass: "Actor",
  });
  expect(created.ok, created.error).toBe(true);

  for (const [name, type] of [[VARIABLE, "bool"], [OTHER_VARIABLE, "float"]] as const) {
    const added = await callBridge(bridge, "add_variable", {
      path: BP,
      name,
      varType: type,
    });
    expect(added.ok, added.error).toBe(true);
  }

  // Two reads and one write of the same member, so access filtering has
  // something to separate rather than one node of each kind.
  for (const nodeClass of ["GetVar", "GetVar", "SetVar"]) {
    const node = await callBridge(bridge, "add_node", {
      path: BP,
      graphName: "EventGraph",
      nodeClass,
      nodeParams: { variableName: VARIABLE },
    });
    expect(node.ok, node.error).toBe(true);
  }
  const other = await callBridge(bridge, "add_node", {
    path: BP,
    graphName: "EventGraph",
    nodeClass: "GetVar",
    nodeParams: { variableName: OTHER_VARIABLE },
  });
  expect(other.ok, other.error).toBe(true);

  // A read of the same member in a DIFFERENT graph. Without this the suite
  // would pass on an implementation that only ever looked at EventGraph,
  // which is the implementation both reports already had.
  const fn = await callBridge(bridge, "create_function", { path: BP, functionName: FUNCTION_GRAPH });
  expect(fn.ok, fn.error).toBe(true);
  const inFunction = await callBridge(bridge, "add_node", {
    path: BP,
    graphName: FUNCTION_GRAPH,
    nodeClass: "GetVar",
    nodeParams: { variableName: VARIABLE },
  });
  expect(inFunction.ok, inFunction.error).toBe(true);
});

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    disconnectBridge();
  }
});

describe("finding every reference to a variable (#1015)", () => {
  it("reports both reads and the write", async () => {
    const found = await callBridge(bridge, "search_blueprint_nodes", {
      assetPath: BP,
      variableName: VARIABLE,
    });
    expect(found.ok, found.error).toBe(true);
    const rows = hits(found.result);
    // Two reads and a write in the event graph, one more read in the function
    // graph. A search that stopped at the event graph would report three.
    expect(rows.length).toBe(4);
    expect(rows.every((r) => r.memberName === VARIABLE)).toBe(true);
    expect(rows.filter((r) => r.access === "get")).toHaveLength(3);
    expect(rows.filter((r) => r.access === "set")).toHaveLength(1);
    expect(new Set(rows.map((r) => r.graphName)).size).toBe(2);
  });

  it("narrows to reads or to writes", async () => {
    const reads = await callBridge(bridge, "search_blueprint_nodes", {
      assetPath: BP,
      variableName: VARIABLE,
      variableAccess: "get",
    });
    expect(reads.ok, reads.error).toBe(true);
    expect(hits(reads.result)).toHaveLength(3);

    const writes = await callBridge(bridge, "search_blueprint_nodes", {
      assetPath: BP,
      variableName: VARIABLE,
      variableAccess: "set",
    });
    expect(writes.ok, writes.error).toBe(true);
    expect(hits(writes.result)).toHaveLength(1);
  });

  it("does not report a different member", async () => {
    // The whole point is naming one variable. A search that also returned the
    // other float would be a node dump wearing a filter.
    const found = await callBridge(bridge, "search_blueprint_nodes", {
      assetPath: BP,
      variableName: OTHER_VARIABLE,
    });
    expect(found.ok, found.error).toBe(true);
    const rows = hits(found.result);
    expect(rows).toHaveLength(1);
    expect(rows[0].memberName).toBe(OTHER_VARIABLE);
  });

  it("reports a graph selector list_graphs also reports", async () => {
    const found = await callBridge(bridge, "search_blueprint_nodes", {
      assetPath: BP,
      variableName: VARIABLE,
    });
    const selectors = new Set(hits(found.result).map((r) => r.graphSelector));
    expect(selectors.size).toBeGreaterThan(0);

    const graphs = await callBridge(bridge, "list_blueprint_graphs", { path: BP });
    expect(graphs.ok, graphs.error).toBe(true);
    const known = new Set(
      ((resultArray(graphs.result, "graphs") ?? []) as Array<{ selector?: string; name?: string }>)
        .map((g) => g.selector ?? g.name),
    );
    for (const selector of selectors) {
      expect(known.has(selector), `${selector} is not a selector list_graphs reports`).toBe(true);
    }
  });
});

describe("finding nodes by title and class (#998)", () => {
  it("matches a title substring case-insensitively", async () => {
    const found = await callBridge(bridge, "search_blueprint_nodes", {
      assetPath: BP,
      titles: [OTHER_VARIABLE.toLowerCase()],
    });
    expect(found.ok, found.error).toBe(true);
    const rows = hits(found.result);
    expect(rows.length).toBeGreaterThan(0);
    expect(rows.every((r) => (r.nodeTitle ?? "").toLowerCase().includes(OTHER_VARIABLE.toLowerCase()))).toBe(true);
  });

  it("matches an exact node class", async () => {
    const found = await callBridge(bridge, "search_blueprint_nodes", {
      assetPath: BP,
      nodeClasses: ["K2Node_VariableSet"],
    });
    expect(found.ok, found.error).toBe(true);
    const rows = hits(found.result);
    expect(rows.length).toBe(1);
    expect(rows[0].nodeClass).toBe("K2Node_VariableSet");
  });

  it("treats the filters as alternatives rather than a conjunction", async () => {
    // A caller asking for "every write of bIsAiming, plus anything titled
    // SprintSpeed" wants one pass and both answers, not the empty intersection.
    const found = await callBridge(bridge, "search_blueprint_nodes", {
      assetPath: BP,
      nodeClasses: ["K2Node_VariableSet"],
      titles: [OTHER_VARIABLE],
    });
    expect(found.ok, found.error).toBe(true);
    const matched = new Set(hits(found.result).map((r) => r.matchedOn));
    expect(matched.has("nodeClass")).toBe(true);
    expect(matched.has("title")).toBe(true);
  });

  it("refuses a call that names no filter", async () => {
    // Without one this is read_graph with extra steps, and a truncated dump
    // that looks like a search result is worse than an error.
    const found = await callBridge(bridge, "search_blueprint_nodes", { assetPath: BP });
    const refused = !found.ok || (found.result as Record<string, unknown>)?.success === false;
    expect(refused).toBe(true);
  });

  it("says how much it walked", async () => {
    const found = await callBridge(bridge, "search_blueprint_nodes", {
      assetPath: BP,
      variableName: VARIABLE,
    });
    const stats = (found.result as Record<string, unknown>).stats as Record<string, number>;
    // An empty result has to be distinguishable from a filter that skipped the
    // graph the node was in.
    expect(stats.graphsScanned).toBeGreaterThan(0);
    expect(stats.nodesScanned).toBeGreaterThan(0);
  });
});
