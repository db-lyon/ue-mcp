// @live-editor headless
/**
 * Fields of an instanced subobject a PCG node owns, written through set_node_settings and import_graph: a Static
 * Mesh Spawner's by-attribute selector takes its AttributeName as a dotted key or as a field object, and a key the
 * current selector class lacks names the class and the property that selects it.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/PcgInstanced`;
const GRAPH = `${DIR}/PCG_SelectorFields`;
const BY_ATTRIBUTE = "/Script/PCG.PCGMeshSelectorByAttribute";
const WEIGHTED = "/Script/PCG.PCGMeshSelectorWeighted";

type Json = Record<string, unknown>;
type Instanced = { class: string; properties: Record<string, unknown> };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;
let spawner = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function selector(nodeName: string): Promise<Instanced> {
  const read = await call("read_pcg_node_settings", { assetPath: GRAPH, nodeName });
  const settings = read.settings as Record<string, { instanced?: Instanced }>;
  return settings.MeshSelectorParameters.instanced as Instanced;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
  await call("create_pcg_graph", { name: "PCG_SelectorFields", packagePath: DIR });
  spawner = String((await call("add_pcg_node", { assetPath: GRAPH, nodeType: "PCGStaticMeshSpawnerSettings" })).nodeName);
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: GRAPH, force: true });
    disconnectBridge();
  }
});

describe("instanced subobject fields on PCG nodes", () => {
  it("set_pcg_node_settings writes a field object into the selector the node owns", async () => {
    const set = await call("set_pcg_node_settings", {
      assetPath: GRAPH,
      nodeName: spawner,
      settings: { MeshSelectorType: BY_ATTRIBUTE, MeshSelectorParameters: { AttributeName: "Mesh" } },
    });
    expect(set.setProperties).toHaveProperty(["MeshSelectorParameters.AttributeName"]);
    const sel = await selector(spawner);
    expect(sel.class).toBe("PCGMeshSelectorByAttribute");
    expect(sel.properties.AttributeName).toBe("Mesh");
  });

  it("set_pcg_node_settings writes a dotted key into the owned selector and rolls it back", async () => {
    const set = await call("set_pcg_node_settings", {
      assetPath: GRAPH, nodeName: spawner, settings: { "MeshSelectorParameters.AttributeName": "MeshPath" },
    });
    expect((set.readBack as Json)["MeshSelectorParameters.AttributeName"]).toBe("MeshPath");
    expect((await selector(spawner)).properties.AttributeName).toBe("MeshPath");
    const rollback = (set.rollback as { payload: Json }).payload;
    await call("set_pcg_node_settings", rollback);
    expect((await selector(spawner)).properties.AttributeName).toBe("Mesh");
  });

  it("a field the current selector class lacks names the type property that selects it", async () => {
    await call("set_pcg_node_settings", { assetPath: GRAPH, nodeName: spawner, settings: { MeshSelectorType: WEIGHTED } });
    const r = await callBridge(bridge, "set_pcg_node_settings", {
      assetPath: GRAPH, nodeName: spawner, settings: { "MeshSelectorParameters.AttributeName": "Mesh" },
    });
    expect(body(r).success).toBe(false);
    expect(String(body(r).error)).toContain("MeshSelectorType");
    expect((await selector(spawner)).class).toBe("PCGMeshSelectorWeighted");
  });

  it("import_pcg_graph accepts the selector's field object", async () => {
    await call("import_pcg_graph", {
      assetPath: GRAPH,
      nodes: [{
        name: "ImportedSpawner",
        class: "PCGStaticMeshSpawnerSettings",
        settings: { MeshSelectorParameters: { AttributeName: "FromImport" }, MeshSelectorType: BY_ATTRIBUTE },
      }],
    });
    const sel = await selector("ImportedSpawner");
    expect(sel.class).toBe("PCGMeshSelectorByAttribute");
    expect(sel.properties.AttributeName).toBe("FromImport");
  });
});
