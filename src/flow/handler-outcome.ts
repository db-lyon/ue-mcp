import type { RollbackRecord, TaskContext, TaskResult } from "@db-lyon/flowkit";

/**
 * A handler's own verdict on the call it just answered.
 *
 * `Bridge.call` rejects on a transport fault and on a JSON-RPC `error`, and
 * resolves on every other well-formed reply. A handler that failed says so in
 * the body instead: `MCPError` in `HandlerUtils.h` builds
 * `{ success: false, error: "..." }`, and the TS-side handlers return
 * `{ success: false, message: "..." }` when they refuse. Both arrive by
 * exactly the same route a successful answer does, so the outcome has to be
 * read off the body, never off the promise.
 *
 * Returns the message the failure carried, or `null` when the call did not
 * report one. Only an explicit boolean `false` counts: a response with no
 * `success` key is an ordinary answer (a listing, a reflection dump), and a
 * per-item `success` nested inside an array is that item's verdict rather than
 * the call's, which is why only the top level is read.
 */
export function handlerFailure(answered: unknown): string | null {
  if (answered === null || typeof answered !== "object" || Array.isArray(answered)) return null;
  const body = answered as Record<string, unknown>;
  if (body.success !== false) return null;
  // `error` is the C++ convention, `message` the TS one, `reason` the machine
  // token some refusals carry in place of prose.
  for (const key of ["error", "message", "reason"]) {
    const value = body[key];
    if (typeof value === "string" && value.trim().length > 0) return value;
  }
  return "the editor answered success: false without saying why";
}

/**
 * Whether this task is running under a flow runner.
 *
 * The two routes into the bridge task classes need opposite things from a
 * handler-reported failure, so they are told apart here rather than forced
 * onto one behaviour:
 *
 *  - **A flow step must fail.** Otherwise the runner walks the rest of the
 *    plan as though the mutation landed, `on_failure` never fires, and
 *    `rollback_on_failure` has nothing to react to. A destructive action that
 *    reported its own failure ran a flow to completion reporting success.
 *  - **An MCP category-tool call must not.** `index.ts` renders a failed
 *    `TaskResult` as `Error [TASK_FAILED]: <message>` and never serializes
 *    `result.data`, so failing there would delete the response body of every
 *    handler that reports its own failure: the error detail, the per-item
 *    verdicts a batch returns alongside it, the rollback descriptor. That
 *    caller reads `success` off the body itself and always has.
 *
 * `FlowRunner` is the only thing that wires `runFlow` and
 * `taskReferenceContext` onto a task's context - flowkit documents `runFlow`
 * as "Wired by FlowRunner; absent when a task runs outside one" - and the
 * context `index.ts` builds for a direct call is assembled by hand with
 * neither. Every route that reaches a task through a runner carries both: a
 * step, a retry of one, a nested flow, an agent tool, `runTask`, and the
 * inverse replayed during rollback. So a failing inverse is now reported as a
 * failing inverse too, instead of counting itself as a rollback that worked.
 */
export function inFlowRun(ctx: TaskContext): boolean {
  return typeof ctx.runFlow === "function" || ctx.taskReferenceContext !== undefined;
}

/**
 * The inverse a FAILING step carries, written out as something a caller can run.
 *
 * A handful of handlers attach a rollback descriptor to a body whose top-level
 * verdict is `success: false`, and they do it deliberately: the mutation
 * PARTIALLY applied, and the descriptor undoes the part that landed. A World
 * rename that moved some external actor packages before `RenameAssets` gave up
 * says so in its own comment; the hygiene fixer, the lightmap UV builder and
 * the mesh fracturer all do the same for the moves, settings and assets they
 * did write.
 *
 * Whether the runner replays that record is `rollback_on_failure`, and nothing
 * else. From flowkit 0.17.1 `flow/runner.js` harvests an inverse from every
 * step carrying one, failed or not, marks the ones off a failed step
 * `fromFailedStep`, and invokes the array in reverse - so the failing step's
 * own inverse, pushed last, is the FIRST one run, and the steps before it
 * unwind behind it. The nested-flow bubble harvests on the same terms, so a
 * child flow's failing step keeps its inverse too. Up to 0.17.0 the harvest
 * was gated on `taskResult.success`, which discarded exactly the record that a
 * partial write makes worth the most.
 *
 * A flow that did not ask for `rollback_on_failure` still replays nothing, and
 * that is the case this string exists for: the record is handed to the caller
 * to run rather than dropped in silence. A task cannot tell the two apart -
 * `TaskContext` carries no flow options - so the text names both outcomes and
 * `flow/flow-tool.ts`, which can see whether rollback ran, reports which one
 * happened.
 *
 * The call goes in the error MESSAGE as well as in a field, because the message
 * survives every reader: a summary line, a journal entry, a terminal.
 *
 * The string is the flow step that runs it, because the record already names
 * the generic bridge task and carries the bridge method in its payload, so
 * there is nothing to translate and nothing for a caller to reconstruct.
 */
export function unappliedRollbackCall(record: RollbackRecord): string {
  const { method, ...payload } = record.payload as Record<string, unknown>;
  return `{ task: "${record.taskName}", options: ${JSON.stringify({ method, ...payload })} }`;
}

/**
 * Fail the step when the handler said the call failed, on the flow path only.
 *
 * Mutates and returns the result it was given, so the caller keeps ownership
 * of `data` and of the rollback record. `data` is left exactly as it was: a
 * failed step still carries the handler's whole body, which is what
 * `formatFlowResult` puts in `steps[].data` for the caller to read.
 *
 * The record on a failing step is named in the error text as well, for the
 * reason `unappliedRollbackCall` gives: an inverse to a partial write that
 * only exists in a field nobody prints is the same as no inverse at all.
 * Everything a flow failure arms fires on this class of failure - the steps
 * BEFORE it are rolled back, the `on_failure` hook runs, and the opt-in git
 * snapshot is restored, none of which used to happen because the run reported
 * success.
 *
 * The text names both outcomes because a task cannot see `rollback_on_failure`
 * and so cannot know which one it is about to get.
 */
export function applyHandlerOutcome(
  ctx: TaskContext,
  answered: unknown,
  result: TaskResult,
): TaskResult {
  const failure = handlerFailure(answered);
  if (failure === null || !inFlowRun(ctx)) return result;
  result.success = false;
  result.error = new Error(
    result.rollback
      ? `${failure} This step wrote part of its change before it stopped, and carries its own inverse for the ` +
        `part that landed: ${unappliedRollbackCall(result.rollback)}. Under rollback_on_failure the runner ` +
        `replays it first, ahead of the steps before it. Without rollback_on_failure nothing runs it and the ` +
        `undo is yours to run.`
      : failure,
  );
  return result;
}
