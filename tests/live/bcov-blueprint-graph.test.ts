// @live-editor headless
/**
 * Blueprint graph editing read back through the graph readers: batch wiring,
 * disconnects, pin defaults, node positions and layout, refresh, T3D round
 * trip, orphan cleanup, validation, batch compile, graph resolution and the
 * node-type catalogues.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Blueprint";
const BP = `${DIR}/BP_BCovGraph`;
const CLEAN_BP = `${DIR}/BP_BCovCleanup`;
const GRAPH = "EventGraph";

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (j: Json, key: string): Row[] => (Array.isArray(j[key]) ? (j[key] as Row[]) : []);
const guid = (s: unknown): string => String(s ?? "").replace(/[^0-9a-f]/gi, "").toLowerCase();

let bridge: EditorBridge;
let eventId = "";
let branchId = "";
let printId = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function addNode(path: string, nodeClass: string, nodeParams?: Json): Promise<string> {
  const r = await callBridge(bridge, "add_node", { assetPath: path, graphName: GRAPH, nodeClass, nodeParams });
  expect(succeeded(r), `add_node ${nodeClass}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return String(body(r).nodeId);
}

async function graphNodes(path = BP): Promise<Row[]> {
  return rows(await call("read_blueprint_graph", { assetPath: path, graphName: GRAPH, limit: 500 }), "nodes");
}

async function execEdges(): Promise<Row[]> {
  return rows(await call("get_blueprint_connections", { assetPath: BP, graphName: GRAPH, kind: "exec" }), "connections");
}

const hasEdge = (edges: Row[], from: string, to: string): boolean =>
  edges.some((e) => guid(e.fromNodeId) === guid(from) && guid(e.toNodeId) === guid(to));

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: CLEAN_BP, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  eventId = await addNode(BP, "CustomEvent", { eventName: "BCovEvent" });
  branchId = await addNode(BP, "Branch");
  printId = await addNode(BP, "CallFunction", { functionName: "PrintString", targetClass: "/Script/Engine.KismetSystemLibrary" });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    await callBridge(bridge, "delete_asset", { assetPath: CLEAN_BP, force: true });
    disconnectBridge();
  }
});

describe("wiring", () => {
  it("connect_pins_batch wires two exec edges that get_connections then reports", async () => {
    const res = await call("connect_pins_batch", {
      assetPath: BP, graphName: GRAPH,
      connections: [
        { sourceNode: eventId, sourcePin: "then", targetNode: branchId, targetPin: "execute" },
        { sourceNode: branchId, sourcePin: "then", targetNode: printId, targetPin: "execute" },
      ],
    });
    expect(res.connected).toBe(2);
    expect(res.failed).toBe(0);
    const edges = await execEdges();
    expect(hasEdge(edges, eventId, branchId)).toBe(true);
    expect(hasEdge(edges, branchId, printId)).toBe(true);
  }, 120_000);

  it("read_blueprint_graph_summary lists the nodes and the exec edges", async () => {
    const summary = await call("read_blueprint_graph_summary", { assetPath: BP, graphName: GRAPH });
    const classes = rows(summary, "nodes").map((n) => n.class);
    expect(classes).toEqual(expect.arrayContaining(["K2Node_CustomEvent", "K2Node_IfThenElse", "K2Node_CallFunction"]));
    expect(rows(summary, "execEdges").length).toBeGreaterThanOrEqual(2);
  }, 120_000);

  it("disconnect_pins breaks only the named pin's link", async () => {
    const res = await call("disconnect_pins", { assetPath: BP, graphName: GRAPH, nodeId: branchId, pinName: "then" });
    expect(res.brokenLinkCount).toBe(1);
    const edges = await execEdges();
    expect(hasEdge(edges, branchId, printId)).toBe(false);
    expect(hasEdge(edges, eventId, branchId)).toBe(true);
  }, 120_000);
});

describe("node state", () => {
  it("set_node_property writes a pin default that read_node_property returns", async () => {
    await call("set_node_property", { assetPath: BP, graphName: GRAPH, nodeId: printId, propertyName: "InString", value: "BCovHello" });
    const read = await call("read_node_property", { assetPath: BP, graphName: GRAPH, nodeId: printId, propertyName: "InString" });
    expect(read.defaultValue).toBe("BCovHello");
  }, 120_000);

  it("refresh_node reconstructs the node and keeps its pins and defaults", async () => {
    const res = await call("refresh_node", { assetPath: BP, graphName: GRAPH, nodeId: printId });
    expect(res.pinsAfter).toBe(res.pinsBefore);
    expect(rows(res, "orphanedPins")).toEqual([]);
    const read = await call("read_node_property", { assetPath: BP, graphName: GRAPH, nodeId: printId, propertyName: "InString" });
    expect(read.defaultValue).toBe("BCovHello");
  }, 120_000);

  it("set_node_position moves a node to the given coordinates", async () => {
    await call("set_node_position", { assetPath: BP, graphName: GRAPH, nodeId: printId, posX: 1234, posY: 567 });
    const node = (await graphNodes()).find((n) => guid(n.id) === guid(printId));
    expect(node?.posX).toBe(1234);
    expect(node?.posY).toBe(567);
  }, 120_000);

  it("auto_layout_graph separates stacked nodes into columns", async () => {
    for (const id of [eventId, branchId, printId]) {
      await call("set_node_position", { assetPath: BP, graphName: GRAPH, nodeId: id, posX: 0, posY: 0 });
    }
    const res = await call("auto_layout_graph", { assetPath: BP, graphName: GRAPH });
    expect(Number(res.moved)).toBeGreaterThan(0);
    const nodes = await graphNodes();
    const evt = nodes.find((n) => guid(n.id) === guid(eventId));
    const branch = nodes.find((n) => guid(n.id) === guid(branchId));
    expect(evt && branch).toBeTruthy();
    expect(Number(branch?.posX)).toBeGreaterThan(Number(evt?.posX));
  }, 120_000);

  it("export_nodes_t3d and import_nodes_t3d paste a copy of a node", async () => {
    const callCount = async () => (await graphNodes()).filter((n) => n.class === "K2Node_CallFunction").length;
    const before = await callCount();
    const exported = await call("export_nodes_t3d", { assetPath: BP, graphName: GRAPH, nodeIds: [printId] });
    expect(exported.count).toBe(1);
    expect(String(exported.t3d)).toContain("K2Node_CallFunction");

    const imported = await call("import_nodes_t3d", { assetPath: BP, graphName: GRAPH, t3d: String(exported.t3d), posX: 2000, posY: 2000 });
    const ids = (imported.nodeIds ?? []) as string[];
    expect(ids).toHaveLength(1);
    expect(guid(ids[0])).not.toBe(guid(printId));
    expect(await callCount()).toBe(before + 1);
    expect((await graphNodes()).some((n) => guid(n.id) === guid(ids[0]))).toBe(true);
  }, 120_000);
});

describe("graph maintenance", () => {
  it("cleanup_graph removes a call node whose function was deleted, and validate_blueprint sees the difference", async () => {
    await call("create_blueprint", { assetPath: CLEAN_BP, parentClass: "Actor" });
    await call("create_function", { assetPath: CLEAN_BP, functionName: "BCovDoomed" });
    const evtId = await addNode(CLEAN_BP, "CustomEvent", { eventName: "BCovCleanEvt" });
    const callId = await addNode(CLEAN_BP, "CallFunction", { functionName: "BCovDoomed" });
    // An unwired call node is pruned by the compiler, so wire it in to make the error visible.
    await call("connect_pins_batch", {
      assetPath: CLEAN_BP,
      graphName: GRAPH,
      connections: [{ sourceNode: evtId, sourcePin: "then", targetNode: callId, targetPin: "execute" }],
    });
    await call("delete_function", { assetPath: CLEAN_BP, functionName: "BCovDoomed" });

    const broken = await call("validate_blueprint", { assetPath: CLEAN_BP });
    expect(broken.valid).toBe(false);
    expect(Number(broken.errorCount)).toBeGreaterThan(0);

    const cleaned = await call("cleanup_graph", { assetPath: CLEAN_BP, graphName: GRAPH });
    expect(Number(cleaned.removed)).toBeGreaterThanOrEqual(1);
    expect(rows(cleaned, "removedNodes").some((n) => guid(n.nodeGuid) === guid(callId))).toBe(true);
    expect((await graphNodes(CLEAN_BP)).some((n) => guid(n.id) === guid(callId))).toBe(false);

    const fixed = await call("validate_blueprint", { assetPath: CLEAN_BP });
    expect(fixed.valid).toBe(true);
    expect(fixed.errorCount).toBe(0);
  }, 180_000);

  it("compile_blueprints compiles each listed Blueprint", async () => {
    const res = await call("compile_blueprints", { assetPaths: [BP] });
    expect(res.total).toBe(1);
    expect(res.failed).toBe(0);
    const item = rows(res, "results")[0];
    expect(item?.path).toBe(BP);
    expect(item?.status).toBe("compiled");
  }, 180_000);

  it("resolve_blueprint_graph finds a function graph created on the Blueprint", async () => {
    await call("create_function", { assetPath: BP, functionName: "BCovResolveMe" });
    const res = await call("resolve_blueprint_graph", { assetPath: BP, graphName: "BCovResolveMe" });
    expect(res.matchCount).toBe(1);
    const match = rows(res, "matches")[0];
    expect(match?.name).toBe("BCovResolveMe");
    expect(String(match?.objectPath)).toContain("BCovResolveMe");
  }, 120_000);
});

describe("node type catalogues", () => {
  it("search_node_types finds PrintString on KismetSystemLibrary with an add_node recipe", async () => {
    const res = await call("search_node_types", { query: "PrintString", includeGraphNodes: false, limit: 100 });
    const hit = rows(res, "results").find((r) => r.name === "PrintString");
    expect(hit, JSON.stringify(rows(res, "results").slice(0, 5))).toBeDefined();
    expect(hit?.class).toBe("KismetSystemLibrary");
    expect((hit?.addNode as Json | undefined)?.nodeClass).toBe("CallFunction");
  }, 120_000);

  it("list_node_types scopes a category to its library class", async () => {
    const res = await call("list_node_types", { category: "string", limit: 1000 });
    const types = rows(res, "nodeTypes");
    expect(types.length).toBeGreaterThan(10);
    expect(types.every((t) => t.class === "KismetStringLibrary")).toBe(true);
    expect(types.map((t) => t.name)).toContain("Concat_StrStr");
  }, 120_000);
});
