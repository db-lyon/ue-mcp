// @session per-session: One project's plan queue, kept in that project's data folder and run on its editor.
/**
 * The plan queue: plans waiting for the editor, run one item at a time
 * (ue-mcp runs one script at a time per editor). A plan's items either run
 * their checks, or (a fix plan) hand each check to a fix loop in a chat
 * session of its own. Kept in <dataDir>/hub/queue.json.
 *
 * The editor is the developer's workspace, so the queue acts only on what it
 * started, under an explicit lease (lease.ts):
 *
 * - An item starts only while the editor is free (QueueRunner.occupied):
 *   otherwise the queue waits and says why (QueueState.waiting).
 * - The lease is taken before the item starts and released when it settles.
 *   Play In Editor the item's run started is ended then, and never any other.
 * - release() is Release the editor: it pauses the queue, stops the item's
 *   run or fix loop (a loop stops at its next checkpoint), ends PIE the item
 *   started, and puts the item back at the front for when a person resumes.
 * - After the editor reconnects, or the daemon restarts, with work waiting, the
 *   queue is paused (pausedBy "reconnect") and waits for a person to resume
 *   it, so an editor someone just restarted is never taken straight back. A
 *   run that was still live is followed again meanwhile.
 * - A crash during an item's run is that item's outcome, crashed, recorded
 *   once and never retried. Any other disconnect puts the item back.
 *
 * A run holds at a manual step until it is judged (verdict-hold.ts), and the
 * plan waits with it: the item is needs_you meanwhile. An ended item takes its
 * run's outcome: passed, failed, unjudged (nobody gave the verdict, no
 * result), cancelled (the run was stopped) or error.
 *
 * What runs an item (a card, QaBroker.runScript, a fix loop) is the hub's,
 * given as a QueueRunner, so this file only keeps order, state and the lease.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import {
  DEFAULT_PLAN_OPTIONS,
  FAILING_ITEM_STATES,
  itemStateOf,
  outcomeOf,
  qaEnded,
  qaScriptKey,
  type PlanItem,
  type PlanItemState,
  type PlanOptions,
  type PlanPlacement,
  type PlanRun,
  type QaRun,
  type QueuePause,
  type QueueState,
} from "./contract.js";
import { writeFileAtomic } from "./atomic-write.js";
import { EditorLease } from "./lease.js";
import { planOptionsOf } from "./scope.js";
import { conflict, notFound } from "./util.js";

/** The most finished plans kept. */
const MAX_RECENT = 20;

/** Why an item went back in the queue, or ended, for the reasons the queue gives. */
export const RELEASED = "The editor was released";
export const CRASHED = "The editor crashed during this run";
export const DAEMON_STOPPED = "The daemon stopped during this run";

/**
 * What starting one item came to: its run (and, for a fix item, its chat
 * session), or why it did not start. pieStarted says the run started Play In
 * Editor itself, for a run the broker may not hold yet (a replay).
 */
export type ItemStart = { runId: string; sessionId?: string; pieStarted?: boolean } | { skipped: string } | { error: string };

export interface QueueRunner {
  /** Whether the editor is connected. */
  connected(): boolean;
  /** Why the editor is not free for this plan's next item, or null when it is (occupancyOf). */
  occupied(plan: PlanRun): string | null;
  start(item: PlanItem, plan: PlanRun): Promise<ItemStart>;
  /** The run as the broker has it now. */
  run(runId: string): QaRun | undefined;
  /** End a run the hub started (QaBroker.stopRun, which releases a verdict hold too). */
  stop(runId: string, reason: string): Promise<void>;
  /** Read a run again, for when its events were missed. */
  refresh(runId: string): Promise<void>;
  /** Stop a fix item's loop: the running attempt stops and is checkpointed. */
  stopFix(item: PlanItem): Promise<void>;
  /** End the Play In Editor session the hub's item started. */
  endPie(): Promise<void>;
}

interface Stored {
  schema: 1;
  paused: boolean;
  pausedBy: QueuePause | null;
  current: PlanRun | null;
  queued: PlanRun[];
  recent: PlanRun[];
}

export interface QueueOptions {
  dataDir: string;
  runner: QueueRunner;
  /** The workspace editor's lease; a fresh one when not given. */
  lease?: EditorLease;
  now?: () => string;
  changed?: (planRunId?: string) => void;
  /** A plan finished: done, failed or cancelled. */
  finished?: (plan: PlanRun) => void;
  /** The Playtest Hub is on: nothing new starts while it is off. Default on. */
  enabled?: () => boolean;
  log?: { warn(message: string, detail?: unknown): void };
}

export const newId = (prefix: string): string => `${prefix}-${Date.now().toString(36)}-${crypto.randomBytes(3).toString("hex")}`;

/** An item that holds the editor: its run or loop is on it, or held at a manual step. */
const live = (i: PlanItem): boolean => i.state === "running" || i.state === "needs_you";
/** An item that has its result. */
const settled = (i: PlanItem): boolean => i.state !== "queued" && !live(i);
const workOf = (p: PlanRun): "run" | "fix" => p.work ?? "run";

export class PlanQueue {
  private s: Stored = { schema: 1, paused: false, pausedBy: null, current: null, queued: [], recent: [] };
  private readonly file: string;
  private readonly now: () => string;
  /** The editor's lease, held while an item runs and until its PIE has ended. */
  readonly lease: EditorLease;
  private pumping = false;
  /** Why the next item waits although nothing is paused. */
  private waiting: string | null = null;

  constructor(private readonly o: QueueOptions) {
    this.file = path.join(o.dataDir, "hub", "queue.json");
    this.now = o.now ?? (() => new Date().toISOString());
    this.lease = o.lease ?? new EditorLease();
    this.load();
  }

  private load(): void {
    try {
      const raw = JSON.parse(fs.readFileSync(this.file, "utf-8")) as Partial<Stored>;
      this.s = {
        schema: 1,
        paused: raw.paused === true,
        pausedBy: raw.paused === true ? raw.pausedBy ?? "person" : null,
        current: raw.current && typeof raw.current === "object" ? raw.current : null,
        queued: Array.isArray(raw.queued) ? raw.queued : [],
        recent: Array.isArray(raw.recent) ? raw.recent.slice(0, MAX_RECENT) : [],
      };
    } catch {
      // None yet, or unreadable: an empty queue.
    }
    // A plan saved by an earlier Studio has other options (manualSteps, no attended): read again as a person's plan.
    for (const p of [this.s.current, ...this.s.queued]) if (p) p.options = storedOptions(p.options);
    const cur = this.s.current;
    if (cur) {
      // The item that was live when the daemon stopped: followed again, under the lease, while the broker has its run; else queued again.
      cur.items.forEach((item, at) => {
        if (!live(item)) return;
        const run = item.runId ? this.o.runner.run(item.runId) : undefined;
        if (!run) {
          Object.assign(item, { state: "queued", runId: null, startedAt: null, reason: "queued again after the hub restarted" });
          return;
        }
        this.lease.take({ purpose: purposeOf(cur, item), planRunId: cur.id, item: at, work: workOf(cur), runId: item.runId, sessionId: item.sessionId ?? null, pie: run.pieStarted === true, ...(item.startedAt ? { since: item.startedAt } : {}) });
      });
    }
    // The daemon restarted with work waiting: it waits for a person, as after an editor reconnect.
    this.reconnected(false);
  }

  private save(planRunId?: string): void {
    try {
      fs.mkdirSync(path.dirname(this.file), { recursive: true });
      writeFileAtomic(this.file, `${JSON.stringify(this.s, null, 2)}\n`);
    } catch (e) {
      this.o.log?.warn("hub: the queue could not be saved", e);
    }
    this.o.changed?.(planRunId);
  }

  state(): QueueState {
    const copy = <T>(v: T): T => JSON.parse(JSON.stringify(v)) as T;
    return {
      paused: this.s.paused,
      pausedBy: this.s.pausedBy,
      current: copy(this.s.current),
      queued: copy(this.s.queued),
      recent: copy(this.s.recent),
      editor: { connected: this.o.runner.connected() },
      lease: this.lease.get(),
      waiting: this.s.paused ? null : this.waiting,
    };
  }

  paused(): boolean {
    return this.s.paused;
  }

  /** The run a plan has on the editor now, if any. */
  currentRun(): string | null {
    const item = this.s.current?.items.find(live);
    return item?.runId ?? null;
  }

  /** The running plan, when one of its items ran this run. */
  planOf(runId: string): PlanRun | undefined {
    return this.s.current?.items.some((i) => i.runId === runId) ? this.s.current : undefined;
  }

  find(id: string): PlanRun | undefined {
    return [this.s.current, ...this.s.queued, ...this.s.recent].find((p): p is PlanRun => p?.id === id);
  }

  /** Queue a plan behind the others, respecting a pause. */
  enqueue(plan: PlanRun): PlanRun {
    this.add(plan);
    return plan;
  }

  /**
   * Queue a plan, and say where it went (PlanPlacement). `person`: a person
   * clicked Run, which resumes a paused queue; with nothing on the editor
   * their plan goes first, and a plan the pause left half done waits behind
   * it. Anything else queued respects the pause. `next`: ahead of every
   * other queued plan.
   */
  add(plan: PlanRun, o: { person?: boolean; next?: boolean } = {}): PlanPlacement {
    const onEditor = !!this.s.current?.items.some(live) || !!this.lease.get();
    let resumed = false;
    if (o.person && this.s.paused) {
      const cur = this.s.current;
      if (cur && !onEditor) {
        cur.state = "queued";
        this.s.queued.unshift(cur);
        this.s.current = null;
      }
      this.s.paused = false;
      this.s.pausedBy = null;
      resumed = true;
    }
    if (o.next || (resumed && !onEditor)) this.s.queued.unshift(plan);
    else this.s.queued.push(plan);
    this.save(plan.id);
    // Synchronous up to the first item's start: a plan that can start is current when this returns.
    void this.pump();
    return { ...this.placementOf(plan.id), ...(resumed ? { resumed } : {}) };
  }

  /** Where a plan stands in the queue now. */
  placementOf(id: string): PlanPlacement {
    if (this.s.current?.id === id) return { state: "started", position: 0, ahead: 0 };
    const at = this.s.queued.findIndex((p) => p.id === id);
    const ahead = at + (this.s.current ? 1 : 0);
    if (this.s.paused) return { state: "queued", position: at + 1, ahead, reason: "the queue is paused" };
    if (ahead === 0) {
      const reason = this.o.enabled?.() === false ? "the Playtest Hub is off" : !this.o.runner.connected() ? "the editor is not connected" : this.waiting;
      return { state: "waiting", position: 1, ahead: 0, reason };
    }
    return { state: "queued", position: at + 1, ahead };
  }

  /** Pause or resume. Resuming clears whatever paused it: a person, a release or a reconnect. */
  setPaused(paused: boolean, by: QueuePause = "person"): void {
    if (this.s.paused === paused && (!paused || this.s.pausedBy === by)) return;
    this.s.paused = paused;
    this.s.pausedBy = paused ? by : null;
    this.save();
    if (!paused) void this.pump();
  }

  /**
   * The editor connected again (or the daemon started) with work waiting: the
   * queue pauses for a person to resume it. Returns whether it paused.
   */
  reconnected(save = true): boolean {
    if (this.s.paused || (!this.s.current && this.s.queued.length === 0)) return false;
    this.s.paused = true;
    this.s.pausedBy = "reconnect";
    if (save) this.save();
    return true;
  }

  /** Move a queued plan to position `to` among the queued ones, 0 for next. */
  move(id: string, to: number): void {
    const at = this.s.queued.findIndex((p) => p.id === id);
    if (at < 0) throw this.find(id) ? conflict("only a queued plan can be moved") : notFound(`no plan ${id}`);
    const [plan] = this.s.queued.splice(at, 1);
    this.s.queued.splice(Math.max(0, Math.min(this.s.queued.length, Math.floor(to))), 0, plan);
    this.save(id);
    void this.pump();
  }

  /** Cancel a plan: a queued one leaves the queue, the running one stops what its item runs. */
  async cancel(id: string): Promise<PlanRun> {
    const at = this.s.queued.findIndex((p) => p.id === id);
    if (at >= 0) {
      const [plan] = this.s.queued.splice(at, 1);
      this.close(plan, "cancelled", "cancelled");
      this.save(id);
      return plan;
    }
    const cur = this.s.current;
    if (!cur || cur.id !== id) throw this.find(id) ? conflict("the plan has already finished") : notFound(`no plan ${id}`);
    const onEditor = cur.items.find(live);
    const held = onEditor ? { ...onEditor } : null;
    this.close(cur, "cancelled", "cancelled");
    this.s.current = null;
    this.save(id);
    if (held) await this.stopItem(cur, held, "The plan was cancelled");
    await this.free();
    void this.pump();
    return cur;
  }

  /**
   * Release the editor: the queue pauses, the item on the editor stops (its
   * run, or its fix loop at the next checkpoint) and goes back to the front
   * for when a person resumes, Play In Editor that item started ends, and the
   * lease is released.
   */
  async release(): Promise<void> {
    await this.putBack("release", RELEASED);
  }

  /**
   * The daemon is stopping: the item on the editor stops as on a release, and
   * Play In Editor its run started ends with it, so nothing the hub started
   * keeps running without a daemon to end it. The queue waits for a person
   * when the daemon is back, as after a reconnect.
   */
  async shutdown(): Promise<void> {
    if (!this.s.current?.items.some(live) && !this.lease.get()) return;
    await this.putBack("reconnect", DAEMON_STOPPED);
  }

  /** Pause, stop the item on the editor and put it back at the front, end the PIE its run started, and release the lease. */
  private async putBack(by: QueuePause, reason: string): Promise<void> {
    this.s.paused = true;
    this.s.pausedBy = by;
    const cur = this.s.current;
    const item = cur?.items.find(live);
    const held = item ? { ...item } : null;
    if (item) Object.assign(item, { state: "queued", runId: null, startedAt: null, reason });
    this.save(cur?.id);
    if (cur && held) await this.stopItem(cur, held, reason);
    await this.free();
  }

  /** Stop what an item started, when it is still going. */
  private async stopItem(plan: PlanRun, item: PlanItem, reason: string): Promise<void> {
    try {
      if (workOf(plan) === "fix") await this.o.runner.stopFix(item);
      else if (item.runId) {
        const run = this.o.runner.run(item.runId);
        if (run && !qaEnded(run.state)) await this.o.runner.stop(item.runId, reason);
      }
    } catch (e) {
      this.o.log?.warn(`hub: ${item.name} could not be stopped`, e);
    }
  }

  /** Whether an item's run started Play In Editor: as its start said, or as the broker holds the run. */
  private startedPie(r: { runId: string; pieStarted?: boolean }): boolean {
    return r.pieStarted === true || this.o.runner.run(r.runId)?.pieStarted === true;
  }

  /** The item settled or stopped: PIE its run started ends, and the lease is released. */
  private async free(): Promise<void> {
    const l = this.lease.get();
    if (!l) return;
    if (l.pie) await this.o.runner.endPie().catch((e: unknown) => this.o.log?.warn("hub: Play In Editor could not be ended after the hub's run", e));
    this.lease.release();
  }

  /** End a plan: every item not finished takes `rest`, and the plan goes to the recent list. */
  private close(plan: PlanRun, state: PlanRun["state"], rest: PlanItemState, reason?: string): void {
    const at = this.now();
    for (const item of plan.items) {
      if (settled(item)) continue;
      Object.assign(item, { state: rest, endedAt: at, ...(reason ? { reason } : {}) });
    }
    plan.state = state;
    plan.endedAt = at;
    this.s.recent.unshift(plan);
    this.s.recent.length = Math.min(this.s.recent.length, MAX_RECENT);
    this.o.finished?.(plan);
  }

  // ── Running ───────────────────────────────────────────────────────

  /** A run changed: the current item's run, or its fix loop's card, moves the plan on. */
  observe(run: QaRun): void {
    const cur = this.s.current;
    if (!cur) return;
    const item = workOf(cur) === "fix"
      ? cur.items.find((i) => live(i) && !!i.sessionId && run.assignment?.sessionId === i.sessionId && qaScriptKey(run.script) === i.check.target)
      : cur.items.find((i) => i.runId === run.runId && live(i));
    if (!item) return;
    if (this.evaluate(cur, item, run)) void this.next();
  }

  /** The item settled: the lease goes, then the next item may start. */
  private async next(): Promise<void> {
    await this.free();
    await this.pump();
  }

  /** Apply a run to its item. Returns whether the item settled. */
  private evaluate(plan: PlanRun, item: PlanItem, run: QaRun): boolean {
    return workOf(plan) === "fix" ? this.evaluateFix(plan, item, run) : this.evaluateRun(plan, item, run);
  }

  private evaluateRun(plan: PlanRun, item: PlanItem, run: QaRun): boolean {
    if (!qaEnded(run.state)) {
      if (run.pieStarted) this.lease.update({ pie: true });
      const waiting = run.steps.some((s) => s.status === "awaiting_verdict");
      if (waiting && item.state !== "needs_you") {
        item.state = "needs_you";
        item.reason = "waiting for a verdict";
        this.save(plan.id);
      } else if (!waiting && item.state === "needs_you") {
        // Judged: the run goes on.
        item.state = "running";
        item.reason = null;
        this.save(plan.id);
      }
      return false;
    }
    const state = itemStateOf(outcomeOf(run));
    // A run that did not get through its steps says why it ended: "no verdict", "PIE ended" and the like.
    const reason = state === "passed" || state === "failed" ? null : run.stopReason ?? (state === "unjudged" ? "no verdict" : state === "cancelled" ? "the run was stopped" : "the run ended before every step settled");
    this.end(plan, item, state, reason);
    return true;
  }

  /** A fix item follows its loop on the card: running until the loop passes, needs the person, stops or fails. */
  private evaluateFix(plan: PlanRun, item: PlanItem, run: QaRun): boolean {
    const a = run.assignment!;
    if (a.state === "running") {
      if (item.runId !== run.runId) {
        item.runId = run.runId;
        this.save(plan.id);
      }
      return false;
    }
    const loop = a.loop;
    const [state, reason]: [PlanItemState, string | null] = loop?.state === "passed" ? ["passed", null]
      : loop?.state === "needs_you" ? ["failed", loop.blocker?.reason ?? "the fix loop stopped for you"]
      : loop?.state === "stopped" || a.state === "stopped" ? ["cancelled", "the fix loop was stopped"]
      : ["error", loop?.error ?? "the fix loop failed"];
    item.runId = run.runId;
    this.end(plan, item, state, reason);
    return true;
  }

  private end(plan: PlanRun, item: PlanItem, state: PlanItemState, reason: string | null): void {
    Object.assign(item, { state, reason, endedAt: this.now() });
    if (plan.options.stopOnFailure && FAILING_ITEM_STATES.includes(state)) {
      this.close(plan, "failed", "cancelled", "the plan stopped at a failure");
      this.s.current = null;
    }
    this.save(plan.id);
  }

  /**
   * The editor went away. A run item that was on it crashed with it, once and
   * for good, when the editor crashed; otherwise it goes back in the queue. A
   * fix loop deals with crashes itself (playtest-loop.ts), so its item stays.
   */
  editorLost(cause?: string): void {
    const cur = this.s.current;
    const item = cur?.items.find(live);
    if (!cur || !item || workOf(cur) === "fix") return;
    if (cause === "crashed") {
      this.end(cur, item, "crashed", CRASHED);
    } else {
      Object.assign(item, { state: "queued", runId: null, startedAt: null, reason: "queued again: the editor disconnected" });
      this.save(cur.id);
    }
    // PIE went with the editor: nothing is left to end.
    this.lease.release();
  }

  /**
   * The periodic look: a run item whose editor went away (its event missed)
   * is queued again, and a live item's run is read again in case its events
   * were missed.
   */
  async tick(): Promise<void> {
    const cur = this.s.current;
    const item = cur?.items.find(live);
    if (cur && item) {
      if (!this.o.runner.connected()) {
        this.editorLost();
      } else if (item.runId && workOf(cur) === "run") {
        const run = this.o.runner.run(item.runId);
        if (run && !qaEnded(run.state)) await this.o.runner.refresh(item.runId).catch(() => undefined);
        const again = this.o.runner.run(item.runId);
        if (again && this.s.current === cur && live(item) && this.evaluate(cur, item, again)) await this.free();
      }
    }
    await this.pump();
  }

  private setWaiting(why: string | null): void {
    if (this.waiting === why) return;
    this.waiting = why;
    this.o.changed?.();
  }

  /** A running plan with nothing left to start ends, paused or not. Returns whether one did. */
  private closeIfDone(): boolean {
    const cur = this.s.current;
    if (!cur || cur.items.some((i) => i.state === "queued" || live(i))) return false;
    const all = cur.items.length > 0 && cur.items.every((i) => FAILING_ITEM_STATES.includes(i.state));
    this.close(cur, all ? "failed" : "done", "cancelled");
    this.s.current = null;
    this.save(cur.id);
    return true;
  }

  /** Start what is next, while the queue runs and the editor is free. */
  async pump(): Promise<void> {
    if (this.pumping) return;
    this.pumping = true;
    try {
      for (;;) {
        this.closeIfDone();
        if (this.s.paused || this.o.enabled?.() === false) return this.setWaiting(null);
        let cur = this.s.current;
        // An item on an editor, or one whose lease is being let go (its PIE ending): the next waits for it.
        if (cur?.items.some(live) || this.lease.get()) return this.setWaiting(null);
        const next = cur ?? this.s.queued[0];
        if (!next || !this.o.runner.connected()) return this.setWaiting(null);
        const why = this.o.runner.occupied(next);
        if (why) return this.setWaiting(why);
        this.setWaiting(null);
        if (!cur) {
          cur = this.s.queued.shift()!;
          cur.state = "running";
          cur.startedAt = this.now();
          this.s.current = cur;
          this.save(cur.id);
        }
        const at = cur.items.findIndex((i) => i.state === "queued");
        const item = cur.items[at];
        Object.assign(item, { state: "running", startedAt: this.now(), reason: null, runId: null });
        this.lease.take({ purpose: purposeOf(cur, item), planRunId: cur.id, item: at, work: workOf(cur), sessionId: item.sessionId ?? null, since: item.startedAt! });
        this.save(cur.id);
        let r: ItemStart;
        try {
          r = await this.o.runner.start(item, cur);
        } catch (e) {
          r = { error: e instanceof Error ? e.message : String(e) };
        }
        // Cancelled, released or shut down while it started: what it started is stopped, PIE its run started included.
        if (this.s.current !== cur || item.state !== "running") {
          if ("runId" in r) {
            // The cancel let the lease go before the run was known: it is held again until that run's PIE has ended.
            if (!this.lease.get()) this.lease.take({ purpose: purposeOf(cur, item), planRunId: cur.id, item: at, work: workOf(cur), sessionId: r.sessionId ?? item.sessionId ?? null });
            this.lease.update({ runId: r.runId, pie: this.startedPie(r) });
            const why = item.reason === RELEASED || item.reason === DAEMON_STOPPED ? item.reason : "The plan was cancelled";
            await this.stopItem(cur, { ...item, runId: r.runId, sessionId: r.sessionId ?? item.sessionId ?? null }, why);
          }
          await this.free();
          continue;
        }
        if ("skipped" in r) {
          this.end(cur, item, "skipped", r.skipped);
          await this.free();
        } else if ("error" in r) {
          this.end(cur, item, "error", r.error);
          await this.free();
        } else {
          item.runId = r.runId;
          if (r.sessionId) item.sessionId = r.sessionId;
          const run = this.o.runner.run(r.runId);
          this.lease.update({ runId: r.runId, sessionId: item.sessionId ?? null, pie: this.startedPie(r) });
          this.save(cur.id);
          // A run that ended at once moves the plan on here; one still going does so from its events.
          if (run && (workOf(cur) === "run" || run.assignment) && this.evaluate(cur, item, run)) await this.free();
          if (live(item)) return;
        }
      }
    } catch (e) {
      // Called with void from events and timers: a throw here must not become an unhandled rejection.
      this.o.log?.warn("hub: the queue could not start its next run", e);
    } finally {
      this.pumping = false;
    }
  }
}

/** A saved plan's options: the ones this ue-mcp knows, each valid, over the defaults. */
function storedOptions(v: unknown): PlanOptions {
  try {
    return planOptionsOf(v ?? undefined, DEFAULT_PLAN_OPTIONS);
  } catch {
    return { ...DEFAULT_PLAN_OPTIONS };
  }
}

/** What the lease says the hub is doing: "Run Tutorial: Door opens". */
const purposeOf = (plan: PlanRun, item: PlanItem): string => `${plan.title}: ${item.name}`;
