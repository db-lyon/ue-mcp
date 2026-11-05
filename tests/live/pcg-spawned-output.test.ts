// @live-editor headless
/**
 * What a PCG graph spawned, read back: read_pcg_generated_output lists the instanced static mesh components a
 * Static Mesh Spawner built and their instance counts, even when nothing reaches the graph's output node.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import type { ToolContext } from "../../src/core/types.js";
import { waitForGeneration } from "../../src/tools/pcg-wait.js";

const DIR = `${TEST_PREFIX}/PcgSpawned`;
const GRAPH = `${DIR}/PCG_SpawnedProbe`;
const VOLUME = "PCG_SpawnedProbeVolume";
const MESHES = ["/Engine/BasicShapes/Cube.Cube", "/Engine/BasicShapes/Sphere.Sphere"];

type Json = Record<string, unknown>;
type Spawned = { name: string; class: string; instanceCount?: number; mesh?: string };
type Row = { data: unknown[]; spawnedComponents: Spawned[]; spawnedInstanceCount: number; note?: string };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: VOLUME });
  await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
  await call("create_pcg_graph", { name: "PCG_SpawnedProbe", packagePath: DIR });
  // The spawner's output is left unwired, as in a graph that only places meshes.
  await call("import_pcg_graph", {
    assetPath: GRAPH,
    nodes: [
      { name: "Sampler", class: "PCGVolumeSamplerSettings", settings: { VoxelSize: "(X=100,Y=100,Z=200)" } },
      { name: "Spawner", class: "PCGStaticMeshSpawnerSettings" },
    ],
    connections: [
      { from: "DefaultInputNode", fromPin: "In", to: "Sampler", toPin: "Volume" },
      { from: "Sampler", to: "Spawner" },
    ],
  });
  await call("set_static_mesh_spawner_meshes", {
    assetPath: GRAPH, nodeName: "Spawner", entries: MESHES.map((mesh) => ({ mesh, weight: 1 })),
  });
  await call("add_pcg_volume", { graphPath: GRAPH, label: VOLUME, location: { x: 0, y: 8000, z: 100 }, extent: { x: 400, y: 400, z: 100 } });
  const run = await call("execute_pcg_graph", { actorLabel: VOLUME });
  const wait = await waitForGeneration({ bridge } as unknown as ToolContext, {
    actorPath: String(run.actorPath), runId: Number(run.runId), timeoutSeconds: 120,
  });
  expect(wait.state).toBe("generated");
}, 240_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: VOLUME });
    await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
    disconnectBridge();
  }
});

describe("read_pcg_generated_output reports what the graph spawned", () => {
  it("lists the spawned ISM components with their meshes and instance counts", async () => {
    const out = await call("read_pcg_generated_output", { actorLabel: VOLUME });
    const row = (out.components as Row[])[0];
    const isms = row.spawnedComponents.filter((c) => c.class.includes("InstancedStaticMeshComponent"));
    expect(isms.length).toBeGreaterThanOrEqual(1);
    for (const ism of isms) expect(Number(ism.instanceCount)).toBeGreaterThan(0);
    expect(isms.some((c) => MESHES.includes(String(c.mesh)))).toBe(true);
    const sum = isms.reduce((n, c) => n + Number(c.instanceCount), 0);
    expect(row.spawnedInstanceCount).toBe(sum);
    expect(out.spawnedInstanceCount).toBe(sum);
    expect(Number(out.spawnedComponentCount)).toBe(row.spawnedComponents.length);
  });

  it("says why the data list is empty when the spawner feeds nothing to the output node", async () => {
    const out = await call("read_pcg_generated_output", { actorLabel: VOLUME });
    const row = (out.components as Row[])[0];
    expect(row.data).toHaveLength(0);
    expect(row.note).toContain("spawnedComponents");
  });

  it("reports nothing spawned once the component is cleaned up", async () => {
    await call("cleanup_pcg", { actorLabel: VOLUME, removeComponents: true });
    let row: Row | undefined;
    for (let i = 0; i < 40; i++) {
      row = ((await call("read_pcg_generated_output", { actorLabel: VOLUME })).components as Row[])[0];
      if (row.spawnedInstanceCount === 0) break;
      await new Promise((r) => setTimeout(r, 250));
    }
    expect(row?.spawnedInstanceCount).toBe(0);
  }, 60_000);
});
