/**
 * How a handler reaches the editor or another action.
 *
 * `callAction` is the rule: another action runs through its task, so its
 * parameter mapping, rollback lift, verdict, locks and any `tasks:` override
 * apply exactly as when it is called on its own. `callOwnBridgeMethod` is the
 * exception, for a handler whose bridge method is its own primitive and has no
 * action to go through.
 */
import type { TaskResult } from "@db-lyon/flowkit";
import type { ToolContext, ToolDef } from "../core/types.js";
import { McpError, ErrorCode } from "../core/errors.js";
import type { FlowContext } from "./context.js";
import { actionPreparation, runAction } from "./run-action.js";
import { createConfiguredTask } from "./task-call.js";
import { handlerFailure } from "./handler-outcome.js";

/**
 * Run the action `taskName` (`<category>.<action>`) through its task and
 * report its result. Never throws: a failure comes back as `success: false`.
 *
 * With no registry on the context (a handler invoked directly, as tests and
 * embedders do), the action runs from its spec in `home`, or in the context's
 * tool graph, on the same per-call path.
 */
export async function callAction(
  ctx: ToolContext,
  taskName: string,
  options: Record<string, unknown>,
  home?: ToolDef,
): Promise<TaskResult> {
  try {
    return withBodyVerdict(await runThroughTask(ctx as FlowContext, taskName, options, home));
  } catch (e) {
    return { success: false, error: e instanceof Error ? e : new Error(String(e)) };
  }
}

/**
 * The verdict the body states. A live call leaves a `success: false` body on a
 * successful result so its caller gets the whole body; a composite reads it
 * off the body instead, on every route.
 */
function withBodyVerdict(result: TaskResult): TaskResult {
  const failure = result.success ? handlerFailure(result.data) : null;
  return failure === null ? result : { ...result, success: false, error: new Error(failure) };
}

async function runThroughTask(
  flowCtx: FlowContext,
  taskName: string,
  options: Record<string, unknown>,
  home?: ToolDef,
): Promise<TaskResult> {
  if (flowCtx.registry) return (await createConfiguredTask(flowCtx, taskName, options)).run();
  const dot = taskName.indexOf(".");
  const category = taskName.slice(0, dot);
  const action = taskName.slice(dot + 1);
  const tool = home?.name === category ? home : flowCtx.getToolGraph?.().find((t) => t.name === category);
  const spec = tool?.actions[action];
  if (!tool || !spec) throw new McpError(ErrorCode.NO_HANDLER, `Action ${taskName} is not available here.`);
  return runAction(flowCtx, taskName, spec, options, actionPreparation(tool.options, action, spec));
}

/**
 * Call the bridge method a handler is built on, when that method has no action
 * of its own to route through (the handler IS its only action). Carries the
 * caller's timeout budget. Reach for `callAction` whenever an action exists.
 */
export function callOwnBridgeMethod(
  ctx: ToolContext,
  method: string,
  params: Record<string, unknown>,
): Promise<unknown> {
  return ctx.callTimeoutMs === undefined
    ? ctx.bridge.call(method, params)
    : ctx.bridge.call(method, params, ctx.callTimeoutMs);
}
