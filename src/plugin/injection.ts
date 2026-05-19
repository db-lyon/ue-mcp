import { z } from "zod";
import type { ToolDef, ActionSpec } from "../types.js";
import { compileSchemaFields, type ManifestInjectAction } from "./manifest.js";

/**
 * Per-category injection plan derived from one plugin's `inject:` block.
 * The keys are PREFIXED action names (e.g. `vpp_scatter_on_terrain`), which
 * is what they look like on the wire and in tool docs.
 */
export interface InjectionPlan {
  /** Category name (e.g. `pcg`). */
  category: string;
  /** Plugin's actionPrefix (e.g. `vpp`). */
  prefix: string;
  /** Plugin name for diagnostics. */
  pluginName: string;
  /** Bare action name → manifest spec. */
  actions: Record<string, ManifestInjectAction>;
}

/**
 * Merge plugin-supplied injection plans into a built-in category tool. Returns
 * a NEW ToolDef with extended actions, rebuilt action enum, merged param
 * schema, and a "Plugin actions:" block appended to the description.
 *
 * Collisions with built-in actions are skipped with a warning and never
 * overwrite. Inter-plugin collisions are first-wins per the loader's left-to-
 * right iteration; this function simply skips any action key already present.
 */
export interface MergeResult {
  tool: ToolDef;
  /** Final prefixed action names that were actually added, in insertion order. */
  added: string[];
  /** Collisions that were rejected: key + reason. */
  skipped: Array<{ action: string; reason: string }>;
}

export function mergeInjectionsIntoTool(
  orig: ToolDef,
  plans: InjectionPlan[],
): MergeResult {
  const newActions: Record<string, ActionSpec> = { ...orig.actions };
  const added: string[] = [];
  const skipped: Array<{ action: string; reason: string }> = [];
  const extraSchema: Record<string, z.ZodType> = {};
  const docLines: string[] = [];

  for (const plan of plans) {
    for (const [bareName, injectSpec] of Object.entries(plan.actions)) {
      const prefixed = `${plan.prefix}_${bareName}`;
      if (newActions[prefixed]) {
        skipped.push({
          action: prefixed,
          reason: orig.actions[prefixed]
            ? `built-in action '${prefixed}' on ${orig.name}; never overridden`
            : `already injected by an earlier plugin on ${orig.name}`,
        });
        continue;
      }

      // Plugin actions dispatch via the registry under `${tool}.${action}`;
      // no bridge/handler is needed on the ActionSpec itself. The description
      // is what surfaces in the tool's AI docs.
      newActions[prefixed] = {
        description: injectSpec.description ?? `Plugin action from ${plan.pluginName}`,
      };
      added.push(prefixed);
      docLines.push(
        injectSpec.description
          ? `- ${prefixed}: ${injectSpec.description}`
          : `- ${prefixed} (${plan.pluginName})`,
      );

      // Lift per-action schema fields to the top level as optional, matching
      // the pattern built-in categories already use (one flat schema, action
      // selects which params apply).
      const compiled = compileSchemaFields(injectSpec.schema);
      for (const [k, v] of Object.entries(compiled)) {
        if (!(k in extraSchema) && !(k in orig.schema)) {
          extraSchema[k] = v;
        }
      }
    }
  }

  if (added.length === 0) {
    return { tool: orig, added, skipped };
  }

  const allNames = Object.keys(newActions) as [string, ...string[]];
  const newDescription =
    orig.description + "\n\nPlugin actions:\n" + docLines.join("\n");

  const newSchema: Record<string, z.ZodType> = {
    ...orig.schema,
    action: z.enum(allNames).describe("Action to perform"),
    ...extraSchema,
  };

  // Keep the original handler — it is no longer called by index.ts (dispatch
  // goes through the registry) but other call sites (tests, the HTTP flow
  // surface) may still invoke ToolDef.handler directly.
  return {
    tool: {
      ...orig,
      description: newDescription,
      schema: newSchema,
      actions: newActions,
    },
    added,
    skipped,
  };
}
