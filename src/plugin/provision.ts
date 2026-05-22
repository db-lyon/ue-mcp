import { z } from "zod";
import type { ToolDef, ActionSpec } from "../types.js";
import {
  compileSchemaFields,
  type ManifestProvidedCategory,
} from "./manifest.js";

/**
 * Per-category provision plan derived from one plugin's `provides:` block.
 * Unlike injection, a provided category is owned end-to-end by the plugin:
 * action names are NOT prefixed, the entire top-level MCP tool is the
 * plugin's namespace.
 */
export interface ProvisionPlan {
  /** Provided category name (e.g. `voxel_terrain`). */
  category: string;
  /** Plugin name for diagnostics. */
  pluginName: string;
  /** Category description. */
  description?: string;
  /** Bare action name -> manifest spec. */
  spec: ManifestProvidedCategory;
}

/**
 * Build a top-level ToolDef from one plugin-provided category. Dispatch is
 * via the task registry under `${category}.${action}`, matching how built-in
 * categories route inside index.ts.
 */
export function buildProvidedTool(plan: ProvisionPlan): ToolDef {
  const actions: Record<string, ActionSpec> = {};
  const extraSchema: Record<string, z.ZodType> = {};
  const docLines: string[] = [];

  for (const [actionName, actionSpec] of Object.entries(plan.spec.actions)) {
    actions[actionName] = {
      description:
        actionSpec.description ?? `Plugin action from ${plan.pluginName}`,
    };
    docLines.push(
      actionSpec.description
        ? `- ${actionName}: ${actionSpec.description}`
        : `- ${actionName}`,
    );
    const compiled = compileSchemaFields(actionSpec.schema);
    for (const [k, v] of Object.entries(compiled)) {
      if (!(k in extraSchema)) extraSchema[k] = v;
    }
  }

  const actionNames = Object.keys(actions) as [string, ...string[]];
  const summary =
    plan.description ?? `Plugin-provided category from ${plan.pluginName}`;
  const description = `${summary}\n\nActions:\n${docLines.join("\n")}`;

  return {
    name: plan.category,
    description,
    schema: {
      action: z.enum(actionNames).describe("Action to perform"),
      ...extraSchema,
    },
    actions,
    // Same fallback handler shape as categoryTool() so direct invocations
    // (tests, HTTP surface) keep working even though index.ts dispatches
    // through the registry.
    handler: async () => {
      throw new Error(
        `provided category '${plan.category}' must be dispatched through the task registry; direct handler invocation is not supported`,
      );
    },
  };
}
