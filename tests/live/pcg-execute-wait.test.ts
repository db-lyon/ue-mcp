// @live-editor headless
/**
 * execute_pcg_graph followed by the server's wait (the code behind wait=true), against a real editor: the wait
 * reports the run that actually happened, and a run whose Generate started nothing says what the component holds.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import type { ToolContext } from "../../src/core/types.js";
import { waitForGeneration } from "../../src/tools/pcg-wait.js";

const DIR = `${TEST_PREFIX}/PcgExecuteWait`;
const GRAPH = `${DIR}/PCG_ExecuteWait`;
const VOLUME = "PCG_ExecuteWaitVolume";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;
let ctx: ToolContext;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  ctx = { bridge } as unknown as ToolContext;
  await callBridge(bridge, "delete_actor", { actorLabel: VOLUME });
  await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
  await call("create_pcg_graph", { name: "PCG_ExecuteWait", packagePath: DIR });
  await call("import_pcg_graph", {
    assetPath: GRAPH,
    nodes: [{ name: "Sampler", class: "PCGVolumeSamplerSettings", settings: { VoxelSize: "(X=100,Y=100,Z=200)" } }],
    connections: [
      { from: "DefaultInputNode", fromPin: "In", to: "Sampler", toPin: "Volume" },
      { from: "Sampler", to: "DefaultOutputNode", toPin: "Out" },
    ],
  });
  await call("add_pcg_volume", { graphPath: GRAPH, label: VOLUME, location: { x: 0, y: 6000, z: 100 }, extent: { x: 200, y: 200, z: 100 } });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: VOLUME });
    await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
    disconnectBridge();
  }
});

describe("execute_pcg_graph wait reports the actual run", () => {
  it("a regenerate the wait follows ends as generated, matching the run record", async () => {
    const before = Number((await call("get_pcg_generation_state", { actorLabel: VOLUME })).finishCount ?? 0);
    const run = await call("execute_pcg_graph", { actorLabel: VOLUME, seed: 11 });
    const wait = await waitForGeneration(ctx, { actorPath: String(run.actorPath), runId: Number(run.runId), timeoutSeconds: 120 });
    expect(wait.state).toBe("generated");
    expect((wait.lastRun as { id: number; state: string }).id).toBe(run.runId);
    expect((wait.lastRun as { state: string }).state).toBe("generated");
    const state = await call("get_pcg_generation_state", { actorLabel: VOLUME });
    expect(Number(state.finishCount)).toBeGreaterThan(before);
    expect(state.generated).toBe(true);
  }, 180_000);

  it("a second regenerate is either followed to its finish or reported with the output already there", async () => {
    const run = await call("execute_pcg_graph", { actorLabel: VOLUME });
    const wait = await waitForGeneration(ctx, { actorPath: String(run.actorPath), runId: Number(run.runId), timeoutSeconds: 120 });
    expect(["generated", "not_started"]).toContain(wait.state);
    if (wait.state === "not_started") {
      expect(wait.alreadyGenerated).toBe(true);
      expect(wait.lastFinishState).toBe("generated");
    } else {
      expect((wait.lastRun as { state: string }).state).toBe("generated");
    }
  }, 180_000);
});
