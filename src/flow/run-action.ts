import type { TaskResult } from "@db-lyon/flowkit";
import type { ActionSpec, CategoryOptions, FlowActionSpec, ToolDef } from "../core/types.js";
import { stripAction } from "../surface/routing-params.js";
import { prepareCall, finishCall, forwardToBridge, type CallPipeline, type CallPreparation } from "../dispatch/call-pipeline.js";
import { McpError, ErrorCode } from "../core/errors.js";
import type { FlowContext } from "./context.js";
import { liftRollback } from "./rollback.js";
import { applyHandlerOutcome } from "./handler-outcome.js";
import { ensureGuard, isDialogRefusal } from "../editor/dialog-guard.js";
import { paramMapperOf } from "../surface/epic-input.js";
import { composeAction } from "./composite.js";
import { dialogGate } from "./gates.js";

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
  return dialogGate(await ensureGuard(session), taskName, "action");
}

/** The error a refused call reports, worded by the refusal itself. */
function refusalError(taskName: string, refusal: Record<string, unknown>): Error {
  const said = typeof refusal.error === "string" ? refusal.error : undefined;
  return new Error(said ?? `'${taskName}' was refused: a modal dialog is blocking the editor.`);
}

/**
 * Hold the asset locks `name` writes while it runs.
 *
 * Inside a run that already holds a scope (a live call, a flow run) the locks
 * join it and are released when that run ends. Otherwise a scope opened here
 * covers this action and anything it calls. A modal that refuses the lock
 * request is reported as the refusal, as the handler gate reports one.
 */
async function withActionLocks(
  ctx: FlowContext,
  name: string,
  params: Record<string, unknown>,
  run: (ctx: FlowContext) => Promise<TaskResult>,
): Promise<TaskResult> {
  const own = ctx.assetLocks ? undefined : ctx.openAssetLocks?.(ctx);
  const scope = ctx.assetLocks ?? own;
  if (!scope) return run(ctx);
  try {
    try {
      await scope.acquireFor(name, params);
    } catch (e) {
      const details = e instanceof McpError ? e.details : undefined;
      if (!isDialogRefusal(details)) throw e;
      const refusal = details as unknown as Record<string, unknown>;
      return { success: false, data: refusal, error: refusalError(name, refusal) };
    }
    return await run(own ? { ...ctx, assetLocks: own } : ctx);
  } finally {
    await own?.releaseAll();
  }
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
    paramChoices: (spec.kind === "bridge" || spec.kind === "flow") && spec.paramSpec && spec.paramChoices?.length
      ? { params: spec.paramSpec, choices: spec.paramChoices }
      : undefined,
  };
}

/**
 * Run one action: THE per-call path. The flow registry's task classes and a
 * category tool's own handler both come here, so a live MCP call, a flow step,
 * a batch op and a gateway call get the same preparation, the same verdict
 * read off the body, the same lifted rollback, the same modal check and the
 * same asset locks.
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
  home?: ToolDef,
): Promise<TaskResult> {
  return runLifecycle(ctx, familyOf(name, spec, prep, home), options);
}

/** The family an action spec belongs to. */
export function familyOf(name: string, spec: ActionSpec, prep?: CallPreparation, home?: ToolDef): ActionFamily {
  if (spec.kind === "bridge") return bridgeFamily(name, spec.bridge, paramMapperOf(spec), spec.timeoutMs, prep);
  if (spec.kind === "handler") return handlerFamily(name, spec.handler, prep);
  if (spec.kind === "flow") return flowFamily(name, spec, prep, home);
  throw new McpError(ErrorCode.NO_HANDLER, `Action '${name}' has no handler or bridge method`);
}

/** What `prepare` hands the later phases. */
export interface PreparedCall {
  pipeline: CallPipeline;
  /** What the editor is sent, when it differs from the prepared parameters. */
  send?: Record<string, unknown>;
}

/**
 * How one family of actions runs. The phases are fixed by runLifecycle:
 * prepare, gate, lock, execute, verdict. A family supplies each phase's work.
 */
export interface ActionFamily {
  readonly name: string;
  /**
   * Whether the action is checked against the dialog guard before it runs.
   * A bridge action is not: the guarded bridge refuses its method at the
   * boundary every route shares.
   */
  readonly gated: boolean;
  /** Whether the call's timeout budget travels on the context to the executor. */
  readonly budgetOnContext: boolean;
  prepare(options: Record<string, unknown>): PreparedCall;
  execute(callCtx: FlowContext, prepared: PreparedCall, runCtx: FlowContext): Promise<unknown>;
  verdict(runCtx: FlowContext, answered: unknown, prepared: PreparedCall): TaskResult;
}

/**
 * The one lifecycle every action runs through: prepare the parameters, gate
 * on a modal, hold the asset locks it writes, execute, and read the verdict.
 */
export async function runLifecycle(
  ctx: FlowContext,
  family: ActionFamily,
  options: Record<string, unknown>,
): Promise<TaskResult> {
  const prepared = family.prepare(options);
  if (family.gated) {
    // A flow is a minutes-long run and a modal can appear at any point in it,
    // so the gate is checked per call, not once before the run.
    const refusal = await refuseIfBlocked(ctx, family.name);
    if (refusal) return { success: false, data: refusal, error: refusalError(family.name, refusal) };
  }
  return withActionLocks(ctx, family.name, prepared.pipeline.params, async (lockedCtx) => {
    // The budget travels on the context, not in the parameters: a handler that
    // forwards its params to the bridge must not turn it into a bridge argument.
    const callCtx = family.budgetOnContext && prepared.pipeline.timeoutMs !== undefined
      ? { ...lockedCtx, callTimeoutMs: prepared.pipeline.timeoutMs }
      : lockedCtx;
    const answered = await family.execute(callCtx, prepared, ctx);
    return family.verdict(ctx, answered, prepared);
  });
}

/** A bridge action: its parameters mapped and sent to one method. */
export function bridgeFamily(
  name: string,
  method: string,
  mapParams: ((p: Record<string, unknown>) => Record<string, unknown>) | undefined,
  timeoutMs: number | undefined,
  prep?: CallPreparation,
): ActionFamily {
  return {
    name,
    gated: false,
    budgetOnContext: false,
    prepare(rawOptions) {
      // `action` is the dispatcher's, not a bridge parameter. Stripped before
      // the mapper, since a mapper that forwards its input verbatim would carry it.
      const pipeline = prepareCall(stripAction(rawOptions), prep);
      return { pipeline, send: forwardToBridge(pipeline, pipeline.params, mapParams, name) };
    },
    // The caller's budget wins over the action's authored one: an action that
    // declares 120s is stating a floor it needs, not a ceiling.
    execute: (callCtx, prepared) =>
      callCtx.bridge.call(method, prepared.send ?? prepared.pipeline.params, prepared.pipeline.timeoutMs ?? timeoutMs),
    verdict(runCtx, answered, prepared) {
      const raw = finishCall(answered, prepared.pipeline);
      if (typeof raw !== "object" || raw === null) {
        return applyHandlerOutcome(runCtx, answered, { success: true, data: { result: raw } });
      }
      // The response passes through INTACT, rollback descriptor included.
      const data = raw as Record<string, unknown>;
      const result: TaskResult = { success: true, data };
      // Lifted off the answer the editor gave, not off the projected copy, so a
      // caller's `select`/`omit` cannot filter the runner's undo record away.
      const answeredRollback = answered !== null && typeof answered === "object"
        ? (answered as Record<string, unknown>).rollback
        : undefined;
      const record = liftRollback(answeredRollback ?? data.rollback);
      if (record) result.rollback = record;
      // The verdict comes from the same unprojected answer: `select: ["path"]`
      // leaves no `success` key to read.
      return applyHandlerOutcome(runCtx, answered, result);
    },
  };
}

/** An in-process handler. */
export function handlerFamily(
  name: string,
  fn: (ctx: FlowContext, params: Record<string, unknown>) => Promise<unknown>,
  prep?: CallPreparation,
): ActionFamily {
  return {
    name,
    gated: true,
    budgetOnContext: true,
    prepare: (options) => ({ pipeline: prepareCall(options, prep) }),
    execute: (callCtx, prepared) => fn(callCtx, prepared.pipeline.params),
    verdict(runCtx, answered, prepared) {
      const data = finishCall(answered, prepared.pipeline);
      // A handler reports a refusal by RETURNING `{ success: false }`, so the
      // verdict is read off `answered`, where a `select` cannot hide it.
      return applyHandlerOutcome(runCtx, answered, {
        success: true,
        data: typeof data === "object" && data !== null
          ? (data as Record<string, unknown>)
          : { result: data },
      });
    },
  };
}

/**
 * A flow-backed action: its children run through the runner under the same
 * lock scope, and it reads its own verdict. `home` is the tool it belongs to,
 * for a context with no registry to resolve children from.
 */
export function flowFamily(name: string, spec: FlowActionSpec, prep?: CallPreparation, home?: ToolDef): ActionFamily {
  return {
    name,
    gated: true,
    budgetOnContext: true,
    prepare: (options) => ({ pipeline: prepareCall(stripAction(options), prep) }),
    execute: (callCtx, prepared, runCtx) => composeAction(runCtx, name, spec, callCtx, prepared.pipeline, home),
    verdict: (_runCtx, answered) => answered as TaskResult,
  };
}

export async function runBridge(
  ctx: FlowContext,
  name: string,
  method: string,
  mapParams: ((p: Record<string, unknown>) => Record<string, unknown>) | undefined,
  timeoutMs: number | undefined,
  rawOptions: Record<string, unknown>,
  prep?: CallPreparation,
): Promise<TaskResult> {
  return runLifecycle(ctx, bridgeFamily(name, method, mapParams, timeoutMs, prep), rawOptions);
}

export async function runHandler(
  ctx: FlowContext,
  name: string,
  fn: (ctx: FlowContext, params: Record<string, unknown>) => Promise<unknown>,
  options: Record<string, unknown>,
  prep?: CallPreparation,
): Promise<TaskResult> {
  return runLifecycle(ctx, handlerFamily(name, fn, prep), options);
}

export async function runFlowAction(
  ctx: FlowContext,
  name: string,
  spec: FlowActionSpec,
  options: Record<string, unknown>,
  prep?: CallPreparation,
  home?: ToolDef,
): Promise<TaskResult> {
  return runLifecycle(ctx, flowFamily(name, spec, prep, home), options);
}
