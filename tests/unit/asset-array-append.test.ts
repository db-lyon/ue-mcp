import { describe, expect, it, vi } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import type { ToolContext } from "../../src/types.js";

describe("asset append_array_elements", () => {
  it("maps structured elements to the generic bridge handler", async () => {
    const call = vi.fn(async () => ({ success: true }));
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const elements = [
      { Offset: { x: 1, y: 2, z: 3 } },
      { Offset: { x: 4, y: 5, z: 6 } },
    ];

    await assetTool.handler(ctx, {
      action: "append_array_elements",
      assetPath: "/Game/AI/SOD_Test",
      propertyName: "Slots",
      elements,
    });

    expect(call).toHaveBeenCalledWith("append_asset_array_elements", {
      assetPath: "/Game/AI/SOD_Test",
      propertyName: "Slots",
      elements,
    }, undefined);
  });

  it("rejects an empty element batch before dispatch", () => {
    expect(assetTool.schema.elements.safeParse([]).success).toBe(false);
    expect(assetTool.schema.elements.safeParse([{ Enabled: true }]).success).toBe(true);
  });
});
