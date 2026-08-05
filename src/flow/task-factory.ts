import type { TaskResult, TaskConstructor } from "@db-lyon/flowkit";
import { UeMcpTask } from "../task.js";
import { stripEditorTarget } from "../types.js";
import type { FlowContext } from "./context.js";
import { liftRollback } from "./rollback.js";

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
      // `editor` addresses a session; it is never a bridge parameter. Strip it
      // before the mapper too, since a mapper that forwards its input verbatim
      // would carry it into the call.
      const options = stripEditorTarget(this.options as Record<string, unknown>);
      const params = mapParams ? mapParams(options) : options;
      const raw = await this.bridge.call(method, params, timeoutMs);
      if (typeof raw !== "object" || raw === null) {
        return { success: true, data: { result: raw } };
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
      const record = liftRollback(raw2.rollback);
      if (record) result.rollback = record;
      return result;
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
