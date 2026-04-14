import { BaseTask, type TaskResult, type RollbackRecord } from "@db-lyon/flowkit";
import type { FlowContext } from "./context.js";

/**
 * Generic task for bridge-delegation actions.
 *
 * Used two ways:
 *
 * 1. **YAML-defined tasks** (`class_path: flow.bridge`):
 *    The `method` option specifies the bridge method to call.
 *    Remaining options are passed as bridge params.
 *
 * 2. **Built-in tasks** via `bridgeTaskClass()` factory:
 *    The bridge method is baked into the class closure.
 *    Options are passed through as bridge params.
 *
 * Handlers may attach a `rollback: { method, payload }` to their response.
 * When present, it is lifted onto `TaskResult.rollback` so the flow runner
 * can invoke the inverse on failure when `rollback_on_failure` is enabled.
 */
export class BridgeTask extends BaseTask {
  get taskName() {
    return `bridge:${(this.options as Record<string, unknown>).method ?? "unknown"}`;
  }

  async execute(): Promise<TaskResult> {
    const { method, ...params } = this.options as Record<string, unknown>;
    if (!method || typeof method !== "string") {
      throw new Error('BridgeTask requires a "method" option');
    }
    const ctx = this.ctx as FlowContext;
    const raw = await ctx.bridge.call(method as string, params);

    if (typeof raw !== "object" || raw === null) {
      return { success: true, data: { result: raw } };
    }

    const { rollback, ...rest } = raw as Record<string, unknown>;
    const result: TaskResult = { success: true, data: rest };

    if (rollback && typeof rollback === "object") {
      const rb = rollback as { method?: unknown; payload?: unknown };
      if (typeof rb.method === "string") {
        const record: RollbackRecord = {
          taskName: rb.method,
          payload:
            rb.payload && typeof rb.payload === "object"
              ? (rb.payload as Record<string, unknown>)
              : {},
        };
        result.rollback = record;
      }
    }

    return result;
  }
}
