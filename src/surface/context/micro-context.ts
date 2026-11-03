/**
 * Micro context strategy: collapse the entire surface behind a single gateway
 * tool, mirroring the native MCP toolset gateway (list_toolsets /
 * describe_toolset / call_tool). The category tools are NOT advertised; the
 * agent enumerates with list_categories, learns a category with describe, and
 * invokes anything with call. This is the smallest possible seed.
 *
 * Live calls and flow steps resolve to the target's registered task. The
 * gateway's own call handler also supports direct handler/bridge calls for
 * embedders that have no task registry; plugin actions require the registry
 * route.
 */
import { z } from "zod";
import { registeredMethodsOf, withholdFromTool, withholdUnregistered } from "../registered-surface.js";
import type { ActionSpec, ToolDef } from "../../core/types.js";
import { categoryTool } from "../category-tool.js";
import { EDITOR_TARGET_PARAM, stripAction } from "../routing-params.js";
import { createFlowTool } from "../../flow/flow-tool.js";
import { microGuide } from "./instructions.js";
import { takeTimeout } from "../../dispatch/call-pipeline.js";
import { McpError, ErrorCode } from "../../core/errors.js";
import { actionSchema } from "../action-schema.js";
import { takeFieldSelection } from "./field-select.js";
import { coerceJsonStringArgs, flatValidationMessage, unknownRecord } from "./call-envelope.js";
import { describeCategory, discoveryResults, readOffset, splitDescription } from "./lean-context.js";

/**
 * The one tool micro mode advertises. Named here rather than inline because
 * dispatch has to recognise it: a call through the gateway carries its real
 * category and action as parameters, so anything classifying the call (the
 * multi-editor write gate, #817) has to look past the gateway to find them.
 */
export const MICRO_GATEWAY_TOOL = "tools";

/** The gateway action that invokes something. `category` + `method` name it. */
export const MICRO_GATEWAY_CALL = "call";

/**
 * The flow tool is a gateway category in micro mode, not a second advertised
 * tool. Dispatch routes `call` with this category to the flow route.
 */
export const MICRO_FLOW_CATEGORY = "flow";

/** A gateway `call` aimed at the flow category, as the flow tool's own params; null otherwise. */
export function microFlowCall(tool: ToolDef, params: Record<string, unknown>): Record<string, unknown> | null {
  if (tool.name !== MICRO_GATEWAY_TOOL || params.action !== MICRO_GATEWAY_CALL || params.category !== MICRO_FLOW_CATEGORY) return null;
  const method = typeof params.method === "string" ? params.method : "";
  refuseNestedAction(MICRO_FLOW_CATEGORY, method, params.args);
  const args = params.args && typeof params.args === "object" && !Array.isArray(params.args)
    ? params.args as Record<string, unknown>
    : {};
  const editor = params[EDITOR_TARGET_PARAM];
  return { ...args, action: method, ...(editor === undefined ? {} : { [EDITOR_TARGET_PARAM]: editor }) };
}

/** Unwrap the gateway without preparing paths or projecting a result twice.
 *  The target's existing task owns that work. Routing options beside `args`
 *  override the same options inside it, just as prepareCall promises. */
export function microCallParams(params: Record<string, unknown>): Record<string, unknown> {
  const inner = params.args && typeof params.args === "object" && !Array.isArray(params.args)
    ? params.args as Record<string, unknown>
    : {};
  const timeoutMs = takeTimeout(params).timeoutMs ?? takeTimeout(inner).timeoutMs;
  const outerSelection = takeFieldSelection(params).selection;
  const innerSelection = takeFieldSelection(inner).selection;
  const select = outerSelection.select ?? innerSelection.select;
  const omit = outerSelection.omit ?? innerSelection.omit;
  return {
    ...stripAction(inner),
    ...(timeoutMs === undefined ? {} : { timeoutMs }),
    ...(select === undefined ? {} : { select }),
    ...(omit === undefined ? {} : { omit }),
  };
}

/** Resolve only advertised actions; never turn gateway arguments into a
 *  task class path that the registry could import from disk. */
/**
 * `action` selects the method on every category, so an `args.action` would be
 * overwritten and never reach the handler (#1216). It is refused, not dropped,
 * unless it only repeats the method name.
 */
function refuseNestedAction(category: string, method: string, args: unknown): void {
  const given = args && typeof args === "object" ? (args as Record<string, unknown>).action : undefined;
  // Repeating the method name is harmless; anything else would be lost.
  if (given === undefined || given === method) return;
  throw new McpError(
    ErrorCode.INVALID_PARAMS,
    `args.action was not sent to ${category}.${method}: 'action' is the dispatcher's own key on every category `
      + `and never reaches a handler. If this action takes an action asset or name, pass it under its other `
      + `parameter name; tools(action="describe", category="${category}", method="${method}") lists them.`,
  );
}

export function resolveMicroCall(tools: ToolDef[], params: Record<string, unknown>) {
  const category = typeof params.category === "string" ? params.category : "";
  const method = typeof params.method === "string" ? params.method : "";
  const tool = tools.find((candidate) => candidate.name === category);
  if (!tool && category === MICRO_FLOW_CATEGORY) {
    throw new McpError(ErrorCode.UNKNOWN_ACTION, `A flow step cannot call the flow category; nest it with a "flow:" step instead.`);
  }
  if (!tool) {
    throw new McpError(ErrorCode.UNKNOWN_ACTION, `Unknown category "${category}". Use tools(action="list_categories").`);
  }
  if (!Object.hasOwn(tool.actions, method)) {
    throw new McpError(ErrorCode.UNKNOWN_ACTION, `Unknown action "${method}" on ${category}. Use tools(action="describe", category="${category}").`);
  }
  refuseNestedAction(category, method, params.args);
  return { taskName: `${category}.${method}`, params: validatedMicroParams(tool, method, microCallParams(params)) };
}

/**
 * A gateway call is checked against the same contract as a direct call to the
 * category (#1172): the category's declared shape, parsed with the same zod
 * object, refused with the same message. Before, a micro call's `args` reached
 * the task unchecked, and micro is now the default route.
 *
 * Only the refusal is shared. The parameters go on as the caller sent them,
 * apart from a JSON-encoded object or array the shape declares (#1370), so a
 * key the shape does not declare still reaches the target and is reported
 * there as not read.
 */
function validatedMicroParams(tool: ToolDef, method: string, params: Record<string, unknown>): Record<string, unknown> {
  const coerced = coerceJsonStringArgs(tool, params);
  const { message } = flatValidationMessage(tool, { ...coerced, action: method });
  if (message !== undefined) throw new McpError(ErrorCode.INVALID_PARAMS, message);
  return coerced;
}

/** The categories each gateway was built to reach: the enabled set, never a
 *  category the project's `disable:` list removed. */
const gatewayTargets = new WeakMap<ToolDef, ToolDef[]>();

/** What `tools.call` may resolve when no session graph is on the context. */
export function microGatewayTargets(gateway: ToolDef): ToolDef[] | undefined {
  return gatewayTargets.get(gateway);
}

/**
 * The flow tool's actions and descriptions, for discovery only. Calls are
 * routed to the server's own flow tool by dispatch (microFlowCall), so these
 * sources are never read.
 */
let flowDescriptor: ToolDef | undefined;
function flowCategory(): ToolDef {
  const unrouted = (): never => {
    throw new McpError(ErrorCode.NO_HANDLER, "flow runs through the server's flow route, not the gateway handler.");
  };
  return (flowDescriptor ??= createFlowTool(unrouted, unrouted));
}

/**
 * The gateway's advertised description. It and the schema below are the whole
 * micro seed beside one instruction line, so every word is paid on every
 * session; the usage lives in the guide action.
 */
const GATEWAY_DESCRIPTION =
  "Every Unreal Editor action and flow, by category. Read guide first. list_categories; describe a category "
  + "(signatures) or one method (parameters); call category, method, args; search finds actions by intent.";

export function buildMicroGateway(tools: ToolDef[]): ToolDef {
  const flow = flowCategory();
  // Flow is listed, searched and described like a category; dispatch runs it.
  const discoverable = [...tools, flow];
  const byName = new Map(discoverable.map((t) => [t.name, t] as const));
  const summaries = discoverable.map((t) => ({ category: t.name, summary: splitDescription(t.description).summary }));

  const actions: Record<string, ActionSpec> = {
    guide: {
      kind: "handler",
      effect: "read",
      description: "How to use the gateway: discovery, call shape, signature legend, spatial edits, categories and flows.",
      handler: async () => microGuide(tools),
    },
    search: {
      kind: "handler",
      effect: "read",
      description: "Find actions by keyword or intent; returns category.signature lines. Params: query, limit? (default 20)",
      handler: async (ctx, p) => {
        const query = typeof p.query === "string" ? p.query : "";
        if (!query.trim()) throw new Error("Provide a query to search for actions.");
        const limit = typeof p.limit === "number" && p.limit > 0 ? Math.min(p.limit, 100) : 20;
        const results = discoveryResults(withholdUnregistered(discoverable, registeredMethodsOf(ctx?.bridge)), query, limit);
        return { query, count: results.length, results };
      },
    },
    list_categories: {
      kind: "handler",
      effect: "read",
      description: "List every category with a one-line summary.",
      handler: async (ctx) => {
        const visible = new Set(withholdUnregistered(discoverable, registeredMethodsOf(ctx?.bridge)).map((t) => t.name));
        const categories = summaries.filter((s) => visible.has(s.category));
        return {
        count: categories.length,
        categories,
        next: 'tools(action="describe", category="<name>"), then tools(action="call", category, method, args)',
      };
      },
    },
    describe: {
      kind: "handler",
      effect: "read",
      description: "A category's action signatures a page at a time, or one action's parameter schema. Params: category, method?, offset?",
      handler: async (ctx, p) => {
        const category = typeof p.category === "string" ? p.category : "";
        const declared = byName.get(category);
        const tool = declared && withholdFromTool(declared, registeredMethodsOf(ctx?.bridge));
        if (!tool) {
          throw new McpError(
            ErrorCode.UNKNOWN_ACTION,
            `Unknown category "${category}". Categories: ${summaries.map((s) => s.category).join(", ")}.`,
          );
        }
        if (typeof p.method === "string" && p.method) return actionSchema(tool, p.method);
        return describeCategory(tool, readOffset(p));
      },
    },
    call: {
      kind: "handler",
      effect: "unknown",
      description: "Invoke any action. Params: category, method (the action name), args (object of the action's params).",
      handler: async (ctx, p) => {
        const category = typeof p.category === "string" ? p.category : "";
        const method = typeof p.method === "string" ? p.method : "";
        const tool = byName.get(category);
        if (!tool) {
          throw new McpError(ErrorCode.UNKNOWN_ACTION, `Unknown category "${category}". Use tools(action="list_categories").`);
        }
        const spec = tool.actions[method];
        if (!spec) {
          throw new McpError(ErrorCode.UNKNOWN_ACTION, `Unknown action "${method}" on ${category}. Use tools(action="describe", category="${category}").`);
        }
        const rawArgs = p.args && typeof p.args === "object" ? (p.args as Record<string, unknown>) : {};
        refuseNestedAction(category, method, rawArgs);
        // The target category's own handler does the per-call preparation
        // (folding, path repair, routing params), so it is written once (#1081).
        // Dispatch already took the budget off `args` and put it on the context.
        const subParams: Record<string, unknown> = { ...rawArgs, action: method };
        if (ctx.callTimeoutMs !== undefined) subParams.timeoutMs = ctx.callTimeoutMs;
        return tool.handler(ctx, subParams);
      },
    },
  };

  const gateway = categoryTool(
    MICRO_GATEWAY_TOOL,
    GATEWAY_DESCRIPTION,
    actions,
    {},
    // Every real parameter of a gateway call is one level down, so the path
    // repair, the field projection and the per-call budget have to be applied
    // there. Preparing `{category, method, args}` instead left a backslashed
    // path inside `args` unrepaired and forwarded `args.select` to the editor
    // as a method argument.
    { nestedParamsKey: "args" },
  );
  gateway.description = GATEWAY_DESCRIPTION;
  // Only the gateway's own keys are advertised. timeoutMs, select and omit are
  // still honoured beside args (the advertised object passes them through) or
  // inside it, and the guide documents them.
  gateway.schema = {
    action: z.enum(Object.keys(actions) as [string, ...string[]]),
    category: z.string().optional(),
    method: z.string().optional().describe("action name"),
    args: unknownRecord().optional().describe("the action's parameters"),
    query: z.string().optional(),
    // No `maximum`: zod 4 would advertise the safe-integer bound that .int() implies.
    offset: z.number().int().min(0).meta({ maximum: undefined }).optional(),
    limit: z.number().int().min(1).max(100).optional(),
  };
  gatewayTargets.set(gateway, tools);
  return gateway;
}
