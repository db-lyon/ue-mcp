import { readFileSync } from "node:fs";
import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/types.js";

describe("animation.create_control_rig (#1133)", () => {
  it("publishes the factory-backed creation contract", () => {
    const action = animationTool.actions.create_control_rig;
    expect(action.bridge).toBe("create_control_rig");
    expect(action.effect).toBe("mutate");
    expect(action.description).toContain("skeletalMeshPath? | skeletonPath?");
    expect(action.description).toContain("delete_asset rollback");
  });

  it("forwards only the creation parameters", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const context = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(context, {
      action: "create_control_rig",
      skeletalMeshPath: "/Game/Meshes/SKM_Hero",
      name: "CR_Hero",
      packagePath: "/Game/Rigs",
      onConflict: "error",
      assetPath: "/Game/ShouldNotLeak",
    });

    expect(call).toHaveBeenCalledWith("create_control_rig", {
      skeletalMeshPath: "/Game/Meshes/SKM_Hero",
      skeletonPath: undefined,
      name: "CR_Hero",
      packagePath: "/Game/Rigs",
      onConflict: "error",
    }, undefined);
  });

  it("registers the handler and keeps its safeguards", () => {
    const handlers = new URL("../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/", import.meta.url);
    const registration = readFileSync(new URL("AnimationHandlers.cpp", handlers), "utf8");
    expect(registration).toContain('TEXT("create_control_rig"), &CreateControlRig');
    const source = readFileSync(new URL("AnimationHandlers_ControlRig.cpp", handlers), "utf8");
    expect(source).toContain("UControlRigBlueprintFactory::CreateControlRigFromSkeletalMeshOrSkeleton(Source)");
    expect(source).toContain("MCPCheckAssetExists(PackagePath, Name, OnConflict");
    expect(source).toContain("UEditorAssetLibrary::DeleteAsset(CreatedPackage)");
    expect(source).toContain("MCPSetDeleteAssetRollback(Result, Created->GetPathName())");
  });
});
