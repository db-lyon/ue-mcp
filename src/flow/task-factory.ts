import type { TaskResult, TaskConstructor } from "@db-lyon/flowkit";
import { UeMcpTask } from "../task.js";
import { stripEditorTarget } from "../types.js";
import { prepareCall, finishCall, type CallPreparation } from "../call-pipeline.js";
import type { FlowContext } from "./context.js";
import { liftRollback } from "./rollback.js";
import { applyHandlerOutcome } from "./handler-outcome.js";
import { DialogGuard, ensureGuard } from "../dialog-guard.js";

/**
 * Refuse a flow step while a modal is up, through the one guard.
 *
 * The bridge-backed steps are covered at the bridge boundary. An in-process
 * handler never goes near it, so without this the gate simply was not on that
 * route: a modal raised mid-run was missed by every remaining step.
 *
 * Returns the refusal to report, or null to run the step.
 */
async function refuseStepIfBlocked(
  ctx: FlowContext,
  taskName: string,
  options: Record<string, unknown>,
): Promise<Record<string, unknown> | null> {
  const session = ctx.session;
  if (!session) return null;
  // The micro gateway arrives as one task carrying the real category and
  // method in its options. Asking the allowlist about the wrapper refused
  // respond_to_dialog, which is the single call that clears the dialog, so the
  // gateway could never escape one.
  const subject = taskName === "tools.call"
    ? `${String(options.category ?? "")}.${String(options.method ?? "")}`
    : taskName;
  // No early return for an allow-listed subject. `check` already exempts them,
  // and it additionally applies the mode to the one allow-listed subject that
  // PRESSES a button, so short-circuiting here let a flow step answer a modal
  // under interactive by walking around the gate rather than through it.
  const guard = await ensureGuard(session);
  const decision = await guard.check(subject, "action");
  return decision.allow ? null : (decision.refusal ?? null);
}

/** The error a refused step reports, worded by the refusal itself. */
function refusalError(taskName: string, refusal: Record<string, unknown>): Error {
  const said = typeof refusal.error === "string" ? refusal.error : undefined;
  return new Error(said ?? `'${taskName}' was refused: a modal dialog is blocking the editor.`);
}

/**
 * Create a TaskConstructor for a bridge-delegation action.
 * The bridge method (and optional param mapper) are closed over in the class,
 * along with the action's authored timeout and the category's preparation.
 *
 * `timeoutMs` is the action's OWN budget, the floor it needs. It is not the
 * caller's: the caller's arrives in the parameters and is read off the
 * pipeline below, where it wins.
 */
export function bridgeTaskClass(
  name: string,
  method: string,
  mapParams?: (p: Record<string, unknown>) => Record<string, unknown>,
  timeoutMs?: number,
  prep?: CallPreparation,
): TaskConstructor {
  class FactoryBridgeTask extends UeMcpTask {
    get taskName() { return name; }
    async execute(): Promise<TaskResult> {
      // `editor` addresses a session; it is never a bridge parameter. Strip it
      // before the mapper too, since a mapper that forwards its input verbatim
      // would carry it into the call.
      const options = stripEditorTarget(this.options as Record<string, unknown>);
      // THE per-call preparation. This is the route a live MCP call and a flow
      // step both take, so everything a call is promised has to happen here:
      // the routing parameters come off, the paths are repaired, and the
      // category folds its accepted spellings into the canonical ones.
      const pipeline = prepareCall(options, prep);
      const params = mapParams ? mapParams(pipeline.params) : pipeline.params;
      // The caller's budget wins over the action's authored one: an action
      // that declares 120s is stating a floor it needs, not a ceiling the
      // caller may not raise.
      const answered = await this.bridge.call(method, params, pipeline.timeoutMs ?? timeoutMs);
      const raw = finishCall(answered, pipeline);
      if (typeof raw !== "object" || raw === null) {
        return applyHandlerOutcome(this.ctx, answered, { success: true, data: { result: raw } });
      }
      // Handlers attach `rollback` to their response. This class never lifted
      // it, so every rollback emitted by a registered action was silently
      // dropped and rollback_on_failure had nothing to undo.
      //
      // The response is passed through INTACT, rollback descriptor included.
      // Every MCP category-tool call routes through this class, not just
      // flows, and data is serialized as the whole tool result - stripping the
      // key here deleted the rollback descriptor from ~90 handlers' documented
      // responses, and left bridge-backed actions inconsistent with
      // handler-backed ones, which pass data through untouched.
      const raw2 = raw as Record<string, unknown>;
      const result: TaskResult = { success: true, data: raw2 };
      // Lifted off the answer the editor gave, not off the projected copy: a
      // caller's `select`/`omit` shapes the response it reads, and must not be
      // able to filter the runner's undo record out of existence.
      // Falling back to the projected copy keeps this a strict superset of what
      // was lifted before: the projection only ever removes keys, so this can
      // add a record, never drop one.
      const answeredRollback = answered !== null && typeof answered === "object"
        ? (answered as Record<string, unknown>).rollback
        : undefined;
      const record = liftRollback(answeredRollback ?? raw2.rollback);
      if (record) result.rollback = record;
      // The step's verdict comes from the same unprojected answer, for the
      // same reason: `select: ["path"]` leaves no `success` key to read. The
      // bridge resolves a `success: false` body normally, so without this a
      // handler-reported failure was a passing step, and `rollback_on_failure`
      // only ever fired on a transport fault.
      return applyHandlerOutcome(this.ctx, answered, result);
    }
  }
  Object.defineProperty(FactoryBridgeTask, "name", { value: `BridgeTask_${name}` });
  return FactoryBridgeTask as unknown as TaskConstructor;
}

/**
 * Create a TaskConstructor that wraps an existing async handler function.
 * Used for the ~19 direct-handler actions (editor control, project ops, etc.).
 */
export function handlerTaskClass(
  name: string,
  fn: (ctx: FlowContext, params: Record<string, unknown>) => Promise<unknown>,
  prep?: CallPreparation,
): TaskConstructor {
  class FactoryHandlerTask extends UeMcpTask {
    get taskName() { return name; }
    async execute(): Promise<TaskResult> {
      const pipeline = prepareCall(this.options as Record<string, unknown>, prep);
      // The budget travels on the context, not in the parameters: a custom
      // handler that forwards its params to the bridge must not turn it into a
      // bridge argument. Same rule, same shape, as the category route.
      const ctx = pipeline.timeoutMs === undefined
        ? this.ctx
        : { ...this.ctx, callTimeoutMs: pipeline.timeoutMs };
      // A flow is a minutes-long run, and a modal can appear at any point in
      // it. The tool route checks once, before flow.run starts; without this,
      // a dialog raised at step 3 was missed by every later step, which
      // includes the editor lifecycle handlers. Checking per step is what
      // makes "any process, at any point" true of flows as well.
      const refusal = await refuseStepIfBlocked(
        this.ctx,
        name,
        this.options as Record<string, unknown>,
      );
      if (refusal) {
        return { success: false, data: refusal, error: refusalError(name, refusal) };
      }
      const answered = await fn(ctx, pipeline.params);
      const data = finishCall(answered, pipeline);
      // Same defect as the bridge class had, from the same hardcoded `true`:
      // a direct handler reports a refusal by RETURNING `{ success: false,
      // message }` rather than throwing (stop_editor when a dialog blocks the
      // editor, a PIE launch the user declined), and only a throw was ever
      // reaching the runner. Read off `answered` so a `select` cannot hide the
      // verdict, and the data still passes through untouched.
      return applyHandlerOutcome(this.ctx, answered, {
        success: true,
        data: typeof data === "object" && data !== null
          ? (data as Record<string, unknown>)
          : { result: data },
      });
    }
  }
  Object.defineProperty(FactoryHandlerTask, "name", { value: `HandlerTask_${name}` });
  return FactoryHandlerTask as unknown as TaskConstructor;
}
