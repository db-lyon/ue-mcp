import type { OptionSpecs, TaskResult, TaskConstructor } from "@db-lyon/flowkit";
import { UeMcpTask } from "../task.js";
import { stripEditorTarget } from "../surface/target-params.js";
import type { CallPreparation } from "../dispatch/call-pipeline.js";
import type { FlowContext } from "./context.js";
import { runBridge, runFlowAction, runHandler } from "./run-action.js";
import { takeHeldComposite } from "./composite.js";
import { stripAction } from "../surface/routing-params.js";
import type { FlowActionSpec } from "../core/types.js";

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
 *
 * `optionsSchema` is the action's declared parameters. The flow runner checks
 * a step's options against it before the handler runs.
 */
export function handlerTaskClass(
  name: string,
  fn: (ctx: FlowContext, params: Record<string, unknown>) => Promise<unknown>,
  prep?: CallPreparation,
  optionsSchema?: OptionSpecs,
): TaskConstructor {
  class FactoryHandlerTask extends UeMcpTask {
    static optionsSchema = optionsSchema;
    get taskName() { return name; }
    async execute(): Promise<TaskResult> {
      return runHandler(this.ctx, name, fn, this.options as Record<string, unknown>, prep);
    }
  }
  Object.defineProperty(FactoryHandlerTask, "name", { value: `HandlerTask_${name}` });
  return FactoryHandlerTask as unknown as TaskConstructor;
}

/**
 * A task that reads one value off its context and answers it as `data.value`.
 * The parts a read composite is reduced from; never an action of its own.
 */
export function readTaskClass(name: string, read: (ctx: FlowContext) => unknown): TaskConstructor {
  class FactoryReadTask extends UeMcpTask {
    get taskName() { return name; }
    async execute(): Promise<TaskResult> {
      return { success: true, data: { value: await read(this.ctx) } };
    }
  }
  Object.defineProperty(FactoryReadTask, "name", { value: `ReadTask_${name}` });
  return FactoryReadTask as unknown as TaskConstructor;
}

/**
 * Create a TaskConstructor for a flow-backed action. Its children run through
 * the runner via `ctx.step`; `expand` lists them for plans when the input
 * decides them.
 */
export function compositeTaskClass(
  name: string,
  spec: FlowActionSpec,
  prep?: CallPreparation,
): TaskConstructor {
  class FactoryCompositeTask extends UeMcpTask {
    static expand = spec.expand
      ? (options: Record<string, unknown>) => spec.expand!(stripAction(stripEditorTarget(options)))
      : undefined;
    get taskName() { return name; }
    async execute(): Promise<TaskResult> {
      // The live runner built for this call hands its prepared input over once.
      const held = takeHeldComposite(this.ctx, name);
      if (held) return held.settle(this.ctx);
      const options = stripEditorTarget(this.options as Record<string, unknown>);
      return runFlowAction(this.ctx, name, spec, options, prep);
    }
  }
  Object.defineProperty(FactoryCompositeTask, "name", { value: `CompositeTask_${name}` });
  return FactoryCompositeTask as unknown as TaskConstructor;
}
