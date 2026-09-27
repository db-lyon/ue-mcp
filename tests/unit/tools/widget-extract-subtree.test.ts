/**
 * widget(extract_subtree) param contract.
 *
 * The C++ handler reads sourceAssetPath / sourceWidgetName /
 * destinationAssetPath / destinationParentClass / destinationRootName /
 * dryRun, and declares them in its parameter spec (#1057), so the action has
 * no mapParams and forwards the bag as sent. Pin the wire shape here.
 */
import { describe, it, expect, vi } from "vitest";
import { widgetTool } from "../../../src/tools/widget.js";

const action = widgetTool.actions.extract_subtree;

describe("widget(extract_subtree)", () => {
  it("targets the extract_widget_subtree bridge method", () => {
    expect(action.bridge).toBe("extract_widget_subtree");
  });

  async function send(params: Record<string, unknown>): Promise<Record<string, unknown>> {
    const call = vi.fn().mockResolvedValue({ success: true });
    await widgetTool.handler({ bridge: { call } } as never, { action: "extract_subtree", ...params });
    expect(call.mock.calls[0][0]).toBe("extract_widget_subtree");
    return call.mock.calls[0][1] as Record<string, unknown>;
  }

  it("forwards every param the C++ handler reads, under the same names", async () => {
    expect(action.mapParams).toBeUndefined();
    const sent = await send({
      sourceAssetPath: "/Game/UI/WBP_Window",
      sourceWidgetName: "ResultsRowPreview",
      destinationAssetPath: "/Game/UI/Rows/WBP_ResultsRow",
      destinationParentClass: "/Script/UMG.UserWidget",
      destinationRootName: "ResultsRow",
      dryRun: false,
    });

    expect(sent).toEqual({
      sourceAssetPath: "/Game/UI/WBP_Window",
      sourceWidgetName: "ResultsRowPreview",
      destinationAssetPath: "/Game/UI/Rows/WBP_ResultsRow",
      destinationParentClass: "/Script/UMG.UserWidget",
      destinationRootName: "ResultsRow",
      dryRun: false,
    });
  });

  it("leaves dryRun unset so the handler applies its own default", async () => {
    const sent = await send({
      sourceAssetPath: "/Game/UI/WBP_Window",
      sourceWidgetName: "Row",
      destinationAssetPath: "/Game/UI/Rows/WBP_Row",
    });
    expect(sent.dryRun).toBeUndefined();
  });

  it("declares every forwarded param on the tool schema", () => {
    for (const key of [
      "sourceAssetPath",
      "sourceWidgetName",
      "destinationAssetPath",
      "destinationParentClass",
      "destinationRootName",
      "dryRun",
    ]) {
      expect(widgetTool.schema[key], `widget schema is missing ${key}`).toBeDefined();
    }
    expect(widgetTool.schema.sourceAssetPath.safeParse("/Game/UI/WBP_Window").success).toBe(true);
    expect(widgetTool.schema.dryRun.safeParse(true).success).toBe(true);
  });
});
