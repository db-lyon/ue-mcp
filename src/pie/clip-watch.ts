// @session per-session: Follows one clip run through the ToolContext of the call that started it, so it polls that call's editor.
/**
 * Following a playtest clip run from the server (#2097).
 *
 * The editor keeps one clip run and answers pie_clip_status with it as it stands. One watcher per run polls that
 * while the run plays and puts what changed between two answers on the clip event bus: where playback stands, each
 * check and region whose status changed, each event fired, the manual checks it waits on, and how it ended. A client
 * then draws the timeline cursor and the check notes from the event stream instead of polling.
 */
import type { ToolContext } from "../surface/tool-types.js";
import { emitClipEvent, type ClipEvent, type ClipPosition } from "./clip-events.js";
import { clipWaitFollowUp, type ClipWaitClock } from "./clip-wait.js";
import { LiveTake } from "./clip-take-live.js";

type Body = Record<string, unknown>;

/** A clip event before the watcher stamps it with the run, the time and its seq. */
export type ClipChange = ClipEvent extends infer E ? (E extends ClipEvent ? Omit<E, "runId" | "clip" | "at" | "seq"> : never) : never;

const realClock: ClipWaitClock = {
  now: () => Date.now(),
  sleep: (ms) =>
    new Promise((resolve) => {
      const t = setTimeout(resolve, ms);
      t.unref?.();
    }),
};

const POLL_MS = 250;
const TERMINAL: ReadonlySet<string> = new Set(["finished", "stopped", "idle"]);

const num = (v: unknown): number => (typeof v === "number" && Number.isFinite(v) ? v : 0);
const str = (v: unknown): string | undefined => (typeof v === "string" && v ? v : undefined);
const list = (v: unknown): Body[] => (Array.isArray(v) ? v.filter((x): x is Body => !!x && typeof x === "object") : []);
const ids = (v: unknown): string[] => (Array.isArray(v) ? v.filter((x): x is string => typeof x === "string") : []);

function positionOf(s: Body): ClipPosition {
  const holding = str(s.holding);
  return {
    state: str(s.state) ?? "running",
    position: num(s.position),
    length: num(s.length),
    gameMs: num(s.game_ms),
    heldMs: num(s.held_ms),
    frames: num(s.frames),
    ...(holding ? { holding } : {}),
  };
}

/** What identifies a check's or region's state for change detection. */
const checkKey = (c: Body) => JSON.stringify([c.status, c.started_frame, c.ended_frame, (c.hold as Body | undefined)?.resolved_frame]);
const regionKey = (r: Body) => JSON.stringify([r.status, r.drift]);

/**
 * The changes from one pie_clip_status answer to the next, in the order a timeline applies them. prev null is the
 * first answer of the run, which starts it.
 */
export function diffClipStatus(prev: Body | null, next: Body): ClipChange[] {
  const out: ClipChange[] = [];
  const pos = positionOf(next);
  if (!prev) {
    out.push({
      type: "run_started",
      mode: str(next.mode) ?? "play",
      ...(str(next.clip_name) ? { clipName: str(next.clip_name) } : {}),
      ...(str(next.region) ? { region: str(next.region) } : {}),
      ...(typeof next.rate === "number" ? { rate: next.rate } : {}),
      ...(str(next.results_path) ? { resultsPath: str(next.results_path) } : {}),
      ...pos,
    });
  } else {
    const was = positionOf(prev);
    if (was.position !== pos.position || was.holding !== pos.holding || was.state !== pos.state || was.frames !== pos.frames) {
      out.push({ type: "position", ...pos });
    }
  }

  const fired = list(next.events);
  for (const event of fired.slice(prev ? list(prev.events).length : 0)) out.push({ type: "event_fired", event });

  const before = new Map(list(prev?.checks).map((c) => [String(c.id), checkKey(c)]));
  for (const check of list(next.checks)) {
    const k = checkKey(check);
    // A first answer reports only the checks that have moved past pending; the client has the clip for the rest.
    if (prev ? before.get(String(check.id)) !== k : check.status !== "pending") out.push({ type: "check", check });
  }

  const regionsBefore = new Map(list(prev?.regions).map((r) => [String(r.id), regionKey(r)]));
  for (const region of list(next.regions)) {
    const k = regionKey(region);
    if (prev ? regionsBefore.get(String(region.id)) !== k : region.status !== "pending" && region.status !== "empty") {
      out.push({ type: "region", region });
    }
  }

  const awaiting = ids(next.awaiting);
  if (awaiting.length > 0 && JSON.stringify(awaiting) !== JSON.stringify(ids(prev?.awaiting))) {
    out.push({ type: "verdict_requested", awaiting });
  }

  const state = str(next.state) ?? "";
  if (TERMINAL.has(state) && (!prev || !TERMINAL.has(str(prev.state) ?? ""))) out.push(finishedOf(next, state === "idle" ? "finished" : state));
  return out;
}

function finishedOf(s: Body, state: string): ClipChange {
  return {
    type: "run_finished",
    state,
    ...(str(s.stop_reason) ? { stopReason: str(s.stop_reason) } : {}),
    ...(typeof s.passed === "boolean" ? { passed: s.passed } : {}),
    ...(s.summary && typeof s.summary === "object" ? { summary: s.summary as Body } : {}),
    ...(s.drift && typeof s.drift === "object" ? { drift: s.drift as Body } : {}),
    ...(str(s.results_path) ? { resultsPath: str(s.results_path) } : {}),
  };
}

export interface ClipWatchOptions {
  clock?: ClipWaitClock;
  /** Consecutive failed status calls before the watcher gives up. Default 5. */
  maxFailures?: number;
  /** The longest one watcher follows a run. Default 6 hours. */
  maxDurationMs?: number;
}

const watchers = new Map<string, ClipWatcher>();

export class ClipWatcher {
  readonly runId: string;
  ctx: ToolContext;
  clip: string;
  last: Body | null = null;
  ended = false;
  private seq = 0;
  private readonly clock: ClipWaitClock;
  private readonly maxFailures: number;
  private readonly maxDurationMs: number;

  constructor(ctx: ToolContext, runId: string, first: Body | null, opts: ClipWatchOptions = {}) {
    this.ctx = ctx;
    this.runId = runId;
    this.clip = str(first?.clip) ?? "inline";
    this.clock = opts.clock ?? realClock;
    this.maxFailures = opts.maxFailures ?? 5;
    this.maxDurationMs = opts.maxDurationMs ?? 6 * 60 * 60 * 1000;
    if (first) this.apply(first);
  }

  /** Emits what changed since the last answer, and ends the watch once the run has. */
  apply(status: Body): void {
    if (this.ended || (str(status.run_id) && status.run_id !== this.runId)) return;
    if (str(status.clip)) this.clip = str(status.clip)!;
    const changes = diffClipStatus(this.last, status);
    const take = status.take && typeof status.take === "object" ? (status.take as Body) : null;
    const live = take?.live && typeof take.live === "object" ? (take.live as Body) : null;
    const delta = live ? this.liveTake.apply(live) : null;
    if (delta) {
      // Before run_finished, so a take's last notes land before the run ends.
      const at = changes.findIndex((c) => c.type === "run_finished");
      const progress: ClipChange = {
        type: "take_progress",
        ...(str(take?.take_id) ? { takeId: str(take?.take_id) } : {}),
        ...(typeof take?.punch_in === "number" ? { punchIn: take.punch_in } : {}),
        ...delta,
      };
      changes.splice(at < 0 ? changes.length : at, 0, progress);
    }
    for (const change of changes) {
      this.emit(change);
      if (change.type === "run_finished") this.ended = true;
    }
    this.last = status;
  }

  /** The take in flight while the run records, for a client that joins mid-take. */
  readonly liveTake = new LiveTake();

  stop(): void {
    this.ended = true;
  }

  async run(): Promise<void> {
    const started = this.clock.now();
    let failures = 0;
    while (!this.ended) {
      await this.clock.sleep(POLL_MS);
      if (this.ended) break;
      if (this.clock.now() - started > this.maxDurationMs) break;
      try {
        const status = (await this.ctx.bridge.call("pie_clip_status", { run_id: this.runId })) as Body;
        failures = 0;
        if (status && status.success !== false) this.apply(status);
      } catch {
        if (++failures >= this.maxFailures) {
          this.emit({ type: "run_finished", state: "unreachable" });
          break;
        }
      }
    }
    this.ended = true;
    if (watchers.get(this.runId) === this) watchers.delete(this.runId);
  }

  private emit(change: ClipChange): void {
    emitClipEvent({ ...change, runId: this.runId, clip: this.clip, at: new Date().toISOString(), seq: ++this.seq } as ClipEvent);
  }
}

/** Follows a run, or hands the call's context to the watcher already following it. */
export function startClipWatch(ctx: ToolContext, runId: string, first: Body | null, opts: ClipWatchOptions = {}): ClipWatcher {
  const existing = watchers.get(runId);
  if (existing && !existing.ended) {
    existing.ctx = ctx;
    if (first) existing.apply(first);
    return existing;
  }
  const w = new ClipWatcher(ctx, runId, first, opts);
  if (w.ended) return w;
  watchers.set(runId, w);
  void w.run();
  return w;
}

export function getClipWatch(runId: string): ClipWatcher | undefined {
  return watchers.get(runId);
}

export function stopAllClipWatches(): void {
  for (const w of [...watchers.values()]) w.stop();
  watchers.clear();
}

/**
 * The follow-up of pie(clip_run), pie(clip_record) and pie(clip_status): follow a run that has not ended, so its
 * events flow (after a server restart too), then wait as clip-wait.ts does when asked to.
 */
export async function clipRunFollowUp(
  ctx: ToolContext,
  params: Record<string, unknown>,
  answered: unknown,
  clock?: ClipWaitClock,
): Promise<unknown> {
  if (answered && typeof answered === "object") {
    const body = answered as Body;
    const state = str(body.state) ?? "";
    if (body.success !== false && str(body.run_id) && !TERMINAL.has(state)) {
      startClipWatch(ctx, body.run_id as string, body, clock ? { clock } : {});
    }
  }
  return withLiveTake(await clipWaitFollowUp(ctx, params, answered, clock));
}

/**
 * Replaces the raw sample window of a take in flight with the take so far as notes and keys (`take.live_take`),
 * from the watcher that has been folding the windows in since the punch-in.
 */
export function withLiveTake(answered: unknown): unknown {
  if (!answered || typeof answered !== "object") return answered;
  const body = answered as Body;
  const take = body.take && typeof body.take === "object" ? (body.take as Body) : null;
  if (!take || !("live" in take)) return answered;
  const { live: _live, ...rest } = take;
  const w = str(body.run_id) ? watchers.get(body.run_id as string) : undefined;
  return { ...body, take: w && w.liveTake.next > 0 ? { ...rest, live_take: w.liveTake.snapshot() } : rest };
}
