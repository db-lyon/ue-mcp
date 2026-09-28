import { deepMerge, type LoadedConfig } from "@db-lyon/flowkit";
import { FlowConfigSchema, type FlowConfig } from "./schema.js";
import { ProjectConfig } from "../config/project-config.js";
import type { ToolDef } from "../core/types.js";
import { actionTaskEntry, universalFlows, universalTask } from "./universal.js";

/**
 * The defaults layer for `tools`: the universal layer's flows, and a task per
 * action. An action the universal layer ships reads its entry from there; one
 * it cannot know (a plugin's injected action) gets the same projection the
 * generator wrote the rest with.
 */
export function buildDefaults(tools: ToolDef[]): Record<string, unknown> {
  const tasks: Record<string, unknown> = {};
  for (const tool of tools) {
    for (const [actionName, spec] of Object.entries(tool.actions)) {
      const taskName = `${tool.name}.${actionName}`;
      tasks[taskName] = universalTask(taskName) ?? actionTaskEntry(tool.name, actionName, spec);
    }
  }
  tasks["shell"] = universalTask("shell");
  return { tasks, flows: universalFlows() };
}

/** Built-in flows that ship with ue-mcp: the universal layer's. */
export function builtinFlows(): Record<string, unknown> {
  return universalFlows();
}

/**
 * Plugin-contributed tasks and flows to merge into the defaults layer. The
 * user's own ue-mcp.yml continues to win - plugins sit between built-ins and
 * user config in the layered order, so a user can always override.
 */
export interface PluginContribution {
  tasks?: Record<string, unknown>;
  flows?: Record<string, unknown>;
}

/**
 * Load the flow config for a directory, layered on top of built-in defaults.
 * Returns the merged config even if no project ue-mcp.yml exists.
 *
 * Layer order (lowest precedence first):
 *   universal layer (universal/ue-mcp.universal.yml)
 *   plugin contributions
 *   the project's cascade (config/project-config.ts): user-global, ue-mcp.yml,
 *   the selected overlay, ue-mcp.local.yml
 *
 * onGlobalConfigError lets a live reader preserve an earlier valid snapshot;
 * the default warns and ignores the optional user-global file.
 */
export function loadFlowConfig(
  tools: ToolDef[],
  configDir?: string,
  pluginContribution?: PluginContribution,
  onGlobalConfigError?: (file: string, error: unknown) => void,
): LoadedConfig<FlowConfig> {
  const dir = configDir ?? process.cwd();
  const defaults = buildDefaults(tools);

  if (pluginContribution) {
    const baseTasks = (defaults.tasks ?? {}) as Record<string, unknown>;
    const baseFlows = (defaults.flows ?? {}) as Record<string, unknown>;
    defaults.tasks = { ...baseTasks, ...(pluginContribution.tasks ?? {}) };
    defaults.flows = { ...baseFlows, ...(pluginContribution.flows ?? {}) };
  }

  const layered = ProjectConfig.for(dir).flowDoc(onGlobalConfigError);
  return { config: FlowConfigSchema.parse(deepMerge(defaults, layered)), configDir: dir };
}
