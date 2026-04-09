import { z } from "zod";
import type { IBridge } from "./bridge.js";
import type { ProjectContext } from "./project.js";
import { McpError, ErrorCode } from "./errors.js";

export interface ToolContext {
  bridge: IBridge;
  project: ProjectContext;
}

export interface ToolDef {
  name: string;
  description: string;
  schema: Record<string, z.ZodType>;
  handler: (ctx: ToolContext, params: Record<string, unknown>) => Promise<unknown>;
}

export interface ActionSpec {
  bridge?: string;
  mapParams?: (p: Record<string, unknown>) => Record<string, unknown>;
  handler?: (ctx: ToolContext, params: Record<string, unknown>) => Promise<unknown>;
}

export function categoryTool(
  name: string,
  summary: string,
  actions: Record<string, ActionSpec>,
  actionDocs: string,
  extraSchema?: Record<string, z.ZodType>,
): ToolDef {
  const actionNames = Object.keys(actions) as [string, ...string[]];
  return {
    name,
    description: `${summary}\n\nActions:\n${actionDocs}`,
    schema: {
      action: z.enum(actionNames).describe("Action to perform"),
      ...extraSchema,
    },
    handler: async (ctx, params) => {
      const action = params.action as string;
      const spec = actions[action];
      if (!spec) {
        throw new McpError(ErrorCode.UNKNOWN_ACTION, `Unknown action '${action}'. Available: ${actionNames.join(", ")}`);
      }
      if (spec.handler) {
        return spec.handler(ctx, params);
      }
      if (spec.bridge) {
        const mapped = spec.mapParams ? spec.mapParams(params) : stripAction(params);
        return ctx.bridge.call(spec.bridge, mapped);
      }
      throw new McpError(ErrorCode.NO_HANDLER, `Action '${action}' has no handler or bridge method`);
    },
  };
}

function stripAction(params: Record<string, unknown>): Record<string, unknown> {
  const { action: _, ...rest } = params;
  return rest;
}

export function bp(bridge: string, mapParams?: (p: Record<string, unknown>) => Record<string, unknown>): ActionSpec {
  return { bridge, mapParams };
}

export function bpSame(): ActionSpec {
  return {};
}

/* ── Directive response ─────────────────────────────────────────────
 * Handlers can return this to emit a mandatory instruction as a
 * separate MCP content block *before* the tool result.  Because the
 * directive occupies its own block it is structurally impossible for
 * the agent to parse the result without also seeing the instruction.
 * ─────────────────────────────────────────────────────────────────── */
export interface DirectiveResponse {
  __directive: true;
  directive: string;   // instruction text — emitted as its own content block
  result: unknown;     // actual tool result
}

export function directive(text: string, result: unknown): DirectiveResponse {
  return { __directive: true, directive: text, result };
}

export function isDirectiveResponse(v: unknown): v is DirectiveResponse {
  return typeof v === "object" && v !== null && (v as Record<string, unknown>).__directive === true;
}
