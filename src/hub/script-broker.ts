// @session per-session: Starts and follows script runs through one project's editor and remembers the runs it started.
/**
 * ue-mcp's own HubBroker: starts a playtest script with pie(script_run),
 * follows it with pie(script_status) (the hub's queue asks for it on each
 * tick), and stops it with pie(script_stop). It holds the runs it saw as
 * QaRuns and tells the hub whenever one is saved.
 *
 * It starts runs in a PIE session of its own (start_pie), which the run then
 * owns (QaRun.pieStarted): the hub ends that session with the run and never
 * any other. A client with a richer idea of a run, such as Studio's cards in
 * chat sessions, supplies its own HubBroker instead.
 */
import {
  qaDraftFromScript,
  qaEnded,
  qaMergeAnswer,
  qaRunFromAnswer,
  type QaRun,
} from "./contract.js";
import type { HubBroker, HubHost } from "./ports.js";
import type { VerdictHold } from "./verdict-hold.js";
import { answerOf, failureOf } from "./util.js";

type Host = Pick<HubHost, "callAction" | "log">;

export class ScriptRunBroker implements HubBroker {
  private readonly runs = new Map<string, QaRun>();
  private readonly listeners = new Set<(run: QaRun, replaces?: string) => void>();
  private planWait: ((run: QaRun) => number | undefined) | null = null;
  private hold: VerdictHold | null = null;

  constructor(private readonly host: Host, private readonly now: () => string = () => new Date().toISOString()) {}

  all(): QaRun[] {
    return [...this.runs.values()];
  }

  get(runId: string): QaRun | undefined {
    return this.runs.get(runId);
  }

  onSaved(listener: (run: QaRun, replaces?: string) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  setPlanWait(wait: (run: QaRun) => number | undefined): void {
    this.planWait = wait;
  }

  /** A plan's own verdict wait for a run, in minutes; for a hold that applies it. */
  waitMinutesOf(run: QaRun): number | undefined {
    return this.planWait?.(run);
  }

  /** Whether the broker started the run, which makes it the hub's to hold. */
  owns(run: QaRun): boolean {
    return this.runs.has(run.runId);
  }

  /** Hold runs at manual steps for a person's verdict (verdict-hold.ts). Its stop() should call this broker's stopRun. */
  attachHold(hold: VerdictHold): void {
    this.hold = hold;
  }

  private save(run: QaRun): void {
    this.runs.set(run.runId, run);
    this.hold?.observe(run);
    for (const l of this.listeners) {
      try {
        l(run);
      } catch (e) {
        this.host.log.warn("hub: a run listener failed", e);
      }
    }
  }

  async status(runId: string): Promise<{ answer: Record<string, unknown>; run: QaRun | null } | { error: string }> {
    const had = this.runs.get(runId);
    // A run this broker started has events to follow; another client's is read from its results.
    const args: Record<string, unknown> = { run_id: runId };
    if (had) args.since_seq = had.lastSeq;
    const r = await this.host.callAction("pie", had ? "script_status" : "script_results", args);
    const body = answerOf(r);
    const error = failureOf(r);
    if (error || !body) return { error: error ?? "ue-mcp's answer was not JSON" };
    const base = had ?? qaRunFromAnswer(body, { seenAt: this.now() });
    if (!base) return { answer: body, run: null };
    const run = qaMergeAnswer(base, body);
    if (body.state === "idle" && Array.isArray(body.steps) && body.steps.length === 0) return { answer: body, run: had ?? null };
    this.save(run);
    return { answer: body, run };
  }

  async readScriptAt(script: string): Promise<{ run: QaRun } | { error: string; status?: number }> {
    const r = await this.host.callAction("pie", "script_read", { path: script });
    const body = answerOf(r);
    const error = failureOf(r);
    if (error) return { error, status: 409 };
    const run = qaDraftFromScript(body, { seenAt: this.now() });
    return run ? { run } : { error: "the script has no steps to read", status: 409 };
  }

  async startScript(script: string): Promise<{ run: QaRun } | { error: string }> {
    const r = await this.host.callAction("pie", "script_run", { script, start_pie: true, record: true });
    const answer = answerOf(r);
    const error = failureOf(r);
    if (error || !answer) return { error: error ?? "ue-mcp's answer was not JSON" };
    if (typeof answer.run_id !== "string") return { error: "ue-mcp started no run" };
    const run = qaRunFromAnswer({ ...answer, script }, { seenAt: this.now() });
    if (!run) return { error: "ue-mcp's answer named no run" };
    this.save(run);
    return { run };
  }

  async stopRun(runId: string, reason: string): Promise<{ run: QaRun } | { error: string; status?: number }> {
    const run = this.runs.get(runId);
    if (!run || qaEnded(run.state)) return { error: "only a run that is going can be stopped", status: 409 };
    // PIE ends with the run only when it was held at a step and the run started that session.
    const endsPie = this.hold?.stopping(runId) ?? false;
    const r = await this.host.callAction("pie", "script_stop", { run_id: runId, reason });
    const error = failureOf(r);
    if (error) return { error, status: 409 };
    if (endsPie) {
      const pie = await this.host.callAction("editor", "play_in_editor", { pieAction: "stop" }).catch((e: Error) => ({ isError: true, content: [{ type: "text", text: e.message }] }));
      const pieError = failureOf(pie);
      if (pieError && !/already/i.test(pieError)) this.host.log.warn(`hub: PIE did not stop with ${run.scriptName}: ${pieError}`);
    }
    const read = await this.status(runId).catch(() => null);
    return { run: (read && "run" in read && read.run) || this.runs.get(runId) || run };
  }
}
