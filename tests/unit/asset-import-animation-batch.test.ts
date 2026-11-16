import { describe, expect, it, vi } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { handlerSpecs } from "../../src/tools/specs/asset.generated.js";
import type { ToolContext } from "../../src/surface/tool-types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

describe("asset.import_animation batch and readback (#1228)", () => {
  it("takes filePath or filePaths, exactly one", async () => {
    const spec = handlerSpecs.import_animation;
    expect(spec.params.slice(0, 2).map((p) => [p.name, p.required])).toEqual([["filePath", false], ["filePaths", false]]);
    expect(spec.choices).toEqual([{ mode: "exactlyOne", branches: [["filePath"], ["filePaths"]] }]);

    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = {
      filePaths: ["C:/clips/run.fbx", "C:/clips/walk.fbx"],
      packagePath: "/Game/Anims",
      skeletonPath: "/Game/Char/SK_Char_Skeleton",
      importSettings: { importRotation: { pitch: 0, yaw: 0, roll: 0 } },
    };
    await assetTool.handler(ctx, { action: "import_animation", ...params });
    expect(call).toHaveBeenCalledWith("import_animation", params, undefined);
  });

  it("reads back each imported sequence", () => {
    expect(assetTool.actions.import_animation.description).toContain("hasRootMotion");
    const source = readHandlerFile("AssetHandlers_Import.cpp");
    expect(source).toContain("Animations.Add(MakeShared<FJsonValueObject>(MCPDescribeImportedAnimation(Seq)))");
    expect(source).toContain('Out->SetNumberField(TEXT("boneTrackCount"), Tracks.Num())');
  });
});
