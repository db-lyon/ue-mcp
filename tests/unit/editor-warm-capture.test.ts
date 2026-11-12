import { describe, expect, it, vi } from "vitest";
import fs from "node:fs";
import { editorTool } from "../../src/tools/editor.js";
import { actionSchema } from "../../src/surface/action-schema.js";
import type { ToolContext } from "../../src/core/types.js";

describe("editor warm scene capture contract", () => {
  it("forwards a warm start and a status/cancel request without requiring an output path for status", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const start = { outputPath: "Saved/warm.png", warmupFrames: 12, warmupTimeoutSeconds: 30,
      fullyLoadTextures: false, width: 1920, height: 1080, fov: 60,
      location: { x: 12, y: 34, z: 56 }, world: "pie", pieInstance: 2 };
    await editorTool.handler(ctx, { action: "capture_scene_png", ...start });
    expect(call).toHaveBeenLastCalledWith("capture_scene_png", start, undefined);
    await editorTool.handler(ctx, { action: "capture_scene_png", captureId: "capture-1", cancel: true });
    expect(call).toHaveBeenLastCalledWith("capture_scene_png", { captureId: "capture-1", cancel: true }, undefined);
    const schema = actionSchema(editorTool, "capture_scene_png");
    expect(schema.params.find(p => p.name === "outputPath")?.required).toBe(false);
    expect(schema.params.map(p => p.name)).toEqual(expect.arrayContaining(["captureId", "cancel", "warmupFrames", "warmupTimeoutSeconds"]));
  });

  it("submits the capture view to normal streaming and releases every asynchronous hook", () => {
    const source = fs.readFileSync("plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/HandlerSceneCaptureJob.cpp", "utf8");
    expect(source).toContain("ScreenSize / FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f))");
    expect(source).toContain("Streaming.AddViewInformation(Location, ScreenSize, FovScreenSize, 1.0f, false, WarmStreamingViewDurationSeconds");
    expect(source).toContain("SubmitWarmStreamingView(Comp->GetComponentLocation(), Target->SizeX, Comp->FOVAngle, World)");
    expect(source).toContain("Streaming.UpdateResourceStreaming(0.0f, false)");
    expect(source).toContain("Job->Rig.Capture(false)");
    expect(source).toContain("bAlwaysPersistRenderingState = true");
    expect(source).toContain("Job->LastFrame == GFrameCounter");
    expect(source).toContain("RemoveTicker(Ticker)");
    expect(source).toContain("OnWorldCleanup.Remove(WorldCleanup)");
    expect(source).not.toMatch(/StreamAllResources|SetForceMipLevelsToBeResident|SetCVar|bThrottleCPUWhenNotForeground/);
    expect(fs.readFileSync("plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/UE_MCP_Bridge.cpp", "utf8"))
      .toContain("UEMCP::SceneCapture::ShutdownWarmCapture()");
  });
});
