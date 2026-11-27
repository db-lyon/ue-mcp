// @session per-session: Plays arrangements on the open editor of the project whose host it is handed.
/**
 * The open editor as a runner. The plugin plays one thing, an arrangement or
 * one region of it, handed whole with the clips it places (PlayRequest), so
 * it reads no hub file:
 *
 *   pie(arrangement_play)    PlayRequest                      -> { pieStarted? }
 *   pie(arrangement_status)  { runId }                        -> PlayStatus
 *   pie(arrangement_stop)    { runId, reason }
 *   pie(arrangement_verdict) { runId, check, verdict, note? }  verdict "pass" | "fail"
 */
import type { CheckResult, CheckStatus, PlayRequest, PlayStatus } from "./contract.js";
import type { HubHost, HubRunner } from "./ports.js";
import { answerOf, failureOf } from "./util.js";

const STATES: ReadonlySet<string> = new Set(["starting", "running", "awaiting_verdict", "finished", "stopped", "error"]);
const CHECK_STATUSES: ReadonlySet<string> = new Set(["pending", "running", "passed", "failed", "awaiting_verdict", "skipped"]);

/** A PlayStatus from the plugin's answer, read leniently. */
export function playStatusOf(runId: string, body: Record<string, unknown>): PlayStatus {
  const checks: CheckResult[] = (Array.isArray(body.checks) ? body.checks : []).flatMap((c: unknown) => {
    if (!c || typeof c !== "object") return [];
    const o = c as Record<string, unknown>;
    if (typeof o.id !== "string" || !CHECK_STATUSES.has(String(o.status))) return [];
    return [{ id: o.id, status: o.status as CheckStatus, ...(typeof o.frame === "number" ? { frame: o.frame } : {}), ...(typeof o.message === "string" && o.message ? { message: o.message } : {}) }];
  });
  const state = STATES.has(String(body.state)) ? (body.state as PlayStatus["state"]) : "error";
  return {
    runId,
    state,
    ...(typeof body.frame === "number" ? { frame: body.frame } : {}),
    checks,
    ...(typeof body.error === "string" && body.error ? { error: body.error } : state === "error" && !STATES.has(String(body.state)) ? { error: `the editor answered state ${String(body.state)}` } : {}),
    ...(body.pieStarted === true ? { pieStarted: true } : {}),
  };
}

export class EditorRunner implements HubRunner {
  readonly kind = "editor" as const;

  constructor(private readonly host: Pick<HubHost, "callAction">) {}

  unavailable(): string | null {
    return null;
  }

  private async call(action: string, args: Record<string, unknown>): Promise<Record<string, unknown>> {
    const r = await this.host.callAction("pie", action, args);
    const failed = failureOf(r);
    if (failed) throw new Error(failed);
    return answerOf(r) ?? {};
  }

  async play(request: PlayRequest): Promise<{ pieStarted?: boolean }> {
    const body = await this.call("arrangement_play", request as unknown as Record<string, unknown>);
    return body.pieStarted === true ? { pieStarted: true } : {};
  }

  async status(runId: string): Promise<PlayStatus> {
    return playStatusOf(runId, await this.call("arrangement_status", { runId }));
  }

  async stop(runId: string, reason: string): Promise<void> {
    await this.call("arrangement_stop", { runId, reason });
  }

  async verdict(runId: string, check: string, verdict: "pass" | "fail", note?: string): Promise<void> {
    await this.call("arrangement_verdict", { runId, check, verdict, ...(note ? { note } : {}) });
  }
}
