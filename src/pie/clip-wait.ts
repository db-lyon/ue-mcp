// @session per-session: Follows one clip run through the ToolContext of the call that started it, so it polls that call's editor.
/**
 * Waiting for a playtest clip run (#2097).
 *
 * The editor plays a clip on later frames, so pie(clip_run) and pie(clip_status) answer at once. With wait the server
 * polls pie_clip_status until the run finishes, stops, or reaches a manual check that waits for pie(clip_verdict), and
 * answers with the run as it then stands.
 */
import type { ToolContext } from "../surface/tool-types.js";

/** Injectable for tests. */
export interface ClipWaitClock {
  now(): number;
  sleep(ms: number): Promise<void>;
}

const realClock: ClipWaitClock = {
  now: () => Date.now(),
  sleep: (ms) =>
    new Promise((resolve) => {
      const t = setTimeout(resolve, ms);
      t.unref?.();
    }),
};

type Body = Record<string, unknown>;

export type ClipWaitState = "finished" | "stopped" | "awaiting_verdict" | "timed_out" | "unreachable";

const DEFAULT_TIMEOUT_SECONDS = 600;
const POLL_MS = 250;

/** Why a status ends the wait, or null to keep polling. */
export function clipWaitState(status: Body): ClipWaitState | null {
  const state = typeof status.state === "string" ? status.state : "";
  if (state === "finished" || state === "idle") return "finished";
  if (state === "stopped") return "stopped";
  if (Array.isArray(status.awaiting) && status.awaiting.length > 0) return "awaiting_verdict";
  return null;
}

export async function clipWaitFollowUp(
  ctx: ToolContext,
  params: Record<string, unknown>,
  answered: unknown,
  clock: ClipWaitClock = realClock,
): Promise<unknown> {
  if (params.wait !== true || !answered || typeof answered !== "object") return answered;
  const body = answered as Body;
  if (body.success === false || typeof body.run_id !== "string" || !body.run_id) return answered;
  const runId = body.run_id;
  const first = clipWaitState(body);
  if (first) return { ...body, wait: { state: first, durationMs: 0 } };

  const timeoutSeconds = typeof params.timeout_seconds === "number" ? params.timeout_seconds : DEFAULT_TIMEOUT_SECONDS;
  const started = clock.now();
  let failures = 0;
  let latest: Body = body;
  for (;;) {
    await clock.sleep(POLL_MS);
    try {
      const status = (await ctx.bridge.call("pie_clip_status", { run_id: runId })) as Body;
      failures = 0;
      if (status && status.success !== false) latest = status;
    } catch (err) {
      if (++failures >= 5) {
        return { ...latest, wait: { state: "unreachable" satisfies ClipWaitState, durationMs: clock.now() - started, error: String(err) } };
      }
      continue;
    }
    const state = clipWaitState(latest);
    if (state) return { ...latest, wait: { state, durationMs: clock.now() - started } };
    if (clock.now() - started >= timeoutSeconds * 1000) {
      return {
        ...latest,
        wait: { state: "timed_out" satisfies ClipWaitState, durationMs: clock.now() - started },
        warnings: [`The clip run is still going after ${timeoutSeconds}s; read it with pie(clip_status) or end it with pie(clip_stop).`],
      };
    }
  }
}
