import { readFileSync } from "node:fs";
import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/types.js";

const workflowActions = [
  "begin_control_rig_edit",
  "read_control_rig_edit",
  "apply_control_rig_edits",
  "bake_control_rig_edit",
] as const;

describe("animation Control Rig edit workflow", () => {
  it("filters read metadata without narrowing begin, skip, or unfiltered session responses", () => {
    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_ControlRigSequencer.cpp",
        import.meta.url,
      ),
      "utf8",
    );
    const readHandler = source.slice(
      source.indexOf("FAnimationHandlers::ReadControlRigEdit"),
      source.indexOf("FAnimationHandlers::ApplyControlRigEdits"),
    );

    expect(source).toContain("if (ControlFilter && !ControlFilter->Contains(Control->GetFName())) continue;");
    expect(readHandler).toContain("if (RequestedNames)");
    expect(readHandler).toContain("Session.ControlRig, &ControlNames");
    expect(readHandler).toContain('SetNumberField(TEXT("controlCount"), RequestedControls.Num())');
    expect(readHandler).toContain('SetArrayField(TEXT("controls"), RequestedControls)');
    expect(source.match(/ControlRigSequencerSessionJson\(Session\);/g)).toHaveLength(3);
  });

  it("keeps the source animation active when layered conversion clears baked rig keys", () => {
    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_ControlRigSequencer.cpp",
        import.meta.url,
      ),
      "utf8",
    );

    expect(source).toContain('SetBoolField(TEXT("layered"), Session.ControlRig->IsAdditive())');
    expect(source).toContain("SetControlRigLayeredMode(RigTrack, true)");
    expect(source).toContain("AnimationTrack->SetEvalDisabled(false)");
  });

  it("keeps a support frame for the exporter's exact-end sample", () => {
    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_ControlRigSequencer.cpp",
        import.meta.url,
      ),
      "utf8",
    );

    expect(source).toContain("FFrameNumber(EndFrameExclusive + 1)");
    expect(source).toContain("RigSections[0]->SetEndFrame");
    expect(source).toContain("falling outside every section and snapping to reference pose");
  });

  it("cancels AnimSequence RateScale so a session maps the raw timeline once", () => {
    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_ControlRigSequencer.cpp",
        import.meta.url,
      ),
      "utf8",
    );
    const nativeTest = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Tests/AnimationControlRigTimelineTests.cpp",
        import.meta.url,
      ),
      "utf8",
    );

    expect(source).toContain("const float RawTimelinePlayRate = static_cast<float>(1.0 / SourceRateScale)");
    expect(source).toContain("AnimationSection->Params.PlayRate = RawTimelinePlayRate");
    expect(nativeTest).toContain("SourceAnimation->RateScale = 3.06608796f");
    expect(nativeTest).toContain("for (int32 Frame = 0; Frame <= 40; ++Frame)");
    expect(nativeTest).toContain("Section->MapTimeToAnimation(FFrameTime(Frame), DisplayRate)");
  });

  it("validates offset ranges before expanding them into frames", () => {
    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_ControlRigSequencer.cpp",
        import.meta.url,
      ),
      "utf8",
    );

    const validation = source.indexOf("Start < RangeStart || End >= RangeEndExclusive");
    const expansion = source.indexOf("for (int64 Frame = Start; Frame <= End; ++Frame)");
    expect(validation).toBeGreaterThan(-1);
    expect(expansion).toBeGreaterThan(validation);
    expect(source).toContain("ControlRigSequencerMaxFrames = 100000");
    expect(source).toContain("FrameCount > ControlRigSequencerMaxFrames");
    expect(source).toContain("Control Rig edits could not be saved and were rolled back");
    expect(source).toContain("AnimSequence export completed in memory but the output asset could not be saved");
  });

  it("keeps validation artifacts inside their native root and never overwrites a run", () => {
    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_Validation.cpp",
        import.meta.url,
      ),
      "utf8",
    );

    expect(source).toContain("FPaths::IsUnderDirectory(Candidate, NormalizedRoot)");
    expect(source).toContain("outputDirectory already contains animation validation artifacts");
    expect(source).toContain("Delete(*SamplesPath, false, true)");
    expect(source).toContain("MakeCompactPoseIndex(FMeshPoseBoneIndex(Index))");
    expect(source).toContain("MakeMeshPoseIndex(Index).GetInt()");
  });

  it("reports asset-rate-scaled duration and notify timing from the native analyzer", () => {
    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_Validation.cpp",
        import.meta.url,
      ),
      "utf8",
    );

    expect(source).toContain("const double RateScale = static_cast<double>(Sequence->RateScale)");
    expect(source).toContain("const double PlaybackRateMagnitude = FMath::Abs(RateScale)");
    expect(source).toContain("DurationSeconds / PlaybackRateMagnitude");
    expect(source).toContain('SetNumberField(TEXT("rawTriggerTimeSeconds"), RawTriggerTimeSeconds)');
    expect(source).toContain("RawTriggerTimeSeconds / PlaybackRateMagnitude");
    expect(source.match(/SetNumberField\(TEXT\("rateScale"\), RateScale\)/g)).toHaveLength(2);
    expect(source.match(/SetNumberField\(TEXT\("effectiveDurationSeconds"\), EffectiveDurationSeconds\)/g)).toHaveLength(2);
    expect(source.match(/SetArrayField\(TEXT\("notifies"\), NotifyValues\)/g)).toHaveLength(2);
    expect(source).toContain('SetField(TEXT("effectiveTriggerTimeSeconds"), MakeShared<FJsonValueNull>())');
  });

  it("routes scalar controls through native Sequencer APIs with metadata and readback", () => {
    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_ControlRigSequencer.cpp",
        import.meta.url,
      ),
      "utf8",
    );

    expect(source).toContain("GetLocalControlRigBools(");
    expect(source).toContain("SetLocalControlRigBools(");
    expect(source).toContain("GetLocalControlRigFloats(");
    expect(source).toContain("SetLocalControlRigFloats(");
    expect(source).toContain("GetLocalControlRigInts(");
    expect(source).toContain("SetLocalControlRigInts(");
    expect(source).toContain("Session.Track->SetSectionToKey(Session.Section, Write.Control)");
    expect(source).toContain('SetStringField(TEXT("controlType"), ControlType)');
    expect(source).toContain('SetArrayField(TEXT("enumOptions"), Options)');
    expect(source).toContain("ControlRigSequencerIsValidEnumValue(ControlEnum, Write.IntValue)");
    expect(source).toContain("ControlRigSequencerRegisterWriteFrames(WrittenKeys");
    expect(source).toContain("SourceAnimation->GetAdditiveAnimType() != AAT_None");
    expect(source).toContain("ControlRigSequencerReadNormalizedQuaternion");
    expect(source).toContain("PreviousRotation | Rotation");
    expect(source).toContain("does not change a channel supported by");
    expect(source).toContain("ControlRigSequencerTransformMatches");
  });

  it("implements component-space contact locking with transactional key QA", () => {
    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_ControlRigSequencer.cpp",
        import.meta.url,
      ),
      "utf8",
    );

    expect(source).toContain('Op == TEXT("contact_lock")');
    expect(source).toContain("ControlRigSequencerSmoothStep");
    expect(source).toContain("Skeleton->GetBoneTranslationRetargetingMode(SkeletonDriverIndex)");
    expect(source).toContain("AnimationCore::SolveFabrik(");
    expect(source).toContain("DesiredLocal.SetTranslation(SourceLocal.GetTranslation())");
    expect(source).toContain("GetControlOffsetTransform(");
    expect(source).toContain("FkChainControls.Num() - 1");
    expect(source).toContain('SetStringField(TEXT("solver"), TEXT("fk_rotation_chain"))');
    expect(source).toContain("UAnimPoseExtensions::GetAnimPoseAtTime(");
    expect(source).toContain("SourceSection->MapTimeToAnimation(");
    expect(source).toContain("GetRelativeTransformReverse(");
    expect(source).toContain("CellCount > ControlRigSequencerMaxFrames");
    expect(source).toContain("contact_constraint_tolerance_exceeded");
    expect(source).toContain('TEXT("bake_and_analyze_required")');
    expect(source).toContain('SetArrayField(TEXT("contactQa"), ContactResults)');
    expect(source).toContain("MCPIsProtectedAssetPath(SequencePath)");
    expect(source.indexOf("for (FControlRigPreparedContactQA& Contact : PreparedContacts)")).toBeGreaterThan(
      source.indexOf("BatchSetControlTransforms("),
    );
    expect(animationTool.actions.contact_lock).toBeUndefined();
  });

  it("makes partial IK retarget mappings explicit in batch results", () => {
    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_StateMachine.cpp",
        import.meta.url,
      ),
      "utf8",
    );

    expect(source).toContain('SetArrayField(TEXT("unmappedTargetChains"), UnmappedTargetChains)');
    expect(source).toContain('SetBoolField(TEXT("mappingComplete"), UnmappedTargetChains.IsEmpty())');
    expect(source).toContain("SourceMesh->GetSkeleton()->IsCompatibleForEditor(Anim->GetSkeleton())");
    expect(source).toContain("FIKRetargetProcessor ValidationProcessor");
    expect(source).toContain("ValidationProcessor.IsInitialized()");
    expect(source).toContain("FScopedBatchRetargetEditorInstanceRestore RestoreEditorInstances");
    expect(source.indexOf("ValidationProcessor.Initialize(ValidationParameters)")).toBeLessThan(
      source.indexOf("UIKRetargetBatchOperation::RunBatchRetarget(Inputs)"),
    );
    expect(animationTool.actions.batch_retarget_animations.description).toContain("partial retargets are explicit");
  });

  it("documents the UE 5.8 Control Rig boundary and cross-version analysis", () => {
    for (const action of workflowActions) {
      const spec = animationTool.actions[action];
      expect(spec).toBeDefined();
      expect(spec.bridge).toBe(action);
      expect(spec.description).toContain("UE 5.8");
      expect(spec.description).toContain("unsupported_engine_version");
      expect(spec.description?.toLowerCase()).toContain("fallback");
    }

    // Do not advertise planned context inspection until its native handler exists.
    expect(animationTool.actions.inspect_animation_context).toBeUndefined();

    const analysis = animationTool.actions.analyze_animation;
    expect(analysis.bridge).toBe("analyze_animation");
    expect(analysis.description).toContain("Cross-version");
    expect(analysis.description).not.toContain("unsupported_engine_version");

    const source = readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers_Validation.cpp",
        import.meta.url,
      ),
      "utf8",
    );
    expect(source).toContain("ENGINE_MINOR_VERSION >= 6");
    expect(source).toContain("static_cast<UObject&>(*SkeletalMesh)");
  });

  it("makes the per-character Control Rig baseline a prerequisite", () => {
    const begin = animationTool.actions.begin_control_rig_edit.description;
    const skill = readFileSync(
      new URL("../../skills/ue-mcp-animation/SKILL.md", import.meta.url),
      "utf8",
    );
    const guide = readFileSync(
      new URL("../../docs/control-rig-animation.md", import.meta.url),
      "utf8",
    );

    expect(begin).toContain("Baseline first");
    expect(begin).toContain("read_control_rig_hierarchy/read_control_rig_graph");
    expect(begin).toContain("epic_create");
    expect(begin).toContain("epic_import_bones_from_asset");
    expect(begin).toContain("epic_add_control");
    expect(begin).toContain("epic_add_backward_solve_graph");
    expect(begin).toContain("unchanged source round-trip");
    expect(begin).toContain("rejects rigs without inverse execution");
    expect(animationTool.schema.controlRigPath.description).toContain("verified baseline");
    expect(animationTool.schema.rigMode.description).toContain("verified baseline");
    expect(skill).toContain("Establish the character's authoring baseline");
    expect(skill).toContain("source-to-controls-to-bones round trip");
    expect(guide).toContain("Establish a per-character Control Rig baseline");
    expect(guide).toContain("it does not invent one");
    expect(guide).toContain("alone creates no imported bones");
    expect(guide.replace(/\s+/g, " ")).toContain("Do not create a new rig per animation");
  });

  it("validates typed transform and scalar operations", () => {
    const operations = animationTool.schema.operations;

    expect(operations.safeParse([
      {
        op: "set",
        control: "hand_r_ctrl",
        frame: 12,
        transform: {
          translation: { x: 10, y: 2, z: 3 },
          rotationDegrees: { pitch: 5, yaw: 15, roll: -2 },
          scale: { x: 1, y: 1, z: 1 },
        },
        space: "global",
      },
      {
        op: "set_keys",
        control: "hand_r_ik_ctrl",
        keys: [
          {
            frame: 6,
            transform: {
              translation: { x: 30, y: 4, z: 120 },
              rotationQuaternion: { x: 0, y: 0, z: 0, w: 1 },
              scale: { x: 1, y: 1, z: 1 },
            },
          },
          {
            frame: 12,
            transform: {
              translation: { x: 34, y: 6, z: 126 },
              rotationQuaternion: { x: 0, y: 0, z: 0.173648, w: 0.984808 },
              scale: { x: 1, y: 1, z: 1 },
            },
          },
        ],
        space: "global",
      },
      {
        op: "offset",
        control: "foot_l_ctrl",
        startFrame: 4,
        endFrame: 18,
        translationCm: { x: 0, y: 0, z: 2.5 },
        rotationDegrees: { pitch: 0, yaw: 3, roll: 0 },
        scaleMultiplier: { x: 1, y: 1, z: 1 },
        space: "local",
        blendInFrames: 2,
        blendOutFrames: 3,
      },
      {
        op: "contact_lock",
        control: "foot_l_ik_ctrl",
        drivenReference: "ball_l",
        startFrame: 4,
        endFrame: 18,
        target: {
          translation: { x: 12, y: -8, z: 1.5 },
          rotationQuaternion: { x: 0, y: 0, z: 0, w: 1 },
        },
        blendInFrames: 2,
        blendOutFrames: 3,
        stabilizeControls: ["knee_pole_l_ctrl"],
        positionToleranceCm: 0.1,
        rotationToleranceDegrees: 0.5,
      },
      {
        op: "set_bool",
        control: "arm_r_fk_ik_switch",
        frames: [0, 12, 30],
        value: true,
      },
      {
        op: "set_float",
        control: "hand_r_space_blend",
        frame: 12,
        value: 0.75,
      },
      {
        op: "set_int",
        control: "hand_r_space",
        frame: 12,
        value: 2,
      },
    ]).success).toBe(true);

    const completeTransform = {
      translation: { x: 0, y: 0, z: 0 },
      rotationDegrees: { pitch: 0, yaw: 0, roll: 0 },
      scale: { x: 1, y: 1, z: 1 },
    };
    expect(operations.safeParse([{ op: "set", control: "root_ctrl", transform: completeTransform }]).success).toBe(false);
    expect(operations.safeParse([{ op: "set", control: "root_ctrl", frame: 0, frames: [0], transform: completeTransform }]).success).toBe(false);
    expect(operations.safeParse([{ op: "offset", control: "root_ctrl", startFrame: 10, endFrame: 2, translationCm: { x: 1, y: 0, z: 0 } }]).success).toBe(false);
    expect(operations.safeParse([{ op: "offset", control: "root_ctrl", startFrame: 0, endFrame: 2 }]).success).toBe(false);
    const contactTarget = { translation: { x: 0, y: 0, z: 0 } };
    expect(operations.safeParse([{ op: "contact_lock", control: "foot_ik", startFrame: 10, endFrame: 2, target: contactTarget }]).success).toBe(false);
    expect(operations.safeParse([{ op: "contact_lock", control: "foot_ik", startFrame: 0, endFrame: 4, target: contactTarget, blendInFrames: 3, blendOutFrames: 2 }]).success).toBe(false);
    expect(operations.safeParse([{ op: "contact_lock", control: "foot_ik", startFrame: 0, endFrame: 4, target: contactTarget, stabilizeControls: ["pole", "POLE"] }]).success).toBe(false);
    expect(operations.safeParse([{ op: "contact_lock", control: "foot_ik", startFrame: 0, endFrame: 4, target: contactTarget, stabilizeControls: ["FOOT_IK"] }]).success).toBe(false);
    expect(operations.safeParse([{ op: "contact_lock", control: "foot_ik", startFrame: 0, endFrame: 50_000, target: contactTarget, stabilizeControls: ["pole"] }]).success).toBe(false);
    expect(operations.safeParse([{ op: "contact_lock", control: "foot_ik", startFrame: 0, endFrame: 4, target: { ...contactTarget, rotationQuaternion: { x: 0, y: 0, z: 0, w: 2 } } }]).success).toBe(false);
    expect(operations.safeParse([{ op: "contact_lock", control: "foot_ik", startFrame: 0, endFrame: 4, target: { ...contactTarget, scale: { x: 1, y: 1, z: 1 } } }]).success).toBe(false);
    expect(operations.safeParse([{ op: "contact_lock", control: "foot_ik", startFrame: 0, endFrame: 4, target: contactTarget, positionToleranceCm: 0 }]).success).toBe(false);
    expect(operations.safeParse([{ op: "set_bool", control: "arm_r_fk_ik_switch", frame: 0, value: 1 }]).success).toBe(false);
    expect(operations.safeParse([{ op: "set_bool", control: "arm_r_fk_ik_switch", frame: 0, frames: [0], value: true }]).success).toBe(false);
    expect(operations.safeParse([{ op: "set_float", control: "blend", frame: 0, value: Number.POSITIVE_INFINITY }]).success).toBe(false);
    expect(operations.safeParse([{ op: "set_int", control: "space", frame: 0, value: 1.5 }]).success).toBe(false);
    expect(operations.safeParse([{ op: "set_int", control: "space", frame: 0, frames: [0], value: 1 }]).success).toBe(false);
    expect(animationTool.schema.createLink.safeParse(true).success).toBe(false);
    const quaternionTransform = {
      translation: { x: 0, y: 0, z: 0 },
      rotationQuaternion: { x: 0, y: 0, z: 0, w: 1 },
      scale: { x: 1, y: 1, z: 1 },
    };
    expect(operations.safeParse([{ op: "set_keys", control: "hand_r_ik_ctrl", keys: [
      { frame: 2, transform: quaternionTransform },
      { frame: 2, transform: quaternionTransform },
    ] }]).success).toBe(false);
    expect(operations.safeParse([{ op: "set_keys", control: "hand_r_ik_ctrl", keys: [{
      frame: 2,
      transform: { ...quaternionTransform, rotationQuaternion: { x: 0, y: 0, z: 0, w: 2 } },
    }] }]).success).toBe(false);
    expect(operations.safeParse([{ op: "set_keys", control: "hand_r_ik_ctrl", keys: [{
      frame: 2,
      transform: { translation: { x: 0, y: 0, z: 0 }, rotationQuaternion: { x: 0, y: 0, z: 0, w: 1 } },
    }] }]).success).toBe(false);
    expect(animationTool.schema.rigMode.safeParse("fk").success).toBe(true);
    expect(animationTool.schema.rigMode.safeParse("asset").success).toBe(true);
    expect(animationTool.schema.rigMode.safeParse("python").success).toBe(false);
  });

  it("maps begin/read/apply/bake parameters exactly to the native contracts", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(ctx, {
      action: "begin_control_rig_edit",
      sequencePath: "/Game/MCP/LS_Wave_Edit",
      skeletalMeshPath: "/Game/Characters/Mannequins/Meshes/SKM_Manny",
      sourceAnimationPath: "/Game/Characters/Mannequins/Animations/ABP_Manny/MM_Unarmed_Idle_Ready",
      rigMode: "asset",
      controlRigPath: "/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body",
      layered: true,
      startFrame: 0,
      endFrame: 60,
      displayRate: 30,
      bindingTag: "mcp.manny.wave",
      onConflict: "skip",
      assetPath: "/Game/ShouldNotLeak",
    });
    expect(call).toHaveBeenLastCalledWith("begin_control_rig_edit", {
      sequencePath: "/Game/MCP/LS_Wave_Edit",
      skeletalMeshPath: "/Game/Characters/Mannequins/Meshes/SKM_Manny",
      sourceAnimationPath: "/Game/Characters/Mannequins/Animations/ABP_Manny/MM_Unarmed_Idle_Ready",
      rigMode: "asset",
      controlRigPath: "/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body",
      layered: true,
      startFrame: 0,
      endFrame: 60,
      displayRate: 30,
      bindingTag: "mcp.manny.wave",
      onConflict: "skip",
    }, undefined);

    await animationTool.handler(ctx, {
      action: "read_control_rig_edit",
      sequencePath: "/Game/MCP/LS_Wave_Edit",
      bindingTag: "mcp.manny.wave",
      controlNames: ["hand_r_ctrl", "foot_l_ctrl"],
      frames: [0, 12, 30],
      space: "global",
      sourceAnimationPath: "/Game/ShouldNotLeak",
    });
    expect(call).toHaveBeenLastCalledWith("read_control_rig_edit", {
      sequencePath: "/Game/MCP/LS_Wave_Edit",
      bindingTag: "mcp.manny.wave",
      controlNames: ["hand_r_ctrl", "foot_l_ctrl"],
      frames: [0, 12, 30],
      space: "global",
    }, undefined);

    const operations = [{
      op: "set",
      control: "hand_r_ctrl",
      frames: [12, 18],
      transform: {
        translation: { x: 35, y: 8, z: 125 },
        rotationDegrees: { pitch: 0, yaw: 30, roll: 10 },
        scale: { x: 1, y: 1, z: 1 },
      },
      space: "global",
    }];
    await animationTool.handler(ctx, {
      action: "apply_control_rig_edits",
      sequencePath: "/Game/MCP/LS_Wave_Edit",
      bindingTag: "mcp.manny.wave",
      operations,
      frames: [999],
    });
    expect(call).toHaveBeenLastCalledWith("apply_control_rig_edits", {
      sequencePath: "/Game/MCP/LS_Wave_Edit",
      bindingTag: "mcp.manny.wave",
      operations,
    }, undefined);

    await animationTool.handler(ctx, {
      action: "bake_control_rig_edit",
      sequencePath: "/Game/MCP/LS_Wave_Edit",
      bindingTag: "mcp.manny.wave",
      outputAssetPath: "/Game/MCP/Animations/AN_Manny_Wave",
      frameRate: 30,
      reduceKeys: false,
      tolerance: 0.001,
      createLink: false,
      onConflict: "error",
      operations,
    });
    expect(call).toHaveBeenLastCalledWith("bake_control_rig_edit", {
      sequencePath: "/Game/MCP/LS_Wave_Edit",
      bindingTag: "mcp.manny.wave",
      outputAssetPath: "/Game/MCP/Animations/AN_Manny_Wave",
      frameRate: 30,
      reduceKeys: false,
      tolerance: 0.001,
      createLink: false,
      onConflict: "error",
    }, undefined);
  });

  it("maps the AnimSequence-only analysis contract without leaking edit-session fields", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(ctx, {
      action: "analyze_animation",
      assetPath: "/Game/Characters/Mannequins/Animations/ABP_Manny/MM_Unarmed_Idle_Ready",
      skeletalMeshPath: "/Game/Characters/Mannequins/Meshes/SKM_Manny",
      boneNames: ["root", "pelvis", "foot_l", "foot_r"],
      frames: [0, 15, 30, 45, 60],
      sampleRate: 30,
      loop: true,
      outputDirectory: "manny_idle_ready",
      sequencePath: "/Game/ShouldNotLeak",
      bindingTag: "should-not-leak",
    });

    expect(call).toHaveBeenCalledWith("analyze_animation", {
      assetPath: "/Game/Characters/Mannequins/Animations/ABP_Manny/MM_Unarmed_Idle_Ready",
      skeletalMeshPath: "/Game/Characters/Mannequins/Meshes/SKM_Manny",
      boneNames: ["root", "pelvis", "foot_l", "foot_r"],
      frames: [0, 15, 30, 45, 60],
      sampleRate: 30,
      loop: true,
      outputDirectory: "manny_idle_ready",
    }, undefined);
  });
});
