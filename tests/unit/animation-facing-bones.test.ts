import { readFileSync } from "node:fs";
import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/core/types.js";

const source = readFileSync(
  new URL("../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_Validation.cpp", import.meta.url),
  "utf8",
);

describe("animation(analyze_animation) facingBones (#1163)", () => {
  it("forwards facingBones under the name the handler reads", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(ctx, {
      action: "analyze_animation",
      assetPath: "/Game/Anims/Strafe_R",
      facingBones: ["LeftUpLeg", "RightUpLeg"],
    });

    expect(call).toHaveBeenCalledWith("analyze_animation", {
      assetPath: "/Game/Anims/Strafe_R",
      facingBones: ["LeftUpLeg", "RightUpLeg"],
    }, undefined);
  });

  it("declares and documents the parameter and its summary", () => {
    expect(animationTool.schema.facingBones.safeParse(["a", "b"]).success).toBe(true);
    const description = animationTool.actions.analyze_animation.description ?? "";
    expect(description).toContain("facingBones?");
    for (const field of ["facingYawDegrees", "averageYawDegrees", "minYawDegrees", "maxYawDegrees"]) {
      expect(description).toContain(field);
    }
  });

  it("reads facingBones through the recorded helper and reports it on the summary", () => {
    expect(source).toContain('TryGetArrayParam(Params, TEXT("facingBones"), FacingBonesJson)');
    expect(source).not.toMatch(/Params->TryGet/);
    expect(source).toContain('Summary->SetObjectField(TEXT("facing"), Facing)');
    expect(source).toContain('SampleObject->SetNumberField(TEXT("facingYawDegrees"), Yaw)');
  });

  it("measures the vector in the root bone's frame and averages it circularly", () => {
    expect(source).toContain("RootTransform.GetRotation().UnrotateVector(Between)");
    expect(source).toContain("FMath::Atan2(SumSin, SumCos)");
  });

  it("stays generic: no bone names are baked into the metric", () => {
    const block = source.slice(source.indexOf("// facingBones (#1163)"), source.indexOf("const bool bLoop"));
    expect(block).not.toMatch(/TEXT\("(pelvis|spine|thigh|clavicle|LeftUpLeg|RightUpLeg)/i);
  });
});
