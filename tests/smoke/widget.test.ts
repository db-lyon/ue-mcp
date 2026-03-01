import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/WBP_SmokeTest` });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/EUW_SmokeTest` });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/EUB_SmokeTest` });
  disconnectBridge();
});

describe("widget — read / list", () => {
  it("list_widget_blueprints", async () => {
    const r = await callBridge(bridge, "list_widget_blueprints", { recursive: true });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("widget — create (with cleanup)", () => {
  it("create_widget_blueprint", async () => {
    const r = await callBridge(bridge, "create_widget_blueprint", {
      name: "WBP_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("read_widget_tree", async () => {
    const r = await callBridge(bridge, "read_widget_tree", {
      assetPath: `${TEST_PREFIX}/WBP_SmokeTest`,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_editor_utility_widget", async () => {
    const r = await callBridge(bridge, "create_editor_utility_widget", {
      path: `${TEST_PREFIX}/EUW_SmokeTest`,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_editor_utility_blueprint", async () => {
    const r = await callBridge(bridge, "create_editor_utility_blueprint", {
      path: `${TEST_PREFIX}/EUB_SmokeTest`,
    });
    expect(r.ok, r.error).toBe(true);
  });
});
