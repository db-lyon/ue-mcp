// @live-editor headless
/**
 * PCG Custom HLSL node source and edge validation (#1828), in a scratch graph under /Game/MCPTest/BCov/PcgHlsl:
 * get_custom_hlsl and set_custom_hlsl read and write a Custom HLSL node's kernel text, read back through
 * get_custom_hlsl; validate_pcg_edge answers whether an edge can be added without changing the graph, and
 * connect_pcg_nodes says why the graph refused one.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/PcgHlsl`;
const GRAPH = `${DIR}/PCG_BCovHlsl`;
const SOURCE = "// BCov kernel body\n";
const FUNCTIONS = "/** CUSTOM SHADER FUNCTIONS **/\nfloat BCovHalf(float X) { return X * 0.5; }\n";

type Json = Record<string, unknown>;
type Edge = { from: string; fromPin: string; to: string; toPin: string };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let is59 = false;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const edges = async (): Promise<Edge[]> => (await call("read_pcg_graph", { assetPath: GRAPH })).edges as Edge[];

beforeAll(async () => {
  bridge = await getBridge();
  const caps = body(await callBridge(bridge, "get_bridge_capabilities", {}));
  const [major, minor] = String(caps.engineVersion).split(".").map(Number);
  is59 = major > 5 || (major === 5 && minor >= 9);
  await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
  await call("create_pcg_graph", { name: "PCG_BCovHlsl", packagePath: DIR });
  await call("import_pcg_graph", {
    assetPath: GRAPH,
    nodes: [
      { name: "Sampler", class: "PCGVolumeSamplerSettings" },
      { name: "Tagger", class: "PCGAddTagSettings" },
      { name: "Hlsl", class: "PCGCustomHLSLSettings" },
    ],
    connections: [{ from: "Sampler", fromPin: "Out", to: "Tagger", toPin: "In" }],
  });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
    disconnectBridge();
  }
});

describe("Custom HLSL source", () => {
  it("set_custom_hlsl writes the kernel source and functions, and get_custom_hlsl reads them back", async () => {
    const before = await call("get_pcg_custom_hlsl", { assetPath: GRAPH, nodeName: "Hlsl" });
    expect(before.readOnly).toBe(false);
    expect(typeof before.declarationsText).toBe("string");

    const out = await call("set_pcg_custom_hlsl", { assetPath: GRAPH, nodeName: "Hlsl", sourceText: SOURCE, functionsText: FUNCTIONS });
    expect(out.updated).toBe(true);
    expect(out.sourceText).toBe(SOURCE);
    expect((out.rollback as Json).method).toBe("set_pcg_custom_hlsl");
    expect(((out.rollback as Json).payload as Json).sourceText).toBe(before.sourceText);

    const after = await call("get_pcg_custom_hlsl", { assetPath: GRAPH, nodeName: "Hlsl" });
    expect(after.sourceText).toBe(SOURCE);
    expect(after.functionsText).toBe(FUNCTIONS);
    expect((await call("set_pcg_custom_hlsl", { assetPath: GRAPH, nodeName: "Hlsl", sourceText: SOURCE })).unchanged).toBe(true);
  }, 60_000);

  it("get_custom_hlsl and set_custom_hlsl refuse a node that is not a Custom HLSL node", async () => {
    const r = await callBridge(bridge, "get_pcg_custom_hlsl", { assetPath: GRAPH, nodeName: "Tagger" });
    expect(succeeded(r)).toBe(false);
    expect(String(body(r).error)).toContain("not a Custom HLSL node");
    const w = await callBridge(bridge, "set_pcg_custom_hlsl", { assetPath: GRAPH, nodeName: "Tagger", sourceText: SOURCE });
    expect(succeeded(w)).toBe(false);
  }, 60_000);
});

describe("edge validation", () => {
  it("validate_pcg_edge accepts a compatible edge and reports one already there", async () => {
    const out = await call("validate_pcg_edge", { assetPath: GRAPH, sourceNode: "Sampler", sourcePin: "Out", targetNode: "Tagger", targetPin: "In" });
    expect(out.valid).toBe(true);
    expect(out.connectionResult).toBe("Success");
    expect(out.alreadyConnected).toBe(true);
  }, 60_000);

  it("validate_pcg_edge names a missing pin and a cycle without changing the graph", async () => {
    const before = await edges();
    const pin = await call("validate_pcg_edge", { assetPath: GRAPH, sourceNode: "Sampler", targetNode: "Tagger", targetPin: "NoSuchPin" });
    expect(pin.valid).toBe(false);
    expect(pin.connectionResult).toBe("InvalidPin");
    expect(String(pin.reason)).toContain("NoSuchPin");

    const cycle = await call("validate_pcg_edge", { assetPath: GRAPH, sourceNode: "Tagger", sourcePin: "Out", targetNode: "Sampler", targetPin: "Volume" });
    expect(cycle.valid).toBe(false);
    expect(cycle.connectionResult).toBe("CycleDetected");
    expect(await edges()).toEqual(before);
  }, 60_000);

  it("connect_pcg_nodes says why the graph refused an edge", async () => {
    if (!is59) return;
    const r = await callBridge(bridge, "connect_pcg_nodes", { assetPath: GRAPH, sourceNode: "Tagger", sourcePin: "Out", targetNode: "Sampler", targetPin: "Volume" });
    const refused = body(r);
    expect(succeeded(r)).toBe(false);
    expect(refused.connectionResult).toBe("CycleDetected");
    expect(String(refused.connectionReason).length).toBeGreaterThan(0);
    expect((await edges()).some((e) => e.from === "Tagger" && e.to === "Sampler")).toBe(false);
  }, 60_000);
});
