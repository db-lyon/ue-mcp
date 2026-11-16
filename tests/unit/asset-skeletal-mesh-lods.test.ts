import { describe, expect, it, vi } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { handlerSpecs } from "../../src/tools/specs/asset.generated.js";
import { REGISTERED_HANDLER_TIMEOUT_SECONDS } from "../../src/bridge/bridge-timeouts.js";
import type { ToolContext } from "../../src/surface/tool-types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

describe("asset.generate_skeletal_mesh_lods (#1372)", () => {
  it("takes one mesh, a list or a folder, exactly one", async () => {
    const action = assetTool.actions.generate_skeletal_mesh_lods;
    expect(action.bridge).toBe("generate_skeletal_mesh_lods");
    expect(action.effect).toBe("mutate");
    expect(handlerSpecs.generate_skeletal_mesh_lods.choices).toEqual([
      { mode: "exactlyOne", branches: [["assetPath"], ["assetPaths"], ["directory"]] },
    ]);
    expect(REGISTERED_HANDLER_TIMEOUT_SECONDS.generate_skeletal_mesh_lods).toBe(600);

    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = { directory: "/Game/Characters/Armour", lodCount: 4, trianglePercentages: [50, 25, 12.5] };
    await assetTool.handler(ctx, { action: "generate_skeletal_mesh_lods", ...params });
    expect(call.mock.calls[0][0]).toBe("generate_skeletal_mesh_lods");
    expect(call.mock.calls[0][1]).toEqual(params);
  });

  it("regenerates through the editor subsystem and applies per-LOD percentages", () => {
    const source = readHandlerFile("SkeletalMeshHandlers_Lods.cpp");
    expect(source).toContain("USkeletalMeshEditorSubsystem::RegenerateLOD(Mesh, LodCount, bRegenerateImported, false)");
    expect(source).toContain("Info->ReductionSettings.NumOfTrianglesPercentage = Percents[Index - 1]");
  });
});
