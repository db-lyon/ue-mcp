import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import { handlerSpecs } from "../../src/tools/specs/animation.generated.js";
import type { ToolContext } from "../../src/surface/tool-types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

describe("animation.set_control_rig_preview_mesh (#1369)", () => {
  it("forwards the rig and mesh as sent", async () => {
    expect(handlerSpecs.set_control_rig_preview_mesh.params.map((p) => p.name)).toEqual(["assetPath", "skeletalMeshPath"]);
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = { assetPath: "/Game/Retarget/CR_Target_FootIK", skeletalMeshPath: "/Game/Characters/SK_Target" };
    await animationTool.handler(ctx, { action: "set_control_rig_preview_mesh", ...params });
    expect(call).toHaveBeenCalledWith("set_control_rig_preview_mesh", params, undefined);
  });

  it("writes through the rig's own setter and reads it back", () => {
    const source = readHandlerFile("AnimationHandlers_ControlRig.cpp");
    expect(source).toContain("Rig->SetPreviewMesh(Mesh, true)");
    expect(source).toContain("USkeletalMesh* After = Rig->GetPreviewMesh()");
  });
});
