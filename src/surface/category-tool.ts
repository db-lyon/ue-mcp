/**
 * Category tool construction: the `categoryTool` dispatcher, the `bp` action
 * builder and the routing parameters every category tool offers.
 */
import { z } from "zod";
import { McpError, ErrorCode } from "../core/errors.js";
import { MAX_BRIDGE_TIMEOUT_MS } from "../bridge/bridge-timeouts.js";
import { unknownActionMessage } from "./action-schema.js";
import { stripAction } from "./routing-params.js";
import { stripEditorTarget } from "./target-params.js";
import type { FlowContext } from "../flow/context.js";
import { runAction, actionPreparation } from "../flow/run-action.js";
import type { ActionEffect, ActionSpec, BridgeActionSpec, CategoryOptions, ToolDef } from "../core/types.js";

/**
 * An independent copy of one tool. Prefers the rebuilder so the copy's
 * handler dispatches against the copy's actions; falls back to a structural
 * copy of the mutated containers for hand-written tools, whose handlers do
 * not read `actions` at all.
 */
export function cloneToolDef(tool: ToolDef): ToolDef {
  // An action-less tool cannot go back through categoryTool: the action enum
  // it builds needs at least one name. Nothing enriches such a tool either, so
  // the structural copy below is sufficient.
  if (tool.rebuild && Object.keys(tool.actions).length > 0) {
    const copy = tool.rebuild({ ...tool.actions });
    copy.description = tool.description;
    copy.schema = { ...tool.schema };
    copy.injectedEditorParam = tool.injectedEditorParam;
    copy.injectedMigrateParam = tool.injectedMigrateParam;
    return copy;
  }
  return {
    ...tool,
    schema: { ...tool.schema },
    actions: { ...tool.actions },
  };
}

/** An independent copy of a whole tool graph. */
export function cloneToolGraph(tools: ToolDef[]): ToolDef[] {
  return tools.map(cloneToolDef);
}

/**
 * The action names an `action` schema advertises.
 *
 * `actionEnum` wraps the enum in a union with a bare string, so reading
 * `_def.values` off it finds nothing. Callers that want the list (the golden
 * recorder, plugin injection tests, anything reporting the surface) go
 * through here rather than reaching into a shape that has already moved once.
 */
export function actionEnumValues(schema: z.ZodType): string[] {
  const def = (schema as unknown as { _def?: { typeName?: string; values?: unknown; options?: z.ZodTypeAny[] } })._def;
  if (!def) return [];
  if (def.typeName === "ZodEnum" && Array.isArray(def.values)) return def.values.map(String);
  if (def.typeName === "ZodUnion" && Array.isArray(def.options)) {
    for (const option of def.options) {
      const values = actionEnumValues(option);
      if (values.length > 0) return values;
    }
  }
  return [];
}

/**
 * The `action` parameter of a category tool.
 *
 * Advertised as an enum, parsed as a string, and the difference matters.
 *
 * The MCP layer validates arguments BEFORE the tool callback runs, so a strict
 * `z.enum` meant a misspelled action never reached dispatch: it came back as a
 * zod issue whose message is the serialized issue list, which carries the full
 * `options` array. On level, with 140 actions, a single typo returned about
 * 8KB naming every action twice and burying the one the caller wanted.
 *
 * Accepting any string moves the refusal into `categoryTool`, which answers
 * with the closest spellings in a couple of lines. The enum stays in the
 * published schema, so a client still gets the list and the agent still gets
 * the guidance; only the failure path changed.
 */
export function actionEnum(names: [string, ...string[]]): z.ZodType {
  return z
    .union([z.enum(names), z.string()])
    .describe("Action to perform. One of the listed values; anything else returns the closest matches.");
}

export const SELECT_PARAM = z
  .union([z.string(), z.array(z.string())])
  .optional()
  // Kept short deliberately: this is repeated in every one of the 24 tool
  // schemas the client loads at startup, so the full account of the semantics
  // lives in project(describe_action) rather than 24 times in the manifest.
  .describe(
    "Keep only these result fields (dotted paths; arrays are traversed, so "
    + "'components.name' keeps every component's name). Unmatched paths are reported.",
  );

export const OMIT_PARAM = z
  .union([z.string(), z.array(z.string())])
  .optional()
  .describe(
    "Drop these result fields (dotted paths, same traversal as select). Runs after select.",
  );

/**
 * The per-call timeout budget, offered by every category tool (#989).
 *
 * It is a routing instruction, never a handler parameter: the dispatcher reads
 * it and strips it, so it cannot reach a bridge method as an argument.
 */
export const TIMEOUT_PARAM = z
  .number()
  .int()
  .positive()
  .max(MAX_BRIDGE_TIMEOUT_MS)
  .optional()
  .describe(
    "How long to wait for this call, in milliseconds. Omitted, the wait is 30s, "
    + "or longer for the actions the editor itself allows longer. Raise it for a "
    + "large batch or an editor busy compiling shaders. A timeout never means the "
    + "call did not happen: read the state back before retrying.",
  );

/** The parameters the dispatcher consumes, built once per category. */
function routingSchema(actionNames: [string, ...string[]]): Record<string, z.ZodType> {
  return {
    action: actionEnum(actionNames),
    // #989: a call budget the caller controls. The client used to wait a flat
    // 30s for every bridge call, and a large batch on a machine that is also
    // compiling shaders finished in the editor after the client had already
    // reported a failure. A retry then applied the mutation twice.
    timeoutMs: TIMEOUT_PARAM,
    select: SELECT_PARAM,
    omit: OMIT_PARAM,
  };
}

/**
 * A page size is at least one row. Applied to every category's `limit` that
 * states no lower bound of its own; anything that is not an optional number
 * is left as declared.
 */
function boundLimit(limit: z.ZodType | undefined): z.ZodType | undefined {
  if (!(limit instanceof z.ZodOptional)) return limit;
  const inner = limit.unwrap();
  if (!(inner instanceof z.ZodNumber) || inner.minValue !== null) return limit;
  const bounded = inner.positive().optional();
  return limit.description === undefined ? bounded : bounded.describe(limit.description);
}

export function categoryTool(
  name: string,
  summary: string,
  actions: Record<string, ActionSpec>,
  extraSchema?: Record<string, z.ZodType>,
  options?: CategoryOptions,
): ToolDef {
  const actionNames = Object.keys(actions) as [string, ...string[]];
  const routing = routingSchema(actionNames);

  const docs = actionNames
    .map((a) => {
      const desc = actions[a].description;
      return desc ? `- ${a}: ${desc}` : `- ${a}`;
    })
    .join("\n");

  // Spread twice: the first sets the order, the last wins, so a category
  // cannot replace a parameter the dispatcher consumes.
  const schema: Record<string, z.ZodType> = { ...routing, ...extraSchema, ...routing };
  if (schema.limit) schema.limit = boundLimit(schema.limit)!;

  const def: ToolDef = {
    name,
    options,
    description: `${summary}\n\nActions:\n${docs}`,
    schema,
    actions,
    handler: async (ctx, rawParams) => {
      // `editor` is a routing instruction, never a handler parameter, and only
      // on a tool that had it injected. Strip it here so no path can forward
      // it into a bridge call.
      const params = def.injectedEditorParam ? stripEditorTarget(rawParams) : rawParams;
      const action = params.action as string;
      const spec = actions[action];
      if (!spec) {
        // Read the live keys, not the construction-time tuple: enrichment adds
        // epic_* actions after the fact, and a stale list here sends an agent
        // hunting for an action the tool actually has.
        //
        // A category can carry hundreds of actions, and pasting all of them
        // into every typo's error spends more context than the call would
        // have. Lead with the closest spellings, which is what a typo needs,
        // and name the two ways to see the rest.
        throw new McpError(ErrorCode.UNKNOWN_ACTION, unknownActionMessage(action, name, Object.keys(actions)));
      }
      // The same run path a live call's task takes (flow/run-action.ts): one
      // preparation, the verdict read off the body, the modal check on a
      // handler. `action` names the task there, so it comes off here too.
      const result = await runAction(
        ctx as FlowContext,
        `${name}.${action}`,
        spec,
        stripAction(params),
        actionPreparation(options, action, spec),
      );
      // A failed run still carries the body that says why (a modal refusal,
      // or a handler's own verdict inside a flow); hand that back as the
      // live route does.
      if (result.success || result.data !== undefined) return result.data;
      throw result.error ?? new Error(`Action '${name}.${action}' failed`);
    },
  };
  // Rebuilding through the same constructor is what makes a per-session copy
  // real: the handler closes over `actions`, so a copy that only replaced the
  // record would still dispatch against the original's.
  def.rebuild = (nextActions) =>
    categoryTool(name, summary, nextActions, extraSchema, options);
  return def;
}

type MapParams = (p: Record<string, unknown>) => Record<string, unknown>;

/**
 * Declare an action that forwards to a bridge method.
 *
 * The effect comes FIRST and there is no overload without it, which is what
 * forces the answer at every one of the thousand-odd call sites rather than
 * leaving it to a verb list somewhere else to work out afterwards. It is the
 * only argument a caller cannot derive from the rest of the line.
 */
export function bp(effect: ActionEffect, bridge: string, mapParams?: MapParams): BridgeActionSpec;
export function bp(effect: ActionEffect, description: string, bridge: string, mapParams?: MapParams): BridgeActionSpec;
export function bp(effect: ActionEffect, ...args: unknown[]): BridgeActionSpec {
  // bp(effect, bridge) or bp(effect, bridge, mapParams) - no description
  // bp(effect, description, bridge[, mapParams]) - with description
  if (args.length >= 2 && typeof args[0] === "string" && typeof args[1] === "string") {
    return {
      kind: "bridge",
      effect,
      description: args[0] as string,
      bridge: args[1] as string,
      mapParams: args[2] as MapParams | undefined,
    };
  }
  return {
    kind: "bridge",
    effect,
    bridge: args[0] as string,
    mapParams: args[1] as MapParams | undefined,
  };
}
