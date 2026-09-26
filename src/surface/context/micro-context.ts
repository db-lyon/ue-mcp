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
import type { ActionSpec, ToolDef } from "../../core/types.js";
import { categoryTool } from "../category-tool.js";
import { stripAction } from "../routing-params.js";
import { takeTimeout } from "../../dispatch/call-pipeline.js";
import { McpError, ErrorCode } from "../../core/errors.js";
import { actionSchema } from "../action-schema.js";
import { takeFieldSelection } from "./field-select.js";
import { flatValidationMessage } from "./call-envelope.js";
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
export function resolveMicroCall(tools: ToolDef[], params: Record<string, unknown>) {
  const category = typeof params.category === "string" ? params.category : "";
  const method = typeof params.method === "string" ? params.method : "";
  const tool = tools.find((candidate) => candidate.name === category);
  if (!tool) {
    throw new McpError(ErrorCode.UNKNOWN_ACTION, `Unknown category "${category}". Use tools(action="list_categories").`);
  }
  if (!Object.hasOwn(tool.actions, method)) {
    throw new McpError(ErrorCode.UNKNOWN_ACTION, `Unknown action "${method}" on ${category}. Use tools(action="describe", category="${category}").`);
  }
  return { taskName: `${category}.${method}`, params: validatedMicroParams(tool, method, microCallParams(params)) };
}

/**
 * A gateway call is checked against the same contract as a direct call to the
 * category (#1172): the category's declared shape, parsed with the same zod
 * object, refused with the same message. Before, a micro call's `args` reached
 * the task unchecked, and micro is now the default route.
 *
 * Only the refusal is shared. The parameters go on as the caller sent them,
 * which is what this route has always forwarded, so a key the shape does not
 * declare still reaches the target and is reported there as not read.
 */
function validatedMicroParams(tool: ToolDef, method: string, params: Record<string, unknown>): Record<string, unknown> {
  const { message } = flatValidationMessage(tool, { ...params, action: method });
  if (message !== undefined) throw new McpError(ErrorCode.INVALID_PARAMS, message);
  return params;
}

/** The categories each gateway was built to reach: the enabled set, never a
 *  category the project's `disable:` list removed. */
const gatewayTargets = new WeakMap<ToolDef, ToolDef[]>();

/** What `tools.call` may resolve when no session graph is on the context. */
export function microGatewayTargets(gateway: ToolDef): ToolDef[] | undefined {
  return gatewayTargets.get(gateway);
}

export function buildMicroGateway(tools: ToolDef[]): ToolDef {
  const byName = new Map(tools.map((t) => [t.name, t] as const));
  const summaries = tools.map((t) => ({ category: t.name, summary: splitDescription(t.description).summary }));

  const actions: Record<string, ActionSpec> = {
    search: {
      kind: "handler",
      effect: "read",
      description: "Find actions by keyword or intent; returns category.signature lines. Params: query, limit? (default 20)",
      handler: async (_ctx, p) => {
        const query = typeof p.query === "string" ? p.query : "";
        if (!query.trim()) throw new Error("Provide a query to search for actions.");
        const limit = typeof p.limit === "number" && p.limit > 0 ? Math.min(p.limit, 100) : 20;
        const results = discoveryResults(tools, query, limit);
        return { query, count: results.length, results };
      },
    },
    list_categories: {
      kind: "handler",
      effect: "read",
      description: "List every category with a one-line summary.",
      handler: async () => ({
        count: summaries.length,
        categories: summaries,
        next: 'tools(action="describe", category="<name>"), then tools(action="call", category, method, args)',
      }),
    },
    describe: {
      kind: "handler",
      effect: "read",
      description: "A category's action signatures a page at a time, or one action's parameter schema. Params: category, method?, offset?",
      handler: async (_ctx, p) => {
        const category = typeof p.category === "string" ? p.category : "";
        const tool = byName.get(category);
        if (!tool) {
          return { error: `Unknown category "${category}".`, categories: summaries.map((s) => s.category) };
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
    "Gateway to every ue-mcp category (micro context mode). Find actions with search, inspect parameters with describe, then invoke with call.",
    actions,
    {
      category: z.string().optional().describe('Category name for describe/call, e.g. "blueprint"'),
      method: z.string().optional().describe('Action name for call or a single-action describe, e.g. "create"'),
      args: z.record(z.unknown()).optional().describe("Params object passed to the called action"),
      query: z.string().optional().describe("Keyword or intent for search"),
      offset: z.number().int().min(0).optional().describe("describe: first signature of the page (nextOffset)"),
      limit: z.number().int().min(1).max(100).optional().describe("Max search results (default 20)"),
    },
    // Every real parameter of a gateway call is one level down, so the path
    // repair, the field projection and the per-call budget have to be applied
    // there. Preparing `{category, method, args}` instead left a backslashed
    // path inside `args` unrepaired and forwarded `args.select` to the editor
    // as a method argument.
    { nestedParamsKey: "args" },
  );
  gatewayTargets.set(gateway, tools);
  return gateway;
}
