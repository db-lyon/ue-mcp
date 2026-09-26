/**
 * A live MCP call's task, resolved the way a flow step's is.
 *
 * The context carries the session's registry and its merged `tasks:`
 * definitions, so a `tasks:` override (class_path, option defaults) applies to
 * the live call, and `call`/`resolve` inside any task work on this path too.
 */
import type { BaseTask, TaskDefinition, TaskRegistry } from "@db-lyon/flowkit";
import type { FlowContext } from "./context.js";
import { createConfiguredTask, LIVE_REFERENCES_KEY } from "./task-call.js";

export interface LiveTaskSource {
  registry: TaskRegistry;
  /** The session's merged task definitions. Absent, every name resolves to itself. */
  definitions?: Record<string, TaskDefinition>;
  /** Host namespaces for `${ns.path}` in configured option defaults. */
  namespaces?: Record<string, unknown>;
}

/** The context every task of one live call runs under. */
export function liveTaskContext(source: LiveTaskSource, ctx: FlowContext): FlowContext {
  return {
    ...ctx,
    registry: source.registry,
    taskDefinitions: source.definitions,
    [LIVE_REFERENCES_KEY]: { steps: [], namespaces: source.namespaces },
  };
}

/**
 * Create the task a live call runs. The caller's options are runtime data and
 * stay literal; only configured defaults interpolate, as with `FlowRunner.runTask`
 * minus its interpolation of call options, which would rewrite a `${...}` a
 * caller sent as data (Python source, a format string).
 */
export function createLiveTask(
  source: LiveTaskSource,
  ctx: FlowContext,
  taskName: string,
  options: Record<string, unknown>,
): Promise<BaseTask<unknown>> {
  return createConfiguredTask(liveTaskContext(source, ctx), taskName, options);
}
