import type { ToolContext } from "../core/types.js";

/** A rendering job progresses on editor frames, outside any bridge call. */
export interface MeshStudioClock {
  now(): number;
  sleep(ms: number): Promise<void>;
}

const realClock: MeshStudioClock = {
  now: () => Date.now(),
  sleep: (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
};

function report(value: unknown): Record<string, unknown> | undefined {
  return value !== null && typeof value === "object" && !Array.isArray(value)
    ? value as Record<string, unknown> : undefined;
}

/** Return the final manifest through the normal bridge result pipeline. */
export async function waitForMeshStudioRender(
  ctx: ToolContext,
  _params: Record<string, unknown>,
  answered: unknown,
  clock: MeshStudioClock = realClock,
): Promise<unknown> {
  let last = report(answered);
  if (!last || last.state !== "running" || typeof last.jobId !== "string") return answered;
  const jobId = last.jobId;
  const started = clock.now();
  // The native job has a 540s deadline, including when the connection drops.
  // Leave time for its final manifest and a cancellation within the 600s call.
  const budgetMs = Math.min(ctx.callTimeoutMs ?? 570_000, 570_000);
  let failure = "Mesh studio render did not finish within the wait budget";
  try {
    while (clock.now() - started < budgetMs) {
      await clock.sleep(Math.min(250, Math.max(0, budgetMs - (clock.now() - started))));
      const next = report(await ctx.bridge.call("get_mesh_studio_render", { jobId }, 15_000));
      if (!next || next.jobId !== jobId || typeof next.state !== "string") {
        throw new Error("Invalid mesh studio render status response");
      }
      last = next;
      if (next.state !== "running") return next;
    }
  } catch (error) {
    failure = error instanceof Error ? error.message : String(error);
  }
  try {
    const cancelled = report(await ctx.bridge.call("cancel_mesh_studio_render", { jobId }, 15_000));
    if (cancelled?.jobId === jobId && typeof cancelled.state === "string" && cancelled.state !== "running") {
      // A job can finish between the last poll and cancellation. Preserve that
      // completed result, including its per-item failures, if it won the race.
      if (cancelled.state === "completed") return cancelled;
      return { ...cancelled, success: false, waitError: failure };
    }
  } catch {
    // The editor still owns the job and its deadline when the socket is gone.
  }
  return { ...last, success: false, state: "disconnected", error: failure, cleanupPending: true };
}
