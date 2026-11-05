import { describe, expect, it, vi } from "vitest";
import type { ToolContext } from "../../src/core/types.js";
import { editorTool } from "../../src/tools/editor.js";
import {
  captureScreenshotSequence,
  sequenceFrameName,
  type CaptureSequenceClock,
} from "../../src/tools/editor-capture-sequence.js";

function clock(): CaptureSequenceClock & { slept: number[]; advance(ms: number): void } {
  let now = 0;
  const slept: number[] = [];
  return {
    slept,
    now: () => now,
    sleep: async (ms) => { slept.push(ms); now += ms; },
    advance: (ms) => { now += ms; },
  };
}

function bridgeFor(timer: ReturnType<typeof clock>, captureMs: number, failAt?: number) {
  const call = vi.fn(async (_method: string, params: Record<string, unknown>) => {
    timer.advance(captureMs);
    const index = call.mock.calls.length - 1;
    if (index === failAt) return { success: false, error: "No PIE world" };
    return { success: true, target: "pie", filename: `C:/Saved/Screenshots/${String(params.filename)}`, width: 1280, height: 720 };
  });
  return { ctx: { bridge: { call } } as unknown as ToolContext, call };
}

describe("editor(capture_screenshot_sequence)", () => {
  it("is a mutate handler with its own parameters on the editor schema", () => {
    const spec = editorTool.actions.capture_screenshot_sequence;
    expect(spec.kind).toBe("handler");
    expect(spec.effect).toBe("mutate");
    expect(editorTool.schema.frameCount.safeParse(5).success).toBe(true);
    expect(editorTool.schema.intervalMs.safeParse(0).success).toBe(true);
  });

  it("numbers frames and keeps the caller's extension", () => {
    expect(sequenceFrameName("Validation/run", 3, 4)).toBe("Validation/run_0003.png");
    expect(sequenceFrameName("shot.JPG", 12, 4)).toBe("shot_0012.JPG");
  });

  it("captures on a fixed schedule through capture_screenshot targeting PIE", async () => {
    const timer = clock();
    const { ctx, call } = bridgeFor(timer, 100);
    const result = await captureScreenshotSequence(ctx, { filename: "seq", frameCount: 3, intervalMs: 250, pieInstance: 1 }, timer);
    expect(call.mock.calls.map((c) => c[1])).toEqual([
      { target: "pie", pieInstance: 1, filename: "seq_0000.png" },
      { target: "pie", pieInstance: 1, filename: "seq_0001.png" },
      { target: "pie", pieInstance: 1, filename: "seq_0002.png" },
    ]);
    // Each 100 ms capture is followed by a 150 ms wait to the next 250 ms slot.
    expect(timer.slept).toEqual([150, 150]);
    expect(result).toMatchObject({ success: true, capturedFrames: 3, requestedFrames: 3, maxLateMs: 0 });
    expect((result.frames as Array<Record<string, unknown>>).map((f) => f.capturedAtMs)).toEqual([0, 250, 500]);
  });

  it("reports late frames without shifting the schedule", async () => {
    const timer = clock();
    const { ctx } = bridgeFor(timer, 300);
    const result = await captureScreenshotSequence(ctx, { filename: "seq", frameCount: 3, intervalMs: 200 }, timer);
    expect(timer.slept).toEqual([]);
    const frames = result.frames as Array<Record<string, number>>;
    expect(frames.map((f) => f.scheduledAtMs)).toEqual([0, 200, 400]);
    expect(frames.map((f) => f.lateMs)).toEqual([0, 100, 200]);
    expect(result.maxLateMs).toBe(200);
  });

  it("stops at the first failed frame and returns what was written", async () => {
    const timer = clock();
    const { ctx, call } = bridgeFor(timer, 10, 1);
    const result = await captureScreenshotSequence(ctx, { filename: "seq", frameCount: 5, intervalMs: 0 }, timer);
    expect(call).toHaveBeenCalledTimes(2);
    expect(result).toMatchObject({ success: false, failedFrame: 1, capturedFrames: 1 });
    expect(String(result.error)).toContain("No PIE world");
  });

  it("refuses bad parameters before capturing anything", async () => {
    const timer = clock();
    const { ctx, call } = bridgeFor(timer, 0);
    expect(await captureScreenshotSequence(ctx, { frameCount: 2 }, timer)).toMatchObject({ success: false });
    expect(await captureScreenshotSequence(ctx, { filename: "a", frameCount: 601 }, timer)).toMatchObject({ success: false });
    expect(await captureScreenshotSequence(ctx, { filename: "a", intervalMs: -1 }, timer)).toMatchObject({ success: false });
    expect(call).not.toHaveBeenCalled();
  });
});
