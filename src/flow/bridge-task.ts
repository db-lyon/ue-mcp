import type { TaskResult } from "@db-lyon/flowkit";
import { UeMcpTask } from "../task.js";
import { stripEditorTarget } from "../surface/target-params.js";
import type { CallPreparation } from "../dispatch/call-pipeline.js";
import { bridgeFamily, runLifecycle, type ActionFamily } from "./run-action.js";
import type { TaskRequirements } from "./task-env.js";

/**
 * The base of every action task: one lifecycle (prepare, gate, lock, execute,
 * verdict) run by runLifecycle, with the family supplying each phase. A
 * subclass names its family; it never reimplements a phase.
 */
export abstract class ActionTask extends UeMcpTask {
  /** The family this task's action belongs to. */
  protected abstract family(): ActionFamily;

  /** The options the action reads: `editor` selects the session, so it is never one. */
  protected actionOptions(): Record<string, unknown> {
    return stripEditorTarget(this.options as Record<string, unknown>);
  }

  async execute(): Promise<TaskResult> {
    return runLifecycle(this.ctx, this.family(), this.actionOptions());
  }
}

/** A bridge method bound to an action at registration. */
export interface BridgeBinding {
  name: string;
  method: string;
  mapParams?: (p: Record<string, unknown>) => Record<string, unknown>;
  /** The action's OWN budget, the floor it needs. The caller's wins over it. */
  timeoutMs?: number;
  prep?: CallPreparation;
}

/**
 * The one bridge executor.
 *
 * Every built-in bridge action is a subclass with its method bound
 * (`bridgeTaskClass`). Registered bare as `ue-mcp.bridge`, it takes the method
 * from its `method` option, for YAML-defined tasks; either way the call goes
 * through the same lifecycle, so it repairs paths, takes the locks its method
 * writes and reads its verdict like any action does.
 *
 * Handlers may attach a `rollback: { method, payload }` to their response;
 * it is lifted onto `TaskResult.rollback`. This class is also the inverse
 * executor: `liftRollback` names `ue-mcp.bridge` as the task the runner
 * replays.
 */
export class BridgeTask extends ActionTask {
  static requires: TaskRequirements = { editor: true };
  /** Set on a subclass whose method is bound; absent on the bare `ue-mcp.bridge`. */
  static binding?: BridgeBinding;

  private get bound(): BridgeBinding | undefined {
    return (this.constructor as typeof BridgeTask).binding;
  }

  get taskName() {
    return this.bound?.name ?? `bridge:${(this.options as Record<string, unknown>).method ?? "unknown"}`;
  }

  protected override actionOptions(): Record<string, unknown> {
    const options = super.actionOptions();
    if (this.bound) return options;
    const { method: _method, ...rest } = options;
    return rest;
  }

  protected family(): ActionFamily {
    const bound = this.bound;
    if (bound) return bridgeFamily(bound.name, bound.method, bound.mapParams, bound.timeoutMs, bound.prep);
    const method = (this.options as Record<string, unknown>).method;
    if (!method || typeof method !== "string") {
      throw new Error('BridgeTask requires a "method" option');
    }
    // The method name stands in for the action; its effect is the method's.
    // The caller's budget arrives on the context, as a flow step's does.
    return bridgeFamily(method, method, undefined, this.ctx.callTimeoutMs);
  }
}

/**
 * `ue-mcp.reconnect`: connect this run's bridge, waiting up to `timeoutMs`
 * (default 5000). A connect that does not land is left to the bridge's own
 * reconnect timer, so the step never fails.
 */
export class ReconnectTask extends UeMcpTask<{ timeoutMs?: number }> {
  static requires: TaskRequirements = { editor: true };

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
