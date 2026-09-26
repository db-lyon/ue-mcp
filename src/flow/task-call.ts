/**
 * Resolving one task call through the configured task definitions, the way a
 * flow step resolves its task: `tasks:` overrides pick the class and layer
 * their option defaults under the caller's options.
 *
 * Kept free of server imports, because the public `ue-mcp/task` entry point
 * uses it.
 */
import { resolveReferences } from "@db-lyon/flowkit";
import type { BaseTask, ReferenceContext, TaskContext, TaskDefinition, TaskRegistry } from "@db-lyon/flowkit";

/**
 * Every built-in action is also registered under this class path. The default
 * definition of `<category>.<action>` points at it, and an override that wraps
 * a built-in calls it by this name instead of by the task name it replaced.
 */
export const BUILTIN_CLASS_PREFIX = "ue-mcp.builtin/";

export function builtinClassPath(taskName: string): string {
  return `${BUILTIN_CLASS_PREFIX}${taskName}`;
}

/** Deeper than any real composition; a chain this long is a loop the class check missed. */
const MAX_TASK_DEPTH = 32;

/** Context key holding the task classes on the current call path. */
const CHAIN_KEY = "__ueMcpTaskChain";

/**
 * Context key for the reference scope of a live call. Not flowkit's
 * `taskReferenceContext`: that key marks a runner-driven context, and a live
 * call must keep its own success verdict (see handler-outcome.ts).
 */
export const LIVE_REFERENCES_KEY = "__ueMcpLiveReferences";

type TaskClass = abstract new (...args: never[]) => BaseTask<unknown>;

/** A task call that would re-enter a class already on the call path. */
export class TaskCycleError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "TaskCycleError";
  }
}

/** A configured task name resolved to its class path and merged options. */
export function resolveConfiguredTask(
  taskName: string,
  definitions: Record<string, TaskDefinition> | undefined,
  options: Record<string, unknown>,
  references?: ReferenceContext,
): { classPath: string; options: Record<string, unknown> } {
  const def = definitions?.[taskName];
  const defaults = (def?.options ?? {}) as Record<string, unknown>;
  // Configured defaults are configuration and interpolate; the caller's
  // options are runtime data and stay literal, as in flowkit.
  const resolvedDefaults = references ? resolveReferences(defaults, references) : defaults;
  return { classPath: def?.class_path ?? taskName, options: { ...resolvedDefaults, ...options } };
}

/**
 * Create the task a call resolves to, refusing a call that would re-enter a
 * class already on the call path. `caller` is the class making the call.
 */
export async function createConfiguredTask(
  ctx: TaskContext,
  taskName: string,
  options: Record<string, unknown> = {},
  caller?: TaskClass,
): Promise<BaseTask<unknown>> {
  const registry = ctx.registry as TaskRegistry | undefined;
  if (!registry) {
    throw new Error(`Cannot resolve task "${taskName}": no task registry in this context.`);
  }
  const references = ctx.taskReferenceContext ?? (ctx[LIVE_REFERENCES_KEY] as ReferenceContext | undefined);
  const resolved = resolveConfiguredTask(taskName, ctx.taskDefinitions, options, references);
  const chain = [...((ctx[CHAIN_KEY] as TaskClass[] | undefined) ?? []), ...(caller ? [caller] : [])];
  const target = await registry.resolve(resolved.classPath);
  if (chain.includes(target as unknown as TaskClass) || chain.length >= MAX_TASK_DEPTH) {
    throw new TaskCycleError(
      `Task "${taskName}" resolves to class_path "${resolved.classPath}", which is already running on this call path. `
      + `An override that wraps a built-in calls the built-in by its base alias, "${builtinClassPath(taskName)}", `
      + "not by the task name it replaced.",
    );
  }
  // A task-to-task call is ordinary work: it does not inherit a hook or rollback phase.
  const childCtx: TaskContext = { ...ctx, executionPhase: "task", [CHAIN_KEY]: chain };
  return registry.create(resolved.classPath, childCtx, resolved.options);
}
