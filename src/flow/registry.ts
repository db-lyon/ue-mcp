import { TaskRegistry, ShellTask } from "@db-lyon/flowkit";
import type { TaskConstructor } from "@db-lyon/flowkit";
import type { ToolDef, ActionSpec, ToolContext } from "../types.js";
import type { FlowContext } from "./context.js";
import { BridgeTask } from "./bridge-task.js";
import { bridgeTaskClass, handlerTaskClass } from "./task-factory.js";

/**
 * Walk all category tools and register every action as a flowkit task.
 *
 * - Bridge actions → factory classes with method + mapParams in closure
 * - Handler actions → factory classes wrapping the existing handler function
 *
 * Also registers `ue-mcp.bridge` as a class_path for YAML-defined bridge tasks.
 */
export function buildFlowRegistry(tools: ToolDef[]): TaskRegistry {
  const registry = new TaskRegistry();

  // Register built-in task class paths
  registry.registerClassPath("ue-mcp.bridge", BridgeTask as unknown as TaskConstructor);
  registry.register("shell", ShellTask as unknown as TaskConstructor);

  for (const tool of tools) {
    for (const [actionName, spec] of Object.entries(tool.actions)) {
      const taskName = `${tool.name}.${actionName}`;

      if (spec.handler) {
        // Wrap the existing handler function — adapt ToolContext to FlowContext
        const originalHandler = spec.handler;
        registry.register(
          taskName,
          handlerTaskClass(taskName, (ctx: FlowContext, params: Record<string, unknown>) => {
            const toolCtx: ToolContext = { bridge: ctx.bridge, project: ctx.project };
            return originalHandler(toolCtx, params);
          }),
        );
      } else if (spec.bridge) {
        registry.register(
          taskName,
          bridgeTaskClass(taskName, spec.bridge, spec.mapParams),
        );
      }
    }
  }

  return registry;
}
