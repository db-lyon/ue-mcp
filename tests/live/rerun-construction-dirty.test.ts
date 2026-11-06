// @live-editor headless
/**
 * rerun_construction_scripts leaves a package dirty only when the rerun changed the actor (#1547). A rerun that
 * reproduces the saved state puts the package back to clean, so the next save does not rewrite the file.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const BP = `${TEST_PREFIX}/RerunDirty/BP_RerunDirty`;
const LABEL = "RerunDirty_Holder";
const POINTS = [
  { x: 0, y: -14000, z: 100 },
  { x: 1500, y: -14000, z: 100 },
  { x: 3000, y: -13000, z: 100 },
];

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const rerun = (): Promise<Json> => call("rerun_construction_scripts", { actorLabels: [LABEL] });
const dirtyPackages = async (): Promise<string[]> => (await call("get_world_state", {})).dirtyPackages as string[];

beforeAll(async () => {
  bridge = await getBridge();
  // A saved map: an untitled world's /Temp packages are never listed as dirty.
  await call("demo_go_home", {});
  await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  await call("add_component", { assetPath: BP, componentClass: "SplineComponent", componentName: "Path" });
  await call("place_actor", { actorClass: `${BP}.BP_RerunDirty_C`, label: LABEL, location: { x: 0, y: -14000, z: 100 } });
  await call("save_level", {});
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
    await callBridge(bridge, "save_level", {});
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("rerun_construction_scripts dirties only what it changed (#1547)", () => {
  it("a rerun that reproduces the saved actor leaves its package clean", async () => {
    const result = await rerun();
    expect(result.reran).toBe(1);
    expect(result.unchanged).toEqual([LABEL]);
    expect(result.changed).toEqual([]);
    expect(result.updated).toBe(false);
    expect(result.dirtiedPackages).toEqual([]);
    const restored = result.restoredCleanPackages as string[];
    expect(restored.length).toBeGreaterThan(0);
    const dirty = await dirtyPackages();
    for (const pkg of restored) expect(dirty).not.toContain(pkg);
  }, 120_000);

  it("a rerun that changes the actor leaves its package dirty and names it", async () => {
    // Unmarked points are construction output, so the rerun replaces them with the Blueprint's defaults.
    await call("set_spline_points", { actorLabel: LABEL, componentName: "Path", points: POINTS, markEdited: false });
    await call("save_level", {});
    const result = await rerun();
    expect(result.changed).toEqual([LABEL]);
    expect(result.unchanged).toEqual([]);
    expect(result.updated).toBe(true);
    const dirtied = result.dirtiedPackages as string[];
    expect(dirtied.length).toBeGreaterThan(0);
    const dirty = await dirtyPackages();
    for (const pkg of dirtied) expect(dirty).toContain(pkg);
  }, 120_000);
});
