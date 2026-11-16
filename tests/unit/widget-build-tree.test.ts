import { describe, expect, it, vi } from "vitest";
import { z } from "zod";
import { widgetTool } from "../../src/tools/widget.js";
import type { ToolContext } from "../../src/surface/tool-types.js";

// build_tree hands the nested spec to the bridge untouched; the bridge
// validates it whole, builds it, and undoes it on any failure (#1300).
const TREE = {
  widgetClass: "HorizontalBox",
  widgetName: "Row_Key",
  children: [
    { widgetClass: "TextBlock", widgetName: "Label_Key", properties: { Text: "Whose key was it?" } },
    { widgetClass: "Button", widgetName: "Opt_Key_0", children: [{ widgetClass: "TextBlock", widgetName: "OptText_Key_0", properties: { Text: "Mine" } }] },
  ],
};

describe("widget build_tree (#1300)", () => {
  it("forwards the nested tree and parent with the long timeout", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const parsed = z.object(widgetTool.schema).parse({ action: "build_tree", assetPath: "/Game/UI/WBP_Form", tree: TREE, parentWidgetName: "FormRoot" });
    await widgetTool.handler(ctx, parsed);
    const [method, params, timeoutMs] = call.mock.calls[0] as [string, Record<string, unknown>, number];
    expect(method).toBe("build_tree");
    expect(params).toMatchObject({ assetPath: "/Game/UI/WBP_Form", parentWidgetName: "FormRoot" });
    expect(params.tree).toEqual(TREE);
    expect(timeoutMs).toBe(120_000);
  });
});

describe("widget check_structure (#1350)", () => {
  it("routes to check_widget_structure as a read", async () => {
    const call = vi.fn().mockResolvedValue({ success: true, pass: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    await widgetTool.handler(ctx, z.object(widgetTool.schema).parse({ action: "check_structure", assetPath: "/Game/UI/WBP_Form" }));
    const [method, params] = call.mock.calls[0] as [string, Record<string, unknown>];
    expect(method).toBe("check_widget_structure");
    expect(params).toMatchObject({ assetPath: "/Game/UI/WBP_Form" });
    expect(widgetTool.actions.check_structure.effect).toBe("read");
  });
});
