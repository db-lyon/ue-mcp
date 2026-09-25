import { TaskRegistry, ShellTask } from "@db-lyon/flowkit";
import type { TaskConstructor, TaskContextInput } from "@db-lyon/flowkit";
import type { ToolDef } from "../types.js";
import type { CallPreparation } from "../call-pipeline.js";
import type { FlowContext } from "./context.js";
import { BridgeTask } from "./bridge-task.js";
import { bridgeTaskClass, handlerTaskClass } from "./task-factory.js";
import { MICRO_GATEWAY_TOOL, MICRO_GATEWAY_CALL, microGatewayTargets, resolveMicroCall } from "../lean-context.js";
import { McpError, ErrorCode } from "../errors.js";

/** A gateway call is an alias for the target task, not a handler that executes
 *  another action and repackages its result. Both MCP and FlowRunner create
 *  tasks here, so plugin dispatch, failures and rollback keep the same path. */
class MicroTaskRegistry extends TaskRegistry {
  /** `targets` is what the gateway itself reaches: the enabled categories. */
  constructor(private readonly targets: ToolDef[]) {
    super();
  }

  override async create(name: string, ctx: TaskContextInput, options: Record<string, unknown>) {
    if (name === `${MICRO_GATEWAY_TOOL}.${MICRO_GATEWAY_CALL}`) {
      const graph = (ctx as FlowContext).getToolGraph?.() ?? this.targets;
      const call = resolveMicroCall(graph, options);
      if (!this.listRegistered().includes(call.taskName)) {
        throw new McpError(ErrorCode.NO_HANDLER, `Action ${call.taskName} has no registered task.`);
      }
      return super.create(call.taskName, ctx, call.params);
    }
    return super.create(name, ctx, options);
  }
}

/**
 * Walk all category tools and register every action as a flowkit task.
 *
 * - Bridge actions → factory classes with method + mapParams in closure
 * - Handler actions → factory classes wrapping the existing handler function
 *
 * Also registers `ue-mcp.bridge` as a class_path for YAML-defined bridge tasks.
 */
export function buildFlowRegistry(tools: ToolDef[]): TaskRegistry {
  // The registry keeps disabled categories so flows can name them directly;
  // the gateway resolves only the categories it was built from.
  const gateway = tools.find((tool) => tool.name === MICRO_GATEWAY_TOOL);
  const registry = gateway
    ? new MicroTaskRegistry(microGatewayTargets(gateway) ?? tools)
    : new TaskRegistry();

  // Register built-in task class paths
  registry.registerClassPath("ue-mcp.bridge", BridgeTask as unknown as TaskConstructor);
  registry.register("shell", ShellTask as unknown as TaskConstructor);

  for (const tool of tools) {
    for (const [actionName, spec] of Object.entries(tool.actions)) {
      const taskName = `${tool.name}.${actionName}`;

      // Everything the shared preparation cannot work out for itself. This is
      // the live dispatch route, so anything missing from here is a piece of
      // the advertised contract that only works in the tests: the category's
      // parameter folding, the action name that folding branches on, and the
      // nesting a gateway's parameters arrive under.
      const prep: CallPreparation = {
        action: actionName,
        normalizeParams: tool.options?.normalizeParams,
        paramGroups: tool.options?.paramGroups,
        nestedParamsKey: tool.options?.nestedParamsKey,
      };

      if (spec.handler) {
        // FlowContext is a structural superset of ToolContext (see
        // context.ts), so we pass ctx straight through. Rebuilding it
        // field-by-field used to silently drop new accessors at this
        // boundary - never reintroduce that pattern.
        const originalHandler = spec.handler;
        registry.register(
          taskName,
          handlerTaskClass(taskName, (ctx: FlowContext, params: Record<string, unknown>) => {
            return originalHandler(ctx, params);
          }, prep),
        );
      } else if (spec.bridge) {
        registry.register(
          taskName,
          // The action's authored budget travels with it. Three actions
          // (blueprint.flush_component_templates, widget.add_widget,
          // widget.remove_widget) declare 120s because their method has no
          // entry in the editor's own timeout table, and dropping it here gave
          // them the 30s default on every live call.
          bridgeTaskClass(taskName, spec.bridge, spec.mapParams, spec.timeoutMs, prep),
        );
      }
    }
  }

  return registry;
}
