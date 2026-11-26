// @session per-session: Plays one project's track and scene runs on its editor and remembers the runs it started.
/**
 * Plays a track or a scene (#2113): the composition's segments one after
 * another in one PIE session, a clip segment with pie(clip_run) and an inline
 * clip, a step-list segment with pie(script_run). It follows the segment on
 * the editor by polling its status, starts the next once it ends, and tells
 * the hub of the run as a QaRun whose steps are the instances, so the queue,
 * the history and status treat it like any run.
 *
 * Only the first segment may start PIE. A later segment that would have to
 * start it again (PIE ended between segments) ends the run instead, since a
 * fresh session is not the game the track was playing.
 */
import * as crypto from "node:crypto";
import {
  instanceResults,
  outcomeOf,
  qaEnded,
  qaRunFromAnswer,
  rollUpInstances,
  type ArrangementComposition,
  type ArrangementRun,
  type ArrangementRunKind,
  type InstanceResult,
  type Performer,
  type QaRun,
  type QaStepStatus,
  type SegmentState,
} from "./contract.js";
import type { HubHost } from "./ports.js";
import { answerOf, failureOf } from "./util.js";

type Host = Pick<HubHost, "callAction" | "log">;

export interface ArrangementStart {
  kind: ArrangementRunKind;
  /** The track or scene; "arrangement"; or "<scene>/<track>" for a slot. */
  goalId: string;
  title: string;
  composition: ArrangementComposition;
  /** A scene's tracks, for their titles and players in the results. */
  tracks?: Array<{ goalId: string; title: string; player: number; performer?: Performer }>;
  /** The frame the timelines played from (at 60 per second); the composition counts from there. */
  from?: number;
  /** The map to open before Play In Editor starts, when the editor has another one open: the arrangement's level. */
  level?: string;
}

interface Live {
  runId: string;
  start: ArrangementStart;
  segs: SegmentState[];
  current: number;
  state: QaRun["state"];
  stopReason?: string;
  pieStarted: boolean;
  seenAt: string;
  timer: NodeJS.Timeout | null;
  polling: boolean;
  run: QaRun;
  version: string;
}

/** How often the segment on the editor is read. */
export const ARRANGEMENT_POLL_MS = 500;

const STEP_OF: Record<InstanceResult["status"], QaStepStatus> = { pending: "pending", running: "running", passed: "passed", failed: "failed", skipped: "skipped", awaiting_verdict: "awaiting_verdict" };

export class ArrangementRunner {
  private readonly runs = new Map<string, Live>();
  private readonly listeners = new Set<(run: QaRun) => void>();

  constructor(private readonly host: Host, private readonly o: { now?: () => string; pollMs?: number } = {}) {}

  private now(): string {
    return this.o.now?.() ?? new Date().toISOString();
  }

  all(): QaRun[] {
    return [...this.runs.values()].map((l) => l.run);
  }

  get(runId: string): QaRun | undefined {
    return this.runs.get(runId)?.run;
  }

  /** Whether the run is one this runner played: the queue reads and stops it here. */
  owns(runId: string): boolean {
    return this.runs.has(runId);
  }

  onSaved(listener: (run: QaRun) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  dispose(): void {
    for (const l of this.runs.values()) if (l.timer) clearInterval(l.timer);
  }

  /** Start a run: its first segment is on the editor when this answers. */
  async start(s: ArrangementStart): Promise<{ run: QaRun } | { error: string }> {
    const segments = s.composition.segments;
    if (segments.length === 0) return { error: `${s.title} has nothing on it to play` };
    const runId = `${s.kind}_${s.goalId.replace(/[^A-Za-z0-9-]/g, "_").slice(0, 40)}_${Date.now().toString(36)}`;
    const live: Live = {
      runId,
      start: s,
      segs: segments.map(() => ({ runId: null, state: "pending" })),
      current: 0,
      state: "armed",
      pieStarted: false,
      seenAt: this.now(),
      timer: null,
      polling: false,
      run: undefined as unknown as QaRun,
      version: crypto.createHash("sha256").update(JSON.stringify(segments)).digest("hex").slice(0, 16),
    };
    live.run = this.toRun(live);
    if (s.level) {
      const why = await this.openLevel(s.level);
      if (why) return { error: why };
    }
    const first = await this.startSegment(live, 0);
    if (first) return { error: first };
    this.runs.set(runId, live);
    live.timer = setInterval(() => void this.poll(runId), this.o.pollMs ?? ARRANGEMENT_POLL_MS);
    live.timer.unref?.();
    this.save(live);
    return { run: live.run };
  }

  /**
   * Open the arrangement's level unless the editor has it open already. It
   * never drops unsaved work: a map with unsaved changes refuses the switch,
   * and the run does not start, saying why.
   */
  private async openLevel(level: string): Promise<string | null> {
    const current = answerOf(await this.host.callAction("level", "get_current", {}).catch(() => null));
    const open = typeof current?.mapPackagePath === "string" ? current.mapPackagePath : typeof current?.levelPath === "string" ? current.levelPath.replace(/\.[^/]*$/, "") : null;
    if (open === level) return null;
    const r = await this.host.callAction("level", "load", { levelPath: level });
    const error = failureOf(r);
    return error ? `the arrangement's level ${level} did not open: ${error}` : null;
  }

  /** Start segment n. Returns why it did not start, or null. */
  private async startSegment(l: Live, n: number): Promise<string | null> {
    const seg = l.start.composition.segments[n];
    l.current = n;
    const name = `${l.start.title} ${n + 1}/${l.start.composition.segments.length}`.slice(0, 120);
    const args = seg.kind === "clip"
      ? { clip: seg.clip, name, start_pie: true, keep_pie: true }
      : { script: seg.target, start_pie: true, keep_pie: true, record: true };
    const r = await this.host.callAction("pie", seg.kind === "clip" ? "clip_run" : "script_run", args);
    const body = answerOf(r);
    const error = failureOf(r);
    if (error || !body || typeof body.run_id !== "string") {
      l.segs[n] = { runId: null, state: "error", reason: error ?? "ue-mcp started no run" };
      return `segment ${n + 1} (${seg.kind === "clip" ? "clips" : seg.target}) did not start: ${error ?? "ue-mcp started no run"}`;
    }
    const startedPie = body.pie_started === true;
    if (n === 0) l.pieStarted = startedPie;
    else if (startedPie) {
      // PIE had ended: the game the track was playing is gone.
      l.segs[n] = { runId: body.run_id, state: "error", reason: "Play In Editor ended before this segment" };
      await this.host.callAction("pie", seg.kind === "clip" ? "clip_stop" : "script_stop", { run_id: body.run_id, reason: "PIE ended between segments" }).catch(() => undefined);
      return "Play In Editor ended between segments";
    }
    l.segs[n] = { runId: body.run_id, state: "running", ...(seg.kind === "clip" ? { position: 0, checks: {} } : {}) };
    l.state = "running";
    return null;
  }

  /** Read the segment on the editor, and move on when it ended. */
  private async poll(runId: string): Promise<void> {
    const l = this.runs.get(runId);
    if (!l || l.polling || qaEnded(l.state)) return;
    l.polling = true;
    try {
      await this.read(l);
    } catch (e) {
      this.host.log.warn(`hub: reading ${runId} failed`, e);
    } finally {
      l.polling = false;
    }
  }

  private async read(l: Live): Promise<void> {
    const n = l.current;
    const seg = l.start.composition.segments[n];
    const st = l.segs[n];
    if (!st.runId || st.state !== "running") return;
    const r = await this.host.callAction("pie", seg.kind === "clip" ? "clip_status" : "script_status", { run_id: st.runId });
    const body = answerOf(r);
    if (!body || failureOf(r)) return;
    let ended = false;
    let stopReason: string | undefined;
    if (seg.kind === "clip") {
      const checks: Record<string, string> = {};
      for (const c of Array.isArray(body.checks) ? (body.checks as Array<Record<string, unknown>>) : []) if (typeof c.id === "string" && typeof c.status === "string") checks[c.id] = c.status;
      const state = typeof body.state === "string" ? body.state : "running";
      ended = state === "finished" || state === "stopped" || state === "idle";
      stopReason = typeof body.stop_reason === "string" ? body.stop_reason : undefined;
      l.segs[n] = { ...st, position: typeof body.position === "number" ? body.position : st.position, checks, state: ended ? (state === "finished" ? "finished" : "stopped") : "running", ...(stopReason ? { reason: stopReason } : {}) };
      l.state = Array.isArray(body.awaiting) && body.awaiting.length > 0 ? "awaiting_verdict" : "running";
    } else {
      const run = qaRunFromAnswer({ ...body, script: seg.target }, { seenAt: this.now() });
      const outcome = run ? outcomeOf(run) : "running";
      ended = !!run && qaEnded(run.state);
      stopReason = run?.stopReason;
      const awaiting = (run?.steps ?? []).filter((s) => s.status === "awaiting_verdict").map((s) => ({ check: String(s.index), instruction: s.instruction }));
      l.segs[n] = { ...st, outcome, awaiting, state: ended ? (run!.state === "stopped" ? "stopped" : "finished") : "running", ...(stopReason ? { reason: stopReason } : {}) };
      l.state = outcome === "needs_you" ? "awaiting_verdict" : "running";
    }
    if (ended) {
      const last = n + 1 >= l.start.composition.segments.length;
      // A clip segment that stopped on its own (PIE ended) ends the run; a step-list one that failed does not.
      const broke = seg.kind === "clip" && l.segs[n].state === "stopped";
      if (last || broke) this.finish(l, "finished", broke ? stopReason ?? "the segment stopped" : undefined);
      else {
        const why = await this.startSegment(l, n + 1);
        if (why) this.finish(l, "finished", why);
      }
    }
    this.save(l);
  }

  private finish(l: Live, state: "finished" | "stopped", reason?: string): void {
    l.state = state;
    if (reason) l.stopReason = reason;
    if (l.timer) clearInterval(l.timer);
    l.timer = null;
  }

  /** Read the run again now, for when the queue looks. */
  async refresh(runId: string): Promise<void> {
    await this.poll(runId);
  }

  /** Stop the run and the segment on the editor. PIE it started is the hub's to end (QaRun.pieStarted). */
  async stop(runId: string, reason: string): Promise<{ run: QaRun } | { error: string; status?: number }> {
    const l = this.runs.get(runId);
    if (!l || qaEnded(l.state)) return { error: "only a run that is going can be stopped", status: 409 };
    const n = l.current;
    const seg = l.start.composition.segments[n];
    const st = l.segs[n];
    if (st.runId && st.state === "running") {
      const r = await this.host.callAction("pie", seg.kind === "clip" ? "clip_stop" : "script_stop", { run_id: st.runId, reason });
      const error = failureOf(r);
      if (error && !/not active|no .*run|already/i.test(error)) this.host.log.warn(`hub: stopping ${st.runId} answered: ${error}`);
      l.segs[n] = { ...st, state: "stopped", reason };
    }
    this.finish(l, "stopped", reason);
    this.save(l);
    return { run: l.run };
  }

  /** A verdict for a manual check: a clip's check by its composed id, or a step-list instance's step by index. */
  async verdict(runId: string, check: string, verdict: string, note?: string): Promise<{ ok: true } | { error: string; status?: number }> {
    const l = this.runs.get(runId);
    if (!l || qaEnded(l.state)) return { error: "the run is not going", status: 409 };
    const seg = l.start.composition.segments[l.current];
    const st = l.segs[l.current];
    if (!st.runId) return { error: "the run has nothing on the editor to judge", status: 409 };
    const r = seg.kind === "clip"
      ? await this.host.callAction("pie", "clip_verdict", { check, verdict, ...(note ? { note } : {}), source: "human" })
      : await this.host.callAction("pie", "verdict", { run_id: st.runId, step: Number(check), verdict, ...(note ? { note } : {}), source: "human" });
    const error = failureOf(r);
    if (error) return { error, status: 409 };
    await this.poll(runId);
    return { ok: true };
  }

  /** The run as clients draw it. */
  detail(runId: string): ArrangementRun | undefined {
    const l = this.runs.get(runId);
    return l ? this.arrangementOf(l) : undefined;
  }

  private arrangementOf(l: Live): ArrangementRun {
    const c = l.start.composition;
    // A run from a frame reports at the places on the timelines, so a client draws it where the clips are.
    const shift = l.start.from ? (l.start.from * c.rate) / 60 : 0;
    const instances = instanceResults(c, l.segs).map((i) => (shift ? { ...i, start: i.start + shift, end: i.end + shift } : i));
    return {
      kind: l.start.kind,
      goalId: l.start.goalId,
      rate: c.rate,
      length: c.length + shift,
      ...(shift ? { from: shift } : {}),
      version: l.version,
      instances,
      ...(l.start.tracks?.length
        ? { tracks: (l.start.tracks ?? []).map((t) => ({ ...t, status: rollUpInstances(instances.filter((i) => i.goalId === t.goalId)) })) }
        : {}),
      segments: c.segments.map((s, index) => ({ index, kind: s.kind, runId: l.segs[index].runId, offset: s.offset + shift, length: s.length, state: l.segs[index].state })),
    };
  }

  private toRun(l: Live): QaRun {
    const a = this.arrangementOf(l);
    const ended = qaEnded(l.state);
    const steps = a.instances.map((inst, index) => ({
      index,
      instruction: `${inst.name} at ${(inst.start / a.rate).toFixed(1)} s on ${l.start.tracks?.find((t) => t.goalId === inst.goalId)?.title ?? inst.goalId}`,
      judge: (l.start.composition.instances[index]?.manual ? "manual" : "auto") as "manual" | "auto",
      status: STEP_OF[inst.status],
    }));
    const n = (s: QaStepStatus) => steps.filter((x) => x.status === s).length;
    return {
      runId: l.runId,
      script: `${l.start.kind}:${l.start.goalId}`,
      scriptName: l.start.title,
      state: ended ? l.state : l.state === "armed" ? "armed" : l.state,
      steps,
      awaiting: steps.filter((s) => s.status === "awaiting_verdict").map((s) => s.index),
      lastSeq: 0,
      summary: { total: steps.length, passed: n("passed"), failed: n("failed"), skipped: n("skipped"), pending: steps.length - n("passed") - n("failed") - n("skipped"), awaiting: n("awaiting_verdict") },
      resultsPath: null,
      sessionId: null,
      toolUseId: null,
      seenAt: l.seenAt,
      pieStarted: l.pieStarted,
      ...(l.stopReason ? { stopReason: l.stopReason } : {}),
      arrangement: a,
    };
  }

  private save(l: Live): void {
    l.run = this.toRun(l);
    for (const listener of this.listeners) {
      try {
        listener(l.run);
      } catch (e) {
        this.host.log.warn("hub: a run listener failed", e);
      }
    }
  }
}
