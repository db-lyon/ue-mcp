import { readFileSync } from "node:fs";
import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/core/types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

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
    // configure_ik_rig refuses an unknown pass and names both it takes. The MCP
    // SDK validates arguments before the tool callback runs, so a strict enum
    // here would replace that message with a transport schema dump.
    expect(animationTool.schema.autoSetup.safeParse("reflection").success).toBe(true);
    expect(animationTool.schema.autoSetup.description).toContain("retarget");
    expect(animationTool.schema.autoSetup.description).toContain("full_body");
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
    // The spec declares each element's fields; the ranges and counts are the
    // handler's, which refuses them by name before anything is written (#1057).
    expect(animationTool.schema.fullBodyIK.safeParse({ goals: [] }).success).toBe(false);
    expect(animationTool.schema.chains.safeParse([{ name: "Spine", startBone: "pelvis" }]).success).toBe(false);
    expect(animationTool.schema.exclusions.safeParse([{ bone: "neck_01" }]).success).toBe(false);
    const ik = readHandlerFile("AnimationHandlers_IKRigAuthoring.cpp");
    expect(ik).toContain("constexpr int32 MaxChains = 256;");
    expect(ik).toContain("constexpr int32 MaxGoals = 256;");
    expect(ik).toContain("constexpr int32 MaxExclusions = 2048;");
    expect(ik).toContain("must be a finite number in [0, 1]");

    expect(animationTool.schema.chainMappings.safeParse([
      { targetChain: "LeftArm", sourceChain: "LeftArm" },
      { targetChain: "LeftMetacarpal", sourceChain: null },
    ]).success).toBe(true);
    expect(animationTool.schema.chainMappings.safeParse([{ sourceChain: "LeftArm" }]).success).toBe(false);
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
    expect(animationTool.schema.pose.safeParse({ name: "bad" }).success).toBe(false);
    const retarget = readHandlerFile("AnimationHandlers_IKRetargeterAuthoring.cpp");
    expect(retarget).toContain("pose.rootOffsetZ and pose.snapBoneToGround are mutually exclusive");
    expect(retarget).toContain("FMath::Abs(Length - 1.0) > NormalizedTolerance");
  });

  it("forwards the configure calls as sent, under their C++ specs (#1057)", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const context = { bridge: { call } } as unknown as ToolContext;
    for (const action of ["configure_ik_rig", "configure_ik_retargeter"] as const) {
      expect(animationTool.actions[action].mapParams, action).toBeUndefined();
    }

    const chains = [{ name: "RightArm", startBone: "upperarm_r", endBone: "hand_r", goal: "hand_r_Goal" }];
    const fullBodyIK = {
      rootBone: "pelvis",
      goals: [{ name: "hand_r_Goal", bone: "hand_r", strengthAlpha: 1 }],
    };
    const rig = {
      rigPath: "/Game/Rigs/IK_Manny",
      autoSetup: "full_body",
      retargetRoot: "pelvis",
      rootMotionBone: "root",
      chains,
      fullBodyIK,
      exclusions: [{ bone: "neck_01", excluded: false }],
    };
    await animationTool.handler(context, { action: "configure_ik_rig", ...rig });
    expect(call).toHaveBeenLastCalledWith("configure_ik_rig", rig, undefined);

    const retargeter = {
      retargeterPath: "/Game/Rigs/RTG_UE4_Manny",
      sourceRig: "/Game/Rigs/IK_UE4",
      targetRig: "/Game/Rigs/IK_Manny",
      sourcePreviewMesh: "/Game/Meshes/SK_UE4",
      targetPreviewMesh: "/Game/Meshes/SKM_Manny",
      ensureDefaultOps: true,
      autoMapMode: "exact",
      forceRemap: true,
      chainMappings: [{ targetChain: "LeftArm", sourceChain: "LeftArm" }],
      pose: { side: "target", name: "Manny Pose", create: true, autoAlign: "chain_to_chain" },
    };
    await animationTool.handler(context, { action: "configure_ik_retargeter", ...retargeter });
    expect(call).toHaveBeenLastCalledWith("configure_ik_retargeter", retargeter, undefined);
  });

  it("registers guarded native handlers with transactions and checked saves", () => {
    const registry = readHandlerFile("AnimationHandlers.cpp");
    const ik = readHandlerFile("AnimationHandlers_IKRigAuthoring.cpp");
    const retarget = readHandlerFile("AnimationHandlers_IKRetargeterAuthoring.cpp");
    const legacy = readHandlerFile("AnimationHandlers_StateMachine.cpp");
    const handlerUtils = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Public/HandlerUtils.h",
      import.meta.url,
    ), "utf8");

    expect(registry).toContain('TEXT("configure_ik_rig"), &ConfigureIKRig');
    expect(registry).toContain('TEXT("configure_ik_retargeter"), &ConfigureIKRetargeter');
    for (const source of [ik, retarget]) {
      expect(source).toContain("UE_MCP_HAS_5_8_API");
      expect(source).toMatch(/TEXT\("unsupported_engine_version"\)|MCPUnsupportedEngineError\(/);
      expect(source).toContain("FScopedTransaction");
      expect(source).toContain("UndoTransaction");
    }
    expect(ik).toContain("SaveAssetPackageChecked");
    expect(retarget).toContain("SaveAssetPackageChecked");
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
