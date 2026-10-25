/**
 * The shape of an editor(batch) call, as every gate reads it.
 *
 * A leaf module: the locking gate needs the steps to take the union of their
 * locks, the batch action needs the same names, and neither may import the
 * other through the tool graph both belong to.
 */

/** The task name of the batch action. */
export const BATCH_TASK = "editor.batch";

/**
 * The most steps one batch carries. The whole list runs as one game-thread
 * job, so this bounds how long the editor is held by a single call; a longer
 * edit is two batches. Mirrored by MaxBatchCalls in BatchHandlers.cpp.
 */
export const MAX_BATCH_CALLS = 256;

/** One step as the caller writes it. */
export interface BatchCall {
  category: string;
  method: string;
  args?: Record<string, unknown>;
}

/** The well-formed steps of a batch, read off the action's parameters. */
export function batchCallsOf(params: Record<string, unknown>): BatchCall[] {
  if (!Array.isArray(params.ops)) return [];
  const out: BatchCall[] = [];
  for (const entry of params.ops) {
    if (entry === null || typeof entry !== "object" || Array.isArray(entry)) continue;
    const call = entry as Record<string, unknown>;
    if (typeof call.category !== "string" || typeof call.method !== "string") continue;
    const args = call.args;
    out.push({
      category: call.category,
      method: call.method,
      args: args !== null && typeof args === "object" && !Array.isArray(args) ? (args as Record<string, unknown>) : undefined,
    });
  }
  return out;
}
