import { readFileSync } from "node:fs";
import { describe, expect, it, vi } from "vitest";
import { classifyActionClass } from "../../src/action-class.js";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/types.js";

describe("animation.set_live_post_process_anim_blueprint", () => {
  it("forwards the live-only selector, generated class path, and clear flag", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(ctx, {
      action: "set_live_post_process_anim_blueprint",
      actorPath: "/Game/UEDPIE_0_Map.Map:PersistentLevel.Character_0",
      componentName: "CharacterMesh0",
      animBlueprintClassPath: "/Game/Animations/ABP_Post.ABP_Post_C",
      world: "pie",
    });

    expect(call).toHaveBeenCalledWith("set_live_post_process_anim_blueprint", {
      actorLabel: undefined,
      actorPath: "/Game/UEDPIE_0_Map.Map:PersistentLevel.Character_0",
      componentName: "CharacterMesh0",
      animBlueprintClassPath: "/Game/Animations/ABP_Post.ABP_Post_C",
      clear: undefined,
      world: "pie",
    }, undefined);
  });

  it("exposes the generated-class path and clear flag in the schema", () => {
    expect(animationTool.schema.animBlueprintClassPath.safeParse("/Game/ABP.ABP_C").success).toBe(true);
    expect(animationTool.schema.animBlueprintClassPath.safeParse(42).success).toBe(false);
    expect(animationTool.schema.clear.safeParse(true).success).toBe(true);
    expect(animationTool.schema.clear.safeParse("true").success).toBe(false);
  });

  it("is a mutation, so multi-editor dispatch requires an explicit target", () => {
    expect(classifyActionClass("animation", "set_live_post_process_anim_blueprint")).toEqual({
      class: "mutate",
      source: "override",
    });
  });

  it("keeps the native action transient and validates generated classes before mutation", () => {
    const registry = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers.cpp",
      import.meta.url,
    ), "utf8");
    const header = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers.h",
      import.meta.url,
    ), "utf8");
    const live = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_SkeletalLive.cpp",
      import.meta.url,
    ), "utf8");
    const nativeTest = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Tests/LivePostProcessAnimBlueprintTests.cpp",
      import.meta.url,
    ), "utf8");

    expect(registry).toContain('RegisterHandler(TEXT("set_live_post_process_anim_blueprint"), &SetLivePostProcessAnimBlueprint)');
    expect(header).toContain("SetLivePostProcessAnimBlueprint");
    expect(live).toContain("LoadObject<UAnimBlueprintGeneratedClass>");
    expect(live).toContain("IsCompatibleForEditor");
    expect(live).toContain("SK->GetAnimClass() == NewClass");
    expect(live).toContain("if (!bAlreadySet)");
    expect(live).toContain("SetOverridePostProcessAnimBP(NewClass, /*ReinitAnimInstances*/ true)");
    expect(live).toContain("SK->GetPostProcessInstance()");
    expect(live).toContain('SetBoolField(TEXT("transient"), true)');
    expect(live).toContain("no asset or component template was modified");
    expect(live).not.toContain("SK->Modify()");
    expect(nativeTest).toContain("LivePostProcessAnimBlueprint.RegistrationAndValidation");
    expect(nativeTest).toContain('Registry.HasHandler(TEXT("set_live_post_process_anim_blueprint"))');
  });
});
