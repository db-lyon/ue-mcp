/**
 * `blueprint(delete_graph)`, against a real editor (#1010).
 *
 * The report is about a collapsed subgraph that outlived its composite node:
 * nothing could remove it, and leftover variable gets inside it went on
 * blocking `delete_variable`. Two halves of that are checkable here.
 *
 *   A graph is addressed the way `list_graphs` names one, so the action
 *   reaches graphs the name-list-based deletes never search.
 *   While the owning node is still alive, `delete_node` is the correct call -
 *   it takes the bound graph with it - and this refuses rather than leaving
 *   the node behind pointing at nothing.
 *
 * The fully orphaned state, node already gone and graph still there, is what
 * the report hit and is NOT reproduced here: nothing in the surface collapses
 * nodes into a subgraph or deletes a node while keeping its bound graph, so a
 * fixture cannot get into it on purpose. What is asserted is that a subgraph
 * IS reachable and removable once nothing owns it, which is the property the
 * orphan case needs.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

const BP = `${TEST_PREFIX}/BP_DeleteGraph`;
const FUNCTION_GRAPH = "DoomedFunction";

type Graph = { name?: string; selector?: string };

let bridge: EditorBridge;

const graphNames = async (): Promise<string[]> => {
  const listed = await callBridge(bridge, "list_blueprint_graphs", { path: BP });
  expect(listed.ok, listed.error).toBe(true);
  return ((resultArray(listed.result, "graphs") ?? []) as Graph[]).map((g) => g.name ?? "");
};

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
  const created = await callBridge(bridge, "create_blueprint", { path: BP, parentClass: "Actor" });
  expect(created.ok, created.error).toBe(true);
});

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    disconnectBridge();
  }
});

describe("addressing a graph the way list_graphs names one (#1010)", () => {
  it("removes a graph and reports what went with it", async () => {
    const made = await callBridge(bridge, "create_function", { path: BP, functionName: FUNCTION_GRAPH });
    expect(made.ok, made.error).toBe(true);
    expect(await graphNames()).toContain(FUNCTION_GRAPH);

    const deleted = await callBridge(bridge, "delete_graph", { path: BP, graphName: FUNCTION_GRAPH });
    expect(deleted.ok, deleted.error).toBe(true);
    const result = deleted.result as Record<string, unknown>;
    expect(result.deleted).toBe(true);
    expect(result.kind).toBe("function");
    expect(typeof result.deletedNodeCount).toBe("number");

    expect(await graphNames()).not.toContain(FUNCTION_GRAPH);
  });

  it("is idempotent, like the other deletes", async () => {
    // The caller asked for the graph to be gone. It is gone.
    const again = await callBridge(bridge, "delete_graph", { path: BP, graphName: FUNCTION_GRAPH });
    expect(again.ok, again.error).toBe(true);
    expect((again.result as Record<string, unknown>).alreadyDeleted).toBe(true);
  });

  it("refuses to delete an event graph without force", async () => {
    // Removing it does not leave a tidier asset, it leaves one whose events
    // have nowhere to live.
    const refused = await callBridge(bridge, "delete_graph", { path: BP, graphName: "EventGraph" });
    const message = String(refused.error ?? JSON.stringify(refused.result));
    expect(message).toMatch(/event graph/i);
    expect(await graphNames()).toContain("EventGraph");
  });
});

describe("a subgraph that something still owns (#1010)", () => {
  it("points at delete_node instead of orphaning the owning node", async () => {
    const composite = await callBridge(bridge, "add_node", {
      path: BP,
      graphName: "EventGraph",
      nodeClass: "K2Node_Composite",
    });
    if (!composite.ok) {
      // Nothing in the surface authors a collapsed graph, so if this engine
      // build will not spawn one the case cannot be set up. Say so rather than
      // reporting a pass.
      expect.fail(`could not create a composite node to own a subgraph: ${composite.error}`);
    }

    const owned = (await graphNames()).find((name) => !["EventGraph", "UserConstructionScript"].includes(name));
    expect(owned, "the composite node created no bound graph").toBeTruthy();

    const refused = await callBridge(bridge, "delete_graph", { path: BP, graphName: owned });
    const message = String(refused.error ?? JSON.stringify(refused.result));
    // delete_node is the correct call while the node exists: its DestroyNode
    // takes the bound graph with it, and removing the graph alone leaves the
    // node pointing at nothing.
    expect(message).toMatch(/delete_node/i);
    expect(await graphNames()).toContain(owned);

    // force is the way past it, and the response says the node was left.
    const forced = await callBridge(bridge, "delete_graph", { path: BP, graphName: owned, force: true });
    expect(forced.ok, forced.error).toBe(true);
    const result = forced.result as Record<string, unknown>;
    expect(result.deleted).toBe(true);
    expect(result.ownerNodeLeftBehind).toBe(true);
    expect(await graphNames()).not.toContain(owned);
  });
});
