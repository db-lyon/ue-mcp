import { describe, expect, it } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import { handlerSpecs } from "../../src/tools/specs/animation.generated.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

describe("animation.set_sequence_properties (#1230)", () => {
  it("advertises the additive settings it writes and that unknown keys refuse", () => {
    const properties = handlerSpecs.set_sequence_properties.params.find((p) => p.name === "properties");
    expect(properties?.description).toContain("additiveAnimType?: None|LocalSpaceBase|RotationOffsetMeshSpace");
    expect(properties?.description).toContain("refPoseType?");
    expect(properties?.description).toContain("An unknown key or value refuses the whole call");
    expect(animationTool.actions.set_sequence_properties.bridge).toBe("set_sequence_properties");
  });

  it("validates every key before writing and rebuilds additive data through the named property", () => {
    const source = readHandlerFile("AnimationHandlers_Sequence.cpp");
    expect(source).toContain("Unknown property '%s'. set_sequence_properties writes: %s");
    expect(source).toContain("Seq->AdditiveAnimType = Additive.GetValue()");
    expect(source).toContain("GET_MEMBER_NAME_CHECKED(UAnimSequence, AdditiveAnimType)");
    expect(source).toContain("Seq->PostEditChangeProperty(Event)");
    // The refusal comes before the first sequence is loaded.
    expect(source.indexOf("Unknown property '%s'")).toBeLessThan(source.indexOf("const FMCPSeqProps Prev = FMCPSeqProps::Read(Seq)"));
  });
});

describe("animation loop flag and blend sample rate scale (#1663)", () => {
  it("set_sequence_properties writes and reports loop", () => {
    const properties = handlerSpecs.set_sequence_properties.params.find((p) => p.name === "properties");
    expect(properties?.description).toContain("loop?");
    const source = readHandlerFile("AnimationHandlers_Sequence.cpp");
    expect(source).toContain('TEXT("loop"), TEXT("enableRootMotion")');
    expect(source).toContain("if (Loop.IsSet()) Seq->bLoop = Loop.GetValue();");
    expect(source).toContain('J->SetBoolField(TEXT("loop"), bLoop);');
    expect(source).toContain('Result->SetBoolField(TEXT("loop"), AnimSeq->bLoop);');
  });
});

describe("animation.compose_pose_sequence (#1230)", () => {
  it("publishes the per-bone compose contract from its C++ spec", () => {
    const action = animationTool.actions.compose_pose_sequence;
    expect(action.bridge).toBe("compose_pose_sequence");
    expect(action.effect).toBe("mutate");
    expect(action.mapParams).toBeUndefined();
    expect(handlerSpecs.compose_pose_sequence.params.map((p) => p.name)).toEqual([
      "name", "packagePath", "sources", "layers", "numFrames", "frameRate", "easeToSource", "easeCurve", "onConflict",
    ]);
    const layers = handlerSpecs.compose_pose_sequence.params.find((p) => p.name === "layers");
    expect(layers?.fields?.map((f) => f.name)).toEqual([
      "source", "weight", "bones", "nameContains", "excludeContains", "includeDescendants", "rotationOnly",
    ]);
  });

  it("slerps rotations, writes every bone, and deletes a partial asset", () => {
    expect(readHandlerFile("AnimationHandlers.cpp")).toContain('TEXT("compose_pose_sequence"), &ComposePoseSequence');
    const source = readHandlerFile("AnimationHandlers_Compose.cpp");
    expect(source).toContain("FQuat::Slerp(A.GetRotation(), B.GetRotation(), Alpha)");
    expect(source).toContain("for (int32 Bone = 0; Bone < BoneCount && FailErr.IsEmpty(); ++Bone)");
    expect(source).toContain("UEditorAssetLibrary::DeleteLoadedAsset(NewSeq)");
    expect(source).toContain("MCPSetDeleteAssetRollback(Result, AssetPath)");
  });
});
