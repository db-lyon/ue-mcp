/**
 * Following a PIE test script run from the server.
 *
 * The C++ runner keeps the run's events, numbered by seq, and answers pie_script_status with the ones after since_seq.
 * One watcher per run polls that, puts each new event on the script event bus, and puts manual steps that await a
 * verdict to the person through MCP elicitation when the client can show a form. A step it could not ask about is left
 * for pie(verdict), and is never asked about again in that run.
 */
import type { ElicitParams, ElicitPrimitiveSchema, ToolContext } from "../core/types.js";
import { clientAdvertisesElicitation } from "../editor/dialog-mode.js";
import { minHumanAnswerMs } from "../editor/dialog-guard.js";
import {
  emitScriptEvent,
  type ScriptEvent,
  type ScriptStep,
  type ScriptStepStatus,
  type ScriptSummary,
} from "./script-events.js";

/** Injectable for tests. */
export interface WatchClock {
  now(): number;
  sleep(ms: number): Promise<void>;
}

const realClock: WatchClock = {
  now: () => Date.now(),
  sleep: (ms) =>
    new Promise((resolve) => {
      const t = setTimeout(resolve, ms);
      t.unref?.();
    }),
};

export type ScriptWaitState = "finished" | "stopped" | "awaiting_verdict" | "timed_out" | "unreachable";

/** How the last verdict form went, for a run that has manual steps. */
export type ElicitationOutcome = "accepted" | "declined" | "cancelled" | "auto_answered" | "failed" | "unavailable";

export interface ScriptWait {
  state: ScriptWaitState;
  durationMs: number;
  /** Manual steps still waiting for pie(verdict), when state is awaiting_verdict. */
  awaiting?: number[];
  elicitation?: ElicitationOutcome;
  error?: string;
}

export interface ScriptWatchOptions {
  /** The script's asset path, or "inline". Read from the bridge's run_started event when it carries one. */
  script?: string;
  /** For a replay run, the run it replays. */
  replayOf?: string;
  clock?: WatchClock;
  /** Consecutive failed status calls before the watcher gives up. Default 5. */
  maxFailures?: number;
  /** The longest one watcher follows a run. Default 6 hours. */
  maxDurationMs?: number;
}

/** The answer of pie_script_status, as far as the watcher reads it. */
export interface ScriptStatus {
  success?: boolean;
  error?: string;
  run_id?: string;
  state?: string;
  current_step?: number;
  steps?: Array<Record<string, unknown>>;
  awaiting?: Array<number | { index?: number; instruction?: string }>;
  events?: BridgeScriptEvent[];
  last_seq?: number;
  results_path?: string;
  summary?: ScriptSummary;
  replay_of?: string;
  script?: string;
  [key: string]: unknown;
}

/** One event as the C++ runner records it. */
export interface BridgeScriptEvent {
  seq: number;
  type: string;
  run_id?: string;
  step?: number | Record<string, unknown>;
  at_ms?: number;
  wall_time?: string;
  data?: Record<string, unknown>;
}

const TERMINAL: ReadonlySet<string> = new Set(["finished", "stopped"]);
const MIN_POLL_MS = 250;
const MAX_POLL_MS = 1000;

const watchers = new Map<string, ScriptWatcher>();
/** The last seq emitted per run, kept past a watcher's end so a later watcher of the same run emits nothing twice. */
const emittedSeq = new Map<string, number>();

export class ScriptWatcher {
  readonly runId: string;
  ctx: ToolContext;
  script: string;
  replayOf?: string;
  lastSeq = 0;
  lastStatus: ScriptStatus | null = null;
  /** Set once the watcher stops: the run ended, the bridge stopped answering, or the cap ran out. */
  ended: { state: "finished" | "stopped" | "unreachable"; error?: string } | null = null;
  /** Manual steps put to a form in this run, whatever the answer. */
  readonly asked = new Set<number>();
  /** Manual steps whose verdict the form posted. */
  readonly answered = new Set<number>();
  elicitation: ElicitationOutcome | undefined;
  private elicitInFlight: Promise<void> | null = null;
  private readonly clock: WatchClock;
  private readonly maxFailures: number;
  private readonly maxDurationMs: number;
  private readonly started: number;
  private waiters: Array<() => void> = [];
  private pollMs = MIN_POLL_MS;
  private wake: (() => void) | null = null;

  constructor(ctx: ToolContext, runId: string, opts: ScriptWatchOptions) {
    this.ctx = ctx;
    this.runId = runId;
    this.script = opts.script ?? "inline";
    this.replayOf = opts.replayOf;
    this.clock = opts.clock ?? realClock;
    this.maxFailures = opts.maxFailures ?? 5;
    this.maxDurationMs = opts.maxDurationMs ?? 6 * 60 * 60 * 1000;
    this.started = this.clock.now();
    this.lastSeq = emittedSeq.get(runId) ?? 0;
  }

  get eliciting(): boolean {
    return this.elicitInFlight !== null;
  }

  /** Resolves after the next poll, or at once once the watcher has stopped. */
  changed(): Promise<void> {
    if (this.ended) return Promise.resolve();
    return new Promise((resolve) => this.waiters.push(resolve));
  }

  /** Manual steps waiting for a verdict that no form has posted. */
  pendingManual(): number[] {
    const status = this.lastStatus;
    if (!status || status.state !== "awaiting_verdict") return [];
    return awaitingOf(status).map((a) => a.index).filter((i) => !this.answered.has(i));
  }

  async run(): Promise<void> {
    let failures = 0;
    let lastError: string | undefined;
    try {
      for (;;) {
        if (this.ended) return;
        if (this.clock.now() - this.started >= this.maxDurationMs) {
          this.finish("unreachable", `Stopped following run ${this.runId} after ${Math.round(this.maxDurationMs / 60000)} minutes.`);
          return;
        }
        let status: ScriptStatus | null = null;
        try {
          const answer = (await this.ctx.bridge.call("pie_script_status", { run_id: this.runId, since_seq: this.lastSeq })) as ScriptStatus;
          if (answer && typeof answer === "object" && answer.success !== false) status = answer;
          else lastError = (answer && typeof answer === "object" && typeof answer.error === "string") ? answer.error : "pie_script_status failed";
        } catch (e) {
          lastError = e instanceof Error ? e.message : String(e);
        }
        if (this.ended) return;

        if (status) {
          failures = 0;
          const fresh = this.absorb(status);
          if (fresh > 0) this.pollMs = MIN_POLL_MS;
          if (status.state === "idle") {
            this.finish("stopped", `The bridge has no run ${this.runId}.`);
            return;
          }
          if (status.state && TERMINAL.has(status.state)) {
            this.finish(status.state as "finished" | "stopped");
            return;
          }
          if (status.state === "awaiting_verdict") this.maybeElicit(status);
        } else if (++failures >= this.maxFailures) {
          this.finish("unreachable", `pie_script_status failed ${failures} times in a row: ${lastError}`);
          return;
        }

        this.notify();
        await Promise.race([this.clock.sleep(this.pollMs), new Promise<void>((r) => (this.wake = r))]);
        this.wake = null;
        this.pollMs = Math.min(this.pollMs * 2, MAX_POLL_MS);
      }
    } finally {
      if (watchers.get(this.runId) === this) watchers.delete(this.runId);
    }
  }

  /** Take a status answer: remember it and emit each event not seen before. Returns how many were new. */
  private absorb(status: ScriptStatus): number {
    this.lastStatus = { ...this.lastStatus, ...status };
    if (typeof status.replay_of === "string") this.replayOf = status.replay_of;
    if (typeof status.script === "string" && status.script) this.script = status.script;
    const events = Array.isArray(status.events) ? [...status.events].sort((a, b) => a.seq - b.seq) : [];
    let fresh = 0;
    for (const e of events) {
      if (typeof e?.seq !== "number" || e.seq <= this.lastSeq) continue;
      this.lastSeq = e.seq;
      emittedSeq.set(this.runId, e.seq);
      const mapped = mapBridgeEvent(e, this, status);
      if (mapped) {
        emitScriptEvent(mapped);
        fresh++;
      }
    }
    if (typeof status.last_seq === "number" && status.last_seq > this.lastSeq) {
      this.lastSeq = status.last_seq;
      emittedSeq.set(this.runId, status.last_seq);
    }
    return fresh;
  }

  private maybeElicit(status: ScriptStatus): void {
    if (this.elicitInFlight) return;
    const unasked = awaitingOf(status).filter((a) => !this.asked.has(a.index) && !this.answered.has(a.index));
    if (unasked.length === 0) return;
    if (!this.ctx.elicit || !clientAdvertisesElicitation(this.ctx.elicit)) {
      for (const a of unasked) this.asked.add(a.index);
      this.elicitation = "unavailable";
      return;
    }
    for (const a of unasked) this.asked.add(a.index);
    this.elicitInFlight = this.elicitVerdicts(unasked).finally(() => {
      this.elicitInFlight = null;
      this.pollMs = MIN_POLL_MS;
      this.wake?.();
    });
  }

  private async elicitVerdicts(steps: Array<{ index: number; instruction: string }>): Promise<void> {
    const elicit = this.ctx.elicit!;
    const startedAt = this.clock.now();
    let result;
    try {
      result = await elicit(verdictForm(this.runId, this.script, steps));
    } catch {
      this.elicitation = "failed";
      return;
    }
    if (result.action !== "accept") {
      // No person reads a form and declines it this fast: the client answered without showing it.
      const minMs = minHumanAnswerMs();
      const elapsed = this.clock.now() - startedAt;
      this.elicitation = minMs > 0 && elapsed < minMs ? "auto_answered" : result.action === "decline" ? "declined" : "cancelled";
      return;
    }
    this.elicitation = "accepted";
    const content = result.content ?? {};
    for (const s of steps) {
      const verdict = content[`step_${s.index}`];
      if (verdict !== "pass" && verdict !== "fail" && verdict !== "skip") continue;
      const note = content[`step_${s.index}_note`];
      const params: Record<string, unknown> = { run_id: this.runId, step: s.index, verdict, source: "elicitation" };
      if (typeof note === "string" && note.trim()) params.note = note.trim();
      try {
        const answer = (await this.ctx.bridge.call("pie_script_verdict", params)) as { success?: boolean } | null;
        if (!answer || typeof answer !== "object" || answer.success !== false) this.answered.add(s.index);
      } catch {
        // Left for pie(verdict).
      }
    }
  }

  /** Stop following the run. The run itself goes on. */
  stop(): void {
    if (!this.ended) this.finish("stopped", "watch stopped");
    this.wake?.();
  }

  private finish(state: "finished" | "stopped" | "unreachable", error?: string): void {
    this.ended = { state, ...(error !== undefined ? { error } : {}) };
    this.notify();
  }

  private notify(): void {
    const waiting = this.waiters;
    this.waiters = [];
    for (const w of waiting) w();
  }
}

function awaitingOf(status: ScriptStatus): Array<{ index: number; instruction: string }> {
  const out: Array<{ index: number; instruction: string }> = [];
  for (const a of status.awaiting ?? []) {
    const index = typeof a === "number" ? a : typeof a?.index === "number" ? a.index : undefined;
    if (index === undefined) continue;
    const given = typeof a === "object" && typeof a.instruction === "string" ? a.instruction : undefined;
    out.push({ index, instruction: given ?? instructionOf(status, index) });
  }
  return out;
}

function instructionOf(status: ScriptStatus | null, index: number): string {
  const step = status?.steps?.find((s) => s.index === index) ?? status?.steps?.[index];
  return typeof step?.instruction === "string" ? step.instruction : `Step ${index + 1}`;
}

/** The one form for every manual step that awaits a verdict. */
export function verdictForm(runId: string, script: string, steps: Array<{ index: number; instruction: string }>): ElicitParams {
  const properties: Record<string, ElicitPrimitiveSchema> = {};
  for (const s of steps) {
    properties[`step_${s.index}`] = {
      type: "string",
      title: `Step ${s.index + 1}`,
      description: s.instruction,
      enum: ["pass", "fail", "skip"],
    };
    properties[`step_${s.index}_note`] = {
      type: "string",
      title: `Step ${s.index + 1} note`,
      description: "Optional. What you saw.",
    };
  }
  return {
    message: [
      `Test script ${script} (run ${runId}) needs your verdict on ${steps.length === 1 ? "one manual step" : `${steps.length} manual steps`}:`,
      "",
      ...steps.map((s) => `Step ${s.index + 1}: ${s.instruction}`),
      "",
      "Pick pass, fail or skip for each. A step left blank waits for pie(verdict).",
    ].join("\n"),
    requestedSchema: { type: "object", properties },
  };
}

function toStep(raw: Record<string, unknown> | undefined, index: number, fallbackStatus: ScriptStepStatus): ScriptStep {
  const r = raw ?? {};
  const step: ScriptStep = {
    index: typeof r.index === "number" ? r.index : index,
    instruction: typeof r.instruction === "string" ? r.instruction : `Step ${index + 1}`,
    judge: r.judge === "manual" ? "manual" : "auto",
    status: (typeof r.status === "string" ? r.status : fallbackStatus) as ScriptStepStatus,
  };
  if (r.expect && typeof r.expect === "object") step.expect = r.expect as ScriptStep["expect"];
  if (typeof r.agent_judgeable === "boolean") step.agentJudgeable = r.agent_judgeable;
  if (r.evidence && typeof r.evidence === "object") step.evidence = r.evidence as Record<string, unknown>;
  if (typeof r.note === "string") step.note = r.note;
  const source = r.verdict_source ?? r.verdictSource;
  if (source === "auto" || source === "action" || source === "elicitation" || source === "agent" || source === "source_run") step.verdictSource = source;
  if (typeof r.within_ms === "number") step.withinMs = r.within_ms;
  if (typeof r.started_at_ms === "number") step.startedAtMs = r.started_at_ms;
  if (typeof r.ended_at_ms === "number") step.endedAtMs = r.ended_at_ms;
  if (typeof r.source_status === "string") step.sourceStatus = r.source_status as ScriptStepStatus;
  if (typeof r.regression === "boolean") step.regression = r.regression;
  if (Array.isArray(r.arrange)) step.arrange = r.arrange as Array<Record<string, unknown>>;
  if (Array.isArray(r.act)) step.act = r.act as Array<Record<string, unknown>>;
  const actResult = r.act_result ?? r.actResult;
  if (actResult && typeof actResult === "object") step.actResult = actResult as Record<string, unknown>;
  return step;
}

/** A summary count under its bridge (snake_case) or stream (camelCase) name. */
function raw(s: object | undefined, snake: string, camel: string): unknown {
  const o = s as Record<string, unknown> | undefined;
  return o?.[snake] ?? o?.[camel];
}

const STATUS_AFTER: Record<string, ScriptStepStatus> = {
  step_started: "running",
  step_passed: "passed",
  step_failed: "failed",
  awaiting_verdict: "awaiting_verdict",
};

/** One bridge event as a ScriptEvent, or null for a type this version does not know. */
export function mapBridgeEvent(e: BridgeScriptEvent, w: { runId: string; script: string; replayOf?: string }, status: ScriptStatus | null): ScriptEvent | null {
  const data = e.data ?? {};
  const script = typeof data.script === "string" && data.script ? data.script : w.script;
  const base = {
    runId: typeof e.run_id === "string" && e.run_id ? e.run_id : w.runId,
    script,
    at: typeof e.wall_time === "string" && e.wall_time ? e.wall_time : new Date().toISOString(),
    atMs: typeof e.at_ms === "number" ? e.at_ms : 0,
    seq: e.seq,
    ...(typeof data.replay_of === "string" ? { replayOf: data.replay_of } : w.replayOf ? { replayOf: w.replayOf } : {}),
    ...(data.mode === "manual" || data.mode === "full" ? { mode: data.mode as "full" | "manual" } : {}),
  };
  const resultsPath = typeof data.results_path === "string" ? data.results_path : typeof status?.results_path === "string" ? status.results_path : undefined;
  const stepIndex = typeof e.step === "number" ? e.step : typeof e.step === "object" && typeof e.step?.index === "number" ? e.step.index : typeof data.index === "number" ? data.index : -1;
  const fromStatus = status?.steps?.find((s) => s.index === stepIndex) ?? (stepIndex >= 0 ? status?.steps?.[stepIndex] : undefined);
  const stepRaw = (): Record<string, unknown> => {
    // The event's own fields win over the status's: the status is read after the event and may have moved on.
    const { status: _later, ...steady } = fromStatus ?? {};
    void _later;
    return { ...steady, ...(typeof e.step === "object" ? e.step : {}), ...(data.step && typeof data.step === "object" ? (data.step as Record<string, unknown>) : {}), ...data };
  };

  switch (e.type) {
    case "run_started":
      if (typeof data.script === "string" && data.script) w.script = data.script;
      return { ...base, type: "run_started", ...(resultsPath ? { resultsPath } : {}) };
    case "step_started":
    case "step_passed":
    case "step_failed": {
      const raw = stepRaw();
      if (!("status" in data)) raw.status = STATUS_AFTER[e.type];
      return { ...base, type: e.type, step: toStep(raw, stepIndex, STATUS_AFTER[e.type]) };
    }
    case "step_acted":
      // The step keeps the status it had; the event's news is act_result.
      return { ...base, type: "step_acted", step: toStep(stepRaw(), stepIndex, "running") };
    case "verdict": {
      const raw = stepRaw();
      if (!("status" in data) && typeof data.verdict === "string") {
        raw.status = data.verdict === "pass" ? "passed" : data.verdict === "fail" ? "failed" : "skipped";
      }
      if (!("verdict_source" in raw) && typeof data.source === "string") raw.verdict_source = data.source;
      return { ...base, type: "verdict", step: toStep(raw, stepIndex, "pending") };
    }
    case "awaiting_verdict": {
      let awaiting: Array<{ index: number; instruction: string }>;
      if (Array.isArray(data.awaiting)) {
        awaiting = awaitingOf({ ...status, awaiting: data.awaiting as ScriptStatus["awaiting"] });
      } else if (stepIndex >= 0) {
        const raw = stepRaw();
        awaiting = [{ index: stepIndex, instruction: typeof raw.instruction === "string" ? raw.instruction : instructionOf(status, stepIndex) }];
      } else {
        awaiting = status ? awaitingOf(status) : [];
      }
      return { ...base, type: "verdict_requested", awaiting };
    }
    case "run_finished": {
      const s = (data.summary ?? status?.summary) as Partial<ScriptSummary> | undefined;
      const summary: ScriptSummary = {
        total: Number(s?.total ?? 0),
        passed: Number(s?.passed ?? 0),
        failed: Number(s?.failed ?? 0),
        skipped: Number(s?.skipped ?? 0),
        pending: Number(s?.pending ?? 0),
        ...(s?.awaiting !== undefined ? { awaiting: Number(s.awaiting) } : {}),
        ...(s?.regressions !== undefined ? { regressions: Number(s.regressions) } : {}),
        ...(raw(s, "human_verdicts", "humanVerdicts") !== undefined ? { humanVerdicts: Number(raw(s, "human_verdicts", "humanVerdicts")) } : {}),
        ...(raw(s, "agent_verdicts", "agentVerdicts") !== undefined ? { agentVerdicts: Number(raw(s, "agent_verdicts", "agentVerdicts")) } : {}),
        ...(raw(s, "unchecked", "unchecked") !== undefined ? { unchecked: Number(raw(s, "unchecked", "unchecked")) } : {}),
      };
      const state = typeof data.state === "string" ? data.state : status?.state && TERMINAL.has(status.state) ? status.state : undefined;
      return { ...base, type: "run_finished", summary, ...(state ? { state } : {}), ...(resultsPath ? { resultsPath } : {}) };
    }
    default:
      return null;
  }
}

/**
 * Follow a run. Idempotent: a run already followed keeps its watcher, which takes this call's context so a form is put
 * to whoever asked last.
 */
export function startScriptWatch(ctx: ToolContext, runId: string, opts: ScriptWatchOptions = {}): ScriptWatcher {
  const existing = watchers.get(runId);
  if (existing && !existing.ended) {
    existing.ctx = ctx;
    if (opts.script && existing.script === "inline") existing.script = opts.script;
    if (opts.replayOf && !existing.replayOf) existing.replayOf = opts.replayOf;
    return existing;
  }
  const w = new ScriptWatcher(ctx, runId, opts);
  watchers.set(runId, w);
  void w.run();
  return w;
}

/** The watcher following a run in this process, if any. */
export function getScriptWatch(runId: string): ScriptWatcher | undefined {
  return watchers.get(runId);
}

export interface WaitScriptOptions {
  timeoutSeconds?: number;
  clock?: WatchClock;
  watch?: ScriptWatchOptions;
}

/**
 * Wait for a run to finish or stop. Returns early with awaiting_verdict when manual steps wait for pie(verdict): the
 * client has no form to show them on, or the person declined it. While a form is up, it keeps waiting.
 */
export async function waitForScriptRun(
  ctx: ToolContext,
  runId: string,
  options: WaitScriptOptions = {},
): Promise<{ status: ScriptStatus | null; wait: ScriptWait }> {
  const clock = options.clock ?? options.watch?.clock ?? realClock;
  const w = startScriptWatch(ctx, runId, { ...options.watch, clock });
  const timeoutMs = Math.max(0, (options.timeoutSeconds ?? 600) * 1000);
  const started = clock.now();
  const done = (state: ScriptWaitState, extra: Partial<ScriptWait> = {}): { status: ScriptStatus | null; wait: ScriptWait } => ({
    status: w.lastStatus,
    wait: {
      state,
      durationMs: clock.now() - started,
      ...(w.elicitation ? { elicitation: w.elicitation } : {}),
      ...extra,
    },
  });

  for (;;) {
    if (w.ended) return done(w.ended.state, w.ended.error ? { error: w.ended.error } : {});
    const pending = w.pendingManual();
    if (pending.length > 0 && !w.eliciting && pending.every((i) => w.asked.has(i))) {
      return done("awaiting_verdict", { awaiting: pending });
    }
    if (clock.now() - started >= timeoutMs) {
      return done("timed_out", { error: `The run did not finish within ${timeoutMs / 1000}s; it is still going.` });
    }
    await w.changed();
  }
}

const VERDICT_HINT = "call pie(verdict, step, verdict, note?); an agent passes source=\"agent\" with evidence {note, captures?}";

function merge(answered: Record<string, unknown>, final: ScriptStatus | null, wait: ScriptWait, keepEvents: boolean): Record<string, unknown> {
  const status: Record<string, unknown> = { ...(final ?? {}) };
  if (!keepEvents) delete status.events;
  const out: Record<string, unknown> = { ...answered, ...status, wait };
  if (wait.state === "awaiting_verdict") out.verdictHint = VERDICT_HINT;
  if (wait.state === "timed_out" || wait.state === "unreachable") {
    const warnings = Array.isArray(answered.warnings) ? [...answered.warnings] : [];
    warnings.push(wait.error ?? "The wait ended before the run did.");
    out.warnings = warnings;
  }
  return out;
}

async function freshStatus(ctx: ToolContext, runId: string, sinceSeq: unknown): Promise<ScriptStatus | null> {
  try {
    const params: Record<string, unknown> = { run_id: runId };
    if (typeof sinceSeq === "number") params.since_seq = sinceSeq;
    const answer = (await ctx.bridge.call("pie_script_status", params)) as ScriptStatus;
    return answer && typeof answer === "object" && answer.success !== false ? answer : null;
  } catch {
    return null;
  }
}

function timeoutOf(params: Record<string, unknown>): number | undefined {
  return typeof params.timeout_seconds === "number" ? params.timeout_seconds : undefined;
}

/**
 * The follow-up of pie(script_run) and pie(script_replay): follow the new run so its events reach the bus, and with
 * wait=true hold the answer until it finishes, stops or needs a verdict nobody was asked for.
 */
export async function scriptRunFollowUp(
  ctx: ToolContext,
  params: Record<string, unknown>,
  answered: unknown,
  clock?: WatchClock,
): Promise<unknown> {
  if (!answered || typeof answered !== "object") return answered;
  const body = answered as Record<string, unknown>;
  if (body.success === false || typeof body.run_id !== "string" || !body.run_id) return answered;
  const runId = body.run_id;
  const watch: ScriptWatchOptions = {
    script: typeof params.script === "string" && params.script ? params.script : typeof body.script === "string" ? body.script : "inline",
    replayOf: typeof body.replay_of === "string" ? body.replay_of : undefined,
    ...(clock ? { clock } : {}),
  };
  startScriptWatch(ctx, runId, watch);
  if (params.wait !== true) return answered;
  const { status, wait } = await waitForScriptRun(ctx, runId, { timeoutSeconds: timeoutOf(params), clock, watch });
  const final = (await freshStatus(ctx, runId, undefined)) ?? status;
  return merge(body, final, wait, false);
}

/**
 * The follow-up of pie(script_status): (re)start following a run that has not ended, so events flow after a server
 * restart, and with wait=true wait as script_run does.
 */
export async function scriptStatusFollowUp(
  ctx: ToolContext,
  params: Record<string, unknown>,
  answered: unknown,
  clock?: WatchClock,
): Promise<unknown> {
  if (!answered || typeof answered !== "object") return answered;
  const body = answered as Record<string, unknown>;
  if (body.success === false || typeof body.run_id !== "string" || !body.run_id) return answered;
  const runId = body.run_id;
  const state = typeof body.state === "string" ? body.state : "";
  if (TERMINAL.has(state) || state === "idle") return answered;
  const watch: ScriptWatchOptions = {
    script: typeof body.script === "string" ? body.script : undefined,
    replayOf: typeof body.replay_of === "string" ? body.replay_of : undefined,
    ...(clock ? { clock } : {}),
  };
  startScriptWatch(ctx, runId, watch);
  if (params.wait !== true) return answered;
  const { status, wait } = await waitForScriptRun(ctx, runId, { timeoutSeconds: timeoutOf(params), clock, watch });
  const final = (await freshStatus(ctx, runId, params.since_seq)) ?? status;
  return merge(body, final, wait, true);
}

/** Stop following every run. For tests and shutdown; a stopped watcher is dropped from the registry. */
export function stopAllScriptWatches(): void {
  for (const w of [...watchers.values()]) w.stop();
  watchers.clear();
  emittedSeq.clear();
}
