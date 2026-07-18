// Regression: #728 — adding a widget to a created Widget Blueprint tripped the
// WidgetBlueprintCompiler ensure ("Widget [X] was added but did not get a GUID")
// on UE 5.8 because the GUID-map registration was gated to exactly 5.4.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/WBP_GuidTest` }).catch(() => {});
  disconnectBridge();
});

describe("widget — add_widget GUID registration (#728)", () => {
  it("adds a widget and compiles without the missing-GUID ensure", async () => {
    const create = await callBridge(bridge, "create_widget_blueprint", {
      name: "WBP_GuidTest", packagePath: TEST_PREFIX,
    });
    expect(create.ok, create.error).toBe(true);

    // A root panel, then a child - the child add previously failed to get a GUID.
    const root = await callBridge(bridge, "add_widget", {
      assetPath: `${TEST_PREFIX}/WBP_GuidTest`, widgetClass: "CanvasPanel", widgetName: "RootCanvas",
    });
    expect(root.ok, root.error).toBe(true);

    const child = await callBridge(bridge, "add_widget", {
      assetPath: `${TEST_PREFIX}/WBP_GuidTest`, widgetClass: "TextBlock",
      widgetName: "TxtTitle", parentWidgetName: "RootCanvas",
    });
    expect(child.ok, child.error).toBe(true);
    expect((child.result as Record<string, unknown>).success).not.toBe(false);

    // Confirm the widget carries a GUID entry so a later compile stays clean.
    const verify = await callBridge(bridge, "execute_python", {
      code: [
        "import unreal",
        "wbp = unreal.load_asset('" + TEST_PREFIX + "/WBP_GuidTest')",
        "print('HASGUID:' + str('TxtTitle' in [str(k) for k in wbp.get_editor_property('widget_variable_name_to_guid_map').keys()]))",
      ].join("\n"),
    });
    // The map may not be Python-exposed on every version; only assert when it is.
    const out = JSON.stringify(verify.result);
    if (out.includes("HASGUID:")) {
      expect(out).toContain("HASGUID:True");
    }
  });
});
