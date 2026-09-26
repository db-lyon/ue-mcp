import type { TaskResult, TaskConstructor } from "@db-lyon/flowkit";
import { UeMcpTask } from "../task.js";
import { stripEditorTarget } from "../surface/target-params.js";
import type { CallPreparation } from "../dispatch/call-pipeline.js";
import type { FlowContext } from "./context.js";
import { runBridge, runHandler } from "./run-action.js";

/**
 * Create a TaskConstructor for a bridge-delegation action.
 * The bridge method (and optional param mapper) are closed over in the class,
 * along with the action's authored timeout and the category's preparation.
 *
 * `timeoutMs` is the action's OWN budget, the floor it needs. It is not the
 * caller's: the caller's arrives in the parameters and wins in runBridge.
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
      // `editor` addresses a session; on this route it is never a bridge
      // parameter, so it comes off before the mapper can forward it.
      const options = stripEditorTarget(this.options as Record<string, unknown>);
      return runBridge(this.ctx, name, method, mapParams, timeoutMs, options, prep);
    }
  }
  Object.defineProperty(FactoryBridgeTask, "name", { value: `BridgeTask_${name}` });
  return FactoryBridgeTask as unknown as TaskConstructor;
}

/**
 * Create a TaskConstructor that wraps an existing async handler function.
 * Used for the direct-handler actions (editor control, project ops, etc.).
 */
export function handlerTaskClass(
  name: string,
  fn: (ctx: FlowContext, params: Record<string, unknown>) => Promise<unknown>,
  prep?: CallPreparation,
): TaskConstructor {
  class FactoryHandlerTask extends UeMcpTask {
    get taskName() { return name; }
    async execute(): Promise<TaskResult> {
      return runHandler(this.ctx, name, fn, this.options as Record<string, unknown>, prep);
    }
  }
  Object.defineProperty(FactoryHandlerTask, "name", { value: `HandlerTask_${name}` });
  return FactoryHandlerTask as unknown as TaskConstructor;
}
