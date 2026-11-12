import type { ToolContext } from "../core/types.js";

export interface ShaderWaitClock {
  now(): number;
  sleep(ms: number): Promise<void>;
}

const shaderWaitClock: ShaderWaitClock = {
  now: () => Date.now(),
  sleep: (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
};

interface ShaderStatus extends Record<string, unknown> {
  success: boolean;
  idle: boolean;
  outstandingJobs: number;
  isCompiling: boolean;
  pendingShaderMaps: number;
  pendingMaterials: number;
}

function isShaderStatus(value: unknown): value is ShaderStatus {
  if (!value || typeof value !== "object") return false;
  const body = value as Record<string, unknown>;
  return typeof body.success === "boolean" && typeof body.idle === "boolean"
    && typeof body.isCompiling === "boolean"
    && typeof body.outstandingJobs === "number" && Number.isFinite(body.outstandingJobs) && body.outstandingJobs >= 0
    && typeof body.pendingShaderMaps === "number" && Number.isFinite(body.pendingShaderMaps) && body.pendingShaderMaps >= 0
    && typeof body.pendingMaterials === "number" && Number.isFinite(body.pendingMaterials) && body.pendingMaterials >= 0;
}

/** Poll short reads on the addressed editor, leaving compilation free to advance between ticks. */
export async function waitForShaderCompileIdle(
  ctx: ToolContext,
  params: Record<string, unknown>,
  answered: unknown,
  clock: ShaderWaitClock = shaderWaitClock,
): Promise<unknown> {
  if (params.waitUntilIdle !== true) return answered;
  const timeoutMs = typeof params.timeoutMs === "number" && Number.isFinite(params.timeoutMs) && params.timeoutMs > 0
    ? params.timeoutMs : 60_000;
  const started = clock.now();
  let last = answered;
  let polls = 1;
  let interval = 50;
  const finish = (status: Record<string, unknown>, state: string, error?: string) => ({
    ...status,
    success: error === undefined,
    state,
    waitUntilIdle: true,
    durationMs: clock.now() - started,
    polls,
    ...(error ? { error } : {}),
  });
  for (;;) {
    if (last && typeof last === "object" && (last as Record<string, unknown>).success === false) return last;
    if (!isShaderStatus(last)) return finish({}, "failed", "The shader compiler status response was incomplete.");
    if (clock.now() - started > timeoutMs)
      return finish(last, "timed_out", `Shader compilation did not become idle within ${timeoutMs}ms.`);
    if (last.idle && !last.isCompiling && last.outstandingJobs === 0 && last.pendingShaderMaps === 0 && last.pendingMaterials === 0)
      return finish(last, "idle");
    const remaining = timeoutMs - (clock.now() - started);
    if (remaining <= 0) return finish(last, "timed_out", `Shader compilation did not become idle within ${timeoutMs}ms.`);
    await clock.sleep(Math.min(interval, remaining));
    const callBudget = timeoutMs - (clock.now() - started);
    if (callBudget <= 0) return finish(last, "timed_out", `Shader compilation did not become idle within ${timeoutMs}ms.`);
    const previous = last;
    try {
      last = await ctx.bridge.call("get_shader_compile_status", { waitUntilIdle: false }, callBudget);
    } catch (error) {
      if (clock.now() - started >= timeoutMs)
        return finish(previous, "timed_out", `Shader compilation did not become idle within ${timeoutMs}ms.`);
      throw error;
    }
    polls++;
    interval = Math.min(interval * 2, 500);
  }
}
