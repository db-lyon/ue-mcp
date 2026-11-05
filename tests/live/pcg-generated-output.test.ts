// @live-editor headless
/**
 * PCG generation state and generated output (#1365, #1309), read back from a
 * volume the test generates.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/PcgOutput`;
const GRAPH = `${DIR}/PCG_OutputProbe`;
const VOLUME = "PCG_OutputProbeVolume";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function finishCount(): Promise<number> {
  const state = await call("get_pcg_generation_state", { actorLabel: VOLUME });
  return Number(state.finishCount ?? 0);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: VOLUME });
  await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
  await call("create_pcg_graph", { name: "PCG_OutputProbe", packagePath: DIR });
  await call("import_pcg_graph", {
    assetPath: GRAPH,
    nodes: [{ name: "Sampler", class: "PCGVolumeSamplerSettings", settings: { VoxelSize: "(X=100,Y=100,Z=200)" } }],
    connections: [
      { from: "DefaultInputNode", fromPin: "In", to: "Sampler", toPin: "Volume" },
      { from: "Sampler", to: "DefaultOutputNode", toPin: "Out" },
    ],
  });
  await call("add_pcg_volume", { graphPath: GRAPH, label: VOLUME, location: { x: 0, y: 4000, z: 100 }, extent: { x: 200, y: 200, z: 100 } });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: VOLUME });
    await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
    disconnectBridge();
  }
});

describe("PCG generation state and output", () => {
  it("get_pcg_generation_state counts a finished generation (#1365)", async () => {
    const before = await finishCount();
    await call("force_regenerate_pcg", { actorLabel: VOLUME });
    let after = before;
    for (let i = 0; i < 40 && after <= before; i++) {
      await new Promise((r) => setTimeout(r, 250));
      after = await finishCount();
    }
    expect(after).toBeGreaterThan(before);
    const state = await call("get_pcg_generation_state", { actorLabel: VOLUME });
    expect(state.isGenerating).toBe(false);
    expect(state.lastFinishState).toBe("generated");
  }, 120_000);

  it("read_pcg_generated_output pages the sampled points with their positions (#1309)", async () => {
    const out = await call("read_pcg_generated_output", { actorLabel: VOLUME, attributes: ["$Position", "$Density"], limit: 3 });
    const data = (out.components as Array<{ data: Array<Json> }>)[0].data[0];
    expect(data.kind).toBe("points");
    expect(Number(data.count)).toBeGreaterThan(3);
    const entries = data.entries as Array<{ $Position: { y: number }; $Density: number }>;
    expect(entries).toHaveLength(3);
    for (const e of entries) {
      expect(e.$Position.y).toBeGreaterThan(3700);
      expect(e.$Position.y).toBeLessThan(4300);
    }
  }, 120_000);
});
