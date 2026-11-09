// @live-editor headless
/**
 * force_reload_asset with assetPaths: a material and its instance re-read from
 * disk in one pass, each reported under results, and a set holding a missing
 * package refused before anything is released. Refs #1645
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/ForceReloadBatch";
const PARENT = `${DIR}/M_ReloadParent`;
const CHILD = `${DIR}/MI_ReloadChild`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_folder", { path: DIR });
  await call("create_material", { name: "M_ReloadParent", packagePath: DIR });
  await call("create_material_instance", { name: "MI_ReloadChild", packagePath: DIR, parentPath: PARENT });
  await call("save_asset", { assetPaths: [PARENT, CHILD] });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("force_reload_asset assetPaths", () => {
  it("re-reads a material and its instance together as fresh objects", async () => {
    const reloaded = await call("force_reload_asset", { assetPaths: [PARENT, CHILD] });
    expect(reloaded.packages).toBe(2);
    expect(reloaded.reloadedCount).toBe(2);
    expect(reloaded.failedCount).toBe(0);
    const rows = reloaded.results as Json[];
    expect(rows.map((r) => r.packageName)).toEqual([PARENT, CHILD]);
    for (const row of rows) {
      expect(row.wasLoaded).toBe(true);
      expect(row.objectReplaced).toBe(true);
      expect(row.reloaded).toBe(true);
    }

    // The re-read instance points at the re-read parent.
    const instance = await call("read_material_instance", { assetPath: CHILD });
    expect(JSON.stringify(instance)).toContain("M_ReloadParent");
  }, 120_000);

  it("refuses the whole set when one package is not on disk", async () => {
    const r = await callBridge(bridge, "force_reload_asset", { assetPaths: [PARENT, `${DIR}/M_DoesNotExist`] });
    expect(r.ok && body(r).success !== false).toBe(false);
    expect(`${r.error ?? ""}${JSON.stringify(r.result ?? {})}`).toContain("M_DoesNotExist");
  }, 60_000);
});
