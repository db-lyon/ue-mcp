// @session per-session: Loads each project's own ue-mcp.yml.
import { deepMerge, type LoadedConfig } from "@db-lyon/flowkit";
import { FlowConfigSchema, type FlowConfig } from "./schema.js";
import { projectLayerFiles, readConfigDoc, readGlobalConfigDoc, selectedOverlay } from "../config/ue-mcp-config.js";
import type { ToolDef } from "../surface/tool-types.js";
import { builtinFlows, builtinTaskDefinitions } from "./builtins.js";

/**
 * The defaults every flow config starts from. scripts/generate-default-config.ts
 * dumps exactly this as dist/ue-mcp.default.yml, and
 * tests/unit/default-config.test.ts fails if the two ever differ.
 */
export function buildDefaults(tools: ToolDef[]): Record<string, unknown> {
  const tasks: Record<string, unknown> = {};

  for (const tool of tools) {
    for (const [actionName, spec] of Object.entries(tool.actions)) {
      const taskName = `${tool.name}.${actionName}`;

      // class_path always matches the task name so the per-action factory
      // class (registered in registry.ts with mapParams baked in) is the one
      // that runs. The previous default of class_path: "ue-mcp.bridge" routed
      // every bridge action through the generic BridgeTask which silently
      // dropped mapParams - so YAML callers had to know each handler's exact
      // C++-side param names instead of the documented TS-side ones.
      const taskDef: Record<string, unknown> = {
        class_path: taskName,
        group: tool.name,
      };
      if (spec.description) taskDef.description = spec.description;

      tasks[taskName] = taskDef;
    }
  }

  Object.assign(tasks, builtinTaskDefinitions());

  return { tasks, flows: builtinFlows() };
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
 * Load ue-mcp.yml from the given directory, layered on top of built-in defaults.
 * Returns the merged config even if no project ue-mcp.yml exists.
 *
 * Layer order (lowest precedence first):
 *   built-in defaults
 *   plugin contributions
 *   ~/.ue-mcp/config.yml (warn and ignore on read/parse errors)
 *   ue-mcp.yml
 *   ue-mcp.{env}.yml (UE_MCP_ENV, else the `env:` key)
 *   ue-mcp.local.yml
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
  let defaults = buildDefaults(tools);

  if (pluginContribution) {
    const baseTasks = (defaults.tasks ?? {}) as Record<string, unknown>;
    const baseFlows = (defaults.flows ?? {}) as Record<string, unknown>;
    defaults.tasks = { ...baseTasks, ...(pluginContribution.tasks ?? {}) };
    defaults.flows = { ...baseFlows, ...(pluginContribution.flows ?? {}) };
  }

  const globalDoc = readGlobalConfigDoc(onGlobalConfigError);
  if (Object.keys(globalDoc).length > 0) {
    defaults = deepMerge(defaults, globalDoc) as Record<string, unknown>;
  }

  // The same files, in the same order, with the same overlay rule as the
  // `ue-mcp:` block (config/project.ts). Any layer may be absent.
  let merged: Record<string, unknown> = defaults;
  for (const layer of projectLayerFiles(dir, selectedOverlay(dir))) {
    merged = deepMerge(merged, readConfigDoc(layer.file)) as Record<string, unknown>;
  }
  return { config: FlowConfigSchema.parse(merged), configDir: dir };
}
