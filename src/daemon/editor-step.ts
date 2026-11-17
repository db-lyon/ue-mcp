// @session independent: Reads one editor operation step's outcome. Pure logic over its arguments.
/**
 * Whether one step of a daemon editor operation (start, stop, restart,
 * rebuild) failed, and why.
 *
 * The pipeline marks a call as failed only when it threw or was refused. A
 * handler that ran and reports failure RETURNS `success: false`, so a step is
 * read off both (#1384). Nothing to do is not a failure: a stop that found the
 * editor already down, or a start that found it up and answering.
 */
import { ErrorCode } from "../core/errors.js";
import type { CallOutcome } from "../dispatch/call.js";

function record(v: unknown): Record<string, unknown> | null {
  return v && typeof v === "object" && !Array.isArray(v) ? (v as Record<string, unknown>) : null;
}

/** Null when the step went through, else its reason in one paragraph. */
export function editorStepFailure(outcome: CallOutcome, max = 1200): string | null {
  const payload = outcome.code === undefined || outcome.code === ErrorCode.REFUSED ? record(outcome.data) : null;
  if (outcome.code === undefined) {
    if (!payload || payload.success !== false) return null;
    if (payload.alreadyStopped === true) return null;
    if (payload.alreadyRunning === true && payload.bridgeReady === true) return null;
  }
  const said = typeof payload?.message === "string" ? payload.message : typeof payload?.error === "string" ? payload.error : null;
  const fallback = outcome.code !== undefined && outcome.code !== ErrorCode.REFUSED
    ? `Error [${outcome.code}]: ${outcome.message ?? ""}`
    : JSON.stringify(outcome.data ?? null);
  const reason = (said ?? fallback).trim() || "the step failed";
  return reason.length > max ? reason.slice(0, max) : reason;
}
