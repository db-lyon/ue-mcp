/**
 * Live progress of a PIE test script run, one module-level bus for every run in this process.
 *
 * The script watcher (script-watch.ts) emits these as the bridge reports them, and the daemon republishes each one on
 * its event stream as `script.<type>`. `runId` lets a subscriber follow one run; `seq` is the bridge's own number for
 * the event, so (runId, seq) identifies it across a watcher restart.
 */

export type ScriptEventType =
  | "run_started"
  | "step_started"
  | "step_acted"
  | "step_passed"
  | "step_failed"
  | "verdict_requested"
  | "verdict"
  | "run_finished";

export type ScriptJudge = "auto" | "manual";
export type ScriptStepStatus = "pending" | "running" | "passed" | "failed" | "awaiting_verdict" | "skipped";
/** action and elicitation are a person's verdicts, agent the calling agent's. */
export type ScriptVerdictSource = "auto" | "action" | "elicitation" | "agent" | "source_run";

export interface ScriptExpect {
  value: string;
  op: "decreasing" | "increasing" | "eq" | "gt" | "lt" | "visible" | "exists";
  target?: number | boolean;
  within_ms?: number;
}

export interface ScriptStep {
  /** 0-based. */
  index: number;
  instruction: string;
  judge: ScriptJudge;
  expect?: ScriptExpect;
  /** Manual steps: false means only a person may give the verdict. */
  agentJudgeable?: boolean;
  status: ScriptStepStatus;
  /** What the observer saw for an auto step: {value, at_start, at_end, samples, observed?}; for a manual step, the verdict's {note, captures?}. */
  evidence?: Record<string, unknown>;
  note?: string;
  verdictSource?: ScriptVerdictSource;
  /** The step's window in game milliseconds. */
  withinMs?: number;
  /** Game milliseconds since the run's first observed frame when the step started and settled. */
  startedAtMs?: number;
  endedAtMs?: number;
  /** Replay runs: the step's status in the source run, and whether it passed there and fails here. */
  sourceStatus?: ScriptStepStatus;
  regression?: boolean;
  /** Run when the step starts, before the expect window opens: a scenario's arrange entries, then uat_run's step actions. */
  arrange?: Array<Record<string, unknown>>;
  act?: Array<Record<string, unknown>>;
  /**
   * What arrange and act did: {status: running|done|failed|skipped, reason?, arranged, actions: [{index, action, ok, error?, at_ms}],
   * started_at_ms, ended_at_ms, duration_ms, wall_ms, captures?}. A skipped one carries only status and reason.
   */
  actResult?: Record<string, unknown>;
}

export interface ScriptSummary {
  total: number;
  passed: number;
  failed: number;
  skipped: number;
  pending: number;
  /** Manual steps still waiting for a verdict. */
  awaiting?: number;
  /** Replay runs: steps that passed in the source run and fail in the replay. */
  regressions?: number;
  /** Manual steps settled by a person, and by an agent. */
  humanVerdicts?: number;
  agentVerdicts?: number;
  /** Manual runs: auto steps nothing observed. */
  unchecked?: number;
}

interface ScriptEventBase {
  runId: string;
  /** The script's asset path, or "inline" for a run of inline steps. */
  script: string;
  /** Wall time of the event, ISO 8601. */
  at: string;
  /** Milliseconds since the run started, as the bridge measured it. */
  atMs: number;
  /** The bridge's sequence number for this event, increasing within a run. */
  seq: number;
  /** For a replay run, the run it replays. */
  replayOf?: string;
  /** full, or manual for a run with no PIE where only the manual steps are judged. */
  mode?: "full" | "manual";
}

export type ScriptEvent =
  | (ScriptEventBase & { type: "run_started"; resultsPath?: string })
  | (ScriptEventBase & { type: "step_started" | "step_acted" | "step_passed" | "step_failed" | "verdict"; step: ScriptStep })
  | (ScriptEventBase & { type: "verdict_requested"; awaiting: Array<{ index: number; instruction: string }> })
  | (ScriptEventBase & {
      type: "run_finished";
      /** finished, or stopped when pie(script_stop) ended it. */
      state?: string;
      summary: ScriptSummary;
      resultsPath?: string;
    });

type Listener = (event: ScriptEvent) => void;

const listeners = new Set<Listener>();

export function emitScriptEvent(event: ScriptEvent): void {
  for (const fn of listeners) {
    try {
      fn(event);
    } catch {
      // A broken subscriber (an SSE writer on a dead connection) must not stop the others or the watcher.
    }
  }
}

export function subscribeScriptEvents(listener: Listener): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}
