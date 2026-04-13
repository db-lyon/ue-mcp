import * as fs from "node:fs";
import * as path from "node:path";
import { loadConfig, type LoadedConfig } from "@db-lyon/flowkit";
import { FlowConfigSchema, type FlowConfig } from "./schema.js";
import type { ToolDef } from "../types.js";

/**
 * Build the defaults object from tool definitions.
 * This is the runtime equivalent of scripts/generate-default-config.ts.
 */
export function buildDefaults(tools: ToolDef[]): Record<string, unknown> {
  const tasks: Record<string, unknown> = {};

  for (const tool of tools) {
    for (const [actionName, spec] of Object.entries(tool.actions)) {
      const taskName = `${tool.name}.${actionName}`;
      const isBridge = !!spec.bridge;

      const taskDef: Record<string, unknown> = {
        class_path: isBridge ? "ue-mcp.bridge" : taskName,
        group: tool.name,
      };
      if (spec.description) taskDef.description = spec.description;
      if (isBridge) taskDef.options = { method: spec.bridge };

      tasks[taskName] = taskDef;
    }
  }

  // Built-in shell task
  tasks["shell"] = {
    class_path: "shell",
    group: "util",
    description: "Run a shell command. Params: command, cwd?, timeout?",
  };

  return { tasks, flows: {} };
}

/**
 * Load ue-mcp.yml from the given directory, layered on top of built-in defaults.
 * Returns the merged config even if no project ue-mcp.yml exists.
 */
export function loadFlowConfig(
  tools: ToolDef[],
  configDir?: string,
): LoadedConfig<FlowConfig> | null {
  const dir = configDir ?? process.cwd();
  const configPath = path.join(dir, "ue-mcp.yml");

  if (!fs.existsSync(configPath)) return null;

  return loadConfig({
    filename: "ue-mcp.yml",
    schema: FlowConfigSchema,
    defaults: buildDefaults(tools),
    envVar: "UE_MCP_ENV",
    configDir: dir,
  });
}
