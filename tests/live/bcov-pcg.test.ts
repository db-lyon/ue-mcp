// @live-editor headless
/**
 * PCG graph listing, component reads, graph swaps, cleanup, instance-node
 * unwrapping and level-to-asset export, each read back from the editor.
 * Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Pcg`;
const GRAPH_A = `${DIR}/PCG_BCovA`;
const GRAPH_B = `${DIR}/PCG_BCovB`;
const GRAPH_UNWRAP = `${DIR}/PCG_BCovUnwrap`;
const VOLUME = "BCov_Pcg_Volume";
const LEVEL_ASSET_NAME = "BCovLevel_PCG";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function python(code: string): Promise<string> {
  const out = await call("execute_python", { code });
  return String(out.output ?? "").trim();
}

const sampler = (path: string) => ({
  assetPath: path,
  nodes: [{ name: "Sampler", class: "PCGVolumeSamplerSettings", settings: { VoxelSize: "(X=100,Y=100,Z=200)" } }],
  connections: [
    { from: "DefaultInputNode", fromPin: "In", to: "Sampler", toPin: "Volume" },
    { from: "Sampler", to: "DefaultOutputNode", toPin: "Out" },
  ],
});

interface ComponentRow { actorName: string; graphName?: string }
async function volumeRow(): Promise<ComponentRow | undefined> {
  const comps = await call("get_pcg_components");
  return (comps.components as ComponentRow[]).find((c) => c.actorName === VOLUME);
}

async function generationState(): Promise<Json> {
  return call("get_pcg_generation_state", { actorLabel: VOLUME });
}

async function waitFor(check: () => Promise<boolean>, tries = 60): Promise<boolean> {
  for (let i = 0; i < tries; i++) {
    if (await check()) return true;
    await new Promise((r) => setTimeout(r, 250));
  }
  return false;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: VOLUME });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_pcg_graph", { name: "PCG_BCovA", packagePath: DIR });
  await call("import_pcg_graph", sampler(GRAPH_A));
  await call("create_pcg_graph", { name: "PCG_BCovB", packagePath: DIR });
  await call("import_pcg_graph", sampler(GRAPH_B));
  await call("add_pcg_volume", { graphPath: GRAPH_A, label: VOLUME, location: { x: 4000, y: 4000, z: 100 }, extent: { x: 200, y: 200, z: 100 } });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: VOLUME });
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
});

describe("PCG graphs and components", () => {
  it("list_pcg_graphs pages to both graphs the test created", async () => {
    const paths: string[] = [];
    let cursor: string | undefined;
    for (let page = 0; page < 50; page++) {
      const out = await call("list_pcg_graphs", cursor ? { cursor, limit: 500 } : { limit: 500 });
      for (const g of out.graphs as Array<{ path: string }>) paths.push(g.path);
      cursor = out.nextCursor as string | undefined;
      if (!cursor) break;
    }
    expect(paths).toContain(`${GRAPH_A}.PCG_BCovA`);
    expect(paths).toContain(`${GRAPH_B}.PCG_BCovB`);
  }, 120_000);

  it("get_pcg_components reports the volume with the graph it was given", async () => {
    const row = await volumeRow();
    expect(row, "volume missing from get_pcg_components").toBeDefined();
    expect(row?.graphName).toBe("PCG_BCovA");
  }, 60_000);

  it("toggle_pcg_graph swaps the component's graph and swaps it back", async () => {
    const swapped = await call("toggle_pcg_graph", { actorLabel: VOLUME, graphPath: GRAPH_B });
    expect(String(swapped.previousGraphPath)).toContain("PCG_BCovA");
    expect((await volumeRow())?.graphName).toBe("PCG_BCovB");

    const back = await call("toggle_pcg_graph", { actorLabel: VOLUME, graphPath: GRAPH_A });
    expect(String(back.previousGraphPath)).toContain("PCG_BCovB");
    expect((await volumeRow())?.graphName).toBe("PCG_BCovA");
  }, 60_000);

  it("cleanup_pcg clears what a generation produced", async () => {
    const before = Number((await generationState()).finishCount ?? 0);
    await call("force_regenerate_pcg", { actorLabel: VOLUME });
    expect(await waitFor(async () => Number((await generationState()).finishCount ?? 0) > before)).toBe(true);
    expect((await generationState()).generated).toBe(true);

    const cleaned = await call("cleanup_pcg", { actorLabel: VOLUME, removeComponents: true });
    expect(cleaned.wasGenerated).toBe(true);
    expect(await waitFor(async () => (await generationState()).generated === false)).toBe(true);
  }, 120_000);
});

describe("PCG instance nodes", () => {
  it("unwrap_pcg_instance_nodes gives an instance node its own settings", async () => {
    await call("create_pcg_graph", { name: "PCG_BCovUnwrap", packagePath: DIR });
    // No bridge action builds an instance node, so the fixture uses the engine API directly.
    const nodeName = await python([
      "import unreal",
      `g = unreal.load_asset('${GRAPH_UNWRAP}')`,
      "s = unreal.new_object(unreal.PCGSurfaceSamplerSettings, outer=g)",
      "n = g.add_node_instance(s)",
      "unreal.EditorAssetLibrary.save_loaded_asset(g)",
      "print(n.get_name())",
    ].join("\n"));
    expect(nodeName).toBeTruthy();

    const first = await call("unwrap_pcg_instance_nodes", { assetPath: GRAPH_UNWRAP, nodeName });
    expect(first.unwrappedCount).toBe(1);
    const row = (first.unwrapped as Array<{ nodeName: string; settingsClass: string }>)[0];
    expect(row.nodeName).toBe(nodeName);
    expect(row.settingsClass).toBe("PCGSurfaceSamplerSettings");

    // Read back: the same node now owns its settings, so a second pass leaves it alone.
    const second = await call("unwrap_pcg_instance_nodes", { assetPath: GRAPH_UNWRAP, nodeName });
    expect(second.unwrappedCount).toBe(0);
    expect(second.alreadyOwnedCount).toBe(1);
  }, 120_000);
});

describe("PCG level assets", () => {
  it("export_level_to_pcg_asset writes a data asset update_pcg_level_assets re-exports", async () => {
    // Export reads a saved level; whatever an earlier file left open may be an unsaved one.
    await call("demo_go_home", {});
    const levelPackage = await python(
      "import unreal; print(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_outermost().get_name())",
    );
    expect(levelPackage.startsWith("/Game/"), `the open level '${levelPackage}' is not a saved level`).toBe(true);

    const exported = await call("export_level_to_pcg_asset", { levelPath: levelPackage, assetPath: DIR, assetName: LEVEL_ASSET_NAME });
    expect(exported.created).toBe(true);
    const assetPath = String(exported.assetPath);
    expect(assetPath).toBe(`${DIR}/${LEVEL_ASSET_NAME}.${LEVEL_ASSET_NAME}`);
    expect(Array.isArray(exported.pins)).toBe(true);

    const listed = await call("list_assets", { directory: DIR, recursive: false });
    const row = (listed.assets as Array<{ name: string; className: string }>).find((a) => a.name === LEVEL_ASSET_NAME);
    expect(row?.className).toBe("PCGDataAsset");

    const updated = await call("update_pcg_level_assets", { assetPaths: [assetPath] });
    const assets = updated.assets as Array<{ assetPath: string; pins: unknown[] }>;
    expect(assets).toHaveLength(1);
    expect(assets[0].assetPath).toBe(assetPath);
    expect(assets[0].pins).toEqual(exported.pins);
  }, 180_000);
});
