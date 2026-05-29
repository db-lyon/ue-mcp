/**
 * Public task-authoring surface for ue-mcp.
 *
 * Imported by anyone writing a custom task - whether to back a plugin action,
 * a project-local flow, or a one-off composition of built-in actions:
 *
 * ```ts
 * import { UeMcpTask, type TaskResult } from "ue-mcp/task";
 *
 * export default class InspectActor extends UeMcpTask<{ actorLabel: string }> {
 *   get taskName() { return "mypfx.inspect_actor"; }
 *   async execute(): Promise<TaskResult> {
 *     const details = await this.call("level.get_actor_details", {
 *       actorLabel: this.options.actorLabel,
 *     });
 *     if (!details.success) return details;
 *     return { success: true, data: details.data };
 *   }
 * }
 * ```
 *
 * This is a deliberately thin, server-free entry point: it pulls in flowkit's
 * `BaseTask` and ue-mcp's context types, nothing else. Authors never import
 * `@db-lyon/flowkit` directly - `UeMcpTask` is the supported base class, and
 * the flow runtime underneath it stays an implementation detail.
 */
import { BaseTask } from "@db-lyon/flowkit";
import type { TaskResult, RollbackRecord, TaskContext } from "@db-lyon/flowkit";
import type { IBridge } from "./bridge.js";
import type { FlowContext } from "./flow/context.js";

/**
 * Base class for ue-mcp tasks. Extends flowkit's `BaseTask` and narrows the
 * inherited context to ue-mcp's `FlowContext`, so subclasses get typed
 * `this.ctx` (bridge, project) and a `this.bridge` shortcut without the
 * `this.ctx as FlowContext` cast every hand-written task used to carry.
 *
 * Compose existing actions through `this.call('<category>.<action>', params)`
 * for free observability and rollback hooks; drop to `this.bridge.call(...)`
 * only for methods that have no task wrapping them.
 */
export abstract class UeMcpTask<
  TOpts = Record<string, unknown>,
> extends BaseTask<TOpts> {
  /**
   * Narrow the inherited `ctx` (typed `TaskContext` on `BaseTask`) to
   * ue-mcp's `FlowContext`. `declare` only refines the compile-time type -
   * it emits no field, so the value the runtime injects is untouched.
   */
  protected declare readonly ctx: FlowContext;

  /** The editor bridge for this run. Shortcut for `this.ctx.bridge`. */
  protected get bridge(): IBridge {
    return this.ctx.bridge;
  }
}

export type { TaskResult, RollbackRecord, TaskContext, FlowContext };
