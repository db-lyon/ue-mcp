import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import { handlerSpecs } from "../../src/tools/specs/animation.generated.js";
import type { ToolContext } from "../../src/surface/tool-types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

describe("animation bone-track reads and transform_bone_tracks (#1227)", () => {
  it("read_bone_track and sample_pose take rotationFormat", () => {
    const track = handlerSpecs.read_bone_track.params.map((p) => p.name);
    expect(track).toEqual(["assetPath", "boneName", "frames", "rotationFormat", "space"]);
    expect(handlerSpecs.sample_pose.params.map((p) => p.name)).toContain("rotationFormat");
    expect(animationTool.actions.read_bone_track.description).toContain("rotationFormat=quaternion");
  });

  it("forwards transform_bone_tracks as sent", async () => {
    const action = animationTool.actions.transform_bone_tracks;
    expect(action.bridge).toBe("transform_bone_tracks");
    expect(action.effect).toBe("mutate");
    expect(action.mapParams).toBeUndefined();
    expect(handlerSpecs.transform_bone_tracks.params.map((p) => p.name)).toEqual(["assetPaths", "trackOps", "save"]);

    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = {
      assetPaths: ["/Game/Anim/A_Light_1"],
      trackOps: [
        { op: "rotate", bones: ["pelvis"], rotation: { x: 0, y: 0, z: 0.3007, w: 0.9537 }, rotateTranslation: true },
        { op: "copy_from_donor", bones: ["index_01_r"], donorPath: "/Game/Anim/A_Fist", donorFrame: 18 },
        { op: "reference_delta", sourceBone: "ring_02_r", targetBone: "pinky_02_r", donorPath: "/Game/Anim/A_Fist", donorFrame: 18 },
      ],
    };
    await animationTool.handler(ctx, { action: "transform_bone_tracks", ...params });
    expect(call).toHaveBeenCalledWith("transform_bone_tracks", params, undefined);
  });

  it("registers the handler and validates before writing", () => {
    expect(readHandlerFile("AnimationHandlers.cpp")).toContain('TEXT("transform_bone_tracks"), &TransformBoneTracks');
    const source = readHandlerFile("AnimationHandlers_BoneTracks.cpp");
    expect(source).toContain("RefTarget.GetRotation() * (RefSource.GetRotation().Inverse() * Source)");
    expect(source.indexOf("is not on the skeleton of '%s'. Nothing was written."))
      .toBeLessThan(source.indexOf("Controller.OpenBracket("));
  });
});
