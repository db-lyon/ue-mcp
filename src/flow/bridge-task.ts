import type { TaskResult } from "@db-lyon/flowkit";
import { UeMcpTask } from "../task.js";
import { stripEditorTarget } from "../surface/target-params.js";
import { runBridge } from "./run-action.js";

/**
 * Generic task for bridge-delegation actions.
 *
 * Used two ways:
 *
 * 1. **YAML-defined tasks** (`class_path: ue-mcp.bridge`):
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
 *
 * This class is also the inverse executor: `liftRollback` names
 * `ue-mcp.bridge` as the task the runner replays, so a rollback that the
 * editor refuses is reported through the same reading of the body as any
 * other step.
 */
export class BridgeTask extends UeMcpTask {
  get taskName() {
    return `bridge:${(this.options as Record<string, unknown>).method ?? "unknown"}`;
  }

  async execute(): Promise<TaskResult> {
    const { method, ...rest } = this.options as Record<string, unknown>;
    // `editor` selects the session the step runs in, so it must not travel
    // on to the editor as a bridge parameter.
    const params = stripEditorTarget(rest);
    if (!method || typeof method !== "string") {
      throw new Error('BridgeTask requires a "method" option');
    }
    // Through the one per-call path, so the step repairs paths, takes the
    // locks its method writes and reads its verdict like any action does.
    // The method name stands in for the action; its effect is the method's.
    return runBridge(this.ctx, method, method, undefined, this.ctx.callTimeoutMs, params);
  }
}

/**
 * `ue-mcp.reconnect`: connect this run's bridge, waiting up to `timeoutMs`
 * (default 5000). A connect that does not land is left to the bridge's own
 * reconnect timer, so the step never fails.
 */
export class ReconnectTask extends UeMcpTask<{ timeoutMs?: number }> {
  get taskName() {
    return "ue-mcp.reconnect";
  }

  async execute(): Promise<TaskResult> {
    try {
      await this.bridge.connect(this.options.timeoutMs ?? 5000);
    } catch {
      // The reconnect timer handles it.
    }
    return { success: true, data: { connected: this.bridge.isConnected } };
  }
}
