import { describe, expect, it, vi } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { handlerSpecs } from "../../src/tools/specs/asset.generated.js";
import type { ToolContext } from "../../src/core/types.js";

describe("asset.import_animation", () => {
  it("forwards the FBX animation options the handler reads (#1134)", async () => {
    const action = assetTool.actions.import_animation;
    expect(action.bridge).toBe("import_animation");
    // #1057: the handler declares these names itself, so the bag goes as sent.
    expect(action.mapParams).toBeUndefined();

    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = {
      filePath: "C:/clips/run.fbx",
      packagePath: "/Game/Anims",
      name: "A_Run",
      skeletonPath: "/Game/Char/SK_Char_Skeleton",
      importCustomAttribute: false,
      removeRedundantKeys: false,
      importSettings: { bSnapToClosestFrameBoundary: true },
    };
    await assetTool.handler(ctx, { action: "import_animation", ...params });
    expect(call).toHaveBeenCalledWith("import_animation", params, undefined);

    const declared = handlerSpecs.import_animation.params.map((p) => p.name);
    expect(declared).toEqual(expect.arrayContaining(Object.keys(params)));
  });

  it("accepts importSettings as an object and rejects anything else", () => {
    expect(assetTool.schema.importSettings.safeParse({ snapToClosestFrameBoundary: true }).success).toBe(true);
    expect(assetTool.schema.importSettings.safeParse(undefined).success).toBe(true);
    expect(assetTool.schema.importSettings.safeParse("bSnapToClosestFrameBoundary").success).toBe(false);
  });
});
