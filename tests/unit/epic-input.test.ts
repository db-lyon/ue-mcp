/**
 * The `input` envelope for wrapped engine tools (#798).
 *
 * A flat top-level parameter used to be dropped on the way to the engine,
 * which answered with "input params Json is empty" and no indication of what
 * to send instead. The envelope is now assembled from the tool's own schema,
 * and a call that still cannot satisfy the schema is refused with the shape
 * to send rather than dispatched.
 */
import { describe, expect, it, vi } from "vitest";
import { resolveEpicToolInput, type EpicInputSchema } from "../../src/epic-input.js";
import { widgetTool } from "../../src/tools/widget.js";
import type { ToolContext } from "../../src/types.js";

const GET_WIDGETS_NAME = "UMGToolSet.UMGToolSet.GetWidgets";
const GET_WIDGETS: EpicInputSchema = {
    properties: {
      widgetBlueprint: {
        type: "object",
        title: "/Script/UMGEditor.WidgetBlueprint",
        properties: { refPath: { type: "string" } },
        required: ["refPath"],
      },
    },
  required: ["widgetBlueprint"],
};

const ADD_WIDGET_NAME = "UMGToolSet.UMGToolSet.AddWidget";
const ADD_WIDGET: EpicInputSchema = {
    properties: {
      widgetBlueprint: {
        type: "object",
        properties: { refPath: { type: "string" } },
        required: ["refPath"],
      },
      parentWidget: { type: "string" },
      widgetClass: { type: "string" },
      widgetDisplayName: { type: "string" },
      childIndex: { type: "integer" },
    },
  required: ["widgetBlueprint", "widgetClass"],
};

const ASSET = "/Game/_Project/UI/Computer/Core/WBP_ComputerTaskbar";

describe("wrapped engine tool input envelope", () => {
  it("fills the tool's single asset reference from the canonical assetPath", () => {
    expect(resolveEpicToolInput(GET_WIDGETS_NAME, GET_WIDGETS, { assetPath: ASSET })).toEqual({
      input: { widgetBlueprint: { refPath: ASSET } },
    });
  });

  it("accepts the asset reference spelled as a plain string or as JSON", () => {
    expect(resolveEpicToolInput(GET_WIDGETS_NAME, GET_WIDGETS, { widgetBlueprint: ASSET })).toEqual({
      input: { widgetBlueprint: { refPath: ASSET } },
    });
    expect(resolveEpicToolInput(GET_WIDGETS_NAME, GET_WIDGETS, { widgetBlueprint: JSON.stringify({ refPath: ASSET }) })).toEqual({
      input: { widgetBlueprint: { refPath: ASSET } },
    });
  });

  it("leaves an explicit input untouched and lets it win over a top-level value", () => {
    const explicit = { widgetBlueprint: { refPath: "/Game/UI/WBP_Explicit" } };
    expect(resolveEpicToolInput(GET_WIDGETS_NAME, GET_WIDGETS, { input: explicit, assetPath: ASSET })).toEqual({ input: explicit });
  });

  it("passes a raw inputJson straight through", () => {
    const raw = JSON.stringify({ widgetBlueprint: { refPath: ASSET } });
    expect(resolveEpicToolInput(GET_WIDGETS_NAME, GET_WIDGETS, { inputJson: raw, assetPath: ASSET })).toEqual({ inputJson: raw });
  });

  it("folds every top-level parameter the tool's own schema names", () => {
    const resolved = resolveEpicToolInput(ADD_WIDGET_NAME, ADD_WIDGET, {
      assetPath: ASSET,
      parentWidget: "HorizontalBox_59",
      widgetClass: "/Script/UMG.Button",
      widgetDisplayName: "StartButton",
      childIndex: 0,
    });
    expect(resolved).toEqual({
      input: {
        widgetBlueprint: { refPath: ASSET },
        parentWidget: "HorizontalBox_59",
        widgetClass: "/Script/UMG.Button",
        widgetDisplayName: "StartButton",
        childIndex: 0,
      },
    });
  });

  it("refuses a call it cannot complete, naming the missing arguments and the shape", () => {
    expect(() => resolveEpicToolInput(ADD_WIDGET_NAME, ADD_WIDGET, { assetPath: ASSET }))
      .toThrow(/missing required argument\(s\): widgetClass/);
    expect(() => resolveEpicToolInput(GET_WIDGETS_NAME, GET_WIDGETS, {}))
      .toThrow(/"widgetBlueprint": \{ "refPath": "\/Game\/UI\/WBP_Example" \}/);
  });

  it("reports top-level parameters that are not arguments of the tool", () => {
    expect(() => resolveEpicToolInput(GET_WIDGETS_NAME, GET_WIDGETS, { widgetName: "StartButton" }))
      .toThrow(/not arguments of this tool|not arguments|were not sent: widgetName/);
  });

  it("keeps passing input through for a tool that publishes no schema", () => {
    const bare: EpicInputSchema | undefined = undefined;
    expect(resolveEpicToolInput("Some.Toolset.Tool", bare, { input: { a: 1 } })).toEqual({ input: { a: 1 }, inputJson: undefined });
  });

  it("reaches the bridge with the envelope built, end to end through the widget category", async () => {
    // The REAL declared action, not a synthetic one injected for the test.
    // `widget(epic_get_widgets)` is generated from the recorded catalog and
    // lives in ALL_TOOLS, so this exercises the surface a client actually gets.
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    await widgetTool.handler(ctx, { action: "epic_get_widgets", assetPath: `${ASSET}.uasset` });

    expect(call).toHaveBeenCalledWith(
      "epic_call_tool",
      {
        toolset: "UMGToolSet.UMGToolSet",
        tool: "UMGToolSet.UMGToolSet.GetWidgets",
        input: { widgetBlueprint: { refPath: ASSET } },
      },
      undefined,
    );
  });
});
