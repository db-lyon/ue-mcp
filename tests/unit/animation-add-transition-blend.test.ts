import { readFileSync } from "node:fs";
import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/types.js";

describe("animation.add_transition blend settings (#1149)", () => {
  it("advertises blendDuration and blendLogic", () => {
    const action = animationTool.actions.add_transition;
    expect(action.bridge).toBe("add_transition");
    expect(action.description).toContain("blendDuration?");
    expect(action.description).toContain("blendLogic?");
  });

  it("forwards blendDuration and blendLogic to the bridge", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const context = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(context, {
      action: "add_transition",
      assetPath: "/Game/ABP_Test",
      stateMachineName: "Locomotion",
      fromState: "A",
      toState: "B",
      blendDuration: 1,
      blendLogic: "Inertialization",
    });

    const [method, params] = call.mock.calls[0];
    expect(method).toBe("add_transition");
    expect(params).toMatchObject({ blendDuration: 1, blendLogic: "Inertialization" });
  });

  it("writes and echoes the blend on the new transition node", () => {
    const source = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_StateMachine.cpp",
      import.meta.url,
    ), "utf8");
    const body = source.slice(
      source.indexOf("FAnimationHandlers::AddTransition("),
      source.indexOf("FAnimationHandlers::SetStateAnimation("),
    );
    expect(body).toContain("TransNode->CrossfadeDuration = static_cast<float>(BlendDuration)");
    expect(body).toContain("TransNode->LogicType = ETransitionLogicType::TLT_Inertialization");
    expect(body).toContain('Result->SetNumberField(TEXT("blendDuration"), TransNode->CrossfadeDuration)');
    expect(body).toContain('Result->SetStringField(TEXT("blendLogic"), LogicName(TransNode->LogicType))');
  });
});
