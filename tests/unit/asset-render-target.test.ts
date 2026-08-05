import { describe, expect, it, vi } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import type { ToolContext } from "../../src/types.js";

describe("asset.create_render_target_2d", () => {
  it("forwards every render target param under the name the C++ handler reads", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await assetTool.handler(ctx, {
      action: "create_render_target_2d",
      name: "RT_Minimap",
      packagePath: "/Game/RenderTargets",
      width: 1024,
      height: 512,
      format: "RGBA16F",
      clearColor: { r: 0, g: 0, b: 0, a: 1 },
      generateMips: true,
      targetGamma: 2.2,
      onConflict: "error",
    });

    expect(call).toHaveBeenCalledWith(
      "create_render_target_2d",
      {
        name: "RT_Minimap",
        packagePath: "/Game/RenderTargets",
        width: 1024,
        height: 512,
        format: "RGBA16F",
        clearColor: { r: 0, g: 0, b: 0, a: 1 },
        generateMips: true,
        targetGamma: 2.2,
        onConflict: "error",
      },
      undefined,
    );
  });

  it("bounds the render target dimensions at the size the editor accepts", () => {
    expect(assetTool.schema.width.safeParse(0).success).toBe(false);
    expect(assetTool.schema.width.safeParse(8193).success).toBe(false);
    expect(assetTool.schema.width.safeParse(512.5).success).toBe(false);
    expect(assetTool.schema.width.safeParse(8192).success).toBe(true);
    expect(assetTool.schema.height.safeParse(8193).success).toBe(false);
    expect(assetTool.schema.height.safeParse(1).success).toBe(true);
  });

  it("accepts the render target pixel formats alongside the curve table formats", () => {
    for (const f of ["R8", "RG8", "RGBA8", "RGBA8_SRGB", "R16F", "RG16F", "RGBA16F", "R32F", "RG32F", "RGBA32F", "RGB10A2"]) {
      expect(assetTool.schema.format.safeParse(f).success, f).toBe(true);
    }
    expect(assetTool.schema.format.safeParse("json").success).toBe(true);
    expect(assetTool.schema.format.safeParse("BC7").success).toBe(false);
  });

  it("rejects a negative target gamma", () => {
    expect(assetTool.schema.targetGamma.safeParse(-0.1).success).toBe(false);
    expect(assetTool.schema.targetGamma.safeParse(0).success).toBe(true);
  });
});
