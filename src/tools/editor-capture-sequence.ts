import type { ToolContext } from "../core/types.js";

export interface CaptureSequenceClock {
  now(): number;
  sleep(ms: number): Promise<void>;
}

const realClock: CaptureSequenceClock = {
  now: () => Date.now(),
  sleep: (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
};

export const MAX_SEQUENCE_FRAMES = 600;
const DEFAULT_FRAME_COUNT = 10;
const DEFAULT_INTERVAL_MS = 500;

interface FrameRow {
  index: number;
  filename: string;
  scheduledAtMs: number;
  capturedAtMs: number;
  durationMs: number;
  lateMs: number;
  width?: number;
  height?: number;
}

/** `<base>_0007.png`, keeping the caller's image extension when it gave one. */
export function sequenceFrameName(base: string, index: number, digits: number): string {
  const match = /\.(png|jpg|bmp)$/i.exec(base);
  const stem = match ? base.slice(0, -match[0].length) : base;
  const ext = match ? match[0] : ".png";
  return `${stem}_${String(index).padStart(digits, "0")}${ext}`;
}

function positiveInt(value: unknown, fallback: number): number | undefined {
  if (value === undefined) return fallback;
  return typeof value === "number" && Number.isInteger(value) && value > 0 ? value : undefined;
}

/**
 * Capture a numbered frame sequence on a fixed wall-clock schedule (#1645).
 * Frame k is due at start + k * intervalMs; a capture that overruns its slot
 * is reported as late rather than shifting every later frame.
 */
export async function captureScreenshotSequence(
  ctx: ToolContext,
  p: Record<string, unknown>,
  clock: CaptureSequenceClock = realClock,
): Promise<Record<string, unknown>> {
  const base = typeof p.filename === "string" && p.filename.trim() !== ""
    ? p.filename.trim()
    : typeof p.outputPath === "string" && p.outputPath.trim() !== "" ? p.outputPath.trim() : undefined;
  if (!base) return { success: false, error: "Missing 'filename': the base image path. Frames are written as <filename>_0000.png, <filename>_0001.png and so on." };
  const frameCount = positiveInt(p.frameCount, DEFAULT_FRAME_COUNT);
  if (frameCount === undefined || frameCount > MAX_SEQUENCE_FRAMES)
    return { success: false, error: `'frameCount' must be an integer from 1 to ${MAX_SEQUENCE_FRAMES}.` };
  const intervalMs = p.intervalMs === undefined ? DEFAULT_INTERVAL_MS : p.intervalMs;
  if (typeof intervalMs !== "number" || !Number.isFinite(intervalMs) || intervalMs < 0)
    return { success: false, error: "'intervalMs' must be a number of milliseconds, 0 or more." };

  const forward: Record<string, unknown> = { target: p.target ?? "pie" };
  if (p.pieInstance !== undefined) forward.pieInstance = p.pieInstance;
  if (p.worldPath !== undefined) forward.worldPath = p.worldPath;

  const digits = Math.max(4, String(frameCount - 1).length);
  const frames: FrameRow[] = [];
  const started = clock.now();
  let resolvedTarget: unknown;
  let failure: { index: number; error: unknown } | undefined;

  for (let index = 0; index < frameCount; index++) {
    const due = index * intervalMs;
    const wait = due - (clock.now() - started);
    if (wait > 0) await clock.sleep(wait);
    const shotStarted = clock.now() - started;
    let answered: unknown;
    try {
      answered = await ctx.bridge.call("capture_screenshot", { ...forward, filename: sequenceFrameName(base, index, digits) });
    } catch (error) {
      failure = { index, error: error instanceof Error ? error.message : String(error) };
      break;
    }
    const body = (answered && typeof answered === "object" ? answered : {}) as Record<string, unknown>;
    if (body.success === false) {
      failure = { index, error: body.error ?? "capture_screenshot failed" };
      break;
    }
    if (resolvedTarget === undefined) resolvedTarget = body.target;
    const row: FrameRow = {
      index,
      filename: typeof body.filename === "string" ? body.filename : sequenceFrameName(base, index, digits),
      scheduledAtMs: due,
      capturedAtMs: shotStarted,
      durationMs: clock.now() - started - shotStarted,
      lateMs: Math.max(0, shotStarted - due),
    };
    if (typeof body.width === "number") row.width = body.width;
    if (typeof body.height === "number") row.height = body.height;
    frames.push(row);
  }

  const lateness = frames.map((f) => f.lateMs);
  return {
    success: failure === undefined,
    ...(failure ? { error: `Frame ${failure.index} failed: ${String(failure.error)}`, failedFrame: failure.index } : {}),
    target: resolvedTarget ?? forward.target,
    requestedFrames: frameCount,
    capturedFrames: frames.length,
    intervalMs,
    durationMs: clock.now() - started,
    maxLateMs: lateness.length ? Math.max(...lateness) : 0,
    timingNote: "Frames are scheduled on the server's wall clock. Each capture is synchronous on the editor's game thread, so lateMs is how far a frame started after its slot; a capture slower than intervalMs makes every later frame late.",
    frames,
  };
}
