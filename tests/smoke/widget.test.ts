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

  it("read_widget_tree (empty)", async () => {
    const r = await callBridge(bridge, "read_widget_tree", {
      assetPath: `${TEST_PREFIX}/WBP_SmokeTest`,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("add_widget — root CanvasPanel", async () => {
    const r = await callBridge(bridge, "add_widget", {
      assetPath: `${TEST_PREFIX}/WBP_SmokeTest`,
      widgetClass: "CanvasPanel",
      widgetName: "RootCanvas",
    });
    expect(r.ok, r.error).toBe(true);
    const res = r.result as Record<string, unknown>;
    expect(res.isRoot).toBe(true);
  });

  it("add_widget — child TextBlock", async () => {
    const r = await callBridge(bridge, "add_widget", {
      assetPath: `${TEST_PREFIX}/WBP_SmokeTest`,
      widgetClass: "TextBlock",
      widgetName: "Txt_Hello",
      parentWidgetName: "RootCanvas",
    });
    expect(r.ok, r.error).toBe(true);
    const res = r.result as Record<string, unknown>;
    expect(res.parentWidgetName).toBe("RootCanvas");
  });

  it("read_widget_tree (populated)", async () => {
    const r = await callBridge(bridge, "read_widget_tree", {
      assetPath: `${TEST_PREFIX}/WBP_SmokeTest`,
    });
    expect(r.ok, r.error).toBe(true);
    const res = r.result as Record<string, unknown>;
    const tree = res.widgetTree as Record<string, unknown>;
    expect(tree).toBeDefined();
    expect(tree.name).toBe("RootCanvas");
    expect((tree.children as unknown[]).length).toBeGreaterThanOrEqual(1);
  });

  it("remove_widget — child TextBlock", async () => {
    const r = await callBridge(bridge, "remove_widget", {
      assetPath: `${TEST_PREFIX}/WBP_SmokeTest`,
      widgetName: "Txt_Hello",
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
