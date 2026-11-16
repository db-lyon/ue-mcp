import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import { handlerSpecs } from "../../src/tools/specs/animation.generated.js";
import type { ToolContext } from "../../src/surface/tool-types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

describe("remove_retarget_pose and rename_retarget_pose (#1288)", () => {
  it("declares the pose and side parameters", () => {
    expect(handlerSpecs.remove_retarget_pose.params.map((p) => p.name)).toEqual(["retargeterPath", "poseName", "side"]);
    expect(handlerSpecs.rename_retarget_pose.params.map((p) => p.name)).toEqual(["retargeterPath", "poseName", "newName", "side"]);
  });

  it("forwards a removal as sent", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = { retargeterPath: "/Game/Retarget/RTG_A_to_B", poseName: "ScratchPose_A", side: "target" };
    await animationTool.handler(ctx, { action: "remove_retarget_pose", ...params });
    expect(call).toHaveBeenCalledWith("remove_retarget_pose", params, undefined);
  });

  it("refuses the default pose and is gated to the engines whose API was checked", () => {
    const source = readHandlerFile("AnimationHandlers_StateMachine.cpp");
    expect(source).toContain("PoseName == UIKRetargeter::GetDefaultPoseName()");
    expect(source).toContain("Controller->RemoveRetargetPose(PoseName, SoT)");
    expect(source).toContain("Controller->RenameRetargetPose(PoseName, NewName, SoT)");
    expect(source).toContain('MCPUnsupportedEngineError(TEXT("remove_retarget_pose"), TEXT("5.7"))');
  });
});
