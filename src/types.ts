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
  actions: Record<string, ActionSpec>;
}

/**
 * Operational classification for an action.
 * Projects into /UE/Signals/Classification as a gradient marker.
 */
export type ActionClassification = "read" | "introspect" | "mutate" | "create" | "destructive";

/** Approval policy. Projects into /UE/Signals/Approval. */
export type ActionApproval = "auto" | "advisory" | "required" | "explicit";

/** Risk signal. Projects into /UE/Signals/Risk. */
export type ActionRisk = "trivial" | "minor" | "significant" | "severe" | "catastrophic";

export interface ActionSpec {
  description?: string;
  bridge?: string;
  mapParams?: (p: Record<string, unknown>) => Record<string, unknown>;
  handler?: (ctx: ToolContext, params: Record<string, unknown>) => Promise<unknown>;
  /** Override the bridge call timeout in milliseconds. Defaults to 30s. */
  timeoutMs?: number;
  /**
   * Declared operational classification. When present, the ontology
   * projector uses this value instead of the name-prefix heuristic.
   * Set this on any action whose side-effect profile is surprising.
   */
  classification?: ActionClassification;
  /** Declared approval policy. Overrides classification-derived default. */
  approval?: ActionApproval;
  /** Declared risk. Overrides classification-derived default. */
  risk?: ActionRisk;
  /**
   * Plugins or modules that must be available for this action to
   * succeed. Projected into the point's `requires` subtree as named
   * children (one child per dependency). Category-level defaults
   * (passed to categoryTool) apply unless overridden here.
   */
  requires?: readonly string[];
}

export function categoryTool(
  name: string,
  summary: string,
  actions: Record<string, ActionSpec>,
  actionDocs?: string,
  extraSchema?: Record<string, z.ZodType>,
  defaults?: Partial<Pick<ActionSpec, "classification" | "approval" | "risk" | "requires">>,
): ToolDef {
  const actionNames = Object.keys(actions) as [string, ...string[]];

  // Apply category-level defaults to actions that did not declare
  // their own. Per-action declarations always win.
  if (defaults) {
    for (const key of actionNames) {
      const spec = actions[key];
      if (defaults.classification && spec.classification === undefined) spec.classification = defaults.classification;
      if (defaults.approval && spec.approval === undefined) spec.approval = defaults.approval;
      if (defaults.risk && spec.risk === undefined) spec.risk = defaults.risk;
      if (defaults.requires && spec.requires === undefined) spec.requires = defaults.requires;
    }
  }

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
