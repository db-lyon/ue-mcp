import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import { handlerSpecs } from "../../src/tools/specs/animation.generated.js";
import type { ToolContext } from "../../src/core/types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const PARAMS = [
  "sourcePath",
  "destinationPath",
  "name",
  "packagePath",
  "inPlace",
  "cycleOffsetFrames",
  "cycleOffsetSeconds",
  "onConflict",
];

describe("animation.reverse_sequence (#1162)", () => {
  it("publishes the reversal contract", () => {
    const action = animationTool.actions.reverse_sequence;
    expect(action.bridge).toBe("reverse_sequence");
    expect(action.effect).toBe("mutate");
    expect(action.description).toContain("delete_asset rollback");
    expect(action.description).toContain("Params: sourcePath, destinationPath?, name?, packagePath?, inPlace?");
  });

  it("takes the reversal parameters from its C++ spec (#1057)", async () => {
    expect(animationTool.actions.reverse_sequence.mapParams).toBeUndefined();
    expect(handlerSpecs.reverse_sequence.params.map((p) => p.name).sort()).toEqual([...PARAMS].sort());

    const call = vi.fn().mockResolvedValue({ success: true });
    const context = { bridge: { call } } as unknown as ToolContext;
    await animationTool.handler(context, {
      action: "reverse_sequence",
      sourcePath: "/Game/Anims/A_Walk",
      name: "A_Walk_Back",
      packagePath: "/Game/Anims/Reversed",
      cycleOffsetFrames: 4,
      onConflict: "error",
    });
    expect(call).toHaveBeenCalledWith("reverse_sequence", {
      sourcePath: "/Game/Anims/A_Walk",
      name: "A_Walk_Back",
      packagePath: "/Game/Anims/Reversed",
      cycleOffsetFrames: 4,
      onConflict: "error",
    }, undefined);
  });

  it("reads the same parameter names in C++, through the tracked helpers", () => {
    const registration = readHandlerFile("AnimationHandlers.cpp");
    expect(registration).toContain('TEXT("reverse_sequence"), &ReverseSequence');

    const source = readHandlerFile("AnimationHandlers_Reverse.cpp");
    for (const name of PARAMS) {
      expect(source).toMatch(new RegExp(`\\(Params, TEXT\\("${name}"\\)`));
    }
    expect(source).not.toMatch(/Params->(TryGet|HasField|Get[A-Z])/);
    // Bone keys go through the data controller, and a new copy is undone by deleting it.
    expect(source).toContain("Controller.SetBoneTrackKeys(Bone, Locations, Rotations, Scales, false)");
    expect(source).toContain("MCPSetDeleteAssetRollback(Result, TargetPath)");
    expect(source).toContain('MCPSetRollback(Result, TEXT("reverse_sequence"), Payload)');
  });
});
