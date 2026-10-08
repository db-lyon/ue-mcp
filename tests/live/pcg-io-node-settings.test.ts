/**
 * A graph's input and output nodes are addressable by name in every PCG handler (#1324), against a real editor.
 *
 * UPCGGraph::GetNodes() leaves out DefaultInputNode and DefaultOutputNode, so a handler that searched only that list
 * reported "Node not found" for a node read_graph had just named. The concrete use: writing Pins on a subgraph's
 * input node adds a graph input pin, and a Subgraph node calling that graph grows the same pin.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const SUB = `${TEST_PREFIX}/PCG_IONode_Sub`;
const HOST = `${TEST_PREFIX}/PCG_IONode_Host`;
const INPUT = "DefaultInputNode";
const OUTPUT = "DefaultOutputNode";

type Pin = string | { label: string };
type Body = { success?: boolean; error?: string; errors?: string[]; inputPins?: Pin[]; outputPins?: Pin[]; settings?: Record<string, unknown> };

let bridge: EditorBridge;
let subgraphNode = "";

const labels = (pins: Pin[] | undefined): string[] => (pins ?? []).map((p) => (typeof p === "string" ? p : p.label));

/** A handler refusal arrives as a result with success:false; only a transport failure sets ok:false. */
const body = (r: { ok: boolean; error?: string; result?: unknown }): Body => {
  expect(r.ok, r.error).toBe(true);
  return r.result as Body;
};

const ok = (r: { ok: boolean; error?: string; result?: unknown }): Body => {
  const b = body(r);
  expect(b.success, JSON.stringify(b)).not.toBe(false);
  return b;
};

type PinsRead = { value: string; structured: Array<Record<string, unknown>> };

/** An input or output node's Pins as read_node_settings reports them: export text and the structured array. */
const pinsOf = async (nodeName: string): Promise<PinsRead> => {
  const read = ok(await callBridge(bridge, "read_pcg_node_settings", { assetPath: SUB, nodeName }));
  const pins = read.settings?.Pins as PinsRead | undefined;
  expect(typeof pins?.value, JSON.stringify(read.settings?.Pins)).toBe("string");
  expect(Array.isArray(pins?.structured), JSON.stringify(read.settings?.Pins)).toBe(true);
  return pins!;
};

/** The host's Subgraph node input pins, as the engine has them now. */
const hostInputPins = async (): Promise<string[]> =>
  labels(ok(await callBridge(bridge, "read_pcg_node_settings", { assetPath: HOST, nodeName: subgraphNode })).inputPins);

/** The first top-level element of an array's export text, "((A=1,B=(C=2)),(...))" -> "(A=1,B=(C=2))". */
const firstElement = (text: string): string => {
  expect(text.startsWith("(("), `unexpected Pins export text: ${text}`).toBe(true);
  let depth = 0;
  let quoted = false;
  for (let i = 1; i < text.length; i++) {
    const c = text[i];
    if (c === '"' && text[i - 1] !== "\\") quoted = !quoted;
    if (quoted) continue;
    if (c === "(") depth++;
    if (c === ")" && --depth === 0) return text.slice(1, i + 1);
  }
  throw new Error(`unbalanced Pins export text: ${text}`);
};

/** Pins export text with one more element: a copy of the first, relabelled. */
const withExtraPin = (text: string, label: string): string => {
  const element = firstElement(text).replace(/Label="[^"]*"/, `Label="${label}"`);
  return `${text.slice(0, -1)},${element})`;
};

beforeAll(async () => {
  bridge = await getBridge();
  for (const assetPath of [HOST, SUB]) await callBridge(bridge, "delete_asset", { assetPath, force: true });
  ok(await callBridge(bridge, "create_pcg_graph", { name: "PCG_IONode_Sub", packagePath: TEST_PREFIX }));
  ok(await callBridge(bridge, "create_pcg_graph", { name: "PCG_IONode_Host", packagePath: TEST_PREFIX }));
  const added = ok(await callBridge(bridge, "add_pcg_node", { assetPath: HOST, nodeType: "PCGSubgraphSettings" }));
  subgraphNode = (added as { nodeName?: string }).nodeName ?? "";
  ok(await callBridge(bridge, "set_pcg_subgraph", { assetPath: HOST, nodeName: subgraphNode, subgraphPath: SUB }));
});

afterAll(async () => {
  if (bridge) {
    for (const assetPath of [HOST, SUB]) await callBridge(bridge, "delete_asset", { assetPath, force: true });
    disconnectBridge();
  }
});

describe("graph input and output nodes are addressable (#1324)", () => {
  it("set_node_settings: Pins as export text adds a graph input pin, and the calling Subgraph node grows it", async () => {
    const before = await hostInputPins();
    expect(before).not.toContain("Roads");

    const set = ok(await callBridge(bridge, "set_pcg_node_settings", {
      assetPath: SUB, nodeName: INPUT, propertyName: "Pins", propertyValue: withExtraPin((await pinsOf(INPUT)).value, "Roads"),
    }));
    expect(labels(set.outputPins)).toContain("Roads");

    const graph = ok(await callBridge(bridge, "read_pcg_graph", { assetPath: SUB })) as Body & { inputNode?: { outputPins?: string[] } };
    expect(graph.inputNode?.outputPins).toContain("Roads");
    const after = await hostInputPins();
    expect(after).toHaveLength(before.length + 1);
    expect(after).toEqual(expect.arrayContaining([...before, "Roads"]));
  });

  it("set_node_settings: Pins as a JSON array in a settings object adds a second pin", async () => {
    const { structured } = await pinsOf(INPUT);
    const set = ok(await callBridge(bridge, "set_pcg_node_settings", {
      assetPath: SUB, nodeName: INPUT, settings: { Pins: [...structured, { Label: "Rivers", AllowedTypes: structured[0].AllowedTypes }] },
    }));
    expect(labels(set.outputPins)).toEqual(expect.arrayContaining(["Roads", "Rivers"]));
    expect(await hostInputPins()).toEqual(expect.arrayContaining(["Roads", "Rivers"]));
    expect((await pinsOf(INPUT)).structured.map((p) => p.Label)).toEqual(["In", "Roads", "Rivers"]);
  });

  it("set_node_settings: the output node is addressable too, export text in a settings object", async () => {
    const set = ok(await callBridge(bridge, "set_pcg_node_settings", {
      assetPath: SUB, nodeName: OUTPUT, settings: { Pins: withExtraPin((await pinsOf(OUTPUT)).value, "Debug") },
    }));
    expect(labels(set.inputPins)).toContain("Debug");
    const host = ok(await callBridge(bridge, "read_pcg_node_settings", { assetPath: HOST, nodeName: subgraphNode }));
    expect(labels(host.outputPins)).toContain("Debug");
  });

  it("set_node_settings: a failed write carries its reason in error", async () => {
    const bad = body(await callBridge(bridge, "set_pcg_node_settings", {
      assetPath: SUB, nodeName: INPUT, settings: { NotAProperty: 1 },
    }));
    expect(bad.success).toBe(false);
    expect(bad.error).toContain("NotAProperty");
    expect(bad.errors?.length).toBe(1);
  });

  it("set_node_settings: a guessed input node name points at the real one", async () => {
    const miss = body(await callBridge(bridge, "set_pcg_node_settings", {
      assetPath: SUB, nodeName: "Input", propertyName: "Pins", propertyValue: "()",
    }));
    expect(miss.success).toBe(false);
    expect(miss.error).toContain(INPUT);
  });

  it("remove_node: refuses the input and output nodes and leaves them in place", async () => {
    for (const nodeName of [INPUT, OUTPUT]) {
      const refused = body(await callBridge(bridge, "remove_pcg_node", { assetPath: SUB, nodeName }));
      expect(refused.success).toBe(false);
      expect(refused.error).toContain("cannot be removed");
    }
    const graph = ok(await callBridge(bridge, "read_pcg_graph", { assetPath: SUB })) as Body & { inputNode?: { name: string }; outputNode?: { name: string } };
    expect(graph.inputNode?.name).toBe(INPUT);
    expect(graph.outputNode?.name).toBe(OUTPUT);
  });

  it("set_subgraph: names the input node as the wrong kind of node, not as missing", async () => {
    const wrong = body(await callBridge(bridge, "set_pcg_subgraph", { assetPath: SUB, nodeName: INPUT, subgraphPath: HOST }));
    expect(wrong.success).toBe(false);
    expect(wrong.error).toContain("not a Subgraph node");
  });
});
