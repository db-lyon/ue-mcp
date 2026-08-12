import { readFileSync } from "node:fs";
import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/types.js";

describe("animation IK and retarget authoring", () => {
  it("publishes the native UE 5.8 authoring boundary", () => {
    for (const action of ["configure_ik_rig", "configure_ik_retargeter"] as const) {
      const spec = animationTool.actions[action];
      expect(spec.bridge).toBe(action);
      expect(spec.description).toContain("UE 5.8");
      expect(spec.description).toContain("unsupported_engine_version");
    }
    expect(animationTool.actions.read_ik_rig.description).toContain("concrete goals");
    expect(animationTool.actions.read_ik_retargeter.description).toContain("per-op chain mappings");
  });

  it("validates typed IK and retarget payloads", () => {
    expect(animationTool.schema.autoSetup.safeParse("full_body").success).toBe(true);
    expect(animationTool.schema.autoSetup.safeParse("reflection").success).toBe(false);
    expect(animationTool.schema.fullBodyIK.safeParse({
      rootBone: "pelvis",
      goals: [{
        name: "hand_r_Goal",
        bone: "hand_r",
        positionAlpha: 1,
        rotationAlpha: 1,
        chainDepth: 0,
        strengthAlpha: 1,
        pullChainAlpha: 0,
        pinRotation: 1,
      }],
    }).success).toBe(true);
    expect(animationTool.schema.fullBodyIK.safeParse({
      rootBone: "pelvis",
      goals: [{ name: "bad", bone: "hand_r", strengthAlpha: 2 }],
    }).success).toBe(false);
    expect(animationTool.schema.fullBodyIK.safeParse({
      rootBone: "pelvis",
      goals: Array.from({ length: 257 }, (_, index) => ({ name: `goal_${index}`, bone: "hand_r" })),
    }).success).toBe(false);
    expect(animationTool.schema.chains.safeParse(Array.from({ length: 257 }, (_, index) => ({
      name: `chain_${index}`,
      startBone: "pelvis",
      endBone: "head",
    }))).success).toBe(false);
    expect(animationTool.schema.exclusions.safeParse(Array.from({ length: 2049 }, (_, index) => ({
      bone: `bone_${index}`,
      excluded: true,
    }))).success).toBe(false);

    expect(animationTool.schema.chainMappings.safeParse([
      { targetChain: "LeftArm", sourceChain: "LeftArm" },
      { targetChain: "LeftMetacarpal", sourceChain: null },
    ]).success).toBe(true);
    expect(animationTool.schema.pose.safeParse({
      side: "target",
      name: "Manny Retarget Pose",
      create: true,
      autoAlign: "chain_to_chain",
      rotationOffsets: [{
        bone: "upperarm_r",
        rotationQuaternion: { x: 0, y: 0, z: 0, w: 1 },
      }],
      rootOffsetZ: 2.5,
    }).success).toBe(true);
    expect(animationTool.schema.pose.safeParse({
      side: "target",
      name: "bad",
      rootOffsetZ: 1,
      snapBoneToGround: "ball_l",
    }).success).toBe(false);
    expect(animationTool.schema.pose.safeParse({
      side: "target",
      name: "bad",
      rotationOffsets: [{
        bone: "upperarm_r",
        rotationQuaternion: { x: 0, y: 0, z: 0, w: 2 },
      }],
    }).success).toBe(false);
  });

  it("maps only the native configure contracts", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const context = { bridge: { call } } as unknown as ToolContext;

    const chains = [{ name: "RightArm", startBone: "upperarm_r", endBone: "hand_r", goal: "hand_r_Goal" }];
    const fullBodyIK = {
      rootBone: "pelvis",
      goals: [{ name: "hand_r_Goal", bone: "hand_r", strengthAlpha: 1 }],
    };
    await animationTool.handler(context, {
      action: "configure_ik_rig",
      rigPath: "/Game/Rigs/IK_Manny",
      autoSetup: "full_body",
      retargetRoot: "pelvis",
      rootMotionBone: "root",
      chains,
      fullBodyIK,
      exclusions: [{ bone: "neck_01", excluded: false }],
      retargeterPath: "/Game/ShouldNotLeak",
    });
    expect(call).toHaveBeenLastCalledWith("configure_ik_rig", {
      rigPath: "/Game/Rigs/IK_Manny",
      autoSetup: "full_body",
      retargetRoot: "pelvis",
      rootMotionBone: "root",
      chains,
      fullBodyIK,
      exclusions: [{ bone: "neck_01", excluded: false }],
    }, undefined);

    const pose = { side: "target", name: "Manny Pose", create: true, autoAlign: "chain_to_chain" };
    await animationTool.handler(context, {
      action: "configure_ik_retargeter",
      retargeterPath: "/Game/Rigs/RTG_UE4_Manny",
      sourceRig: "/Game/Rigs/IK_UE4",
      targetRig: "/Game/Rigs/IK_Manny",
      sourcePreviewMesh: "/Game/Meshes/SK_UE4",
      targetPreviewMesh: "/Game/Meshes/SKM_Manny",
      ensureDefaultOps: true,
      autoMapMode: "exact",
      forceRemap: true,
      chainMappings: [{ targetChain: "LeftArm", sourceChain: "LeftArm" }],
      pose,
      rigPath: "/Game/ShouldNotLeak",
    });
    expect(call).toHaveBeenLastCalledWith("configure_ik_retargeter", {
      retargeterPath: "/Game/Rigs/RTG_UE4_Manny",
      sourceRig: "/Game/Rigs/IK_UE4",
      targetRig: "/Game/Rigs/IK_Manny",
      sourcePreviewMesh: "/Game/Meshes/SK_UE4",
      targetPreviewMesh: "/Game/Meshes/SKM_Manny",
      ensureDefaultOps: true,
      autoMapMode: "exact",
      forceRemap: true,
      chainMappings: [{ targetChain: "LeftArm", sourceChain: "LeftArm" }],
      pose,
    }, undefined);
  });

  it("registers guarded native handlers with transactions and checked saves", () => {
    const registry = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers.cpp",
      import.meta.url,
    ), "utf8");
    const ik = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_IKRigAuthoring.cpp",
      import.meta.url,
    ), "utf8");
    const retarget = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_IKRetargeterAuthoring.cpp",
      import.meta.url,
    ), "utf8");
    const legacy = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_StateMachine.cpp",
      import.meta.url,
    ), "utf8");
    const handlerUtils = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Public/HandlerUtils.h",
      import.meta.url,
    ), "utf8");

    expect(registry).toContain('TEXT("configure_ik_rig"), &ConfigureIKRig');
    expect(registry).toContain('TEXT("configure_ik_retargeter"), &ConfigureIKRetargeter');
    for (const source of [ik, retarget]) {
      expect(source).toContain("UE_MCP_HAS_5_8_API");
      expect(source).toContain('TEXT("unsupported_engine_version")');
      expect(source).toContain("FScopedTransaction");
      expect(source).toContain("UndoTransaction");
    }
    expect(ik).toContain("SaveLoadedAsset");
    expect(retarget).toContain("SaveAssetPackage");
    expect(ik).toContain("MCPIsProtectedAssetPath(RigPath)");
    expect(ik).toContain("TSet<FName> RequiredFBIKBones");
    expect(ik).toContain("ExistingSolver->GetRequiredGoals(ConnectedGoals)");
    expect(ik).toContain("RequiredFBIKBones.Add(AutoResults.AutoRetargetDefinition.RetargetDefinition.PelvisBone)");
    expect(ik).toContain("Exclusion.bExcluded && RequiredFBIKBones.Contains(Exclusion.Bone)");
    expect(retarget).toContain("MCPIsProtectedAssetPath(RetargeterPath)");
    expect(retarget).toContain("pose.snapBoneToGround requires both source and target preview meshes");
    expect(retarget).toContain("PoseMesh->GetRefSkeleton().FindBoneIndex(SnapBone)");
    expect(retarget).toContain("MappingProcessor.IsBoneMapped(Bone, Pose.Side)");
    expect(retarget).toContain("Retarget pose auto-align bone is not mapped");
    expect(legacy).toContain('SetStringField(TEXT("rootMotionBone")');
    expect(legacy).toContain('SetArrayField(TEXT("goals")');
    expect(legacy).toContain('SetArrayField(TEXT("retargetOps")');
    expect(legacy).toContain("Inputs.bIncludeReferencedAssets = false");
    expect(legacy).toContain("requireCompleteMapping");
    expect(legacy).toContain("UEditorAssetLibrary::DoesAssetExist(ObjectPath)");
    expect(legacy).toContain("cleanup failed for");
    const setRig = legacy.slice(
      legacy.indexOf("FAnimationHandlers::SetIKRetargeterRig"),
      legacy.indexOf("FAnimationHandlers::AutoAlignRetargetPose"),
    );
    expect(setRig).toContain("if (Controller->GetNumRetargetOps() == 0)");
    expect(setRig).toContain("MCPIsProtectedAssetPath(RetargeterPath)");
    expect(setRig).toContain("FScopedTransaction");
    expect(setRig).toContain("UndoTransaction");
    expect(legacy).toContain("MCPIsProtectedAssetPath(TargetPath)");
    expect(handlerUtils).toContain('Lower == TEXT("/engine")');
    expect(handlerUtils).toContain('Lower == TEXT("/script")');
    expect(handlerUtils).toContain("FPackageName::ExportTextPathToObjectPath(Normalized)");
  });
});
