// @live-editor headless
/**
 * PCG pin labels in graph reads and pin errors: read_graph and export_graph list every node's input and output
 * pin labels, and connect_nodes or disconnect_nodes given a label the node lacks names the labels it has.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/PcgPins`;
const GRAPH = `${DIR}/PCG_PinLabels`;

type Json = Record<string, unknown>;
type NodeRow = { name: string; inputPins?: string[]; outputPins?: string[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const node = (nodes: unknown, name: string): NodeRow | undefined =>
  (nodes as NodeRow[]).find((n) => n.name === name);

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
  await call("create_pcg_graph", { name: "PCG_PinLabels", packagePath: DIR });
  await call("import_pcg_graph", {
    assetPath: GRAPH,
    nodes: [
      { name: "Sampler", class: "PCGVolumeSamplerSettings" },
      { name: "Tagger", class: "PCGAddTagSettings" },
    ],
    connections: [{ from: "DefaultInputNode", fromPin: "In", to: "Sampler", toPin: "Volume" }],
  });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
    disconnectBridge();
  }
});

describe("PCG pin labels", () => {
  it("read_pcg_graph lists each node's input and output pin labels", async () => {
    const read = await call("read_pcg_graph", { assetPath: GRAPH });
    const sampler = node(read.nodes, "Sampler");
    expect(sampler?.inputPins).toContain("Volume");
    expect(sampler?.outputPins).toContain("Out");
    expect(node(read.nodes, "Tagger")?.inputPins?.length).toBeGreaterThan(0);
  });

  it("export_pcg_graph carries the same pin labels per node", async () => {
    const exported = await call("export_pcg_graph", { assetPath: GRAPH, includeSettings: false });
    const sampler = node(exported.nodes, "Sampler");
    expect(sampler?.inputPins).toContain("Volume");
    expect(sampler?.outputPins).toContain("Out");
  });

  it("connect_pcg_nodes refuses an unknown pin and lists the valid labels", async () => {
    const r = await callBridge(bridge, "connect_pcg_nodes", {
      assetPath: GRAPH, sourceNode: "Sampler", sourcePin: "Out", targetNode: "Tagger", targetPin: "NoSuchPin",
    });
    const refused = body(r);
    expect(refused.success).toBe(false);
    expect(refused.reason).toBe("pin_not_found");
    expect(refused.pinSide).toBe("input");
    const tagger = node((await call("read_pcg_graph", { assetPath: GRAPH })).nodes, "Tagger");
    expect(refused.validPins).toEqual(tagger?.inputPins);
    expect(String(refused.error)).toContain("NoSuchPin");
  });

  it("connect_pcg_nodes says when a label is a pin on the other side", async () => {
    const r = await callBridge(bridge, "connect_pcg_nodes", {
      assetPath: GRAPH, sourceNode: "Sampler", sourcePin: "Volume", targetNode: "Tagger",
    });
    const refused = body(r);
    expect(refused.reason).toBe("pin_not_found");
    expect(refused.validPins).toContain("Out");
    expect(String(refused.error)).toContain("is one of its input pins");
  });

  it("disconnect_pcg_nodes refuses an unknown pin instead of reporting it already disconnected", async () => {
    const r = await callBridge(bridge, "disconnect_pcg_nodes", {
      assetPath: GRAPH, sourceNode: "DefaultInputNode", sourcePin: "In", targetNode: "Sampler", targetPin: "NoSuchPin",
    });
    const refused = body(r);
    expect(refused.success).toBe(false);
    expect(refused.reason).toBe("pin_not_found");
    expect(refused.validPins).toContain("Volume");
    const edges = (await call("read_pcg_graph", { assetPath: GRAPH })).edges as Array<{ to: string; toPin: string }>;
    expect(edges.some((e) => e.to === "Sampler" && e.toPin === "Volume")).toBe(true);
  });
});
