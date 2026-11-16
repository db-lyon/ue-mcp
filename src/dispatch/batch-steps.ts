// @session independent: Reads the steps off a batch call's parameters; pure over its arguments.
/**
 * The two shapes of an editor(batch) call: the action's `ops` of
 * {category, method, args}, and the `execute_batch` bridge payload's `ops` of
 * {method, params} it is sent as. The batch action declares both readers as
 * its `steps` and its raw method's `steps`; the gates read them from there.
 */

export { MAX_BATCH_CALLS } from "../bridge/wire-contract.js";

/** One step as the caller writes it. */
export interface BatchCall {
  category: string;
  method: string;
  args?: Record<string, unknown>;
}

const isRecord = (v: unknown): v is Record<string, unknown> =>
  v !== null && typeof v === "object" && !Array.isArray(v);

/** The well-formed steps of a batch, read off the action's parameters. */
export function batchCallsOf(params: Record<string, unknown>): BatchCall[] {
  if (!Array.isArray(params.ops)) return [];
  const out: BatchCall[] = [];
  for (const call of params.ops) {
    if (!isRecord(call)) continue;
    if (typeof call.category !== "string" || typeof call.method !== "string") continue;
    out.push({ category: call.category, method: call.method, args: isRecord(call.args) ? call.args : undefined });
  }
  return out;
}

/** The bridge calls of an `execute_batch` payload. */
export function executeBatchSteps(params: Record<string, unknown>): Array<{ method: string; params: Record<string, unknown> }> {
  if (!Array.isArray(params.ops)) return [];
  const out: Array<{ method: string; params: Record<string, unknown> }> = [];
  for (const call of params.ops) {
    if (!isRecord(call) || typeof call.method !== "string") continue;
    out.push({ method: call.method, params: isRecord(call.params) ? call.params : {} });
  }
  return out;
}
