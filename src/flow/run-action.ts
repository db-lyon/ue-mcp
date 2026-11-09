import type { TaskResult } from "@db-lyon/flowkit";
import type { ActionSpec, BridgeActionSpec, CategoryOptions } from "../core/types.js";
import { stripAction } from "../surface/routing-params.js";
import { prepareCall, finishCall, forwardToBridge, withLiveContract, type CallPreparation } from "../dispatch/call-pipeline.js";
import { McpError, ErrorCode } from "../core/errors.js";
import type { FlowContext } from "./context.js";
import { liftRollback } from "./rollback.js";
import { applyHandlerOutcome } from "./handler-outcome.js";
import { ensureGuard } from "../editor/dialog-guard.js";
import { paramMapperOf } from "../surface/epic-input.js";
import { epicEngineRefusal } from "../surface/epic-engines.js";
import { actionAccepts } from "../surface/action-schema.js";
import type { BridgeCallOptions, IBridge } from "../bridge/bridge.js";

/**
 * Refuse a handler-backed call while a modal is up, through the one guard.
 *
 * The bridge-backed calls are covered at the bridge boundary. An in-process
 * handler never goes near it, so without this the gate simply was not on that
 * route: a modal raised mid-run was missed by every remaining step.
 *
 * Returns the refusal to report, or null to run the call.
 */
async function refuseIfBlocked(
  ctx: FlowContext,
  taskName: string,
): Promise<Record<string, unknown> | null> {
  const session = ctx.session;
  if (!session) return null;
  // No early return for an allow-listed subject. `check` already exempts them,
  // and it additionally applies the mode to the one allow-listed subject that
  // PRESSES a button, so short-circuiting here let a step answer a modal under
  // interactive by walking around the gate rather than through it.
  const guard = await ensureGuard(session);
  const decision = await guard.check(taskName, "action");
  return decision.allow ? null : (decision.refusal ?? null);
}

/** The error a refused call reports, worded by the refusal itself. */
function refusalError(taskName: string, refusal: Record<string, unknown>): Error {
  const said = typeof refusal.error === "string" ? refusal.error : undefined;
  return new Error(said ?? `'${taskName}' was refused: a modal dialog is blocking the editor.`);
}

/**
 * Everything the shared preparation cannot work out for itself: the category's
 * folding, the action name that folding branches on, the nesting a gateway's
 * parameters arrive under, and a spec'd action's required choices.
 */
export function actionPreparation(
  options: CategoryOptions | undefined,
  actionName: string,
  spec: ActionSpec,
): CallPreparation {
  return {
    action: actionName,
    normalizeParams: options?.normalizeParams,
    paramGroups: options?.paramGroups,
    nestedParamsKey: options?.nestedParamsKey,
    paramContract: spec.paramSpec
      ? { params: spec.paramSpec, choices: spec.paramChoices, strict: spec.strictParams }
      : undefined,
    accepts: actionAccepts(spec),
    epicTool: spec.kind === "bridge" ? spec.epicTool : undefined,
  };
}

/**
 * Run one action: THE per-call path. The flow registry's task classes and a
 * category tool's own handler both come here, so a live MCP call, a flow step,
 * a batch op and a gateway call get the same preparation, the same verdict
 * read off the body, the same lifted rollback and the same modal check.
 *
 * `options` carries neither `action` nor a routing `editor`; the task name
 * selects the action, and `prep.action` hands it to the category's folding.
 */
export async function runAction(
  ctx: FlowContext,
  name: string,
  spec: ActionSpec,
  options: Record<string, unknown>,
  prep?: CallPreparation,
): Promise<TaskResult> {
  if (spec.kind === "bridge") return runBridge(ctx, name, spec.bridge, paramMapperOf(spec), spec.timeoutMs, options, prep, spec);
  if (spec.kind === "handler") return runHandler(ctx, name, spec.handler, options, prep);
  throw new McpError(ErrorCode.NO_HANDLER, `Action '${name}' has no handler or bridge method`);
}

/** What a bridge action may run around its call: intercept before it, followUp after it. */
export type BridgeHooks = Pick<BridgeActionSpec, "followUp" | "intercept">;

export async function runBridge(
  ctx: FlowContext,
  name: string,
  method: string,
  mapParams: ((p: Record<string, unknown>) => Record<string, unknown>) | undefined,
  timeoutMs: number | undefined,
  rawOptions: Record<string, unknown>,
  prep?: CallPreparation,
  hooks?: BridgeHooks,
): Promise<TaskResult> {
  // `action` is the dispatcher's, not a bridge parameter. Stripped before the
  // mapper, since a mapper that forwards its input verbatim would carry it.
  const options = stripAction(rawOptions);
  // A wrapped engine tool the attached engine's recorded registry lacks is refused unsent.
  const refused = epicEngineRefusal(name, prep?.epicTool, ctx.bridge?.capabilities?.engineVersion);
  if (refused) return applyHandlerOutcome(ctx, refused, { success: true, data: { ...refused } });
  // The routing parameters come off, the paths are repaired, and the category
  // folds its accepted spellings into the canonical ones.
  const pipeline = prepareCall(options, withLiveContract(prep, ctx.bridge.capabilities?.pluginHandlerSpecs));
  const params = forwardToBridge(pipeline, pipeline.params, mapParams, name);
  // The caller's budget wins over the action's authored one: an action that
  // declares 120s is stating a floor it needs, not a ceiling.
  const hookCtx = pipeline.timeoutMs === undefined
    ? ctx
    : { ...ctx, callTimeoutMs: pipeline.timeoutMs, bridge: withCallTimeout(ctx.bridge, pipeline.timeoutMs) };
  const intercepted = hooks?.intercept ? await hooks.intercept(hookCtx, params) : undefined;
  let answered: unknown;
  if (intercepted !== undefined) {
    answered = intercepted;
  } else {
    // Progress and cancellation reach the editor for a handler that defers its answer.
    const live = ctx.onProgress || ctx.signal ? { onProgress: ctx.onProgress, signal: ctx.signal } : undefined;
    const sent = live
      ? await ctx.bridge.call(method, params, pipeline.timeoutMs ?? timeoutMs, live)
      : await ctx.bridge.call(method, params, pipeline.timeoutMs ?? timeoutMs);
    const followUpParams = pipeline.timeoutMs === undefined ? params : { ...params, timeoutMs: pipeline.timeoutMs };
    const followUpCtx = pipeline.timeoutMs === undefined ? ctx : { ...ctx, callTimeoutMs: pipeline.timeoutMs };
    answered = hooks?.followUp ? await hooks.followUp(followUpCtx, followUpParams, sent) : sent;
  }
  const raw = finishCall(answered, pipeline);
  if (typeof raw !== "object" || raw === null) {
    return applyHandlerOutcome(ctx, answered, { success: true, data: { result: raw } });
  }
  // The response passes through INTACT, rollback descriptor included: data is
  // serialized as the whole tool result.
  const data = raw as Record<string, unknown>;
  const result: TaskResult = { success: true, data };
  // Lifted off the answer the editor gave, not off the projected copy, so a
  // caller's `select`/`omit` cannot filter the runner's undo record away. The
  // projection only removes keys, so the fallback can only add a record.
  const answeredRollback = answered !== null && typeof answered === "object"
    ? (answered as Record<string, unknown>).rollback
    : undefined;
  const record = liftRollback(answeredRollback ?? data.rollback);
  if (record) result.rollback = record;
  // The verdict comes from the same unprojected answer: `select: ["path"]`
  // leaves no `success` key to read, and the bridge resolves a
  // `success: false` body normally.
  return applyHandlerOutcome(ctx, answered, result);
}

/**
 * The bridge as an in-process handler sees it: a call that names no budget of
 * its own gets the caller's, so every handler honours timeoutMs (#1339).
 */
export function withCallTimeout<B extends { call: IBridge["call"] }>(bridge: B, timeoutMs: number): B {
  if (!bridge) return bridge;
  return new Proxy(bridge, {
    get(target, key) {
      if (key === "call") {
        return (method: string, params?: Record<string, unknown>, ms?: number, options?: BridgeCallOptions) =>
          target.call(method, params, ms ?? timeoutMs, options);
      }
      const value = Reflect.get(target, key, target);
      return typeof value === "function" ? value.bind(target) : value;
    },
  });
}

export async function runHandler(
  ctx: FlowContext,
  name: string,
  fn: (ctx: FlowContext, params: Record<string, unknown>) => Promise<unknown>,
  options: Record<string, unknown>,
  prep?: CallPreparation,
): Promise<TaskResult> {
  const pipeline = prepareCall(options, prep);
  // The budget travels on the context, not in the parameters: a handler that
  // forwards its params to the bridge must not turn it into a bridge argument.
  const callCtx = pipeline.timeoutMs === undefined
    ? ctx
    : { ...ctx, callTimeoutMs: pipeline.timeoutMs, bridge: withCallTimeout(ctx.bridge, pipeline.timeoutMs) };
  // A flow is a minutes-long run and a modal can appear at any point in it, so
  // the gate is checked per call, not once before the run.
  const refusal = await refuseIfBlocked(ctx, name);
  if (refusal) {
    return { success: false, data: refusal, error: refusalError(name, refusal) };
  }
  const answered = await fn(callCtx, pipeline.params);
  const data = finishCall(answered, pipeline);
  // A handler reports a refusal by RETURNING `{ success: false }`, so the
  // verdict is read off `answered`, where a `select` cannot hide it.
  return applyHandlerOutcome(ctx, answered, {
    success: true,
    data: typeof data === "object" && data !== null
      ? (data as Record<string, unknown>)
      : { result: data },
  });
}
