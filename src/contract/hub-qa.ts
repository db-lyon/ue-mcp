// @session independent: The hub contract's shared run model: types and pure functions over their arguments. No state, no imports.
/**
 * A PIE test script run as a client's checklist card shows it. Built from
 * ue-mcp's pie(script_run) and pie(script_status) answers (snake_case) and
 * kept current from the script.* events on the daemon's stream (camelCase),
 * as plans/qa-checklist-contract.md in ue-mcp defines them. Every function
 * here returns a new run and never changes the one it is given.
 */

export type QaStepStatus = "pending" | "running" | "passed" | "failed" | "awaiting_verdict" | "skipped";
/** draft: a script the agent created that nobody has run yet; a client's own state, never one pie(script_status) reports. */
export type QaRunState = "draft" | "armed" | "running" | "awaiting_verdict" | "finished" | "stopped" | "idle";
export type QaVerdict = "pass" | "fail" | "skip";
/** Who decided a step: the observer, a person (action, elicitation), an agent, or the source run of a replay. */
export type QaVerdictSource = "auto" | "action" | "elicitation" | "agent" | "source_run";

export interface QaExpect {
  value: string;
  op: string;
  target?: number | boolean;
  within_ms?: number;
}

/** What the observer saw for an auto step, as the editor wrote it. */
export interface QaEvidence {
  value?: string;
  at_start?: number | null;
  at_end?: number | null;
  samples?: number;
  observed?: boolean;
  verdict_frame?: number;
  verdict_at_ms?: number;
  reason?: string;
  /** A verdict's evidence: what its author saw, and the frames it looked at. */
  note?: string;
  captures?: string[];
}

export interface QaStep {
  index: number;
  instruction: string;
  judge: "auto" | "manual";
  expect?: QaExpect;
  status: QaStepStatus;
  withinMs?: number;
  startedAtMs?: number;
  endedAtMs?: number;
  evidence?: QaEvidence;
  note?: string;
  verdictSource?: QaVerdictSource;
  /** Manual steps: false keeps the verdict with a person; an agent never gives it. */
  agentJudgeable?: boolean;
  /** Which player the step is for in a multiplayer playtest: "host" or "client <n>". The client keeps it; a script's steps have no player. */
  player?: string;
  /** Replay runs: the step's status in the source run, and whether it passed there and failed here. */
  sourceStatus?: QaStepStatus;
  regression?: boolean;
  /**
   * The step's arrange and act, as a script step carries them (db-lyon/ue-mcp#1507): the
   * engine sets the step up and drives it, so nobody injects input for it. Kept as given.
   */
  arrange?: unknown;
  act?: unknown;
  /** What the step's arrange and act did in the run, as ue-mcp reports it. */
  actResult?: QaActResult;
}

/** A step's act_result: whether its arrange and act ran, and each action's outcome. */
export interface QaActResult {
  status: "running" | "done" | "failed" | "skipped";
  reason?: string;
  actions?: Array<{ index: number; action: string; ok: boolean; error?: string; at_ms?: number }>;
  duration_ms?: number;
}

export interface QaSummary {
  total: number;
  passed: number;
  failed: number;
  skipped: number;
  /** Every step not settled, awaiting ones included. */
  pending: number;
  awaiting: number;
  regressions?: number;
}

export interface QaRun {
  runId: string;
  /** The script's asset path, or "inline". */
  script: string;
  scriptName: string;
  /** The level the script starts PIE in, as an asset path; absent when it plays in whatever level is open. */
  map?: string;
  state: QaRunState;
  steps: QaStep[];
  /** Manual steps waiting for a verdict, by index. */
  awaiting: number[];
  /** The highest event seq applied: the next status read asks for the events after it. */
  lastSeq: number;
  summary: QaSummary;
  resultsPath: string | null;
  replayOf?: string;
  /** The session whose call started it; null for a run started elsewhere. */
  sessionId: string | null;
  /** The tool call that started it. */
  toolUseId: string | null;
  /** The subagent that started it, by its task name. */
  subagent?: string;
  /** When the hub first saw it, ISO 8601. */
  seenAt: string;
  /** The run's input recording, which a later run of the script can replay to drive the pawn. */
  recordingId?: string;
  /** The subagent the user assigned the playtest to from its card. */
  assignment?: QaAssignment;
  /** A draft whose script was deleted. */
  deleted?: boolean;
  /**
   * A manual run: pie(script_run) with mode manual, started by a verdict on a
   * draft's card. No PIE, every manual step awaiting in any order, and the
   * auto steps not checked until Run starts a full run.
   */
  manual?: boolean;
  /** Why an ended run ended, as ue-mcp recorded it (a stop's reason, such as QA_NO_VERDICT). */
  stopReason?: string;
  /**
   * The run started Play In Editor itself (ue-mcp's pie_started). Only then is
   * that session the run's: the hub pauses and stops PIE for a run only when
   * this is true, never a session the run merely joined.
   */
  pieStarted?: boolean;
  /** The players the client set PIE to before starting the run; absent when it left PIE as it was. */
  pieSetup?: { players: number; netMode: "standalone" | "listen" };
}

/** How long a run holds at a step waiting for a person's verdict before it is stopped, unless the "Wait for a verdict" setting says otherwise. */
export const QA_VERDICT_WAIT_MINUTES = 10;
/** The waits the setting, and a plan's own, may choose, in minutes. */
export const QA_VERDICT_WAIT_CHOICES: readonly number[] = [2, 5, 10, 30, 60];
export const qaVerdictWaitValid = (v: unknown): v is number => typeof v === "number" && QA_VERDICT_WAIT_CHOICES.includes(v);
/** The stop reason of a run nobody judged in time. */
export const QA_NO_VERDICT = "no verdict";

/**
 * A run holding at a manual step for a person's verdict, with Play In Editor
 * paused so nothing happens that nobody sees (GET /qa/held, studio.qa.held).
 */
export interface QaHeld {
  runId: string;
  scriptName: string;
  sessionId: string | null;
  step: number;
  instruction: string;
  /** When the step began to wait; the run stops at dueAt without a verdict. */
  since: string;
  dueAt: string;
  /** PIE is paused. False once the person chose Resume and watch. */
  paused: boolean;
}

/** A playtest handed to a subagent: its agent call, which is its thread, and how it is going. */
export interface QaAssignment {
  /** The agent call running now: the loop's current attempt. */
  callId: string;
  sessionId: string;
  state: "running" | "done" | "failed" | "stopped";
  at: string;
  /** Assign to subagent loops until the playtest passes or a hard blocker stops it. */
  loop?: QaLoop;
}

/** Why a playtest loop stopped for the user. */
export type QaBlockerKind =
  | "verdict"
  | "no_progress"
  | "contradiction"
  | "refused"
  | "outside_project"
  | "needs_person"
  | "crashes"
  | "builds"
  | "attempts";

export interface QaBlocker {
  kind: QaBlockerKind;
  reason: string;
  /** What shows it: the steps' evidence, the refused call, the agent's words. */
  evidence: string[];
  /** The step it is about, when there is one. */
  step?: number;
}

/** A change one attempt made, as the card lists it. */
export interface QaLoopChange {
  path: string;
  status: "added" | "modified" | "deleted";
}

/** One pass of the loop: a subagent that fixes what failed (from the second on) and runs the playtest. */
export interface QaLoopAttempt {
  n: number;
  callId: string;
  /** The run this attempt started. */
  runId?: string;
  /** The checkpoints around it: per-attempt revert restores its changes to `before`. */
  before: string;
  after?: string;
  changes: QaLoopChange[];
  /** The steps not passing at its end, as "Step n: why". */
  failing: string[];
  startedAt: string;
  endedAt?: string;
  reverted?: boolean;
}

export interface QaLoop {
  id: string;
  /** running; passed: every step passed; needs_you: a hard blocker; stopped: Cancel; failed: the loop itself broke. */
  state: "running" | "passed" | "needs_you" | "stopped" | "failed";
  attempts: QaLoopAttempt[];
  /** The checkpoint before the first attempt: Revert all restores to it. */
  baseline: string;
  blocker?: QaBlocker;
  /** Why the loop broke, when it failed. */
  error?: string;
  startedAt: string;
  endedAt?: string;
  /** Revert all ran. */
  reverted?: boolean;
}

/** Whether a step's run carries an act, so the engine drives it. */
export const qaActs = (s: QaStep): boolean => s.act !== undefined && s.act !== null && !(Array.isArray(s.act) && s.act.length === 0);

/** The answer actions a manual step takes. */
export const QA_VERDICTS: readonly QaVerdict[] = ["pass", "fail", "skip"];

export const qaEnded = (state: QaRunState): boolean => state === "finished" || state === "stopped";
export const qaSettled = (status: QaStepStatus): boolean => status === "passed" || status === "failed" || status === "skipped";

type Obj = Record<string, unknown>;
const obj = (v: unknown): Obj | null => (v && typeof v === "object" && !Array.isArray(v) ? (v as Obj) : null);
const str = (v: unknown): string | undefined => (typeof v === "string" ? v : undefined);
const num = (v: unknown): number | undefined => (typeof v === "number" && Number.isFinite(v) ? v : undefined);
const int = (v: unknown): number | undefined => (Number.isInteger(v) ? (v as number) : undefined);

const STEP_STATUSES: readonly QaStepStatus[] = ["pending", "running", "passed", "failed", "awaiting_verdict", "skipped"];
const RUN_STATES: readonly QaRunState[] = ["draft", "armed", "running", "awaiting_verdict", "finished", "stopped", "idle"];
const SOURCES: readonly QaVerdictSource[] = ["auto", "action", "elicitation", "agent", "source_run"];
const stepStatus = (v: unknown): QaStepStatus | undefined => (STEP_STATUSES.includes(v as QaStepStatus) ? (v as QaStepStatus) : undefined);
const runState = (v: unknown): QaRunState | undefined => (RUN_STATES.includes(v as QaRunState) ? (v as QaRunState) : undefined);
const source = (v: unknown): QaVerdictSource | undefined => (SOURCES.includes(v as QaVerdictSource) ? (v as QaVerdictSource) : undefined);

/** Drop the keys left undefined, so a patch never clears a field it does not carry. */
function defined<T extends Obj>(o: T): Partial<T> {
  return Object.fromEntries(Object.entries(o).filter(([, v]) => v !== undefined)) as Partial<T>;
}

/** A step from an answer or results.json (snake_case). */
function stepPatchSnake(s: Obj): Partial<QaStep> {
  return defined({
    index: int(s.index),
    instruction: str(s.instruction),
    judge: s.judge === "manual" ? "manual" as const : s.judge === "auto" ? "auto" as const : undefined,
    expect: (obj(s.expect) ?? undefined) as QaExpect | undefined,
    status: stepStatus(s.status),
    withinMs: num(s.within_ms),
    startedAtMs: num(s.started_at_ms),
    endedAtMs: num(s.ended_at_ms),
    evidence: (obj(s.evidence) ?? undefined) as QaEvidence | undefined,
    note: str(s.note),
    verdictSource: source(s.verdict_source),
    agentJudgeable: typeof s.agent_judgeable === "boolean" ? s.agent_judgeable : undefined,
    sourceStatus: stepStatus(s.source_status),
    regression: typeof s.regression === "boolean" ? s.regression : undefined,
    arrange: s.arrange ?? undefined,
    act: s.act ?? undefined,
    actResult: (obj(s.act_result) ?? undefined) as QaActResult | undefined,
  });
}

/** A step object from a script.* stream event (camelCase, expect and evidence as the editor wrote them). */
function stepPatchCamel(s: Obj): Partial<QaStep> {
  return defined({
    index: int(s.index),
    instruction: str(s.instruction),
    judge: s.judge === "manual" ? "manual" as const : s.judge === "auto" ? "auto" as const : undefined,
    expect: (obj(s.expect) ?? undefined) as QaExpect | undefined,
    status: stepStatus(s.status),
    withinMs: num(s.withinMs),
    startedAtMs: num(s.startedAtMs),
    endedAtMs: num(s.endedAtMs),
    evidence: (obj(s.evidence) ?? undefined) as QaEvidence | undefined,
    note: str(s.note),
    verdictSource: source(s.verdictSource),
    agentJudgeable: typeof s.agentJudgeable === "boolean" ? s.agentJudgeable : typeof s.agent_judgeable === "boolean" ? s.agent_judgeable : undefined,
    sourceStatus: stepStatus(s.sourceStatus),
    regression: typeof s.regression === "boolean" ? s.regression : undefined,
  });
}

export function qaSummaryOf(steps: QaStep[]): QaSummary {
  const n = (st: QaStepStatus) => steps.filter((s) => s.status === st).length;
  const regressions = steps.filter((s) => s.regression).length;
  return {
    total: steps.length,
    passed: n("passed"),
    failed: n("failed"),
    skipped: n("skipped"),
    pending: steps.filter((s) => !qaSettled(s.status)).length,
    awaiting: n("awaiting_verdict"),
    ...(regressions ? { regressions } : {}),
  };
}

/** The indices an awaiting list names: numbers, or { index } objects. */
function indices(v: unknown): number[] | undefined {
  if (!Array.isArray(v)) return undefined;
  return v.map((x) => (typeof x === "number" ? x : int(obj(x)?.index))).filter((x): x is number => Number.isInteger(x));
}

/**
 * Set one step's fields, adding the step when it is new. A settled step never
 * goes back to pending or running: an event older than what the hub already
 * read cannot undo it. Only a verdict replaces a settled status.
 */
function patchStep(steps: QaStep[], patch: Partial<QaStep>, verdict = false): QaStep[] {
  if (patch.index === undefined) return steps;
  const i = steps.findIndex((s) => s.index === patch.index);
  if (i < 0) {
    const added: QaStep = { instruction: "", judge: "auto", status: "pending", ...patch, index: patch.index };
    return [...steps, added].sort((a, b) => a.index - b.index);
  }
  const was = steps[i];
  const p = !verdict && qaSettled(was.status) && patch.status && !qaSettled(patch.status) ? { ...patch, status: was.status } : patch;
  const next = steps.slice();
  next[i] = { ...was, ...p };
  // A person's verdict replacing an agent's clears the agent's evidence, as ue-mcp does.
  if (verdict && was.verdictSource === "agent" && p.verdictSource && p.verdictSource !== "agent") {
    if (!p.evidence) delete next[i].evidence;
    if (p.note === undefined) delete next[i].note;
  }
  return next;
}

/** Recount, and keep the awaiting list to steps that still wait. */
function settle(run: QaRun): QaRun {
  const waiting = new Set(run.steps.filter((s) => s.status === "awaiting_verdict").map((s) => s.index));
  const awaiting = run.awaiting.filter((i) => waiting.has(i));
  for (const i of waiting) if (!awaiting.includes(i)) awaiting.push(i);
  awaiting.sort((a, b) => a - b);
  let state = run.state;
  if (state === "awaiting_verdict" && awaiting.length === 0) state = "running";
  return { ...run, awaiting, state, summary: qaSummaryOf(run.steps) };
}

/** A run from a pie(script_run), script_replay, script_status or script_results answer. Null when it names no run. */
export function qaRunFromAnswer(answer: unknown, where: { sessionId?: string | null; toolUseId?: string | null; subagent?: string; seenAt?: string } = {}): QaRun | null {
  const a = obj(answer);
  const runId = str(a?.run_id);
  if (!a || !runId) return null;
  const steps = (Array.isArray(a.steps) ? a.steps : [])
    .map((s) => obj(s))
    .filter((s): s is Obj => s !== null)
    .map((s) => ({ instruction: "", judge: "auto" as const, status: "pending" as const, ...stepPatchSnake(s) }))
    .filter((s): s is QaStep => s.index !== undefined)
    .sort((x, y) => x.index - y.index);
  const events = Array.isArray(a.events) ? a.events.map((e) => int(obj(e)?.seq) ?? 0) : [];
  const replayOf = str(a.replay_of);
  const recordingId = str(a.recording_id);
  const stopReason = str(a.stop_reason);
  const run: QaRun = {
    runId,
    script: str(a.script) ?? "inline",
    scriptName: str(a.script_name) ?? str(a.script) ?? runId,
    state: runState(a.state) ?? "armed",
    steps,
    awaiting: indices(a.awaiting) ?? [],
    lastSeq: Math.max(int(a.last_seq) ?? 0, ...events, 0),
    summary: qaSummaryOf(steps),
    resultsPath: str(a.results_path) ?? null,
    ...(replayOf ? { replayOf } : {}),
    sessionId: where.sessionId ?? null,
    toolUseId: where.toolUseId ?? null,
    ...(where.subagent ? { subagent: where.subagent } : {}),
    seenAt: where.seenAt ?? new Date().toISOString(),
    ...(recordingId ? { recordingId } : {}),
    ...(a.mode === "manual" ? { manual: true } : {}),
    ...(stopReason ? { stopReason } : {}),
    ...(a.pie_started === true ? { pieStarted: true } : {}),
  };
  return settle(run);
}

/**
 * A later status answer for the same run: its events after the run's lastSeq
 * applied in order, then its own snapshot of the steps, which is current as of
 * its last_seq. An answer for no run, or another one, changes nothing.
 */
export function qaMergeAnswer(run: QaRun, answer: unknown): QaRun {
  const a = obj(answer);
  if (!a || str(a.run_id) !== run.runId) return run;
  let next = run;
  for (const e of Array.isArray(a.events) ? a.events : []) next = qaApplyEditorEvent(next, e);
  // Only the start's answer says it started PIE; a later read never takes that back.
  if (a.pie_started === true && !next.pieStarted) next = { ...next, pieStarted: true };
  const fresh = qaRunFromAnswer(a);
  if (!fresh || fresh.steps.length === 0) return next;
  return settle({
    ...next,
    state: fresh.state,
    steps: fresh.steps,
    awaiting: fresh.awaiting,
    lastSeq: Math.max(next.lastSeq, fresh.lastSeq),
    resultsPath: fresh.resultsPath ?? next.resultsPath,
    ...(fresh.replayOf ? { replayOf: fresh.replayOf } : {}),
    ...(fresh.recordingId ? { recordingId: fresh.recordingId } : {}),
    ...(fresh.stopReason ? { stopReason: fresh.stopReason } : {}),
  });
}

/** What one event says, whichever shape it came in. */
interface Change {
  type: string;
  seq: number;
  step?: Partial<QaStep>;
  awaiting?: number[];
  awaitingSteps?: Array<{ index: number; instruction?: string }>;
  blocking?: boolean;
  state?: QaRunState;
  resultsPath?: string;
  /** run_finished: why the run ended. */
  reason?: string;
}

function apply(run: QaRun, c: Change): QaRun {
  // Each event once: a new daemon replays a run from its first event.
  if (c.seq > 0 && c.seq <= run.lastSeq) return run;
  let next: QaRun = { ...run, lastSeq: Math.max(run.lastSeq, c.seq) };
  if (c.resultsPath) next.resultsPath = c.resultsPath;
  switch (c.type) {
    case "run_started":
      if (next.state === "idle") next.state = "armed";
      break;
    case "step_started":
    case "step_passed":
    case "step_failed":
    case "verdict":
      if (c.step) next.steps = patchStep(next.steps, c.step, c.type === "verdict");
      if (c.type === "step_started" && (next.state === "armed" || next.state === "idle")) next.state = "running";
      break;
    case "verdict_requested":
    case "awaiting_verdict": {
      const asked: Array<{ index: number; instruction?: string }> = c.awaitingSteps ?? (c.awaiting ?? []).map((index) => ({ index }));
      for (const s of asked) {
        const had = next.steps.find((x) => x.index === s.index);
        if (had && qaSettled(had.status)) continue;
        next.steps = patchStep(next.steps, { index: s.index, judge: "manual", status: "awaiting_verdict", ...(s.instruction ? { instruction: s.instruction } : {}) });
      }
      next.awaiting = [...new Set([...next.awaiting, ...asked.map((s) => s.index)])];
      if (c.blocking !== false && !qaEnded(next.state)) next.state = "awaiting_verdict";
      break;
    }
    case "run_finished":
      next.state = c.state ?? "finished";
      if (c.reason) next.stopReason = c.reason;
      break;
    default:
      // A script.* type this version does not know: only its seq counts.
      return next;
  }
  return settle(next);
}

/** An event from an answer's `events` list (snake_case data). */
export function qaApplyEditorEvent(run: QaRun, event: unknown): QaRun {
  const e = obj(event);
  if (!e || (str(e.run_id) && e.run_id !== run.runId)) return run;
  const type = str(e.type) ?? "";
  const d = obj(e.data) ?? {};
  const index = int(e.step);
  const step = index !== undefined && index >= 0 ? { ...stepPatchSnake(d), index } : undefined;
  if (step && type !== "step_started" && !step.evidence && typeof d.reason === "string") step.evidence = { reason: d.reason };
  return apply(run, {
    type,
    seq: int(e.seq) ?? 0,
    step,
    awaiting: indices(d.awaiting),
    awaitingSteps: Array.isArray(d.steps) ? awaitedSteps(d.steps) : undefined,
    blocking: typeof d.blocking === "boolean" ? d.blocking : undefined,
    state: runState(d.state),
    resultsPath: str(d.results_path),
    reason: str(d.reason),
  });
}

/** The { index, instruction } list an awaiting event carries. */
function awaitedSteps(v: unknown[]): Array<{ index: number; instruction?: string }> {
  return v.map((x) => obj(x)).filter((x): x is Obj => x !== null && int(x.index) !== undefined).map((x) => ({ index: x.index as number, instruction: str(x.instruction) }));
}

/** An event from the daemon's stream: `type` is script.<name>, `data` its payload. Another run's event changes nothing. */
export function qaApplyStreamEvent(run: QaRun, type: string, data: unknown): QaRun {
  const d = obj(data);
  if (!d || !type.startsWith("script.") || d.runId !== run.runId) return run;
  const name = type.slice("script.".length);
  const s = obj(d.step);
  return apply(run, {
    type: name,
    seq: int(d.seq) ?? 0,
    step: s ? stepPatchCamel(s) : undefined,
    awaitingSteps: Array.isArray(d.awaiting) ? awaitedSteps(d.awaiting) : undefined,
    state: runState(d.state),
    resultsPath: str(d.resultsPath),
    reason: str(d.reason),
  });
}

/**
 * What a pie(verdict) answer says about the run. A verdict on an ended run is
 * not on the stream once the server stops following it, so the card takes it
 * from here.
 */
export function qaApplyVerdictAnswer(run: QaRun, answer: unknown): QaRun {
  const a = obj(answer);
  const index = int(a?.step);
  if (!a || str(a.run_id) !== run.runId || index === undefined) return run;
  const evidence = obj(a.evidence) as QaEvidence | null;
  const steps = patchStep(run.steps, defined({ index, status: stepStatus(a.status), note: str(a.note), verdictSource: source(a.verdict_source), evidence: evidence ?? undefined }), true);
  return settle({ ...run, steps, state: runState(a.state) ?? run.state, awaiting: indices(a.awaiting) ?? run.awaiting });
}

/**
 * The verdict form ue-mcp raises for manual steps: every field is step_<i>
 * (pass, fail or skip) or step_<i>_note. The run comes from the message,
 * which names it as "(run <id>)". Null for any other form.
 */
export function qaVerdictForm(form: { message: string; fields: Record<string, unknown> }): { runId: string | null; steps: number[] } | null {
  const steps: number[] = [];
  const keys = Object.keys(form.fields);
  if (keys.length === 0) return null;
  for (const k of keys) {
    const m = /^step_(\d+)(_note)?$/.exec(k);
    if (!m) return null;
    if (m[2]) continue;
    const f = obj(form.fields[k]);
    const values = Array.isArray(f?.enum) ? (f!.enum as unknown[]) : [];
    if (values.length !== QA_VERDICTS.length || !QA_VERDICTS.every((v) => values.includes(v))) return null;
    steps.push(Number(m[1]));
  }
  if (steps.length === 0) return null;
  const runId = /\(run ([A-Za-z0-9_-]+)\)/.exec(form.message)?.[1] ?? null;
  return { runId, steps: steps.sort((a, b) => a - b) };
}

/** A verdict an agent gave, which a person may override. */
export const qaAgentVerdict = (s: QaStep): boolean => s.verdictSource === "agent" && qaSettled(s.status);

/** Whether an agent may judge a manual step: unless the script keeps it with a person. */
export const qaAgentMayJudge = (s: QaStep): boolean => s.judge === "manual" && s.agentJudgeable !== false;

/**
 * The step a live run holds at for a person: awaiting a verdict that no
 * running subagent will give (the script keeps it with a person, or no agent
 * is assigned). Null when the run waits for nobody. A manual run (no PIE,
 * every manual step at once) is a person's own and holds nothing.
 */
export function qaWaitingForPerson(run: QaRun): QaStep | null {
  if (qaEnded(run.state) || run.manual || qaIsDraft(run)) return null;
  const agent = run.assignment?.state === "running";
  return run.steps.find((s) => s.status === "awaiting_verdict" && (!agent || !qaAgentMayJudge(s))) ?? null;
}

/**
 * Whether a playtest has work a subagent could do: a step not settled, or an
 * auto step that failed and might pass with the pawn driven.
 */
export function qaAssignable(run: QaRun): boolean {
  // A failed step of either kind is one a subagent can work on: it fixes the project and runs again.
  return run.steps.some((s) => (!qaSettled(s.status) && (s.judge === "auto" || qaAgentMayJudge(s))) || s.status === "failed");
}

/** A draft's run id: the script it is a draft of. */
export const qaDraftId = (scriptPath: string): string => `draft:${qaScriptKey(scriptPath)}`;
export const qaIsDraft = (run: QaRun): boolean => run.state === "draft";

/** A script path without its object name, the same however it was written: /Game/S/Door.Door is /Game/S/Door. */
export function qaScriptKey(path: string): string {
  const dot = path.lastIndexOf(".");
  return dot > path.lastIndexOf("/") ? path.slice(0, dot) : path;
}

/** The step fields a script holds, as pie(script_create) and script_update take them, with the client's player. */
export interface QaScriptStep {
  instruction: string;
  judge: "auto" | "manual";
  expect?: QaExpect;
  agent_judgeable?: boolean;
  player?: string;
  arrange?: unknown;
  act?: unknown;
}

/** A draft card from a pie(script_create), script_update or script_read answer: { path, name, steps, default_within_ms }. Null when it names no script. */
export function qaDraftFromScript(answer: unknown, where: { sessionId?: string | null; players?: Record<number, string>; seenAt?: string } = {}): QaRun | null {
  const a = obj(answer);
  const path = str(a?.path);
  if (!a || !path || !Array.isArray(a.steps)) return null;
  const within = int(a.default_within_ms) ?? 5000;
  const map = str(a.map);
  const steps: QaStep[] = (a.steps as unknown[]).map((x, index) => {
    const s = obj(x) ?? {};
    const expect = (obj(s.expect) ?? undefined) as QaExpect | undefined;
    const judge = s.judge === "manual" ? "manual" as const : "auto" as const;
    const player = where.players?.[index];
    return {
      index,
      instruction: str(s.instruction) ?? "",
      judge,
      ...(expect ? { expect } : {}),
      status: "pending" as const,
      withinMs: (expect?.within_ms ?? 0) > 0 ? expect!.within_ms : within,
      ...(judge === "manual" && typeof s.agent_judgeable === "boolean" ? { agentJudgeable: s.agent_judgeable } : {}),
      ...(player ? { player } : {}),
      ...(s.arrange !== undefined && s.arrange !== null ? { arrange: s.arrange } : {}),
      ...(s.act !== undefined && s.act !== null ? { act: s.act } : {}),
    };
  });
  return {
    ...(map ? { map } : {}),
    runId: qaDraftId(path),
    script: qaScriptKey(path),
    scriptName: str(a.name) ?? qaScriptKey(path).split("/").pop() ?? path,
    state: "draft",
    steps,
    awaiting: [],
    lastSeq: 0,
    summary: qaSummaryOf(steps),
    resultsPath: null,
    sessionId: where.sessionId ?? null,
    toolUseId: null,
    seenAt: where.seenAt ?? new Date().toISOString(),
  };
}

/** A card's steps as a script's, for pie(script_update): what the user edited, with each step's player kept beside it. */
export function qaScriptSteps(run: QaRun): QaScriptStep[] {
  return run.steps.map((s) => ({
    instruction: s.instruction,
    judge: s.judge,
    ...(s.judge === "auto" && s.expect ? { expect: s.expect } : {}),
    ...(s.judge === "manual" && s.agentJudgeable === false ? { agent_judgeable: false } : {}),
    ...(s.player ? { player: s.player } : {}),
    ...(s.arrange !== undefined ? { arrange: s.arrange } : {}),
    ...(s.act !== undefined ? { act: s.act } : {}),
  }));
}

/** The PIE instance a player is: the host is instance 0, client n is instance n. */
export function qaPieInstance(player: string | undefined): number | undefined {
  if (!player) return undefined;
  if (/^host$/i.test(player)) return 0;
  const m = /^client\s*(\d+)?$/i.exec(player.trim());
  return m ? Number(m[1] ?? 1) : undefined;
}
