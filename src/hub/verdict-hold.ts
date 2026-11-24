// @session per-session: Holds the runs of one editor at their manual steps and pauses that editor's PIE.
/**
 * A run holding at a manual step for a person's verdict. A verdict says "a
 * person watched this happen in the game", so it is given live, while the
 * step plays, and the game must not run on while nobody watches:
 *
 * - When a run reaches a step that waits for a person (qaWaitingForPerson),
 *   Play In Editor is paused with the editor's own Pause
 *   (editor(set_pie_paused, mode debug), every PIE world). Script timing is
 *   in game time, so no auto step's window passes meanwhile.
 *   Interim: this pause lands a round trip after the step began waiting, so
 *   its act has already played and some frames have passed. The fix is the
 *   bridge pausing on that frame itself (ue-mcp-asks.md, pause_on_manual);
 *   once ue-mcp ships it, this pause is only the fallback for an older one.
 * - Resume and watch (resume()) unpauses, for a step that must be seen in
 *   motion. Only the person does that; nothing unpauses on its own.
 * - Once the step has its verdict, from the card, the app's bar, the phone
 *   or an agent, PIE is unpaused if it still is and the run goes on.
 * - The wait is the "Wait for a verdict" setting (or the plan's own), read
 *   when the step begins to wait and counted from then; Resume and watch does
 *   not extend it, so the deadline the clients count down to never moves.
 *   When it passes, the run is stopped with QA_NO_VERDICT and PIE with it
 *   (QaBroker.stopRun). The run is "unjudged": no result, since nobody
 *   being there says nothing about the game.
 *
 * Every run Studio started behaves the same: a chat card, a hub plan or an
 * agent. Studio acts only on what it started:
 *
 * - A run another MCP client started (no session) is never held: that client
 *   asks its own person, and its run, its PIE and its timing are its own.
 * - PIE is paused, resumed and stopped for a run only when that run started
 *   the session (QaRun.pieStarted). A run that joined PIE already running,
 *   the developer's own, holds for its verdict without touching their game.
 *   The editor's Pause covers every PIE world, so this is the narrowest
 *   control ue-mcp offers today; ue-mcp-asks.md asks for run-scoped PIE
 *   control and pie.paused/pie.resumed events.
 *
 * Holds are kept in memory: after a daemon restart a run still waiting is
 * held again from the moment it is read.
 */
import type { ActionResult } from "../contract/daemon.js";
import { QA_NO_VERDICT, qaWaitingForPerson, type QaHeld, type QaRun } from "./contract.js";
import { failureOf } from "./util.js";

export interface VerdictHoldOptions {
  /**
   * Whether the run is one this hold may act on: one the client started.
   * Injected: a client decides what "started by us" means (Studio: the run
   * has a chat session). A run another MCP client started is never held.
   * Defaults to a run that carries a sessionId.
   */
  owns?(run: QaRun): boolean;
  callAction(category: string, action: string, args?: Record<string, unknown>): Promise<ActionResult>;
  publish(type: string, data?: Record<string, unknown>): unknown;
  log: { warn(message: string, detail?: unknown): void };
  /** End a held run and PIE with it. */
  stop(runId: string, reason: string): Promise<unknown>;
  /** How many minutes a run waits for a verdict: the plan's own, else the setting. */
  waitMinutes(run: QaRun): number;
  now?: () => number;
}

interface Hold extends QaHeld {
  timer: ReturnType<typeof setTimeout>;
  /** The run started the PIE session it plays in: pausing and stopping it is Studio's to do. */
  ownsPie: boolean;
}

export class VerdictHold {
  private readonly holds = new Map<string, Hold>();
  private readonly now: () => number;

  constructor(private readonly o: VerdictHoldOptions) {
    this.now = o.now ?? Date.now;
  }

  /** Every run holding for a verdict, oldest first. */
  held(): QaHeld[] {
    return [...this.holds.values()].map(({ timer: _t, ownsPie: _o, ...h }) => h).sort((a, b) => (a.since < b.since ? -1 : 1));
  }

  /**
   * The run is being stopped: nothing to resume after. Returns whether PIE
   * should stop with it: it was held, and the run started that session.
   */
  stopping(runId: string): boolean {
    const hold = this.holds.get(runId);
    if (hold) hold.paused = false;
    return !!hold?.ownsPie;
  }

  /** A run changed: hold it at a step that now waits for a person, or let it go once it no longer does. */
  observe(run: QaRun): void {
    // Only a run the client started is the client's to hold.
    const step = (this.o.owns ?? ((r: QaRun) => !!r.sessionId))(run) ? qaWaitingForPerson(run) : null;
    const had = this.holds.get(run.runId);
    if (had && step?.index === had.step) return;
    if (had) this.release(had);
    if (!step) return;
    const ownsPie = run.pieStarted === true;
    const since = this.now();
    const ms = Math.max(1, this.o.waitMinutes(run)) * 60_000;
    const hold: Hold = {
      runId: run.runId,
      scriptName: run.scriptName,
      sessionId: run.sessionId,
      step: step.index,
      instruction: step.instruction,
      since: new Date(since).toISOString(),
      dueAt: new Date(since + ms).toISOString(),
      paused: ownsPie,
      ownsPie,
      timer: setTimeout(() => void this.lapse(run.runId), ms),
    };
    hold.timer.unref?.();
    this.holds.set(run.runId, hold);
    this.changed();
    // PIE the run joined is the developer's: it plays on.
    if (!ownsPie) return;
    void this.setPaused(true).then((error) => {
      if (!error || this.holds.get(run.runId) !== hold) return;
      this.o.log.warn(`playtest: PIE could not be paused for ${run.scriptName}: ${error}`);
      hold.paused = false;
      this.changed();
    });
  }

  /** Resume and watch: PIE runs again while the step still waits for its verdict. */
  async resume(runId: string): Promise<{ held: QaHeld } | { error: string; status: number }> {
    const hold = this.holds.get(runId);
    if (!hold) return { error: "the run is not waiting for a verdict", status: 409 };
    if (hold.paused) {
      const error = await this.setPaused(false);
      if (error) return { error: `PIE could not be resumed: ${error}`, status: 409 };
      hold.paused = false;
      this.changed();
    }
    const { timer: _t, ownsPie: _o, ...view } = hold;
    return { held: view };
  }

  dispose(): void {
    for (const h of this.holds.values()) clearTimeout(h.timer);
    this.holds.clear();
  }

  /** The step has its verdict, or the run ended: PIE runs again if this paused it. */
  private release(hold: Hold): void {
    clearTimeout(hold.timer);
    this.holds.delete(hold.runId);
    this.changed();
    if (hold.paused) void this.setPaused(false).then((error) => error && this.o.log.warn(`playtest: PIE could not be resumed after ${hold.scriptName}'s verdict: ${error}`));
  }

  /** Nobody judged the step in time: the run stops, and PIE with it. */
  private async lapse(runId: string): Promise<void> {
    const hold = this.holds.get(runId);
    if (!hold) return;
    try {
      await this.o.stop(runId, QA_NO_VERDICT);
    } catch (e) {
      this.o.log.warn(`playtest: ${hold.scriptName} could not be stopped after waiting for a verdict`, e);
    }
  }

  private async setPaused(paused: boolean): Promise<string | null> {
    try {
      return failureOf(await this.o.callAction("editor", "set_pie_paused", { paused, mode: "debug" }));
    } catch (e) {
      return e instanceof Error ? e.message : String(e);
    }
  }

  private changed(): void {
    this.o.publish("qa.held", { held: this.held() } as unknown as Record<string, unknown>);
  }
}
