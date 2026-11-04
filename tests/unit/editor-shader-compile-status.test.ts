import { describe, expect, it, vi } from "vitest";
import type { ToolContext } from "../../src/core/types.js";
import { editorTool } from "../../src/tools/editor.js";
import { waitForShaderCompileIdle, type ShaderWaitClock } from "../../src/tools/editor-shader-wait.js";
import { handlerSpecs } from "../../src/tools/specs/editor.generated.js";

const busy = { success: true, idle: false, isCompiling: true, outstandingJobs: 12, pendingShaderMaps: 2, pendingMaterials: 1 };
const idle = { success: true, idle: true, isCompiling: false, outstandingJobs: 0, pendingShaderMaps: 0, pendingMaterials: 0 };

function clock(): ShaderWaitClock & { slept: number[] } {
  let now = 0;
  const slept: number[] = [];
  return { slept, now: () => now, sleep: async (ms) => { slept.push(ms); now += ms; } };
}

describe("editor shader compiler status", () => {
  it("publishes a native read and uses the shared routing timeout", () => {
    expect(editorTool.actions.get_shader_compile_status.effect).toBe("read");
    expect(editorTool.actions.get_shader_compile_status.bridge).toBe("get_shader_compile_status");
    expect(handlerSpecs.get_shader_compile_status.params.map((p) => p.name)).toEqual(["waitUntilIdle"]);
    expect(editorTool.schema.waitUntilIdle.safeParse(true).success).toBe(true);
  });

  it("returns a snapshot without waiting when not requested", async () => {
    const call = vi.fn();
    expect(await waitForShaderCompileIdle({ bridge: { call } } as unknown as ToolContext, {}, busy)).toBe(busy);
    expect(call).not.toHaveBeenCalled();
  });

  it("returns immediately if already idle", async () => {
    const call = vi.fn();
    const timer = clock();
    expect(await waitForShaderCompileIdle({ bridge: { call } } as unknown as ToolContext, { waitUntilIdle: true }, idle, timer))
      .toMatchObject({ success: true, state: "idle", durationMs: 0, polls: 1 });
    expect(call).not.toHaveBeenCalled();
    expect(timer.slept).toEqual([]);
  });

  it("yields between snapshots and returns the final counters", async () => {
    const call = vi.fn().mockResolvedValueOnce(busy).mockResolvedValueOnce(idle);
    const timer = clock();
    expect(await waitForShaderCompileIdle({ bridge: { call } } as unknown as ToolContext, { waitUntilIdle: true, timeoutMs: 500 }, busy, timer))
      .toMatchObject({ success: true, state: "idle", outstandingJobs: 0, durationMs: 150, polls: 3 });
    expect(timer.slept).toEqual([50, 100]);
    expect(call.mock.calls).toEqual([
      ["get_shader_compile_status", { waitUntilIdle: false }, 450],
      ["get_shader_compile_status", { waitUntilIdle: false }, 350],
    ]);
  });

  it("bounds the wait without forcing shader completion", async () => {
    const call = vi.fn().mockResolvedValue(busy);
    const timer = clock();
    expect(await waitForShaderCompileIdle({ bridge: { call } } as unknown as ToolContext, { waitUntilIdle: true, timeoutMs: 60 }, busy, timer))
      .toMatchObject({ success: false, state: "timed_out", durationMs: 60, outstandingJobs: 12 });
    expect(timer.slept).toEqual([50, 10]);
    expect(call).toHaveBeenCalledTimes(1);
  });

  it("waits for pending shader maps after outstanding jobs reach zero", async () => {
    const call = vi.fn().mockResolvedValue(idle);
    const finalizing = { ...busy, outstandingJobs: 0, isCompiling: false };
    expect(await waitForShaderCompileIdle({ bridge: { call } } as unknown as ToolContext,
      { waitUntilIdle: true }, finalizing, clock())).toMatchObject({ state: "idle", polls: 2 });
    expect(call).toHaveBeenCalledTimes(1);
  });

  it("preserves native failures and refuses malformed replies", async () => {
    const call = vi.fn();
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const failure = { success: false, error: "Unavailable" };
    expect(await waitForShaderCompileIdle(ctx, { waitUntilIdle: true }, failure, clock())).toBe(failure);
    expect(await waitForShaderCompileIdle(ctx, { waitUntilIdle: true }, {}, clock())).toMatchObject({ success: false, state: "failed" });
    expect(call).not.toHaveBeenCalled();
  });

  it("reports the last counters when a poll exhausts the remaining budget", async () => {
    const timer = clock();
    const call = vi.fn(async () => { await timer.sleep(10); throw new Error("bridge timeout"); });
    expect(await waitForShaderCompileIdle({ bridge: { call } } as unknown as ToolContext,
      { waitUntilIdle: true, timeoutMs: 60 }, busy, timer))
      .toMatchObject({ success: false, state: "timed_out", outstandingJobs: 12, durationMs: 60 });
  });

  it("propagates transport failures that happen before the deadline", async () => {
    const call = vi.fn().mockRejectedValue(new Error("disconnected"));
    await expect(waitForShaderCompileIdle({ bridge: { call } } as unknown as ToolContext,
      { waitUntilIdle: true, timeoutMs: 1000 }, busy, clock())).rejects.toThrow("disconnected");
  });

  it("does not accept an idle response received after the deadline", async () => {
    const timer = clock();
    const call = vi.fn(async () => { await timer.sleep(20); return idle; });
    expect(await waitForShaderCompileIdle({ bridge: { call } } as unknown as ToolContext,
      { waitUntilIdle: true, timeoutMs: 60 }, busy, timer))
      .toMatchObject({ success: false, state: "timed_out", durationMs: 70 });
  });

  it("carries timeoutMs to the follow-up without forwarding it to C++", async () => {
    const call = vi.fn().mockResolvedValue(busy);
    const action = editorTool.actions.get_shader_compile_status;
    if (action.kind !== "bridge") throw new Error("expected bridge action");
    const original = action.followUp;
    const observed = vi.fn().mockResolvedValue(idle);
    action.followUp = observed;
    try {
      await editorTool.handler({ bridge: { call } } as unknown as ToolContext,
        { action: "get_shader_compile_status", waitUntilIdle: true, timeoutMs: 250 });
      expect(call.mock.calls[0].slice(0, 3)).toEqual(["get_shader_compile_status", { waitUntilIdle: true }, 250]);
      expect(observed.mock.calls[0][1]).toEqual({ waitUntilIdle: true, timeoutMs: 250 });
    } finally {
      action.followUp = original;
    }
  });
});
