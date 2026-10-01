import { describe, expect, it, vi } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { actionSchema } from "../../src/surface/action-schema.js";
import { handlerSpecs } from "../../src/tools/specs/asset.generated.js";
import type { ToolContext } from "../../src/core/types.js";

describe("asset.export DataTables", () => {
  it("exposes the optional format in the action's schema and description", () => {
    expect(assetTool.schema.action.safeParse("export").success).toBe(true);
    const schema = actionSchema(assetTool, "export");
    expect(schema.bridge).toBe("export_asset");
    expect(schema.params.find((p) => p.name === "format")).toMatchObject({
      type: "string",
      required: false,
    });
    expect(handlerSpecs.export_asset.params.find((p) => p.name === "format")?.description)
      .toContain("json | csv");
    for (const word of ["DataTable", "CompositeDataTable", "rowCount", "bytes", "format?"]) {
      expect(schema.description).toContain(word);
    }
    for (const format of [undefined, "json", "csv", "RGBA16F"]) {
      // Other actions share this field; each bridge handler validates its values.
      expect(assetTool.schema.format.safeParse(format).success).toBe(true);
    }
    expect(assetTool.schema.format.safeParse(1).success).toBe(false);
  });

  it.each(["json", "csv"])("forwards explicit %s and returns file metadata without inline rows", async (format) => {
    const response = {
      success: true,
      assetPath: "/Game/Data/DT_Items",
      outputPath: `C:/exports/items.${format}`,
      format,
      rowCount: 4000,
      bytes: 240000,
    };
    const call = vi.fn().mockResolvedValue(response);
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = { assetPath: response.assetPath, outputPath: response.outputPath, format };
    expect(await assetTool.handler(ctx, { action: "export", ...params })).toEqual(response);
    expect(call).toHaveBeenCalledWith("export_asset", params, undefined);
    expect(assetTool.actions.export.mapParams).toBeUndefined();
  });

  it.each(["json", "csv", "txt"])("leaves inference from .%s to the native handler", async (extension) => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = { assetPath: "/Game/Data/DT_Items", outputPath: `exports/items.${extension}` };
    await assetTool.handler(ctx, { action: "export", ...params });
    expect(call).toHaveBeenCalledWith("export_asset", params, undefined);
  });

  it("keeps non-DataTable exports routed through export_asset", async () => {
    const call = vi.fn().mockResolvedValue({ success: true, assetClass: "Texture2D" });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = { assetPath: "/Game/Textures/T_Icon", outputPath: "exports/icon.png" };
    await assetTool.handler(ctx, { action: "export", ...params });
    expect(call).toHaveBeenCalledWith("export_asset", params, undefined);
  });
});
