import { readFileSync } from "node:fs";
import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/types.js";

describe("animation.create_skeleton", () => {
  it("publishes the factory-backed assignment contract", () => {
    const action = animationTool.actions.create_skeleton;
    expect(action.bridge).toBe("create_skeleton");
    expect(action.description).toContain("skeleton factory");
    expect(action.description).toContain("assigns the new skeleton");
    expect(action.description).toContain("onConflict=skip");
    expect(action.description).toContain("same bone count");
    expect(action.description).toContain("transforms are not compared");
    expect(action.description).toContain("machine-readable recovery descriptor");
    expect(animationTool.schema.onConflict.safeParse("skip").success).toBe(true);
    expect(animationTool.schema.onConflict.safeParse("error").success).toBe(true);
    expect(animationTool.schema.onConflict.description).toContain("create_skeleton");
  });

  it("forwards only the native creation contract", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const context = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(context, {
      action: "create_skeleton",
      name: "SK_Character",
      skeletalMeshPath: "/Game/Meshes/SK_Character",
      packagePath: "/Game/Skeletons",
      onConflict: "skip",
      assetPath: "/Game/ShouldNotLeak",
    });

    expect(call).toHaveBeenCalledWith("create_skeleton", {
      name: "SK_Character",
      skeletalMeshPath: "/Game/Meshes/SK_Character",
      packagePath: "/Game/Skeletons",
      onConflict: "skip",
    }, undefined);
  });

  it("keeps the native factory, retry, persistence, and rollback safeguards", () => {
    const source = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers.cpp",
      import.meta.url,
    ), "utf8");
    expect(source).toContain('TEXT("create_skeleton"), &CreateSkeleton');
    expect(source).toContain("USkeletonFactory");
    expect(source).toContain("Factory->TargetSkeletalMesh = SkeletalMesh");
    expect(source).toContain("HasExactReferenceSkeletonHierarchy");
    expect(source).toContain("GetBoneName(BoneIndex)");
    expect(source).toContain("GetParentIndex(BoneIndex)");
    expect(source).toContain("bMeshAlreadyAssigned");
    expect(source).toContain("SaveAssetPackageChecked(Skeleton");
    expect(source).toContain("SaveAssetPackageChecked(SkeletalMesh");
    expect(source).toContain("RestoreMeshAndDeleteCreatedSkeleton");
    expect(source).toContain("UEditorAssetLibrary::DeleteAsset(CreatedSkeleton->GetPathName())");
    expect(source).toContain("Created.EarlyReturn->Type == EJson::Object");
    expect(source).toContain('MCPSetRollback(Result, TEXT("set_asset_property"), Rollback)');
  });
});
