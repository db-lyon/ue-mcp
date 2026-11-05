// @live-editor headless
/**
 * `editor(open_asset)` on the map that is already loaded (#1218).
 *
 * Opening a World loads it as the editor map, which destroys the map loaded before it. Asked for that same map, the
 * load destroyed the world the handler held under a GC scope guard, and the editor died on its world-leak check. The
 * handler now answers alreadyOpen without loading anything; the editor has to still be there afterwards.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const MAP = "/Game/MCP_Home";

let bridge: EditorBridge;

beforeAll(async () => {
  bridge = await getBridge();
  // MCP_Home is not checked in; demo_go_home creates and saves it on a fresh project.
  const home = await callBridge(bridge, "demo_go_home", {});
  expect(home.ok, home.error).toBe(true);
});

afterAll(() => {
  if (bridge) {
    disconnectBridge();
  }
});

describe("open_asset on the loaded map (#1218)", () => {
  it("reports it already open and leaves the editor running", async () => {
    // A saved map, loaded first: earlier suites can leave a transient Untitled world, which is not an asset.
    // Earlier suites can leave an unsaved Untitled map, which load_level refuses to drop unless told to.
    const loaded = await callBridge(bridge, "load_level", { levelPath: MAP, discardUnsaved: true });
    expect(loaded.ok, loaded.error).toBe(true);
    expect((loaded.result as { success?: boolean }).success, JSON.stringify(loaded.result)).not.toBe(false);

    const before = await callBridge(bridge, "get_current_level", {});
    expect(before.ok, before.error).toBe(true);
    expect((before.result as { mapPackagePath?: string }).mapPackagePath).toBe(MAP);

    const opened = await callBridge(bridge, "open_asset", { assetPath: MAP });
    expect(opened.ok, opened.error).toBe(true);
    const result = opened.result as { success?: boolean; alreadyOpen?: boolean; changed?: boolean };
    expect(result.success, JSON.stringify(opened.result)).toBe(true);
    expect(result.alreadyOpen).toBe(true);
    expect(result.changed).toBe(false);

    // The crash took the editor down; a second call proves the bridge and the same world are still there.
    const after = await callBridge(bridge, "get_current_level", {});
    expect(after.ok, after.error).toBe(true);
    expect((after.result as { levelPath?: string }).levelPath).toBe((before.result as { levelPath?: string }).levelPath);
  });
});
