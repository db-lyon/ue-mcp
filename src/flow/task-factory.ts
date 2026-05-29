import type { TaskResult, TaskConstructor } from "@db-lyon/flowkit";
import { UeMcpTask } from "../task.js";
import type { FlowContext } from "./context.js";

/**
 * Create a TaskConstructor for a bridge-delegation action.
 * The bridge method (and optional param mapper) are closed over in the class.
 */
export function bridgeTaskClass(
  name: string,
  method: string,
  mapParams?: (p: Record<string, unknown>) => Record<string, unknown>,
  timeoutMs?: number,
): TaskConstructor {
  class FactoryBridgeTask extends UeMcpTask {
    get taskName() { return name; }
    async execute(): Promise<TaskResult> {
      const params = mapParams
        ? mapParams(this.options as Record<string, unknown>)
        : this.options as Record<string, unknown>;
      const data = await this.bridge.call(method, params, timeoutMs);
      return {
        success: true,
        data: typeof data === "object" && data !== null
          ? (data as Record<string, unknown>)
          : { result: data },
      };
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
): TaskConstructor {
  class FactoryHandlerTask extends UeMcpTask {
    get taskName() { return name; }
    async execute(): Promise<TaskResult> {
      const data = await fn(this.ctx, this.options as Record<string, unknown>);
      return {
        success: true,
        data: typeof data === "object" && data !== null
          ? (data as Record<string, unknown>)
          : { result: data },
      };
    }
  }
  Object.defineProperty(FactoryHandlerTask, "name", { value: `HandlerTask_${name}` });
  return FactoryHandlerTask as unknown as TaskConstructor;
}
