/**
 * Waiting for a pie(drive_pawn) drive to end (#1565).
 *
 * The editor applies the drive on later frames, so its handler answers at once with a driveId. With wait (the
 * default) the server polls pie_drive_status until the drive is no longer running and answers with its whole track.
 */
import type { ToolContext } from "../core/types.js";

/** Injectable for tests. */
export interface DriveClock {
  now(): number;
  sleep(ms: number): Promise<void>;
}

const realClock: DriveClock = {
  now: () => Date.now(),
  sleep: (ms) =>
    new Promise((resolve) => {
      const t = setTimeout(resolve, ms);
      t.unref?.();
    }),
};

type Body = Record<string, unknown>;

/** Polls between this many milliseconds: short for a short drive, never hammering the bridge on a long one. */
function pollMs(durationSeconds: number): number {
  return Math.min(1000, Math.max(100, durationSeconds * 50));
}

export async function driveFollowUp(
  ctx: ToolContext,
  params: Record<string, unknown>,
  answered: unknown,
  clock: DriveClock = realClock,
): Promise<unknown> {
  if (params.wait === false || !answered || typeof answered !== "object") return answered;
  const body = answered as Body;
  if (body.success === false || typeof body.driveId !== "string") return answered;
  if (body.running !== true) {
    // Ended on its first frame (already at the target, no navmesh path): the track is one call away.
    const final = (await ctx.bridge.call("pie_drive_status", { driveId: body.driveId })) as Body;
    return final && final.success !== false ? final : answered;
  }

  const duration = typeof body.durationSeconds === "number" ? body.durationSeconds : 5;
  const timeoutSeconds = typeof params.timeoutSeconds === "number" ? params.timeoutSeconds : duration + 30;
  const started = clock.now();
  const interval = pollMs(duration);
  let failures = 0;
  for (;;) {
    await clock.sleep(interval);
    let status: Body | undefined;
    try {
      status = (await ctx.bridge.call("pie_drive_status", { driveId: body.driveId, includeSamples: false })) as Body;
      failures = 0;
    } catch (err) {
      if (++failures >= 5) {
        return { ...body, wait: { state: "unreachable", durationMs: clock.now() - started, error: String(err) } };
      }
    }
    if (status && status.success === false) return status;
    if (status && status.running !== true) {
      const final = (await ctx.bridge.call("pie_drive_status", { driveId: body.driveId })) as Body;
      return { ...final, wait: { state: "ended", durationMs: clock.now() - started } };
    }
    if (clock.now() - started >= timeoutSeconds * 1000) {
      const latest = status ?? body;
      return {
        ...latest,
        wait: { state: "timed_out", durationMs: clock.now() - started },
        warnings: [`The drive is still running after ${timeoutSeconds}s; read it with pie(drive_status) or end it with pie(drive_stop).`],
      };
    }
  }
}
