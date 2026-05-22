import { z } from "zod";
import type { IBridge } from "./bridge.js";
import type { ProjectContext } from "./project.js";
import { McpError, ErrorCode } from "./errors.js";

/**
 * Elicit a deterministic, user-mediated form response via the MCP client.
 * The server blocks until the client returns one of accept / decline / cancel.
 * Returns null when the connected client did not advertise the `elicitation`
 * capability — handlers that rely on this gate must refuse to proceed in
 * that case rather than fall back to an agent-mediated approval.
 */
export type ElicitFn = (params: ElicitParams) => Promise<ElicitResult>;

export interface ElicitParams {
  message: string;
  requestedSchema: {
    type: "object";
    properties: Record<string, ElicitPrimitiveSchema>;
    required?: string[];
  };
}

export type ElicitPrimitiveSchema =
  | { type: "string"; title?: string; description?: string; enum?: string[]; enumNames?: string[]; default?: string }
  | { type: "number" | "integer"; title?: string; description?: string; default?: number }
  | { type: "boolean"; title?: string; description?: string; default?: boolean };

export interface ElicitResult {
  action: "accept" | "decline" | "cancel";
  content?: Record<string, string | number | boolean | string[]>;
}

export interface ToolContext {
  bridge: IBridge;
  project: ProjectContext;
  /** Lazy accessor for the active flow registry. Returns the merged
   *  built-in + ue-mcp.yml flows. Used by project(get_status) so agents
   *  see which canonical sequences are pre-encoded for this project. */
  getFlows?: () => Array<{ name: string; description?: string }>;
  /** Lazy accessor for the loaded plugin set. Returns one PluginInfo per
   *  entry in the user's `plugins:` array, active or skipped. Used by the
   *  `plugins` introspection category. */
  getPlugins?: () => PluginInfo[];
  /** MCP elicitation gate. When defined, calling this blocks the active
   *  tool invocation until the user responds in their MCP client UI. When
   *  undefined, the connected client does not declare the elicitation
   *  capability — handlers that need a deterministic user signal MUST
   *  refuse instead of degrading to an agent-mediated channel. Used by
   *  feedback(submit) to gate every GitHub post on real user approval. */
  elicit?: ElicitFn;
}

export interface PluginInfo {
  name: string;
  version: string;
  actionPrefix: string;
  status: "active" | "skipped";
  statusReason?: string;
  minServerVersion?: string;
  uePluginDependency?: string;
  uePluginPresent?: boolean;
  injected: Record<string, string[]>;
  /** Categories this plugin contributes as new top-level MCP tools. */
  provided: Record<string, string[]>;
  knowledge: Record<string, string>;
  flows: string[];
  tasks: string[];
  pkgDir: string;
  manifestPath: string;
}

export interface ToolDef {
  name: string;
  description: string;
  schema: Record<string, z.ZodType>;
  handler: (ctx: ToolContext, params: Record<string, unknown>) => Promise<unknown>;
  actions: Record<string, ActionSpec>;
}

export interface ActionSpec {
  description?: string;
  bridge?: string;
  mapParams?: (p: Record<string, unknown>) => Record<string, unknown>;
  handler?: (ctx: ToolContext, params: Record<string, unknown>) => Promise<unknown>;
  /** Override the bridge call timeout in milliseconds. Defaults to 30s. */
  timeoutMs?: number;
}

export function categoryTool(
  name: string,
  summary: string,
  actions: Record<string, ActionSpec>,
  actionDocs?: string,
  extraSchema?: Record<string, z.ZodType>,
): ToolDef {
  const actionNames = Object.keys(actions) as [string, ...string[]];

  // Auto-generate action docs from per-action descriptions if not provided
  const docs = actionDocs ?? actionNames
    .map((a) => {
      const desc = actions[a].description;
      return desc ? `- ${a}: ${desc}` : `- ${a}`;
    })
    .join("\n");

  return {
    name,
    description: `${summary}\n\nActions:\n${docs}`,
    schema: {
      action: z.enum(actionNames).describe("Action to perform"),
      ...extraSchema,
    },
    actions,
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
        return ctx.bridge.call(spec.bridge, mapped, spec.timeoutMs);
      }
      throw new McpError(ErrorCode.NO_HANDLER, `Action '${action}' has no handler or bridge method`);
    },
  };
}

function stripAction(params: Record<string, unknown>): Record<string, unknown> {
  const { action: _, ...rest } = params;
  return rest;
}

export function bp(bridge: string, mapParams?: (p: Record<string, unknown>) => Record<string, unknown>): ActionSpec;
export function bp(description: string, bridge: string, mapParams?: (p: Record<string, unknown>) => Record<string, unknown>): ActionSpec;
export function bp(...args: unknown[]): ActionSpec {
  // bp(bridge) or bp(bridge, mapParams) — no description
  // bp(description, bridge) or bp(description, bridge, mapParams) — with description
  if (args.length >= 2 && typeof args[0] === "string" && typeof args[1] === "string") {
    return { description: args[0] as string, bridge: args[1] as string, mapParams: args[2] as ((p: Record<string, unknown>) => Record<string, unknown>) | undefined };
  }
  return { bridge: args[0] as string, mapParams: args[1] as ((p: Record<string, unknown>) => Record<string, unknown>) | undefined };
}

/* ── Directive response ─────────────────────────────────────────────
 * Handlers can return this to emit a mandatory instruction as a
 * separate MCP content block *before* the tool result.  Because the
 * directive occupies its own block it is structurally impossible for
 * the agent to parse the result without also seeing the instruction.
 *
 * In addition to the prose `directive` (for humans-reading-transcripts
 * and for agents that respect prose), `machine` carries a structured
 * record so downstream tooling (flow runners, feedback dashboards) can
 * detect the directive even if prose is stripped or summarised.
 * ─────────────────────────────────────────────────────────────────── */
export interface DirectiveMachine {
  /** Stable identifier for the directive kind (e.g. "workaround.feedback"). */
  kind: string;
  /** What the agent is expected to do next, as discrete steps. */
  requiredActions: string[];
  /** Free-form metadata - counts, identifiers, payloads - specific to kind. */
  context?: Record<string, unknown>;
}

export interface DirectiveResponse {
  __directive: true;
  directive: string;            // instruction text — emitted as its own content block
  machine?: DirectiveMachine;   // structured mirror for programmatic consumers
  result: unknown;              // actual tool result
}

export function directive(text: string, result: unknown, machine?: DirectiveMachine): DirectiveResponse {
  return { __directive: true, directive: text, machine, result };
}

export function isDirectiveResponse(v: unknown): v is DirectiveResponse {
  return typeof v === "object" && v !== null && (v as Record<string, unknown>).__directive === true;
}
