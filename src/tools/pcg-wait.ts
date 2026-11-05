/**
 * Waiting for a PCG generation to finish (#1365).
 *
 * A generation completes on later editor ticks, so a handler cannot block for it on the game thread. The server polls
 * the cheap get_pcg_generation_state instead, which also binds the component's completion delegates: a run started by
 * anything (execute, the editor, a project UFUNCTION that defers to the next tick) is counted once it ends.
 */
import type { ToolContext } from "../core/types.js";

export type WaitState = "generated" | "cancelled" | "cleaned" | "not_started" | "idle" | "timed_out";

export interface WaitOptions {
  actorLabel?: string;
  actorPath?: string;
  componentName?: string;
  /** Wait for this run (execute/force_regenerate's runId) rather than whatever is in flight. */
  runId?: number;
  timeoutSeconds?: number;
  /**
   * With no runId and nothing generating, how long to wait for a deferred start before reporting idle. With a runId
   * whose run settled as not_started, how long to wait for the generation its Generate deferred.
   */
  graceSeconds?: number;
}

export interface WaitResult {
  success: boolean;
  state: WaitState;
  durationMs: number;
  polls: number;
  runId?: number;
  componentName?: string;
  lastRun?: unknown;
  /** not_started only: the component already held generated output, so nothing needed to run. */
  alreadyGenerated?: boolean;
  /** not_started only: when the component's most recent generation finished, and how. */
  lastFinishUtc?: string;
  lastFinishState?: string;
  note?: string;
  error?: string;
}

interface GenerationState {
  success?: boolean;
  error?: string;
  busy?: boolean;
  finishCount?: number;
  lastFinishState?: string;
  lastFinishUtc?: string;
  generated?: boolean;
  componentName?: string;
  lastRun?: { id?: number; state?: string; deferredStart?: boolean } | null;
}

/** Injectable for tests. */
export interface WaitClock {
  now(): number;
  sleep(ms: number): Promise<void>;
}

const realClock: WaitClock = {
  now: () => Date.now(),
  sleep: (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
};

const FINISHED: ReadonlySet<string> = new Set(["generated", "cancelled", "cleaned", "not_started"]);

export async function waitForGeneration(ctx: ToolContext, options: WaitOptions, clock: WaitClock = realClock): Promise<WaitResult> {
  const selector: Record<string, unknown> = {};
  if (options.actorLabel !== undefined) selector.actorLabel = options.actorLabel;
  if (options.actorPath !== undefined) selector.actorPath = options.actorPath;
  if (options.componentName !== undefined) selector.componentName = options.componentName;
  const timeoutMs = Math.max(0, (options.timeoutSeconds ?? 600) * 1000);
  const graceMs = Math.max(0, (options.graceSeconds ?? 2) * 1000);
  const started = clock.now();
  let polls = 0;
  let pollMs = 250;

  const read = async (): Promise<GenerationState> => {
    polls++;
    return (await ctx.bridge.call("get_pcg_generation_state", selector)) as GenerationState;
  };
  const finish = (state: WaitState, last: GenerationState, error?: string): WaitResult => ({
    success: state !== "timed_out" && error === undefined,
    state,
    durationMs: clock.now() - started,
    polls,
    ...(options.runId !== undefined ? { runId: options.runId } : {}),
    ...(last.componentName !== undefined ? { componentName: last.componentName } : {}),
    ...(last.lastRun !== undefined ? { lastRun: last.lastRun } : {}),
    ...(state === "not_started" ? notStartedDetail(last) : {}),
    ...(error !== undefined ? { error } : {}),
  });

  const first = await read();
  if (first.success === false) return finish("timed_out", first, first.error ?? "get_pcg_generation_state failed");
  const startCount = first.finishCount ?? 0;
  let sawBusy = first.busy === true;
  let last = first;

  for (;;) {
    if (options.runId !== undefined) {
      const run = last.lastRun;
      // Generate can defer its start to a later tick; the editor then moves a not_started run on to that
      // generation, so not_started is final only once the grace window has passed.
      const settledEarly = run?.state === "not_started" && clock.now() - started < graceMs;
      if (run && run.id === options.runId && run.state && FINISHED.has(run.state) && !last.busy && !settledEarly) {
        return finish(run.state as WaitState, last);
      }
      if (run && typeof run.id === "number" && run.id > options.runId && !last.busy) {
        // A later run replaced this one; it can no longer finish on its own.
        return finish("cancelled", last);
      }
    } else if (!last.busy) {
      if (sawBusy || (last.finishCount ?? 0) > startCount) {
        const state = (last.lastFinishState ?? "generated") as WaitState;
        return finish(FINISHED.has(state) ? state : "generated", last);
      }
      if (clock.now() - started >= graceMs) return finish("idle", last);
    }
    if (clock.now() - started >= timeoutMs) {
      return finish("timed_out", last, `The generation did not finish within ${timeoutMs / 1000}s.`);
    }
    await clock.sleep(pollMs);
    pollMs = Math.min(pollMs * 2, 1000);
    last = await read();
    if (last.success === false) return finish("timed_out", last, last.error ?? "get_pcg_generation_state failed");
    if (last.busy) sawBusy = true;
  }
}

/** What a not_started run found: the component's existing output, so "nothing ran" is not read as "nothing there". */
function notStartedDetail(last: GenerationState): Partial<WaitResult> {
  const out: Partial<WaitResult> = {};
  if (last.generated !== undefined) out.alreadyGenerated = last.generated;
  if (last.lastFinishUtc !== undefined) out.lastFinishUtc = last.lastFinishUtc;
  if (last.lastFinishState !== undefined) out.lastFinishState = last.lastFinishState;
  out.note = last.generated
    ? "Generate started nothing: the component's output was already up to date. lastFinishUtc says when it was last generated."
    : "Generate started nothing and the component holds no generated output; check that it has a graph and is active.";
  return out;
}

/**
 * The follow-up execute and force_regenerate run when called with wait=true: wait for the runId they returned and
 * attach the outcome as `wait`. A wait that times out is reported, not turned into a failed regenerate.
 */
export async function waitAfterRegenerate(
  ctx: ToolContext,
  params: Record<string, unknown>,
  answered: unknown,
  clock: WaitClock = realClock,
): Promise<unknown> {
  if (params.wait !== true || !answered || typeof answered !== "object") return answered;
  const body = answered as Record<string, unknown>;
  if (body.success === false || typeof body.runId !== "number") return answered;
  const wait = await waitForGeneration(ctx, {
    actorPath: typeof body.actorPath === "string" ? body.actorPath : undefined,
    componentName: typeof body.componentName === "string" ? body.componentName : undefined,
    runId: body.runId,
    timeoutSeconds: typeof params.timeoutSeconds === "number" ? params.timeoutSeconds : undefined,
  }, clock);
  const warnings = Array.isArray(body.warnings) ? [...body.warnings] : [];
  if (wait.state === "timed_out") warnings.push(wait.error ?? "The wait timed out; the run is still going.");
  return { ...body, wait, ...(warnings.length > 0 ? { warnings } : {}) };
}
