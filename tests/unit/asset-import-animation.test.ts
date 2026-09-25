import { describe, expect, it } from "vitest";
import { assetTool } from "../../src/tools/asset.js";

describe("asset.import_animation", () => {
  it("forwards the FBX animation options the handler reads (#1134)", () => {
    const action = assetTool.actions.import_animation;
    expect(action.bridge).toBe("import_animation");
    expect(action.mapParams?.({
      action: "import_animation",
      filePath: "C:/clips/run.fbx",
      packagePath: "/Game/Anims",
      name: "A_Run",
      skeletonPath: "/Game/Char/SK_Char_Skeleton",
      importCustomAttribute: false,
      removeRedundantKeys: false,
      importSettings: { bSnapToClosestFrameBoundary: true },
    })).toEqual({
      filename: "C:/clips/run.fbx",
      destinationPath: "/Game/Anims",
      assetName: "A_Run",
      skeletonPath: "/Game/Char/SK_Char_Skeleton",
      importCustomAttribute: false,
      removeRedundantKeys: false,
      importSettings: { bSnapToClosestFrameBoundary: true },
    });
  });

  it("accepts importSettings as an object and rejects anything else", () => {
    expect(assetTool.schema.importSettings.safeParse({ snapToClosestFrameBoundary: true }).success).toBe(true);
    expect(assetTool.schema.importSettings.safeParse(undefined).success).toBe(true);
    expect(assetTool.schema.importSettings.safeParse("bSnapToClosestFrameBoundary").success).toBe(false);
  });
});
