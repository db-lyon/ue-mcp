/**
 * The flow tool's actions, restated as a stand-in category.
 *
 * The flow tool is built per server from a task registry and a config source,
 * so it is registered outside `ALL_TOOLS` and absent from the published graph.
 * Effect gating and the skill check still need its actions, so they read this.
 * Restated rather than read off the built tool because `flow-tool.ts` imports
 * what imports this, and `tests/unit/skills.test.ts` fails if the two disagree.
 */
import type { ActionEffect, ToolDef } from "../types.js";

/**
 * `run` is `mutate` rather than `unknown`: a flow is whatever its steps are,
 * no flow in this repo or any shipped plugin is pure reads, and both labels
 * gate identically.
 */
export const FLOW_ACTION_EFFECTS: Record<string, ActionEffect> = {
  run: "mutate",
  plan: "read",
  list: "read",
};

export function flowCategoryForCheck(): ToolDef {
  return {
    name: "flow",
    description: "",
    schema: {},
    actions: Object.fromEntries(
      Object.entries(FLOW_ACTION_EFFECTS).map(([name, effect]) => [
        name,
        { kind: "handler", effect, handler: async () => ({}) },
      ]),
    ),
    handler: async () => ({}),
  };
}
