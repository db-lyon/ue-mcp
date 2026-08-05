import { z } from "zod";
import type { IBridge } from "./bridge.js";
import type { ProjectContext } from "./project.js";
import type { EditorSession, SessionRegistry } from "./session.js";
import { McpError, ErrorCode } from "./errors.js";

/**
 * Elicit a deterministic, user-mediated form response via the MCP client.
 * The server blocks until the client returns one of accept / decline / cancel.
 * Returns null when the connected client did not advertise the `elicitation`
 * capability - handlers that rely on this gate must refuse to proceed in
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
  /** The editor this call was routed to. `bridge` and `project` are always
   *  this session's, so a handler cannot resolve a path in one project while
   *  calling into another project's editor. Absent only for a context built
   *  outside the session registry (tests, direct handler invocation). */
  session?: EditorSession;
  /** Every editor this server drives. Handlers that address sessions
   *  (list_editors / use_editor / add_editor / drop_editor) read it from
   *  here rather than from module state. */
  sessions?: SessionRegistry;
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
   *  capability - handlers that need a deterministic user signal MUST
   *  refuse instead of degrading to an agent-mediated channel. Used by
   *  feedback(submit) to gate every GitHub post on real user approval. */
  elicit?: ElicitFn;
  /**
   * Live progress for a long call, rendered by the MCP client while the tool
   * is still running.
   *
   * This is the ONLY channel a user actually sees mid-call: an MCP server's
   * stderr is captured to a client log file, never to the transcript, so a
   * progress bar printed there is invisible. Present only when the client
   * passed a progress token with the request.
   */
  onProgress?: ProgressFn;
  /**
   * Who is on the other end of the transport, from the MCP `initialize`
   * handshake. Used to explain client-specific rendering limits in a result
   * rather than leaving the user staring at a call that looks frozen.
   */
  client?: { name: string; version?: string };
}

export interface ProgressUpdate {
  /** Monotonic units done. With `total`, clients render a bar. */
  progress: number;
  total?: number;
  /** One line describing what is happening right now. */
  message: string;
}

export type ProgressFn = (update: ProgressUpdate) => void;

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
  /** Set once the per-call editor target has been injected into `schema`.
   *  Dispatch reads it to know whether an `editor` param is a routing
   *  instruction (strip it) or one of the tool's own params (forward it). */
  injectedEditorParam?: boolean;
}

export interface ActionSpec {
  description?: string;
  bridge?: string;
  mapParams?: (p: Record<string, unknown>) => Record<string, unknown>;
  handler?: (ctx: ToolContext, params: Record<string, unknown>) => Promise<unknown>;
  /** Override the bridge call timeout in milliseconds. Defaults to 30s. */
  timeoutMs?: number;
}

/**
 * The per-call editor target (#817). Injected into every category tool only
 * while this server drives more than one editor, so a single-editor client
 * sees the schema it has always seen. Declared once here because the flow
 * tool, the micro gateway, and plugin-provided categories inject the same
 * parameter and must describe it identically.
 */
export const EDITOR_TARGET_PARAM = "editor";

/**
 * Add the target parameter to a tool. Refuses when the tool already declares
 * `editor` of its own: silently shadowing a plugin's parameter would send its
 * value to the router instead of the handler, so the collision is reported
 * and that tool stays untargeted rather than quietly changing meaning.
 */
export function injectEditorTarget(
  tool: ToolDef,
  sessionNames: string[],
): { injected: boolean; reason?: string } {
  if (tool.injectedEditorParam) {
    tool.schema = { ...tool.schema, [EDITOR_TARGET_PARAM]: editorTargetSchema(sessionNames) };
    return { injected: true };
  }
  if (EDITOR_TARGET_PARAM in tool.schema) {
    return {
      injected: false,
      reason: `'${tool.name}' declares its own '${EDITOR_TARGET_PARAM}' parameter, so per-call targeting is unavailable for it. Rename that parameter to make the category targetable.`,
    };
  }
  tool.schema = { ...tool.schema, [EDITOR_TARGET_PARAM]: editorTargetSchema(sessionNames) };
  tool.injectedEditorParam = true;
  return { injected: true };
}

/** Undo injectEditorTarget, restoring the single-editor schema exactly. */
export function removeEditorTarget(tool: ToolDef): boolean {
  if (!tool.injectedEditorParam) return false;
  const { [EDITOR_TARGET_PARAM]: _dropped, ...rest } = tool.schema;
  tool.schema = rest;
  tool.injectedEditorParam = false;
  return true;
}

export function editorTargetSchema(sessionNames: string[]): z.ZodType {
  return z
    .string()
    .optional()
    .describe(
      `Editor session to run this call in: a session name (${sessionNames.join(", ")}), ` +
        `a project name, or a .uproject path. Defaults to the active session ` +
        `(project(action="list_editors") reports it).`,
    );
}

export interface CategoryOptions {
  /**
   * Fold a category's accepted parameter spellings into its canonical ones
   * before dispatch, in one place instead of once per action (#798).
   *
   * It runs after the action is resolved and before the action's own
   * `mapParams`, so it also covers actions injected into the category after
   * construction (Epic toolset wrappers, plugin native modules). Throwing
   * from here is how a category rejects a malformed or contradictory
   * parameter combination with a specific message.
   */
  normalizeParams?: (params: Record<string, unknown>) => Record<string, unknown>;
}

export function categoryTool(
  name: string,
  summary: string,
  actions: Record<string, ActionSpec>,
  actionDocs?: string,
  extraSchema?: Record<string, z.ZodType>,
  options?: CategoryOptions,
): ToolDef {
  const actionNames = Object.keys(actions) as [string, ...string[]];

  // Auto-generate action docs from per-action descriptions if not provided
  const docs = actionDocs ?? actionNames
    .map((a) => {
      const desc = actions[a].description;
      return desc ? `- ${a}: ${desc}` : `- ${a}`;
    })
    .join("\n");

  const def: ToolDef = {
    name,
    description: `${summary}\n\nActions:\n${docs}`,
    schema: {
      action: z.enum(actionNames).describe("Action to perform"),
      ...extraSchema,
    },
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
        throw new McpError(ErrorCode.UNKNOWN_ACTION, `Unknown action '${action}'. Available: ${Object.keys(actions).join(", ")}`);
      }
      const normalized = options?.normalizeParams ? options.normalizeParams(params) : params;
      if (spec.handler) {
        return spec.handler(ctx, normalized);
      }
      if (spec.bridge) {
        const mapped = spec.mapParams ? spec.mapParams(normalized) : stripAction(normalized);
        return ctx.bridge.call(spec.bridge, mapped, spec.timeoutMs);
      }
      throw new McpError(ErrorCode.NO_HANDLER, `Action '${action}' has no handler or bridge method`);
    },
  };
  return def;
}

function stripAction(params: Record<string, unknown>): Record<string, unknown> {
  const { action: _, ...rest } = params;
  return rest;
}

/**
 * Re-point a context at one editor. Bridge, project and session move together
 * so a handler can never resolve a path in one project while calling into
 * another project's editor.
 */
export function sessionContext(ctx: ToolContext, session: EditorSession): ToolContext {
  return { ...ctx, bridge: session.guarded, project: session.project, session };
}

/** Drop the routing parameter from a param bag. */
export function stripEditorTarget(params: Record<string, unknown>): Record<string, unknown> {
  if (!(EDITOR_TARGET_PARAM in params)) return params;
  const { [EDITOR_TARGET_PARAM]: _dropped, ...rest } = params;
  return rest;
}

export function bp(bridge: string, mapParams?: (p: Record<string, unknown>) => Record<string, unknown>): ActionSpec;
export function bp(description: string, bridge: string, mapParams?: (p: Record<string, unknown>) => Record<string, unknown>): ActionSpec;
export function bp(...args: unknown[]): ActionSpec {
  // bp(bridge) or bp(bridge, mapParams) - no description
  // bp(description, bridge) or bp(description, bridge, mapParams) - with description
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
  directive: string;            // instruction text - emitted as its own content block
  machine?: DirectiveMachine;   // structured mirror for programmatic consumers
  result: unknown;              // actual tool result
}

export function directive(text: string, result: unknown, machine?: DirectiveMachine): DirectiveResponse {
  return { __directive: true, directive: text, machine, result };
}

export function isDirectiveResponse(v: unknown): v is DirectiveResponse {
  return typeof v === "object" && v !== null && (v as Record<string, unknown>).__directive === true;
}
