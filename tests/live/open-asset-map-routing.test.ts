// @live-editor headless
/**
 * `editor(open_asset)` on a map that is not the loaded one (#1422).
 *
 * Opening a map through the asset editor loads it as the editor world and destroys the world the handler held. The
 * handler then read the class name off freed memory: one report got "PoseableMeshComponent" for a World, another an
 * access violation. A map now goes through load_level in every path form, and reports World.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOME = "/Game/MCP_Home";
const MAP = `${TEST_PREFIX}/OpenAsset1422/L_OpenAsset1422`;

type Json = Record<string, unknown>;
let bridge: EditorBridge;

const currentMap = async (): Promise<string | undefined> => {
  const level = await callBridge(bridge, "get_current_level", {});
  expect(level.ok, level.error).toBe(true);
  return (level.result as Json).mapPackagePath as string | undefined;
};

beforeAll(async () => {
  bridge = await getBridge();
  // MCP_Home is not checked in; demo_go_home creates it on a fresh project and loads it.
  await callBridge(bridge, "demo_go_home", {});
  await callBridge(bridge, "load_level", { levelPath: HOME });
  await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
  const created = await callBridge(bridge, "create_new_level", { levelPath: MAP });
  expect(created.ok, created.error).toBe(true);
  expect((created.result as Json).success, JSON.stringify(created.result)).not.toBe(false);
  await callBridge(bridge, "save_level", {});
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "load_level", { levelPath: HOME });
  await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
  disconnectBridge();
}, 300_000);

describe("open_asset on another map (#1422)", () => {
  for (const [form, path] of [
    ["package path", MAP],
    ["object path", `${MAP}.L_OpenAsset1422`],
  ] as const) {
    it(`opens it by its ${form} as the editor world and reports World`, async () => {
      const home = await callBridge(bridge, "load_level", { levelPath: HOME });
      expect(home.ok, home.error).toBe(true);
      expect(await currentMap()).toBe(HOME);

      const opened = await callBridge(bridge, "open_asset", { assetPath: path });
      expect(opened.ok, opened.error).toBe(true);
      const result = opened.result as Json;
      expect(result.success, JSON.stringify(result)).toBe(true);
      expect(result.assetClass).toBe("World");
      expect(result.openedVia).toBe("load_level");
      expect(result.changed).toBe(true);

      // The editor is still answering, and the map it loaded is the one asked for.
      expect(await currentMap()).toBe(MAP);
    }, 300_000);
  }
});
