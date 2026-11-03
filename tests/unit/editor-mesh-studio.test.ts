import { describe, expect, it, vi } from "vitest";
import type { ToolContext } from "../../src/core/types.js";
import { editorTool } from "../../src/tools/editor.js";
import { declaredActionEffect } from "../../src/surface/action-effects.js";
import { waitForMeshStudioRender, type MeshStudioClock } from "../../src/tools/mesh-studio-wait.js";

function clock(): MeshStudioClock {
  let now = 0;
  return { now: () => now, sleep: async (ms) => { now += ms; } };
}

const running = { success: true, jobId: "studio-1", state: "running", manifest: "Saved/Icons/manifest.json" };
const completed = {
  ...running, state: "completed", cleanupComplete: true, dirtyPackagesUnchanged: true,
  outcomes: [{ id: "cube", status: "rendered", file: "Saved/Icons/cube.png" }],
};

describe("editor.render_mesh_studio", () => {
  it("forwards the nested assembly contract unchanged and waits for the final manifest", async () => {
    const call = vi.fn().mockResolvedValueOnce(running).mockResolvedValueOnce(completed);
    const params = {
      captures: [{ id: "cube", parts: [{ meshPath: "/Engine/BasicShapes/Cube.Cube", relativeTransform: {
        location: { x: 10, y: 0, z: 5 }, rotation: { pitch: 0, yaw: 45, roll: 0 }, scale: { x: 1, y: 2, z: 1 },
      } }], aspect: { width: 1, height: 2 } }],
      outputDir: "Saved/Icons", resolution: 128, viewDirection: { x: 1, y: -1, z: 1 },
      lightingPreset: "studio", captureMode: "both", fullyLoadTextures: true,
      cubemapPath: "/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap",
    };
    const result = await editorTool.handler({ bridge: { call } } as unknown as ToolContext, { action: "render_mesh_studio", ...params });
    expect(call.mock.calls[0]).toEqual(["render_mesh_studio", params, 600_000]);
    expect(call.mock.calls[1]).toEqual(["get_mesh_studio_render", { jobId: running.jobId }, 15_000]);
    expect(result).toEqual(completed);
    expect(declaredActionEffect("editor", "render_mesh_studio")).toBe("mutate");
    expect(declaredActionEffect("editor", "get_mesh_studio_render")).toBe("read");
    expect(declaredActionEffect("editor", "cancel_mesh_studio_render")).toBe("mutate");
  });

  it("rejects invalid pixel sizes, capture modes and nested aspect types through the recorded schema", async () => {
    const call = vi.fn();
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const base = { action: "render_mesh_studio", outputDir: "Saved/Icons", captures: [{ id: "cube", meshPath: "/Engine/BasicShapes/Cube" }] };
    for (const bad of [{ resolution: 32 }, { resolution: 64.5 }, { captureMode: "hdr" }, {
      captures: [{ id: "cube", meshPath: "/Engine/BasicShapes/Cube", aspect: { width: "wide", height: 1 } }],
    }]) {
      // The category checks the generated per-action contract before dispatch.
      await expect(editorTool.handler(ctx, { ...base, ...bad })).rejects.toThrow();
    }
    expect(call).not.toHaveBeenCalled();
  });

  it("preserves partial failures and all per-item evidence", async () => {
    const final = { ...completed, success: false, outcomes: [...completed.outcomes, { id: "missing", status: "blocked", reason: "no mesh" }] };
    const call = vi.fn().mockResolvedValueOnce(running).mockResolvedValueOnce(final);
    const ctx = { bridge: { call } } as unknown as ToolContext;
    expect(await waitForMeshStudioRender(ctx, {}, running, clock())).toEqual(final);
    expect(call).toHaveBeenCalledTimes(2);
  });

  it("does not poll a refused start or an already completed job", async () => {
    const call = vi.fn();
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const refused = { success: false, error: "PIE is running" };
    expect(await waitForMeshStudioRender(ctx, {}, refused, clock())).toBe(refused);
    expect(await waitForMeshStudioRender(ctx, {}, completed, clock())).toBe(completed);
    expect(call).not.toHaveBeenCalled();
  });

  it("cancels on a wait timeout and returns the cleanup evidence", async () => {
    const cancelled = { ...completed, success: true, state: "cancelled" };
    const call = vi.fn().mockResolvedValueOnce(running).mockResolvedValueOnce(cancelled);
    const ctx = { bridge: { call }, callTimeoutMs: 250 } as unknown as ToolContext;
    expect(await waitForMeshStudioRender(ctx, {}, running, clock())).toMatchObject({
      state: "cancelled", cleanupComplete: true, dirtyPackagesUnchanged: true, success: false, waitError: expect.any(String),
    });
    expect(call.mock.calls[1]).toEqual(["cancel_mesh_studio_render", { jobId: running.jobId }, 15_000]);
  });

  it("carries the caller's timeout through the bridge follow-up to cancellation", async () => {
    vi.useFakeTimers();
    try {
      const cancelled = { ...completed, success: true, state: "cancelled" };
      const call = vi.fn().mockResolvedValueOnce(running).mockResolvedValueOnce(running).mockResolvedValueOnce(cancelled);
      const ctx = { bridge: { call } } as unknown as ToolContext;
      const request = editorTool.handler(ctx, {
        action: "render_mesh_studio", outputDir: "Saved/Icons", timeoutMs: 1,
        captures: [{ id: "cube", meshPath: "/Engine/BasicShapes/Cube.Cube" }],
      });
      await vi.advanceTimersByTimeAsync(1);
      const result = await request;
      expect(call.mock.calls[0][2]).toBe(1);
      expect(call.mock.calls[2][0]).toBe("cancel_mesh_studio_render");
      expect(result).toMatchObject({ state: "cancelled", cleanupComplete: true });
    } finally {
      vi.useRealTimers();
    }
  });

  it("preserves a job which completes while cancellation is in flight", async () => {
    const call = vi.fn().mockResolvedValueOnce(running).mockResolvedValueOnce(completed);
    const ctx = { bridge: { call }, callTimeoutMs: 250 } as unknown as ToolContext;
    expect(await waitForMeshStudioRender(ctx, {}, running, clock())).toEqual(completed);
  });

  it("attempts cancellation after an invalid or disconnected poll, without claiming cleanup", async () => {
    const call = vi.fn().mockResolvedValueOnce({ ...running, jobId: "another-job" }).mockRejectedValueOnce(new Error("socket closed"));
    const ctx = { bridge: { call } } as unknown as ToolContext;
    expect(await waitForMeshStudioRender(ctx, {}, running, clock())).toMatchObject({
      success: false, state: "disconnected", cleanupPending: true, jobId: running.jobId,
    });
    expect(call.mock.calls[1][0]).toBe("cancel_mesh_studio_render");
  });
});
