import { z } from "zod";
import type { IBridge } from "./bridge.js";
import type { ProjectContext } from "./project.js";
import type { EditorSession, SessionRegistry } from "./session.js";
import { McpError, ErrorCode } from "./errors.js";
import { MAX_BRIDGE_TIMEOUT_MS } from "./bridge-timeouts.js";
import { nearestActions } from "./action-schema.js";
import { prepareCall, finishCall } from "./call-pipeline.js";

/**
 * Re-exported from its home in `call-pipeline.ts`, where the whole inbound
 * half of a call lives. Importers are unaffected by the move.
 */
export { takeTimeout } from "./call-pipeline.js";

/**
 * Elicit a deterministic, user-mediated form response via the MCP client.
 * The server blocks until the client returns one of accept / decline / cancel.
 * Returns null when the connected client did not advertise the `elicitation`
 * capability - handlers that rely on this gate must refuse to proceed in
 * that case rather than fall back to an agent-mediated approval.
 */
export interface ElicitFn {
  (params: ElicitParams): Promise<ElicitResult>;
  /**
   * Whether the CONNECTED client advertised the `elicitation` capability.
   *
   * The presence of the function is NOT that answer. The server builds the
   * gate at startup, before any client has connected, so it always has a
   * function to hand over and the capability is only knowable once a client is
   * on the other end. Callers that branch on "can this user actually be asked"
   * must call this rather than test the function for undefined, which is true
   * of every client and would silently promote a client that advertised
   * nothing into the interactive path.
   *
   * Absent on a gate built outside the server (tests, embedders), where the
   * function was handed over deliberately and is taken at face value.
   */
  clientAdvertisesElicitation?: () => boolean;
}

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
   *  see which canonical sequences are pre-encoded for this project.
   *  Takes the session to read, because flows are declared in each project's
   *  own ue-mcp.yml; omitted means this context's session. */
  getFlows?: (forSession?: EditorSession) => Array<{ name: string; description?: string }>;
  /** Lazy accessor for the loaded plugin set. Returns one PluginInfo per
   *  entry in the user's `plugins:` array, active or skipped. Used by the
   *  `plugins` introspection category. Session-scoped for the same reason
   *  as getFlows: `plugins:` is per project. */
  getPlugins?: (forSession?: EditorSession) => PluginInfo[];
  /** Enabled source categories for the addressed editor, including injected actions. */
  getToolGraph?: (forSession?: EditorSession) => ToolDef[];
  /** The per-call timeout budget the caller asked for, in milliseconds (#989).
   *  Set by the category dispatcher when a call carried `timeoutMs`. A handler
   *  that makes its own bridge calls should pass it through; one that does not
   *  simply keeps the default. */
  callTimeoutMs?: number;
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
  /** Manifest units that failed validation while the rest of the plugin
   *  loaded. Non-empty means active but narrower than the manifest declares. */
  degraded: string[];
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
  /** Set once the destination-editor parameter has been injected (#817). */
  injectedMigrateParam?: boolean;
  /**
   * Build an independent copy of this tool (#817).
   *
   * Epic enrichment and plugin injection mutate `actions`, `schema` and
   * `description` in place, so two editors sharing one ToolDef share whatever
   * either of them was enriched with: a project with a toolset the other does
   * not have would advertise that toolset on both. Each session therefore
   * gets its own graph, cloned from the pristine one before anything touches
   * it.
   *
   * Set by `categoryTool`, which is the only thing that can rebuild the
   * dispatch closure so the copy reads its OWN actions rather than the
   * original's. A tool built by hand carries no rebuilder and is copied
   * structurally instead.
   */
  rebuild?: (actions: Record<string, ActionSpec>) => ToolDef;
  /**
   * The category-wide options this tool was built with.
   *
   * Published on the ToolDef rather than kept in the `categoryTool` closure
   * because DISPATCH needs them, and dispatch is the flow registry, not that
   * closure. `normalizeParams` spent its whole life invisible to the live
   * route for exactly this reason: a category advertised the spellings it
   * accepts, the schema let them through, and the folding that was supposed to
   * canonicalise them only ever ran on the route the tests use.
   *
   * Structural copies (`{ ...tool }`) and rebuilt copies both carry it, since
   * `rebuild` passes the same options back through this constructor.
   */
  options?: CategoryOptions;
}

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

export interface ActionSpec {
  description?: string;
  bridge?: string;
  mapParams?: (p: Record<string, unknown>) => Record<string, unknown>;
  handler?: (ctx: ToolContext, params: Record<string, unknown>) => Promise<unknown>;
  /** Override the bridge call timeout in milliseconds. Defaults to 30s. */
  timeoutMs?: number;
  /**
   * This action moves something INTO a second editor, so its category takes a
   * `toEditor` parameter alongside the `editor` one every category gets (#817).
   * Declared on the action rather than assumed from its name, and injected
   * under the same rule: only while this server drives more than one editor.
   * `asset(migrate)` is the one action that has it.
   */
  destinationEditor?: boolean;
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

/**
 * The destination editor for an action that moves content between two of them
 * (#817). `editor` says where a call runs; this says where its output lands.
 */
export const MIGRATE_TARGET_PARAM = "toEditor";

/** Does any of this tool's actions move content into a second editor? */
export function hasDestinationEditorAction(tool: ToolDef): boolean {
  return Object.values(tool.actions).some((spec) => spec.destinationEditor === true);
}

/**
 * Add the destination parameter, under the same rule as the target parameter:
 * only while more than one editor is registered, and never over a parameter the
 * tool already declares.
 */
export function injectMigrateTarget(
  tool: ToolDef,
  sessionNames: string[],
): { injected: boolean; reason?: string } {
  if (!hasDestinationEditorAction(tool)) return { injected: false };
  if (!tool.injectedMigrateParam && MIGRATE_TARGET_PARAM in tool.schema) {
    return {
      injected: false,
      reason: `'${tool.name}' declares its own '${MIGRATE_TARGET_PARAM}' parameter, so cross-editor migration is unavailable for it.`,
    };
  }
  tool.schema = { ...tool.schema, [MIGRATE_TARGET_PARAM]: migrateTargetSchema(sessionNames) };
  tool.injectedMigrateParam = true;
  return { injected: true };
}

/** Undo injectMigrateTarget, restoring the single-editor schema exactly. */
export function removeMigrateTarget(tool: ToolDef): boolean {
  if (!tool.injectedMigrateParam) return false;
  const { [MIGRATE_TARGET_PARAM]: _dropped, ...rest } = tool.schema;
  tool.schema = rest;
  tool.injectedMigrateParam = false;
  return true;
}

export function migrateTargetSchema(sessionNames: string[]): z.ZodType {
  return z
    .string()
    .optional()
    .describe(
      `migrate: the editor to migrate INTO (${sessionNames.join(", ")}), by session name, ` +
        `project name, or .uproject path. Its Content directory becomes destinationContentDir ` +
        `and its asset registry is rescanned afterwards, so the assets are visible there ` +
        `without a manual rescan. Pass this or destinationContentDir, not both.`,
    );
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
  /**
   * This tool is a gateway: every real parameter arrives nested under this key
   * rather than at the top level.
   *
   * Set to `args` by the micro-context gateway. Dispatch reads it so the path
   * repair, the field projection and the per-call budget apply to the
   * parameters the target action will actually see, instead of to the
   * `{category, method, args}` envelope, where none of them are present.
   */
  nestedParamsKey?: string;
}

/**
 * The per-call timeout budget, offered by every category tool (#989).
 *
 * It is a routing instruction, never a handler parameter: the dispatcher reads
 * it and strips it, so it cannot reach a bridge method as an argument.
 */
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
    options,
    description: `${summary}\n\nActions:\n${docs}`,
    schema: {
      action: actionEnum(actionNames),
      // #989: a call budget the caller controls. The client used to wait a flat
      // 30s for every bridge call, and a large batch on a machine that is also
      // compiling shaders finished in the editor after the client had already
      // reported a failure. A retry then applied the mutation twice.
      timeoutMs: TIMEOUT_PARAM,
      select: SELECT_PARAM,
      omit: OMIT_PARAM,
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
        //
        // A category can carry hundreds of actions, and pasting all of them
        // into every typo's error spends more context than the call would
        // have. Lead with the closest spellings, which is what a typo needs,
        // and name the two ways to see the rest.
        const available = Object.keys(actions);
        const close = nearestActions(action, available);
        throw new McpError(
          ErrorCode.UNKNOWN_ACTION,
          `Unknown action '${action}' on '${name}'.`
            + (close.length ? ` Did you mean: ${close.join(", ")}?` : "")
            + ` ${available.length} actions available - project(action="describe_action", category="${name}")`
            + ` lists them with their parameters, and project(action="search_tools") searches by intent.`,
        );
      }
      // THE per-call preparation, in the one place it is written: the routing
      // parameters (`timeoutMs`, `select`, `omit`) come off so no mapParams can
      // forward one into a bridge call, the paths are repaired before anything
      // reads them, and the category's own folding runs last over the repaired
      // bag. This route calls it; the live route in flow/task-factory.ts calls
      // the same function with the same preparation. Neither reimplements a
      // step of it, and nothing per-call belongs in this closure again.
      const pipeline = prepareCall(params, {
        action,
        normalizeParams: options?.normalizeParams,
        nestedParamsKey: options?.nestedParamsKey,
      });
      const requestedTimeout = pipeline.timeoutMs;
      const normalized = pipeline.params;
      const finish = (raw: unknown): unknown => finishCall(raw, pipeline);

      if (spec.handler) {
        // The budget travels on the context, not in the parameters: a custom
        // handler that forwards its params to the bridge must not turn it into
        // a bridge argument (#989).
        return finish(
          await spec.handler(requestedTimeout === undefined ? ctx : { ...ctx, callTimeoutMs: requestedTimeout }, normalized),
        );
      }
      if (spec.bridge) {
        const mapped = spec.mapParams ? spec.mapParams(normalized) : stripAction(normalized);
        // The caller's budget wins over the action's authored one: an action
        // that declares 120s is stating a floor it needs, not a ceiling the
        // caller may not raise.
        return finish(await ctx.bridge.call(spec.bridge, mapped, requestedTimeout ?? spec.timeoutMs));
      }
      throw new McpError(ErrorCode.NO_HANDLER, `Action '${action}' has no handler or bridge method`);
    },
  };
  // Rebuilding through the same constructor is what makes a per-session copy
  // real: the handler closes over `actions`, so a copy that only replaced the
  // record would still dispatch against the original's.
  def.rebuild = (nextActions) =>
    categoryTool(name, summary, nextActions, actionDocs, extraSchema, options);
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
  const { getFlows, getPlugins, getToolGraph } = ctx;
  return {
    ...ctx,
    bridge: session.guarded,
    project: session.project,
    session,
    // Rebound, not copied: these read per-project config, so leaving them
    // pointed at the context's previous session would report one editor's
    // flows and plugins under another editor's name.
    getFlows: getFlows ? () => getFlows(session) : undefined,
    getPlugins: getPlugins ? () => getPlugins(session) : undefined,
    getToolGraph: getToolGraph ? (forSession) => getToolGraph(forSession ?? session) : undefined,
  };
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
