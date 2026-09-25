import { describe, expect, it, vi } from "vitest";
import { editorTool } from "../../src/tools/editor.js";
import { classifyActionClass } from "../../src/action-class.js";
import type { ToolContext } from "../../src/types.js";

/**
 * #1098: a frame range renders in one bridge call, so the call needs the long
 * timeout and every parameter the handler reads, under the same name.
 */
describe("editor.render_sequence_frames", () => {
  it("forwards every parameter by the handler's names, with the long timeout", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await editorTool.handler(ctx, {
      action: "render_sequence_frames",
      assetPath: "/Game/Cinematics/LS_Cycle",
      startFrame: 0,
      endFrame: 41,
      frameStep: 1,
      maxFrames: 100,
      width: 960,
      height: 540,
      outputDir: "Saved/Renders/Cycle",
      format: "png",
      cameraActorLabel: "Cam",
    });

    expect(call).toHaveBeenCalledWith("render_sequence_frames", {
      sequencePath: "/Game/Cinematics/LS_Cycle",
      startFrame: 0,
      endFrame: 41,
      startSeconds: undefined,
      endSeconds: undefined,
      frameStep: 1,
      maxFrames: 100,
      width: 960,
      height: 540,
      outputDir: "Saved/Renders/Cycle",
      format: "png",
      cameraActorLabel: "Cam",
      cameraActorPath: undefined,
      location: undefined,
      rotation: undefined,
      fov: undefined,
      fullyLoadTextures: undefined,
    }, 600_000);
  });

  it("classifies as a mutation", () => {
    expect(classifyActionClass("editor", "render_sequence_frames").class).toBe("mutate");
  });
});
