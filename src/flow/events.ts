import type { PlanStep, FlowStepResult, FlowRunResult } from "@db-lyon/flowkit";

/**
 * Per-flow-run lifecycle events emitted by the flow tool while a flow
 * executes. The HTTP server pipes these into an SSE stream so external
 * clients (editor plugins, dashboards, `curl --no-buffer`) can observe
 * live progress.
 *
 * One module-level bus serves every concurrent run; the `runId` field
 * lets subscribers filter to a specific run. The flow.run response
 * includes the same runId so callers can correlate.
 */
export type FlowEvent =
  | {
      type: "run_started";
      runId: string;
      flowName: string;
      plan: PlanStep[];
      timestamp: number;
    }
  | {
      type: "step_started";
      runId: string;
      flowName: string;
      step: PlanStep;
      timestamp: number;
    }
  | {
      type: "step_completed";
      runId: string;
      flowName: string;
      step: PlanStep;
      // Trimmed: drops result.data (which can be large - shell outputs,
      // CSV rows, etc.). Subscribers that need the full data should pull
      // from the final run_completed event or the flow.run response.
      result: {
        success: boolean;
        skipped: boolean;
        duration: number;
        attempts?: number;
        error?: { message: string; name: string };
      };
      timestamp: number;
    }
  | {
      type: "step_failed";
      runId: string;
      flowName: string;
      step: PlanStep;
      error: { message: string; name: string };
      timestamp: number;
    }
  | {
      type: "run_completed";
      runId: string;
      flowName: string;
      success: boolean;
      duration: number;
      stepCount: number;
      failedStep?: string;
      timestamp: number;
    };

type Listener = (event: FlowEvent) => void;

const listeners = new Set<Listener>();
let runCounter = 0;

export function nextRunId(): string {
  runCounter++;
  return `run-${Date.now()}-${runCounter}`;
}

export function emitFlowEvent(event: FlowEvent): void {
  for (const fn of listeners) {
    try {
      fn(event);
    } catch {
      // Listener errors must not break flow execution. SSE writers can
      // throw on a dead connection - we treat that as "client gone"
      // and silently move on.
    }
  }
}

export function subscribeFlowEvents(listener: Listener): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function trimStepResult(r: FlowStepResult): {
  success: boolean;
  skipped: boolean;
  duration: number;
  attempts?: number;
  error?: { message: string; name: string };
} {
  return {
    success: r.result?.success ?? false,
    skipped: r.skipped,
    duration: r.duration,
    attempts: r.attempts,
    error: r.result?.error
      ? { message: r.result.error.message, name: r.result.error.name }
      : undefined,
  };
}

export function trimError(e: Error): { message: string; name: string } {
  return { message: e.message, name: e.name };
}

// Re-export FlowRunResult shape consumers may want for run_completed payload.
export type { FlowRunResult } from "@db-lyon/flowkit";
