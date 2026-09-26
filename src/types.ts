import type { z } from "zod";
import type { IBridge } from "./bridge.js";
import type { ProjectContext } from "./project.js";
import type { EditorSession, SessionRegistry } from "./session.js";
import type { ParamChoice, ParamSpec } from "./handler-spec.js";
import type { EpicInputSchema, EpicToolRef } from "./epic-input.js";

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

  /**
   * Who is on the other end, as they named themselves at initialize.
   *
   * Read to decide how much the client can be trusted to RENDER, which is a
   * different question from what it advertised support for. Absent on a gate
   * built outside the server, where nothing knows.
   */
  client?: () => { name: string; version?: string } | undefined;
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
  | { type: "boolean"; title?: string; description?: string; default?: boolean }
  | (ElicitMultiSelectBase & { items: { type: "string"; enum: string[] } })
  | (ElicitMultiSelectBase & { items: { anyOf: Array<{ const: string; title: string }> } });

/** A checkbox group: the answer is the list of ticked values. */
interface ElicitMultiSelectBase {
  type: "array";
  title?: string;
  description?: string;
  minItems?: number;
  maxItems?: number;
  default?: string[];
}

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

/**
 * A terminal rendering of a long wait, such as `startProgress` in ui/progress.ts.
 * Handed in by the caller, so a module that waits never draws on its own.
 */
export interface ProgressDisplay {
  update(state: { fraction: number | null; message: string; detail?: string }): void;
  stop(finalLine?: string): void;
}

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
 * What an action does to the editor it is addressed to.
 *
 *   read    observes. Changes neither the editor, its project on disk, nor its
 *           process. Landing one in the wrong editor returns the wrong answer
 *           and changes nothing.
 *   mutate  may change any of those, or has an effect outside the editor
 *           (writes a file, posts an issue, launches or quits a process).
 *   unknown decided by a PARAMETER rather than by the action, so the
 *           declaration cannot say. An arbitrary python string, a console
 *           command and a wrapped third-party tool are the real cases. Gated
 *           as `mutate` everywhere, so the honest label costs nothing at a
 *           gate.
 *
 * Two rules settle the cases that come up while declaring one:
 *
 *   Declare what the action is FOR. An action that offers a `dryRun` is still
 *   a `mutate`: previewing is a mode, not the purpose. `unknown` is for an
 *   action with no inherent direction at all, where a parameter supplies the
 *   whole of what it does.
 *
 *   A response written to a file is still a response. An action whose only
 *   write is the caller-named destination for its own result reads;
 *   `asset(bulk_read_properties)` spilling rows to `outputPath` is a large
 *   answer, not an edit. An action that writes into the addressed project or
 *   into the editor's own state mutates, even when what it writes was derived
 *   from a read.
 *
 * Every variant of `ActionSpec` requires this, which is the point of it. It
 * used to live nowhere, and three separate places each guessed it by matching
 * an action's NAME against a list of verbs. A list of verbs is open-ended and
 * the set of actions is not, so every verb missing from a list was an action
 * classified by accident: a guard asked to stand in front of every mutation
 * matched 542 of 1090 actions and let `write_cpp_file`, `build`, `sculpt`,
 * `place_actor` and every bare verb like `save` and `create` past it.
 *
 * The lesson was already written down one field below, on `destinationEditor`:
 * "Declared on the action rather than assumed from its name." It was applied to
 * a routing flag that affects one action, and not to the field that decides
 * whether a guard sees a call at all.
 */
export type ActionEffect = "read" | "mutate" | "unknown";

/**
 * Where an `effect` value came from.
 *
 * `declared` is a person's answer, written at the declaration and reviewable in
 * a diff. It is the default, and the only thing an action in `ALL_TOOLS` is
 * allowed to be.
 *
 * `inferred` is the name lexicon's answer, and exists only for actions this
 * package never declares: Epic's wrapped engine tools, read out of a live
 * registry at startup and possibly from a toolset no release has seen, and a
 * plugin action whose manifest did not say. Recording which is which is what
 * keeps a guess from being read back later as a fact.
 */
export type ActionEffectSource = "declared" | "inferred";

/** What every action carries, whatever it dispatches to. */
interface ActionSpecBase {
  /** What this action does to the addressed editor. Required, always. */
  effect: ActionEffect;
  /** Omitted means `declared`. Set to `inferred` only by runtime injection. */
  effectSource?: ActionEffectSource;
  description?: string;
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

/** Forwards to a C++ bridge method over the WebSocket. Built by `bp`. */
export interface BridgeActionSpec extends ActionSpecBase {
  kind: "bridge";
  bridge: string;
  mapParams?: (p: Record<string, unknown>) => Record<string, unknown>;
  /**
   * The parameters the bridge method declares in C++, set by specBp (#1057).
   * When present it is the authority on this action's names, types, required
   * flags and aliases; the category's zod shape is shared and cannot say.
   */
  paramSpec?: readonly ParamSpec[];
  /**
   * The spec's required choices (`actorLabel OR actorPath`), set by specBp.
   * The flat category shape cannot express them, so every dispatch route
   * checks them before the call is sent.
   */
  paramChoices?: readonly ParamChoice[];
  /**
   * The wrapped engine tool's input schema, set on generated `epic_*` actions.
   * The structured source their compact signatures are built from (#1172).
   */
  epicSchema?: EpicInputSchema;
  /**
   * The engine tool a generated `epic_*` action wraps. Dispatch builds the
   * `epic_call_tool` envelope from it and `epicSchema` (see paramMapperOf).
   */
  epicTool?: EpicToolRef;
  handler?: never;
}

/** Runs in this Node process. It may still call the bridge itself. */
export interface HandlerActionSpec extends ActionSpecBase {
  kind: "handler";
  handler: (ctx: ToolContext, params: Record<string, unknown>) => Promise<unknown>;
  bridge?: never;
  mapParams?: never;
}

/**
 * Dispatched through the task registry under `${category}.${action}`, which is
 * how a plugin contributes one. It carries no bridge method and no closure of
 * its own on purpose: the registry owns both, and `categoryTool`'s dispatcher
 * refuses a direct call with NO_HANDLER exactly as it always did.
 */
export interface RegistryActionSpec extends ActionSpecBase {
  kind: "registry";
  bridge?: never;
  handler?: never;
  mapParams?: never;
}

/**
 * One action.
 *
 * A tagged union rather than a bag of six optional fields, so the two shapes
 * that were expressible and meaningless are now unwritable: an action with both
 * a bridge method and a handler, and an action with neither that does not say
 * it is a registry action. `{}` no longer type-checks either.
 */
export type ActionSpec = BridgeActionSpec | HandlerActionSpec | RegistryActionSpec;

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
   * Keys that are one parameter under different names after normalizeParams
   * (a mirrored alias, or a path split into name + packagePath). A key is only
   * reported as not read by the editor when no member of its group was read.
   */
  paramGroups?: readonly (readonly string[])[];
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
  /**
   * Advertise this tool's declared shape as-is rather than as `action` +
   * `args` (#1172). Set by the lean `catalog` discovery tool, whose handful of
   * parameters are already compact.
   */
  flatSurface?: boolean;
}
