// @live-editor headless
/**
 * Settings writes refresh a PCG node's pins (#1256), against a real editor.
 *
 * Load PCG Data Asset rebuilds its output pins from its Asset, but only on a change event naming that property.
 * A bare PostEditChange left the node on its single "Out" pin until the graph reloaded, and every edge wired to
 * "Out" in the meantime was dropped at that reload. The fixture asset is exported from the test project's own map.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const MAP = "/Game/MCP_Home";
const GRAPH = `${TEST_PREFIX}/PCG_PinRefresh`;
const DATA = `${TEST_PREFIX}/PCG_PinRefresh_Data`;

let bridge: EditorBridge;

const outputPins = async (nodeName: string): Promise<string[]> => {
  const read = await callBridge(bridge, "read_pcg_node_settings", { assetPath: GRAPH, nodeName });
  expect(read.ok, read.error).toBe(true);
  return ((read.result as { outputPins?: { label: string }[] }).outputPins ?? []).map((p) => p.label);
};

beforeAll(async () => {
  bridge = await getBridge();
  // MCP_Home is not checked in; demo_go_home creates and saves it on a fresh project.
  const home = await callBridge(bridge, "demo_go_home", {});
  expect(home.ok, home.error).toBe(true);
  for (const assetPath of [GRAPH, DATA]) await callBridge(bridge, "delete_asset", { assetPath, force: true });
  const exported = await callBridge(bridge, "export_level_to_pcg_asset", { levelPath: MAP, assetPath: TEST_PREFIX, assetName: "PCG_PinRefresh_Data" });
  expect(exported.ok, exported.error).toBe(true);
  expect((exported.result as { success?: boolean }).success, JSON.stringify(exported.result)).toBe(true);
  const created = await callBridge(bridge, "create_pcg_graph", { name: "PCG_PinRefresh", packagePath: TEST_PREFIX });
  expect(created.ok, created.error).toBe(true);
});

afterAll(async () => {
  if (bridge) {
    for (const assetPath of [GRAPH, DATA]) await callBridge(bridge, "delete_asset", { assetPath, force: true });
    disconnectBridge();
  }
});

describe("settings writes refresh the node's pins (#1256)", () => {
  it("import_graph: a Load node given an Asset exposes the asset's pins at once", async () => {
    const imported = await callBridge(bridge, "import_pcg_graph", {
      assetPath: GRAPH,
      nodes: [{ name: "LoadImported", class: "PCGLoadDataAssetSettings", settings: { Asset: DATA } }],
    });
    expect(imported.ok, imported.error).toBe(true);
    expect(await outputPins("LoadImported")).toEqual(["Root", "Points"]);
  });

  it("set_node_settings: reports the refreshed pins", async () => {
    const added = await callBridge(bridge, "add_pcg_node", { assetPath: GRAPH, nodeType: "PCGLoadDataAssetSettings" });
    expect(added.ok, added.error).toBe(true);
    const nodeName = (added.result as { nodeName: string }).nodeName;
    expect(await outputPins(nodeName)).toEqual(["Out"]);

    const set = await callBridge(bridge, "set_pcg_node_settings", { assetPath: GRAPH, nodeName, propertyName: "Asset", propertyValue: DATA });
    expect(set.ok, set.error).toBe(true);
    expect((set.result as { outputPins?: string[] }).outputPins).toEqual(["Root", "Points"]);
  });
});
