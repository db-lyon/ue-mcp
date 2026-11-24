// @session per-session: One project's hub: its goals, evidence, queue and lease, over that project's editor.
/**
 * The Playtest Hub's service: goals and what goes with them (goals.ts over a
 * GoalStore, kept on this computer by goal-files.ts), the runs and what each
 * tested (an EvidenceStore, kept on this computer by run-index.ts), the plan
 * queue (queue.ts) and the Needs attention list (shared buildAttention), put
 * together into the answers HUB_ROUTES name. Status is worked out on every
 * read with the shared rules (checkHealth, goalStatus), never stored.
 *
 * Checks are every check a goal names, and every script the editor lists
 * that no goal names yet. A script check whose asset the editor does not
 * list is missing on this checkout, but only while the editor is connected
 * and its list is fresh: offline, nothing is flagged. Before it counts as
 * missing, a redirector at its path is looked for through ue-mcp
 * (asset(fixup_redirectors) with dryRun, which reads and changes nothing):
 * a script that moved is followed to its new path, and the evidence store's
 * alias carries its history there. The goal keeps the path it names, so a
 * checkout where the script has not moved still finds it.
 *
 * Runs go through a HubBroker (ports.ts), so a client's cards, players and
 * verdict routing keep working: a plan item runs with broker.startScript
 * (ue-mcp's own ScriptRunBroker by default; a client's broker may run it on
 * the script's card in a chat session).
 */
import * as path from "node:path";
import type { ActionResult } from "../contract/daemon.js";
import {
  DEFAULT_PLAN_OPTIONS,
  HUB_CHANGED,
  HUB_LEASE,
  NEEDS_PERSON,
  buildAttention,
  needsPerson,
  TRACKER_LINKS_MAX,
  checkId,
  checkStateOf,
  goalChecksDeep,
  isResult,
  leafName,
  qaEnded,
  qaIsDraft,
  qaScriptKey,
  requirementsKey,
  rollUpGoals,
  type CheckDetail,
  type CheckMeta,
  type CheckRef,
  type CheckState,
  type CurrentProvenance,
  type Goal,
  type GoalState,
  type HubChanged,
  type HubChangedWhat,
  type HubOverview,
  type PlanItem,
  type PlanPlacement,
  type PlanRun,
  type PlanScope,
  type PlanTrigger,
  type QaRun,
  type QueueState,
  type EditorLease as EditorLeaseView,
  type OccupancyReason,
  type RunDetail,
  type RunRecord,
  type RunStepSpan,
  type SignOff,
} from "./contract.js";
import type { HubBroker, HubFixer, HubHost, HubPolicy, HubScriptIndex, HubWritePlaytests, ListedScript } from "./ports.js";
import { Goals } from "./goals.js";
import { GoalFiles } from "./goal-files.js";
import type { GoalStore } from "./goal-store.js";
import type { EvidenceStore } from "./evidence-store.js";
import { RunIndex } from "./run-index.js";
import { PlanQueue, newId, type ItemStart } from "./queue.js";
import { EditorLease, ON_EDITOR, occupancyOf, type OccupancySignals } from "./lease.js";
import { ProjectWatch } from "./project-watch.js";
import { readProvenanceFile, readResults, readResultsFile, readSeries } from "./evidence.js";
import { provenanceAtStart, scriptHashOf, ueMcpProvenance, withUeMcp, workspaceNow } from "./provenance.js";
import { planOptionsOf, scopeOf } from "./scope.js";
import { answerOf, bad, checkOfId, conflict, failureOf, isObject, linkOf, notFound } from "./util.js";

/** How often the queue and the runs the hub may have missed are looked at. */
export const TICK_MS = 30_000;
/** How long hub.changed waits to gather the changes that come together, such as a run's steps. */
const PUBLISH_MS = 200;
/** How many runs the first backfill reads, and how many each one after. */
const FIRST_BACKFILL = 5000;
const BACKFILL = 300;
/** How long a backfill stands before an overview asks for another. */
const BACKFILL_MS = 60_000;
/** The most runs a page answers. */
const MAX_PAGE = 200;
/** How long a reading of the workspace stands for the overview; a run's start always reads it afresh. */
const WORKSPACE_MS = 10_000;
/** How long the hub waits for PIE its run asked for to come up, so it can end it. */
const PIE_START_WAIT_MS = 20_000;
/** How long the hub lets another session's PIE call finish before it ends PIE its run started. */
const PIE_IN_USE_WAIT_MS = 60_000;
/** How often those waits look again. */
const PIE_POLL_MS = 250;

export type { HubHost } from "./ports.js";

export interface HubOptions extends HubPolicy {
  host: HubHost;
  /** Starts, follows and stops script runs. Injection point: ScriptRunBroker is the daemon's default. */
  broker: HubBroker;
  /**
   * What is using the editor beside the hub's runs, for the occupancy
   * answer: PIE, open dialogs, calls in flight and editor operations, and
   * how long since someone used this computer. Without it, only the runs the
   * broker knows count. The daemon supplies the first four from its events.
   */
  editor?: Omit<OccupancySignals, "connected" | "runs"> & {
    /** Hear when PIE starts or stops. Returns the unsubscribe. */
    onPie?(listener: (running: boolean) => void): () => void;
  };
  /** Watches project files during runs, for tainted runs; one of the project's own otherwise. */
  watch?: ProjectWatch;
  index: HubScriptIndex;
  /**
   * Fix with an agent. Injection point: ue-mcp has no agent loop, so without
   * a fixer a fix request answers 409. A client with an agent loop supplies one.
   */
  fixer?: HubFixer;
  /** A replay that gives the run a client's card: pie(script_replay) with these args. Default: the call, directly. */
  replay?: (script: string, args: Record<string, unknown>) => Promise<ActionResult>;
  /** Write playtests for a goal. Injection point: a client supplies it, such as a flow in a chat session. Without it, write-playtests answers 409. */
  writePlaytests?: HubWritePlaytests;
  /** The project's engine version, major.minor, for what a run tested. */
  engine?: () => string | null;
  /** The workspace's source control state now; read from the project's source control otherwise. */
  workspace?: () => Promise<CurrentProvenance>;
  /** Where goals are kept; this computer's files under the data directory otherwise. */
  goalStore?: GoalStore;
  now?: () => number;
  /** Start the timers; tests drive tick() themselves. */
  timers?: boolean;
  /** How long ending the run's PIE waits for it to come up, and for another session's PIE call; tests shorten them. */
  pieWaits?: { startMs?: number; inUseMs?: number };
}

interface Catalog {
  /** Every check by its id: a goal's checks by where they are now (a moved script by its new path), and scripts no goal names. */
  checks: Map<string, { ref: CheckRef; goalIds: string[]; name?: string; map?: string | null; renamedFrom?: string }>;
  listed: Map<string, ListedScript> | null;
  connected: boolean;
  /** The workspace runs are compared with. */
  current: CurrentProvenance;
  /** What each check says about itself, by check id. */
  metas: Map<string, CheckMeta>;
}

/** What the editor is told while the hub holds it. */
export const LEASE_NOTICE = "UE-MCP is running a playtest";
/** How long a path found to have no redirector is left before it is asked about again. */
const REDIRECT_RETRY_MS = 10 * 60_000;
/** The most moves followed from one path. */
const REDIRECT_HOPS = 4;

export class Hub {
  readonly goals: Goals;
  /** The runs behind every status: this computer's own (RunIndex), the one store built so far. */
  readonly runs: EvidenceStore;
  readonly queue: PlanQueue;
  private readonly now: () => number;
  private readonly unsubscribe: Array<() => void> = [];
  private timer: NodeJS.Timeout | null = null;
  private pending: { what: Set<HubChangedWhat>; goalIds: Set<string>; runIds: Set<string>; planIds: Set<string> } | null = null;
  private publishTimer: NodeJS.Timeout | null = null;
  private backfilled = -Infinity;
  private backfilling: Promise<void> | null = null;
  private firstBackfill = true;
  /** The developer's editor's lease: the hub's hold on it while an item runs (lease.ts). */
  readonly lease: EditorLease;
  /** Project files changed during a run: the run is tainted. */
  private readonly watch: ProjectWatch;
  /** Runs the editor no longer knows (a status read answered idle): they hold nothing. */
  private readonly gone = new Set<string>();
  /** The plan the editor was last told about, so a plan's items do not each show a notification. */
  private toldPlan: string | null = null;
  /** Sessions writing playtests for a goal: each script drafted there joins the goal. */
  private readonly linking = new Map<string, string>();
  /** What went wrong in the background, newest last, shown with the overview's warnings. */
  private readonly notes: string[] = [];
  /** The last reading of the workspace, for the overview. */
  private workspaceRead: { at: number; value: Promise<CurrentProvenance> } | null = null;
  /** PIE was seen running while the lease is held, so PIE not running now means it ended rather than that it has not come up yet. */
  private pieUp = false;
  /** Runs whose start provenance is being read; the read at their end waits for it. */
  private readonly capturing = new Map<string, Promise<void>>();
  /** Paths asked about for a redirector, and when, so a missing script is not asked about on every read. */
  private readonly redirectAsked = new Map<string, number>();

  constructor(private readonly o: HubOptions) {
    this.now = o.now ?? Date.now;
    const iso = () => new Date(this.now()).toISOString();
    this.goals = new Goals({ store: o.goalStore ?? new GoalFiles(o.host.dataDir, iso), projectDir: o.host.projectDir, legacySignOffs: path.join(o.host.dataDir, "hub", "sign-offs.json"), now: iso, changed: (goalId) => this.changed(["goals", "checks", "attention"], { goalId }) });
    this.runs = new RunIndex({ dataDir: o.host.dataDir, now: iso, changed: (runId) => this.changed(["runs", "checks", "goals", "attention"], { runId }), log: o.host.log });
    this.watch = o.watch ?? new ProjectWatch(o.host.projectDir, o.host.log);
    this.lease = new EditorLease(undefined, (lease) => this.leaseChanged(lease));
    this.queue = new PlanQueue({
      dataDir: o.host.dataDir,
      now: iso,
      lease: this.lease,
      runner: {
        connected: () => this.connected(),
        occupied: (plan) => this.occupancy(!plan.options.attended)[0]?.text ?? null,
        start: (item, plan) => (plan.work === "fix" ? this.startFix(item, plan) : this.startItem(item, plan)),
        run: (runId) => o.broker.get(runId),
        stop: (runId, reason) => this.stopRun(runId, reason),
        refresh: async (runId) => void (await o.broker.status(runId)),
        stopFix: async (item) => this.stopFix(item),
        endPie: () => this.endPie(),
      },
      changed: (planRunId) => this.changed(["queue"], { planRunId }),
      finished: (plan) => {
        if (plan.state === "failed") this.changed(["attention"], { planRunId: plan.id });
      },
      enabled: () => this.enabled(),
      log: o.host.log,
    });
    // A plan's own verdict wait applies to the runs it starts.
    o.broker.setPlanWait?.((run) => this.queue.planOf(run.runId)?.options.verdictWaitMinutes);
    // Every run the broker holds now, so history starts with what a client shows.
    for (const run of o.broker.all()) this.runs.record(run);
    this.unsubscribe.push(o.broker.onSaved((run) => this.saw(run)));
    this.unsubscribe.push(o.host.events.subscribe((e) => {
      // With the experiment off the hub asks the editor for nothing.
      if (!this.enabled()) return;
      if (e.type === "editor.connected") {
        // Back after a restart or a crash: queued work waits for a person to resume it.
        this.queue.reconnected();
        void this.backfill(true);
      } else if (e.type === "editor.disconnected") {
        const cause = typeof e.data.cause === "string" ? e.data.cause : undefined;
        // Every run on a crashed editor ended with it, though no event will say so.
        if (cause === "crashed") for (const r of o.broker.all()) if (ON_EDITOR.has(r.state)) this.gone.add(r.runId);
        this.queue.editorLost(cause);
      } else if (e.type === "call.finished" || e.type === "editor.operation" || e.type === "dialog.closed" || e.type === "dialog.answered") {
        // An operation, a call or a dialog ended: the queue may be free to go on.
        void this.queue.pump();
      }
    }));
    const offPie = o.editor?.onPie?.((running) => {
      if (running && this.lease.get()) this.pieUp = true;
      // The hub's PIE ended: a session started from now on is the developer's.
      if (!running) this.lease.update({ pie: false });
      if (this.enabled()) void this.queue.pump();
    });
    if (offPie) this.unsubscribe.push(offPie);
    if (o.timers !== false) {
      this.timer = setInterval(() => void this.tick(), TICK_MS);
      this.timer.unref?.();
      if (this.enabled()) {
        this.watch.start();
        void this.backfill(true);
        void this.queue.pump();
      }
    }
  }

  dispose(): void {
    for (const u of this.unsubscribe) u();
    this.unsubscribe.length = 0;
    if (this.timer) clearInterval(this.timer);
    if (this.publishTimer) clearTimeout(this.publishTimer);
    this.timer = null;
    this.publishTimer = null;
    this.watch.stop();
  }

  /** The experiment switch a client supplies; on by default. */
  private enabled(): boolean {
    return this.o.enabled?.() ?? true;
  }

  private connected(): boolean {
    try {
      return this.o.host.editors().some((e) => e.connected);
    } catch {
      return false;
    }
  }

  /**
   * Why the editor is not free for the hub now, most decisive first (lease.ts
   * occupancyOf); empty when it is. What the lease holds never counts. Runs
   * the editor said it no longer knows hold nothing.
   */
  occupancy(unattended = false): OccupancyReason[] {
    const e = this.o.editor;
    const signals: OccupancySignals = {
      connected: () => this.connected(),
      pie: () => e?.pie() ?? false,
      dialogs: () => e?.dialogs() ?? 0,
      busy: () => e?.busy() ?? { now: this.now(), calls: [] },
      runs: () => this.o.broker.all().filter((r) => !this.gone.has(r.runId)),
      inputAgeMs: () => e?.inputAgeMs() ?? null,
    };
    return occupancyOf(signals, this.lease.held(), { unattended });
  }

  /**
   * Read again every run the broker thinks is on the editor and the hub did not
   * start, so a run whose end was missed does not hold the queue for ever:
   * one the editor answers idle for is gone.
   */
  private async refreshOthers(): Promise<void> {
    const mine = this.lease.held().runId;
    for (const r of this.o.broker.all()) {
      if (r.runId === mine || this.gone.has(r.runId) || r.manual || !ON_EDITOR.has(r.state)) continue;
      const read = await this.o.broker.status(r.runId).catch(() => null);
      if (read && "answer" in read && read.answer.state === "idle") this.gone.add(r.runId);
    }
  }

  /** The lease changed: clients hear of it, and the editor is told once per plan that the hub holds it. */
  private leaseChanged(lease: EditorLeaseView | null): void {
    if (!lease) this.pieUp = false;
    this.o.host.events.publish(HUB_LEASE, { lease } as unknown as Record<string, unknown>);
    this.changed(["queue"], lease ? { planRunId: lease.planRunId } : {});
    if (!lease || lease.planRunId === this.toldPlan) return;
    this.toldPlan = lease.planRunId;
    // Best effort: the hub.lease event clients hear is the signal; a bridge without show_notification changes nothing.
    void Promise.resolve()
      .then(() => this.o.host.callAction("editor", "show_notification", { text: LEASE_NOTICE, subText: lease.purpose, severity: "info" }))
      .catch(() => undefined);
  }

  /** GET /hub/lease. */
  leaseView(): { lease: EditorLeaseView | null } {
    return { lease: this.lease.get() };
  }

  /**
   * Release the editor: the queue pauses, the run or fix loop the hub has on
   * the editor stops, Play In Editor that run started ends, and the lease is
   * released. Everything else on the editor is left alone.
   */
  async release(): Promise<QueueState> {
    await this.queue.release();
    return this.queueView();
  }

  /**
   * The daemon is stopping: the run the hub has on the editor stops and the
   * PIE it started ends, so neither outlives the daemon. A rebuild of the hub
   * (a client configuring it) is dispose(), which leaves both running.
   */
  async shutdown(): Promise<void> {
    await this.queue.shutdown();
  }

  /**
   * End Play In Editor, only while the hub's run started the session. PIE the
   * run asked for that has not come up yet is waited for, since a queued
   * start lands after the run that asked for it may have ended. Another
   * session's PIE call still in flight is let finish first; one that outlasts
   * the wait keeps PIE, which is then that session's.
   */
  private async endPie(): Promise<void> {
    if (!this.lease.held().pie) return;
    const e = this.o.editor;
    if (e && !e.pie()) {
      // PIE was up under the lease and is not now: it has ended already, and the hub has nothing to stop.
      if (this.pieUp || !(await this.until(() => e.pie() || !this.lease.held().pie, this.o.pieWaits?.startMs ?? PIE_START_WAIT_MS))) {
        this.lease.update({ pie: false });
        return;
      }
    }
    // PIE ended meanwhile, by itself or by someone else: nothing is left to end.
    if (!this.lease.held().pie) return;
    if (!(await this.until(() => !this.pieInUse(), this.o.pieWaits?.inUseMs ?? PIE_IN_USE_WAIT_MS))) {
      this.note("Play In Editor the hub's run started was left running: another session's call is still using it");
      this.lease.update({ pie: false });
      return;
    }
    const r = await this.o.host.callAction("editor", "play_in_editor", { pieAction: "stop" });
    const error = failureOf(r);
    if (error && !/already/i.test(error)) throw new Error(error);
    this.lease.update({ pie: false });
  }

  /** A call another session has in flight on PIE: a pie action, or play_in_editor. The hub's own calls are not in busy(). */
  private pieInUse(): boolean {
    const mine = this.lease.held().sessionId;
    const calls = this.o.editor?.busy().calls ?? [];
    return calls.some((c) => (c.category === "pie" || (c.category === "editor" && c.action.startsWith("play_in_editor"))) && !(c.source === "agent" && !!mine && c.sessionId === mine));
  }

  /** Wait until a condition holds, looking every PIE_POLL_MS, at most `ms`. Returns whether it held. */
  private async until(cond: () => boolean, ms: number): Promise<boolean> {
    // The wall clock, not this.now(): a test's fixed clock would never pass the end.
    const end = Date.now() + ms;
    while (!cond()) {
      if (Date.now() >= end) return false;
      await new Promise((r) => setTimeout(r, Math.min(PIE_POLL_MS, Math.max(1, ms))));
    }
    return true;
  }

  private note(text: string): void {
    this.notes.push(text);
    if (this.notes.length > 10) this.notes.shift();
    this.o.host.log.warn(`hub: ${text}`);
  }

  // ── Events ────────────────────────────────────────────────────────

  /** Gather a change into the next studio.hub.changed. */
  private changed(what: HubChangedWhat[], ids: { goalId?: string; runId?: string; planRunId?: string } = {}): void {
    const p = this.pending ?? (this.pending = { what: new Set(), goalIds: new Set(), runIds: new Set(), planIds: new Set() });
    for (const w of what) p.what.add(w);
    if (ids.goalId) p.goalIds.add(ids.goalId);
    if (ids.runId) p.runIds.add(ids.runId);
    if (ids.planRunId) p.planIds.add(ids.planRunId);
    if (this.publishTimer) return;
    this.publishTimer = setTimeout(() => this.flush(), PUBLISH_MS);
    this.publishTimer.unref?.();
  }

  /** Publish what was gathered; an id is named only when the change was about that one thing. */
  flush(): void {
    if (this.publishTimer) clearTimeout(this.publishTimer);
    this.publishTimer = null;
    const p = this.pending;
    this.pending = null;
    if (!p || p.what.size === 0) return;
    const body: HubChanged = {
      what: [...p.what],
      ...(p.goalIds.size === 1 ? { goalId: [...p.goalIds][0] } : {}),
      ...(p.runIds.size === 1 ? { runId: [...p.runIds][0] } : {}),
      ...(p.planIds.size === 1 ? { planRunId: [...p.planIds][0] } : {}),
    };
    this.o.host.events.publish(HUB_CHANGED, body as unknown as Record<string, unknown>);
  }

  /** A run the broker saved: its history and what it tested, the queue's item, the fix-loop line, and links to a goal. */
  private saw(run: QaRun): void {
    if (this.lease.get() && this.o.editor?.pie()) this.pieUp = true;
    const had = this.runs.get(run.runId);
    const rec = this.runs.record(run);
    if (rec && !had && !qaEnded(run.state)) this.captureStart(run);
    if (rec && had && !had.endedAt && rec.endedAt) this.captureEnd(run.runId);
    this.queue.observe(run);
    const goalId = run.sessionId ? this.linking.get(run.sessionId) : undefined;
    if (goalId && qaIsDraft(run) && !run.deleted && run.script !== "inline") {
      const goal = this.goals.get(goalId);
      if (goal && !goal.checks.some((c) => c.kind === "script" && c.target === qaScriptKey(run.script))) {
        try {
          this.goals.addChecks(goalId, [{ kind: "script", target: qaScriptKey(run.script) }]);
        } catch (e) {
          this.note(`the playtest written for ${goalId} could not be added to it: ${e instanceof Error ? e.message : String(e)}`);
        }
      }
    }
    this.taint(run);
    // Drafts and loops change the list without a run record changing.
    if (qaIsDraft(run) || run.assignment) this.changed(["attention", "checks"], { runId: run.runId });
    // Something that held the editor let it go: the queue may go on.
    if ((run.assignment && run.assignment.state !== "running") || qaEnded(run.state)) void this.queue.pump();
  }

  /** A run seen starting is followed for project file changes; when it ends having seen one, it is tainted. */
  private taint(run: QaRun): void {
    if (qaIsDraft(run) || run.manual || run.script === "inline") return;
    if (!qaEnded(run.state)) {
      if (run.state !== "idle") this.watch.runStarted(run.runId);
      return;
    }
    if (this.watch.runEnded(run.runId)) this.runs.setTainted(run.runId);
  }

  /** The periodic look: the queue, runs the hub may have missed the end of, backfill. */
  async tick(): Promise<void> {
    try {
      // Off: no plan starts and the editor is not asked.
      if (!this.enabled()) {
        this.watch.stop();
        return;
      }
      this.watch.start();
      if (this.connected()) await this.refreshOthers();
      await this.queue.tick();
      if (this.now() - this.backfilled > 5 * BACKFILL_MS) void this.backfill();
    } catch (e) {
      this.o.host.log.warn("hub: a tick failed", e);
    }
  }

  // ── History ───────────────────────────────────────────────────────

  /** The workspace now: read afresh, or the last reading while it is recent. */
  private workspace(fresh = false): Promise<CurrentProvenance> {
    if (!fresh && this.workspaceRead && this.now() - this.workspaceRead.at < WORKSPACE_MS) return this.workspaceRead.value;
    const read = this.o.workspace ?? (() => workspaceNow(this.o.host.projectDir));
    const value = read().catch((): CurrentProvenance => ({ vcs: null, revision: null, dirty: null }));
    this.workspaceRead = { at: this.now(), value };
    return value;
  }

  /**
   * What a run tests, read as the hub first sees it running: the workspace,
   * the script's version as pie(script_read) gives it, the engine and the
   * players the client set. A run first seen after it ended gets none, since
   * nothing here knows what it tested.
   */
  private captureStart(run: QaRun): void {
    const job = (async () => {
      const [workspace, read] = await Promise.all([
        this.workspace(true),
        this.o.host.callAction("pie", "script_read", { path: qaScriptKey(run.script) }).catch(() => null),
      ]);
      // The run as it is now: the call that started it may have landed after its first events, with the PIE setup.
      const provenance = provenanceAtStart({ workspace, scriptHash: scriptHashOf(read ? answerOf(read) : null), engine: this.o.engine?.() ?? null, run: this.o.broker.get(run.runId) ?? run });
      this.runs.setProvenance(run.runId, provenance);
    })()
      .catch((e: unknown) => this.o.host.log.warn("hub: what a run tested could not be read", e))
      .finally(() => this.capturing.delete(run.runId));
    this.capturing.set(run.runId, job);
  }

  /** A run that ended: the fields ue-mcp records itself in its results.json, where present, laid over the hub's. */
  private captureEnd(runId: string): void {
    void (this.capturing.get(runId) ?? Promise.resolve()).then(() => {
      const provenance = withUeMcp(this.runs.get(runId)?.provenance, ueMcpProvenance(readResultsFile(this.o.host.projectDir, runId), readProvenanceFile(this.o.host.projectDir, runId)));
      if (provenance) this.runs.setProvenance(runId, provenance);
    });
  }

  /** Read ue-mcp's record of runs into the history. At most one at a time. */
  backfill(force = false): Promise<void> {
    if (this.backfilling) return this.backfilling;
    if (!force && this.now() - this.backfilled < BACKFILL_MS) return Promise.resolve();
    const limit = this.firstBackfill ? FIRST_BACKFILL : BACKFILL;
    this.backfilling = (async () => {
      try {
        const recorded = await this.o.index.recorded(limit);
        for (const { run, times } of recorded) {
          // A run the broker holds is recorded when the broker saves it.
          if (this.o.broker.get(run.runId)) continue;
          this.runs.backfill(run, times);
        }
        this.firstBackfill = false;
      } catch (e) {
        this.o.host.log.warn("hub: the run history could not be backfilled", e);
      } finally {
        this.backfilled = this.now();
        this.backfilling = null;
      }
    })();
    return this.backfilling;
  }

  // ── Checks and goals ──────────────────────────────────────────────

  private async catalog(fresh = false): Promise<Catalog> {
    const connected = this.connected();
    const current = await this.workspace();
    let list: ListedScript[] | null = null;
    try {
      list = await this.o.index.listScripts(fresh);
    } catch {
      list = null;
    }
    const listed = list ? new Map(list.map((s) => [s.path, s])) : null;
    const checks: Catalog["checks"] = new Map();
    const unresolved = new Set<string>();
    for (const goal of this.goals.list()) {
      for (const named of goal.checks) {
        const ref = this.resolve(named, listed);
        const id = checkId(ref);
        if (connected && listed && !listed.has(ref.target)) unresolved.add(ref.target);
        const had = checks.get(id);
        if (had) {
          if (!goal.archived) had.goalIds.push(goal.id);
        } else {
          checks.set(id, { ref: { kind: ref.kind, target: ref.target }, goalIds: goal.archived ? [] : [goal.id], ...(ref.target !== named.target ? { renamedFrom: named.target } : {}) });
        }
      }
    }
    if (unresolved.size && this.enabled()) void this.followRedirects([...unresolved]);
    for (const s of listed?.values() ?? []) {
      const id = checkId({ kind: "script", target: s.path });
      if (!checks.has(id)) checks.set(id, { ref: { kind: "script", target: s.path }, goalIds: [] });
    }
    // Without the editor's list, the scripts the history knows stand in for it.
    if (!listed) {
      for (const id of this.runs.checkIds()) {
        const r = this.runs.byCheck(id)[0];
        if (r && !checks.has(id)) checks.set(id, { ref: { kind: r.kind, target: r.target }, goalIds: [] });
      }
    }
    for (const c of checks.values()) {
      const s = listed?.get(c.ref.target);
      if (s) Object.assign(c, { name: s.name, map: s.map ?? null });
    }
    return { checks, listed, connected, current, metas: this.goals.metas() };
  }

  /**
   * Where a script a goal names is on this checkout: its own path when the
   * editor lists it (or no list is known), else where the evidence store's
   * aliases say it moved, when that is listed. Otherwise its own path, missing.
   */
  private resolve(ref: CheckRef, listed: Map<string, ListedScript> | null): CheckRef {
    if (!listed || listed.has(ref.target)) return ref;
    const aliases = this.runs.aliases();
    let id = checkId(ref);
    for (let hop = 0; hop < REDIRECT_HOPS; hop++) {
      const next = aliases.get(id);
      const to = next ? checkOfId(next) : null;
      if (!to) break;
      if (listed.has(to.target)) return { ...to, ...(ref.required === false ? { required: false } : {}) };
      id = next!;
    }
    return ref;
  }

  /**
   * Ask ue-mcp whether a redirector stands at each path the editor does not
   * list (asset(fixup_redirectors) with dryRun: it reports and changes
   * nothing). A script that moved gets an alias in the evidence store, so its
   * history follows it; a path with none is asked about again later.
   */
  private async followRedirects(targets: string[]): Promise<void> {
    let moved = false;
    for (const from of targets) {
      const asked = this.redirectAsked.get(from);
      if (asked !== undefined && this.now() - asked < REDIRECT_RETRY_MS) continue;
      this.redirectAsked.set(from, this.now());
      const r = await this.o.host.callAction("asset", "fixup_redirectors", { paths: [from], dryRun: true }).catch(() => null);
      const found = (answerOf(r)?.redirectors as Array<{ destinationPath?: unknown }> | undefined)?.find((x) => typeof x?.destinationPath === "string");
      const to = found ? checkOfId(`script:${String(found.destinationPath)}`) : null;
      if (!to || to.target === from) continue;
      this.runs.alias(checkId({ kind: "script", target: from }), checkId(to));
      moved = true;
    }
    if (moved) this.changed(["checks", "goals", "runs"]);
  }

  /** The script is not on this checkout: only said while the editor is connected and has listed its scripts. */
  private missing(ref: CheckRef, cat: Catalog): boolean {
    return cat.connected && cat.listed !== null && !cat.listed.has(ref.target);
  }

  /** A check's state from the ref a goal (or the catalog) names: health follows the script to where it is now, under the project's one rule. */
  private stateOf(named: CheckRef, cat: Catalog, goalIds?: string[]): { state: CheckState; runs: RunRecord[] } {
    const at = this.resolve(named, cat.listed);
    const id = checkId(at);
    const entry = cat.checks.get(id);
    const runs = this.runs.byCheck(id);
    const last = runs[0];
    const renamedFrom = at.target !== named.target ? named.target : entry?.renamedFrom;
    const state = checkStateOf({
      check: { kind: at.kind, target: at.target, ...(named.required === false ? { required: false } : {}) },
      runs,
      missing: this.missing(at, cat),
      rule: this.goals.rules(),
      current: cat.current,
      now: this.now(),
      goalIds: goalIds ?? entry?.goalIds ?? [],
      meta: cat.metas.get(id) ?? (renamedFrom ? cat.metas.get(checkId({ kind: "script", target: renamedFrom })) : undefined),
      name: entry?.name ?? last?.name ?? leafName(at.target),
      map: entry?.map ?? last?.map ?? null,
      steps: last?.summary?.total ?? null,
      ...(renamedFrom ? { renamedFrom } : {}),
    });
    return { state, runs };
  }

  /** Every goal's state, rolled up through the goals each requires (shared rollUpGoals), with the store's sign-offs. */
  private goalStates(cat: Catalog): GoalState[] {
    return rollUpGoals(this.goals.list(), (goal) => goal.checks.map((ref) => this.stateOf(ref, cat)), cat.current, this.goals.signOffs());
  }

  async overview(): Promise<HubOverview> {
    if (this.now() - this.backfilled > BACKFILL_MS) void this.backfill();
    const cat = await this.catalog();
    const goals = this.goalStates(cat);
    const checks = [...cat.checks.values()].map((c) => this.stateOf(c.ref, cat).state);
    return {
      goals,
      checks,
      attention: buildAttention({ runs: this.o.broker.all(), goals, checks, plans: this.queue.state().recent, now: this.now() }),
      queue: this.queueView(),
      recent: this.runs.all().slice(0, 30),
      evidence: { store: this.runs.scope, current: cat.current },
      rules: this.goals.rules(),
      imported: this.goals.imported(),
      editor: { connected: cat.connected },
      warnings: [...this.goals.warnings(), ...this.notes],
    };
  }

  async goal(id: string): Promise<GoalState> {
    if (!this.goals.get(id)) throw notFound(`no goal ${id}`);
    const cat = await this.catalog();
    return this.goalStates(cat).find((g) => g.goal.id === id)!;
  }

  /**
   * The id a goal holds a check under, from the id a client shows: a moved
   * script shows by its new path, while the goal still names the old one.
   */
  async goalCheckId(goalId: string, id: string): Promise<string> {
    const goal = this.goals.get(goalId);
    if (!goal || goal.checks.some((c) => checkId(c) === id)) return id;
    const cat = await this.catalog();
    return goal.checks.map(checkId).find((c) => checkId(this.resolve(checkOfId(c)!, cat.listed)) === id) ?? id;
  }

  /** Change what a check says about itself. */
  updateCheckMeta(id: string, body: unknown): CheckMeta {
    const ref = checkOfId(id);
    if (!ref) throw bad("a check id is <kind>:<target>");
    return this.goals.updateMeta(ref, body);
  }

  async check(id: string, q: { limit?: unknown; before?: unknown }): Promise<CheckDetail> {
    const ref = checkOfId(id);
    if (!ref) throw bad("a check id is <kind>:<target>");
    const cat = await this.catalog();
    const known = cat.checks.get(checkId(ref));
    if (!known && this.runs.byCheck(checkId(ref)).length === 0) throw notFound(`no check ${id}`);
    const { state } = this.stateOf(known?.ref ?? ref, cat);
    const page = this.runs.page({ check: checkId(ref), limit: limitOf(q.limit, 50), before: typeof q.before === "string" ? q.before : null });
    let script: QaRun | null = null;
    if (ref.kind === "script") {
      const r = cat.connected && !state.missing ? await this.o.broker.readScriptAt(ref.target).catch(() => null) : null;
      script = r && "run" in r ? r.run : this.o.broker.all().filter((x) => qaScriptKey(x.script) === ref.target).sort((a, b) => (a.seenAt < b.seenAt ? 1 : -1))[0] ?? null;
    }
    return { state, runs: page.runs, more: page.more, script };
  }

  runsPage(q: { check?: unknown; limit?: unknown; before?: unknown }): { runs: RunRecord[]; more: boolean } {
    let check: string | null = null;
    if (q.check !== undefined && q.check !== "") {
      const ref = typeof q.check === "string" ? checkOfId(q.check) : null;
      if (!ref) throw bad("check is a check id, <kind>:<target>");
      check = checkId(ref);
    }
    return this.runs.page({ check, limit: limitOf(q.limit, 50), before: typeof q.before === "string" ? q.before : null });
  }

  run(runId: string): RunDetail {
    const live = this.o.broker.get(runId);
    const run = live && !qaIsDraft(live) ? live : readResults(this.o.host.projectDir, runId);
    const record = this.runs.get(runId) ?? (run ? this.runs.record(run) : null);
    if (!record) throw notFound(`no run ${runId}`);
    const steps: RunStepSpan[] = (run?.steps ?? []).map((s) => ({ index: s.index, instruction: s.instruction, status: s.status, judge: s.judge, startMs: s.startedAtMs ?? null, endMs: s.endedAtMs ?? null }));
    const history = this.runs.byCheck(checkId(record));
    // Only a pass of the same script version compares step by step: across versions, step 3 may be another step.
    const version = record.provenance?.scriptHash;
    const lastPass = version ? history.find((r) => r.runId !== runId && isResult(r) && r.outcome === "passed" && r.startedAt < record.startedAt && r.provenance?.scriptHash === version) ?? null : null;
    const series = readSeries(this.o.host.projectDir, runId);
    let changedSteps: number[] | undefined;
    if (lastPass) {
      const now = new Set(record.failedSteps ?? []);
      const then = new Set(lastPass.failedSteps ?? []);
      changedSteps = [...new Set([...now, ...then])].filter((i) => now.has(i) !== then.has(i)).sort((a, b) => a - b);
    }
    return {
      record,
      run: run ?? null,
      steps,
      ...(series ? { series } : {}),
      lastPass,
      ...(changedSteps ? { changedSteps } : {}),
      resultsUrl: `/v1/hub/runs/${encodeURIComponent(runId)}/results`,
    };
  }

  /** A run's record: from the history, else from its results.json, kept from then on. */
  private recordFor(runId: string): RunRecord {
    const had = this.runs.get(runId);
    if (had) return had;
    const live = this.o.broker.get(runId);
    const run = live && !qaIsDraft(live) ? live : readResults(this.o.host.projectDir, runId);
    const record = run ? this.runs.record(run) : null;
    if (!record) throw notFound(`no run ${runId}`);
    return record;
  }

  /** Link a run to a tracker item. A link to a URL the run already has replaces it. */
  addRunLink(runId: string, body: unknown): RunRecord {
    const link = linkOf(isObject(body) && body.link !== undefined ? body.link : body);
    const had = this.recordFor(runId);
    const rest = (had.links ?? []).filter((l) => l.url !== link.url);
    if (rest.length >= TRACKER_LINKS_MAX) throw bad(`a run holds at most ${TRACKER_LINKS_MAX} links`);
    return this.runs.setLinks(runId, [...rest, link])!;
  }

  /** Remove a run's link to this URL. */
  removeRunLink(runId: string, url: unknown): RunRecord {
    if (typeof url !== "string" || !url) throw bad("url is the link to remove");
    const had = this.recordFor(runId);
    const links = had.links ?? [];
    if (!links.some((l) => l.url === url)) throw notFound(`run ${runId} has no link to ${url}`);
    return this.runs.setLinks(runId, links.filter((l) => l.url !== url))!;
  }

  // ── Goals ─────────────────────────────────────────────────────────

  /**
   * Sign a goal off as the signed-in account, from this computer or from the
   * paired browser named by `via`. It records the revision, the results of
   * each required check the acceptance rule looks at, and the goal's
   * requirements, so it lapses once those no longer hold (signOffLapse).
   */
  async signOff(id: string, body: unknown, via?: string): Promise<SignOff> {
    const goal = this.goals.get(id);
    if (!goal) throw notFound(`no goal ${id}`);
    if (body !== undefined && body !== null && !isObject(body)) throw bad("body must be { note? }");
    const note = isObject(body) ? body.note : undefined;
    if (note !== undefined && typeof note !== "string") throw bad("note must be a string");
    const by = (await this.o.signer?.()) ?? null;
    if (!by) throw conflict("Sign in to your ue-mcp account to sign off a goal: a sign-off records who gave it.");
    const current = await this.workspace(true);
    const need = Math.max(1, goal.acceptance.stableRuns);
    const cat = await this.catalog();
    const runIds = goal.checks
      .filter((c) => c.required !== false)
      .flatMap((c) => this.runs.byCheck(checkId(this.resolve(c, cat.listed))).filter(isResult).slice(0, need).map((r) => r.runId));
    const s: SignOff = {
      goalId: id,
      by,
      ...(via ? { via } : {}),
      at: new Date(this.now()).toISOString(),
      ...(note?.trim() ? { note: note.trim().slice(0, 2000) } : {}),
      revision: current.revision,
      runIds,
      requirements: requirementsKey(goal),
    };
    this.goals.signOff(s);
    return s;
  }

  clearSignOff(id: string): void {
    if (!this.goals.get(id)) throw notFound(`no goal ${id}`);
    this.goals.clearSignOff(id);
  }

  /**
   * Delete a goal or milestone, with its sign-off. Goals that required it stop
   * requiring it. Its playtests stay as they are: their own records, the other
   * goals that name them, and every past run. While a plan made for it is
   * queued or running, the delete is refused (409) and names the plan, since
   * that plan was started from the goal.
   */
  deleteGoal(id: string): void {
    const goal = this.goals.get(id);
    if (!goal) throw notFound(`no goal ${id}`);
    const q = this.queue.state();
    const plans = [q.current, ...q.queued].filter((p): p is PlanRun => !!p && "goalId" in p.scope && p.scope.goalId === id);
    if (plans.length) {
      const names = plans.map((p) => `"${p.title}"`).join(", ");
      // A person reads plans as runs: Studio's queue lists them under Runs.
      throw conflict(`"${goal.title}" can't be deleted while ${plans.length === 1 ? "a run made for it is" : "runs made for it are"} queued or running: ${names}. Cancel ${plans.length === 1 ? "that run" : "those runs"} or let ${plans.length === 1 ? "it" : "them"} finish, then try again.`);
    }
    this.goals.delete(id);
  }

  /**
   * Write playtests for this goal: a session of its own runs the /playtest
   * flow, seeded with the goal's words and the checks it has, and each script
   * drafted in that session joins the goal.
   */
  writePlaytests(id: string, body: unknown): { sessionId: string } {
    const goal = this.goals.get(id);
    if (!goal) throw notFound(`no goal ${id}`);
    if (body !== undefined && body !== null && !isObject(body)) throw bad("body must be { text? }");
    const asked = isObject(body) ? body.text : undefined;
    if (asked !== undefined && typeof asked !== "string") throw bad("text must be a string");
    const write = this.o.writePlaytests;
    if (!write) throw conflict("no client is connected that can write playtests: it needs a playtest flow of its own");
    const text = (asked as string | undefined)?.trim() || playtestBrief(goal, this.goals.metas());
    const started = write({ title: `Playtests for ${goal.title}`.slice(0, 80), text });
    this.linking.set(started.sessionId, goal.id);
    void started.done.catch((e: unknown) => this.note(`writing playtests for ${goal.id} failed: ${e instanceof Error ? e.message : String(e)}`));
    return { sessionId: started.sessionId };
  }

  // ── Plans ─────────────────────────────────────────────────────────

  queueView(): QueueState {
    return this.queue.state();
  }

  /** The checks a scope stands for, in order, each once. */
  private async expand(scope: PlanScope): Promise<CheckRef[]> {
    const cat = await this.catalog();
    // A goal stands for its required goals' checks, at any depth, then its own.
    const goalChecks = (goalId: string): CheckRef[] => {
      if (!this.goals.get(goalId)) throw notFound(`no goal ${goalId}`);
      return goalChecksDeep(this.goals.list(), goalId);
    };
    let refs: CheckRef[];
    switch (scope.kind) {
      case "goal":
        refs = goalChecks(scope.goalId);
        break;
      case "checks":
        refs = scope.checks;
        break;
      case "all":
        refs = [...cat.checks.values()].map((c) => c.ref).filter((r) => !this.missing(r, cat));
        break;
      case "failing":
      case "stale": {
        const pool = scope.goalId ? goalChecks(scope.goalId) : [...cat.checks.values()].map((c) => c.ref);
        // Stale takes the never run too: both lack recent evidence.
        const wanted = scope.kind === "failing" ? ["failing"] : ["stale", "never_run"];
        refs = pool.filter((r) => wanted.includes(this.stateOf(r, cat).state.health));
        break;
      }
    }
    const seen = new Set<string>();
    // A script that moved runs where it is now.
    return refs.map((r) => this.resolve(r, cat.listed)).filter((r) => {
      const id = checkId(r);
      if (seen.has(id)) return false;
      seen.add(id);
      return true;
    });
  }

  /** A playtest's name as people read it: its own title, the script's name, or the asset's. */
  private nameOf(ref: CheckRef, cat: Catalog): string {
    const id = checkId(ref);
    return cat.metas.get(id)?.title ?? cat.checks.get(id)?.name ?? leafName(ref.target);
  }

  private titleOf(scope: PlanScope, refs: CheckRef[], cat: Catalog): string {
    const goal = "goalId" in scope && scope.goalId ? this.goals.get(scope.goalId)?.title : undefined;
    switch (scope.kind) {
      case "goal":
        return `Run ${goal ?? scope.goalId}`;
      case "failing":
        return goal ? `Run what is failing in ${goal}` : "Run what is failing";
      case "stale":
        return goal ? `Run what is stale in ${goal}` : "Run what is stale";
      case "all":
        return "Run all playtests";
      case "checks":
        return refs.length === 1 ? `Run ${this.nameOf(refs[0], cat)}` : `Run ${refs.length} playtests`;
    }
  }

  /** A plan run queued: its scope expanded to items now, so what it runs does not change under it. */
  async plan(body: unknown, trigger: PlanTrigger = "person"): Promise<PlanRun> {
    return (await this.queuePlan(body, trigger)).plan;
  }

  /**
   * A plan queued, and where it went. A person's plan resumes a paused queue
   * and starts at once when nothing is on the editor (PlanQueue.enqueue).
   */
  async queuePlan(body: unknown, trigger: PlanTrigger = "person"): Promise<{ plan: PlanRun; placement: PlanPlacement }> {
    if (!isObject(body)) throw bad("body must be { title?, scope, options?, next? }");
    if (body.next !== undefined && typeof body.next !== "boolean") throw bad("next must be a boolean");
    if (body.title !== undefined && (typeof body.title !== "string" || !body.title.trim())) throw bad("title must be a non-empty string");
    // A person's plan is the user's run; an agent's only under Automatic.
    if (trigger === "agent" && (this.o.playtestRuns?.() ?? "automatic") === "manual") throw conflict("Playtest runs are set to Manual: an agent cannot start a plan");
    const scope = scopeOf(body.scope);
    const options = planOptionsOf(body.options, DEFAULT_PLAN_OPTIONS);
    const refs = await this.expand(scope);
    if (refs.length === 0) throw conflict("nothing to run: the plan's scope has no playtests");
    const cat = await this.catalog();
    const items: PlanItem[] = refs.map((ref) => ({ check: ref, name: this.nameOf(ref, cat), state: "queued" as const }));
    const plan: PlanRun = {
      id: newId("plan"),
      title: typeof body.title === "string" ? body.title.trim().slice(0, 200) : this.titleOf(scope, refs, cat),
      scope,
      options,
      state: "queued",
      items,
      trigger,
      createdAt: new Date(this.now()).toISOString(),
    };
    const placement = this.queue.add(plan, { person: trigger === "person", next: body.next === true });
    return { plan, placement };
  }

  /** A person's Run on one playtest: a plan of that one check. */
  async runCheck(id: string, body: unknown): Promise<{ plan: PlanRun; placement: PlanPlacement }> {
    const ref = checkOfId(id);
    if (!ref) throw bad("a check id is <kind>:<target>");
    if (body !== undefined && body !== null && !isObject(body)) throw bad("body must be { replay?, next? }");
    const replay = isObject(body) ? body.replay : undefined;
    const next = isObject(body) ? body.next : undefined;
    if (replay !== undefined && typeof replay !== "boolean") throw bad("replay must be a boolean");
    return this.queuePlan({ scope: { kind: "checks", checks: [ref] }, options: { replay: replay === true }, ...(next !== undefined ? { next } : {}) });
  }

  async cancelPlan(id: string): Promise<PlanRun> {
    return this.queue.cancel(id);
  }

  /** Reorder the queue: a queued plan to { to }, 0 for next. */
  movePlan(id: string, body: unknown): QueueState {
    const to = isObject(body) ? body.to : undefined;
    if (!Number.isInteger(to) || (to as number) < 0) throw bad("body must be { to: a position from 0 }");
    this.queue.move(id, to as number);
    return this.queueView();
  }

  setPaused(body: unknown): QueueState {
    const paused = isObject(body) ? body.paused : undefined;
    if (typeof paused !== "boolean") throw bad("body must be { paused: boolean }");
    this.queue.setPaused(paused);
    return this.queueView();
  }

  /** Start one plan item on the editor: its script's card run, or a replay of its last recording. */
  private async startItem(item: PlanItem, plan: PlanRun): Promise<ItemStart> {
    const script = item.check.target;
    const listed = this.connected() ? await this.o.index.listScripts().catch(() => null) : null;
    if (listed && !listed.some((s) => s.path === script)) return { skipped: "the playtest is not on this checkout" };
    // A manual step needs someone watching it play. A replay is no way around that: it would copy an old run's verdict.
    if (!plan.options.attended) {
      const read = await this.o.broker.readScriptAt(script);
      if ("error" in read) return { error: `the script could not be read: ${read.error}` };
      if (needsPerson(read.run.steps)) return { skipped: NEEDS_PERSON };
    }
    const tag = { source: "plan" as const, planRunId: plan.id };
    const src = plan.options.replay ? this.runs.byCheck(checkId(item.check)).find((r) => r.recordingId && r.outcome !== "running" && r.outcome !== "needs_you") : undefined;
    if (src) {
      const args = { run_id: src.recordingId ?? src.runId };
      const r = await (this.o.replay ? this.o.replay(script, args) : this.o.host.callAction("pie", "script_replay", args));
      const body = answerOf(r);
      if (!body || typeof body.run_id !== "string") return { error: `the replay did not start: ${failureOf(r) ?? "ue-mcp started no run"}` };
      this.runs.tag(body.run_id, tag);
      // pie(script_replay) always starts PIE, and ends it itself only when the replay finishes: a stopped one is the hub's to end.
      return { runId: body.run_id, pieStarted: body.pie_started === true };
    }
    const r = await this.o.broker.startScript(script, { planRunId: plan.id, title: plan.title });
    if ("error" in r) return { error: r.error };
    this.runs.tag(r.run.runId, tag);
    return { runId: r.run.runId, pieStarted: r.run.pieStarted === true };
  }

  /** Stop a run the hub started through the broker, so a verdict hold is released with it (and PIE, when the run started it). */
  private async stopRun(runId: string, reason: string): Promise<void> {
    const r = await this.o.broker.stopRun(runId, reason);
    if ("error" in r && !/already|only a run that is going/i.test(r.error)) throw new Error(r.error);
  }

  // ── Fix with an agent ─────────────────────────────────────────────

  /**
   * Fix with an agent (FixRequest): a fix plan in the queue, one item per
   * failing playtest of a goal or of the project, or for one playtest, each
   * a fix loop a client runs (HubFixer), one at a time under the editor's
   * lease like any plan. A playtest already queued for a fix, or whose loop
   * runs, is left out. Returns how many were queued. Without a fixer there
   * is no agent to run a loop: 409.
   */
  async fix(body: unknown): Promise<{ queued: number; plan: PlanRun | null; placement?: PlanPlacement }> {
    if (body !== undefined && body !== null && !isObject(body)) throw bad("body must be { goalId?, checkId? }");
    const goalId = isObject(body) ? body.goalId : undefined;
    const one = isObject(body) ? body.checkId : undefined;
    if (goalId !== undefined && typeof goalId !== "string") throw bad("goalId must be a goal's id");
    if (one !== undefined && (typeof one !== "string" || !checkOfId(one))) throw bad("checkId must be a playtest's id, script:<asset path>");
    if (goalId !== undefined && one !== undefined) throw bad("give goalId or checkId, not both");
    if (!this.o.fixer) throw conflict("no client is connected that can fix playtests: a fix loop needs an agent of its own");
    const refs = typeof one === "string" ? [checkOfId(one)!] : await this.expand(goalId ? { kind: "failing", goalId } : { kind: "failing" });
    const q = this.queue.state();
    const queued = new Set([q.current, ...q.queued].filter((p): p is PlanRun => p?.work === "fix").flatMap((p) => p.items.filter((i) => i.state === "queued" || i.state === "running").map((i) => i.check.target)));
    const running = this.o.fixer.running();
    const fresh = refs.filter((r) => !queued.has(r.target) && !running.has(r.target));
    if (fresh.length === 0) return { queued: 0, plan: null };
    const cat = await this.catalog();
    const goal = typeof goalId === "string" ? this.goals.get(goalId)?.title : undefined;
    const plan: PlanRun = {
      id: newId("plan"),
      title: typeof one === "string" ? `Fix ${this.nameOf(fresh[0], cat)}` : goal ? `Fix what is failing in ${goal}` : "Fix what is failing",
      work: "fix",
      scope: typeof one === "string" ? { kind: "checks", checks: fresh } : goalId ? { kind: "failing", goalId } : { kind: "failing" },
      options: { ...DEFAULT_PLAN_OPTIONS },
      state: "queued",
      items: fresh.map((ref) => ({ check: ref, name: this.nameOf(ref, cat), state: "queued" as const })),
      trigger: "person",
      createdAt: new Date(this.now()).toISOString(),
    };
    this.changed(["attention"]);
    // A person asked for it: it resumes a paused queue like their Run.
    const placement = this.queue.add(plan, { person: true });
    return { queued: fresh.length, plan, placement };
  }

  /**
   * Start one fix item: the client's loop for the script (HubFixer.start).
   * The item follows the loop until it passes, stops for the person, or ends.
   */
  private async startFix(item: PlanItem, plan: PlanRun): Promise<ItemStart> {
    if (!this.o.fixer) return { error: "no client is connected that can fix playtests" };
    return this.o.fixer.start(item, plan);
  }

  /** Stop a fix item's loop: its running attempt stops, and is checkpointed so it can be reverted. */
  private stopFix(item: PlanItem): void {
    const error = this.o.fixer?.stop(item);
    if (error) this.note(`the fix loop for ${item.name} could not be stopped: ${error}`);
  }
}

/** A page size from a query: a whole number up to MAX_PAGE, else the default. */
function limitOf(v: unknown, fallback: number): number {
  const n = Number(v);
  return Number.isInteger(n) && n > 0 ? Math.min(MAX_PAGE, n) : fallback;
}

/** What the /playtest flow is told for Write playtests: the goal's words and the playtests it already has, by name. */
export function playtestBrief(goal: Goal, metas: ReadonlyMap<string, CheckMeta> = new Map()): string {
  const have = goal.checks.map((c) => metas.get(checkId(c))?.title ?? leafName(c.target));
  return [
    `Write a playtest for the goal "${goal.title}".`,
    goal.description ? `What the goal means: ${goal.description}` : "",
    have.length ? `It already has these playtests: ${have.join(", ")}. Check something they do not.` : "",
  ].filter(Boolean).join("\n\n");
}
