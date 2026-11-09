/**
 * The contract between the daemon and its extensions. Public: an extension
 * built against one EXTENSION_API_VERSION keeps working until that number
 * changes, so anything here changes additively or bumps it.
 */
import type { DialogPromptFn, ElicitParams, ElicitResult } from "../core/types.js";
import type { DisconnectCause } from "./disconnect.js";
import type { DaemonEvent } from "./events.js";
import type { UiBundleInfo, UiManifest } from "./ui-bundles.js";
import type { ModelProvider } from "../flow/model-provider.js";
import type { FlowStepApprover } from "../flow/step-approval.js";
import type { AssetDiff, ChangesResult, CheckpointResult, CheckpointState, NativeDiffResult, RestoreResult } from "../checkpoints/service.js";
import type { DaemonSkill } from "../extensions/skills-list.js";
import type { ContextProjection, ContextProjectionOptions } from "../server/context-projection.js";
import type { ActionEffectInfo } from "../surface/action-effects.js";
import { isNewer } from "../core/version-check.js";

export type { ActionEffectInfo } from "../surface/action-effects.js";

export type { ContextProjection, ContextProjectionOptions, ProjectedTool } from "../server/context-projection.js";

export type { DaemonSkill, DaemonSkillOwner, ShadowedSkill, ProjectSkillFolder } from "../extensions/skills-list.js";

export type {
  AssetDiff,
  AssetDiffNode,
  AssetDiffNodePin,
  AssetDiffNodeProperty,
  AssetDiffRecord,
  AssetDiffSection,
  AssetFact,
  AssetThumbnail,
  AssetVersion,
  ChangeEntry,
  CheckpointInfo,
  ChangesResult,
  CheckpointResult,
  CheckpointState,
  NativeDiffResult,
  RestoreRefusal,
  RestoreResult,
} from "../checkpoints/service.js";

export type { ModelProvider, ModelRequest, ModelResponse } from "../flow/model-provider.js";

export const EXTENSION_API_VERSION = 3;

/**
 * The oldest extension API this daemon still serves. Versions up to EXTENSION_API_VERSION
 * only added to the contract, so an extension built for any of them keeps loading.
 */
export const EXTENSION_API_MIN_VERSION = 1;

/** Whether an extension declaring api { min, max } can load here. */
export function extensionApiSupported(min: number, max: number): boolean {
  return min <= EXTENSION_API_VERSION && max >= EXTENSION_API_MIN_VERSION;
}

/** The `ueMcpExtension` field of an extension's package.json. */
export interface ExtensionManifest {
  name?: string;
  api?: { min?: number; max?: number };
  /** The oldest ue-mcp the extension runs on. */
  ueMcp?: string;
  /**
   * The UI bundles it works with. The daemon serves the bundle named for the
   * extension at its own version, or else the newest from `min` up to that
   * version; never one newer. Without `min`, any older bundle qualifies.
   * `root: false` keeps its bundle off /ui/, served only at /ui/ext/<name>/.
   */
  ui?: { min?: string; root?: boolean };
}

/** An extension the daemon did not load, and what it needs. */
export interface RefusedExtension {
  name: string;
  version: string;
  reason: string;
  /** The ue-mcp version it needs, when that is why. */
  needsUeMcp?: string;
}

/**
 * Why an extension cannot run on this ue-mcp, or null when it can. An extension
 * that needs a newer ue-mcp is refused whole, never loaded with parts missing.
 */
export function extensionRefusal(name: string, ext: ExtensionManifest | undefined, installed: string): { reason: string; needsUeMcp?: string } | null {
  const min = ext?.api?.min ?? 1;
  const max = ext?.api?.max ?? min;
  if (!extensionApiSupported(min, max)) {
    return { reason: `${name} needs extension API ${min}..${max}; this ue-mcp serves ${EXTENSION_API_MIN_VERSION}..${EXTENSION_API_VERSION}` };
  }
  if (ext?.ueMcp && isNewer(ext.ueMcp, installed)) {
    return { reason: `${name} needs ue-mcp ${ext.ueMcp} or later; this is ${installed}`, needsUeMcp: ext.ueMcp };
  }
  return null;
}

export interface EditorStatus {
  name: string;
  connected: boolean;
  port: number;
  pid: number | null;
  lastDisconnect: { cause: DisconnectCause; detail?: string; at: string } | null;
}

export interface ExtensionRequest {
  method: string;
  /** Path below /v1/ext/<name>, e.g. "/sessions/42". */
  path: string;
  params: Record<string, string>;
  query: Record<string, string>;
  headers: Record<string, string | string[] | undefined>;
  /** Parsed JSON body, or undefined when there was none. */
  body: unknown;
}

export interface ExtensionResponse {
  status?: number;
  headers?: Record<string, string>;
  /** An object is sent as JSON. */
  body?: unknown;
}

export type ExtensionHandler = (req: ExtensionRequest) => ExtensionResponse | undefined | Promise<ExtensionResponse | undefined>;

/** Options for one callAction. */
export interface CallActionOptions {
  /**
   * Answers a blocking dialog this call meets, in place of an MCP elicitation
   * form, with the same contract: the form (message, requestedSchema) plus the
   * dialog itself, answered accept with content.button, decline or cancel, or
   * delegate to hand that one dialog to the agent as auto mode would. An answer
   * sooner than dialog.minHumanMs is not taken as a person's.
   */
  dialogPrompt?: DialogPromptFn;
  /** How long the call waits on an open prompt before returning the dialog refusal, in ms. Default 60000. */
  dialogWaitMs?: number;
  /**
   * Answers any other form this call raises, such as feedback approval, in
   * place of an MCP elicitation form and with its contract: accept with
   * content, decline or cancel.
   */
  elicit?: (params: ElicitParams) => Promise<ElicitResult>;
  /**
   * The access mode this call runs under (feature "callAction.access").
   * `read-only` refuses an action whose declared effect is `mutate` or
   * `unknown`, and every state-changing bridge call it would make. Default
   * read-write.
   */
  access?: "read-write" | "read-only";
}

/**
 * The data of a dialog.* event. `type` names the event: dialog.opened,
 * dialog.answered (button, and via: prompt or action), dialog.delegated,
 * dialog.closed.
 */
export interface DialogEventData {
  editor: string;
  title: string;
  message: string;
  buttons: string[];
  items?: Array<{ index: number; label: string; cells: string[]; checked: boolean }>;
  mode?: "auto" | "interactive" | "defer";
  button?: string;
  via?: "prompt" | "action";
}

/**
 * Options for one api.context.call: the projection the call runs against, plus
 * the callAction prompts. `strategy` defaults to the one the server runs;
 * `categories` to every category.
 */
export interface ContextCallOptions extends CallActionOptions {
  strategy?: "micro" | "lean" | "full";
  categories?: string[];
}

/** The text and error flag of an action's result. */
export interface ActionResult {
  isError: boolean;
  content: Array<{ type: string; text?: string }>;
}

/**
 * Objects extensions share in process (feature "extension.services"). One
 * provider holds an id at a time; `provide` throws while another does and
 * returns the revoke. `watch` calls back now if the id is provided, on each
 * provide, and with undefined on each revoke, and returns the unsubscribe.
 * Extensions activate in directory order, so a consumer may load before its
 * provider. A provider's services are revoked when it is disposed, and a
 * disposed extension's watches stop first.
 */
export interface ExtensionServicesApi {
  provide(id: string, impl: unknown): () => void;
  get<T>(id: string): T | undefined;
  watch<T>(id: string, cb: (impl: T | undefined) => void): () => void;
}

export interface DaemonExtensionApi {
  /** EXTENSION_API_VERSION of the daemon that loaded this extension. */
  apiVersion: number;
  /** ue-mcp package version. */
  version: string;
  projectDir: string;
  /** A directory for this extension's own state, per project, outside the project tree. */
  dataDir: string;

  events: {
    publish(type: string, data?: Record<string, unknown>): DaemonEvent;
    subscribe(listener: (e: DaemonEvent) => void): () => void;
    since(lastId: number): { events: DaemonEvent[]; gap: boolean };
  };

  /** Serve `method path` under /v1/ext/<name>. `:param` segments are captured. */
  route(method: "GET" | "POST" | "PUT" | "PATCH" | "DELETE", path: string, handler: ExtensionHandler): void;

  /**
   * Run one category action through the same dispatch as an MCP call. A
   * blocking dialog the call meets is put to `opts.dialogPrompt` (feature
   * "callAction.dialogPrompt"), and any other form to `opts.elicit` (feature
   * "callAction.elicit"), the way an MCP client is shown a form; without
   * them, nobody is asked.
   */
  callAction(category: string, action: string, args?: Record<string, unknown>, opts?: CallActionOptions): Promise<ActionResult>;

  /**
   * What this daemon offers beyond EXTENSION_API_VERSION, for feature detection:
   *   "callAction.dialogPrompt"  callAction takes `opts.dialogPrompt` and `opts.dialogWaitMs`
   *   "callAction.elicit"        callAction takes `opts.elicit` for every other form
   *   "callAction.access"        callAction and context.call take `opts.access`, and `read-only`
   *                              refuses every call that may change state
   *   "modes"                    `modes` reads and sets this project's feedback and dialog modes
   *   "modes.playtest"           `modes.get` carries `playtest` and `modes.setPlaytest` sets it
   *   "events.dialog"            dialog.opened, dialog.answered, dialog.delegated and
   *                              dialog.closed on the event stream (see DialogEventData)
   *   "models.provider"          `models.register` supplies the model flow agent steps run on
   *   "events.script"            script.run_started, script.step_started, script.step_acted,
   *                              script.step_passed, script.step_failed, script.verdict_requested,
   *                              script.verdict and script.run_finished on the event stream (see ScriptEvent)
   *   "checkpoints"              `checkpoints` drives each session's shadow history, and
   *                              checkpoint.created, checkpoint.restored, checkpoints.toggled and
   *                              checkpoints.cleared are on the event stream (API 3)
   *   "checkpoints.assetDiff"    `checkpoints.changes` gives each package an assetDiff built on
   *                              Unreal's diff code, and `assetDiff`, `thumbnail` and
   *                              `openNativeDiff` are on `checkpoints`
   *   "skills"                   `skills.list` lists ue-mcp's packaged skills and the project's own
   *   "context.projection"       `context.project` gives the instructions and tools a strategy and
   *                              category scope would advertise, and `context.call` runs one call
   *                              against that surface
   *   "actions.effects"          `actions.effects` lists each action's effect and whether it is destructive
   *   "extension.versions"       `versions` names this extension's loaded, recorded and installed versions
   *   "ui.scopedKeys"            a key from `ui.trustKey` verifies this extension's own bundles and
   *                              bundles no loaded extension names, never another extension's
   *   "ui.extensions"            `ui.own` gives this extension's own bundle, served at /ui/ext/<name>/
   *   "extension.services"       `services` lets extensions provide and use each other's objects in process
   *   "flows.stepApproval"       `flows.approveSteps` asks this extension before each task of a run that
   *                              names it (POST /v1/flows/<name>/run with `stepApproval`)
   * Absent on a daemon older than the field.
   */
  features?: readonly string[];

  /**
   * Approve each flow step as it runs (feature "flows.stepApproval").
   * `approveSteps` registers `approver` under this extension's name, replacing
   * an earlier one, and returns the function that removes it. A run started
   * with `stepApproval: { approver: <this extension's name>, token }` asks it
   * before every task it creates, rollbacks excepted, with the task's
   * resolved options and the run's token; a denial fails that step with the
   * reason and the task never runs. A run naming an approver that is not
   * registered fails its first step instead of running unapproved.
   */
  flows?: {
    approveSteps(approver: FlowStepApprover): () => void;
  };

  /** Objects shared with other extensions in process (feature "extension.services"). */
  services?: ExtensionServicesApi;

  /**
   * The model behind flow agent steps (`class_path: agent` and `agent_prompt`, and the
   * built-in playtest flow), feature "models.provider", API 2. `register` makes `provider` the
   * one those steps call, ahead of ANTHROPIC_API_KEY; the newest registration still live wins,
   * and the function it returns removes it. Without either, an agent step fails with
   * "No model is configured for flow agent steps."
   */
  models?: {
    register(provider: ModelProvider): () => void;
  };

  /**
   * Checkpoints per editor session (#1482), feature "checkpoints", API 3: the
   * same operations and payloads as the /v1/checkpoints routes. `editor` names
   * a session; omitted is the active one. `setEnabled` is the per-session
   * override, kept in memory for the daemon's lifetime; null follows
   * ue-mcp.yml again.
   */
  checkpoints?: {
    state(opts?: { editor?: string; limit?: number }): Promise<CheckpointState>;
    setEnabled(enabled: boolean | null, opts?: { editor?: string }): Promise<CheckpointState>;
    create(label?: string, opts?: { editor?: string }): Promise<CheckpointResult>;
    changes(from: string, to?: string, opts?: { editor?: string; assetDiffs?: boolean }): Promise<ChangesResult>;
    /** One package's asset diff, for a path `changes` deferred (feature "checkpoints.assetDiff"). */
    assetDiff?(from: string, to: string | undefined, path: string, opts?: { editor?: string }): Promise<AssetDiff>;
    /** The PNG an asset diff's thumbnail id names, or null (feature "checkpoints.assetDiff"). */
    thumbnail?(id: string, opts?: { editor?: string }): { file: string; type: string } | null;
    /** Unreal's own diff of a package, brought to the front (feature "checkpoints.assetDiff"). */
    openNativeDiff?(path: string, from: string, to?: string, opts?: { editor?: string }): Promise<NativeDiffResult>;
    restore(checkpoint: string, opts?: { paths?: string[]; force?: boolean; editor?: string }): Promise<RestoreResult>;
    clear(opts?: { editor?: string }): Promise<{ editor: string; cleared: true }>;
  };

  /**
   * Every skill a client of this project can load (feature "skills"), the
   * same list as GET /v1/skills: ue-mcp's packaged skills and every skill
   * directory under the project's .claude/skills and .agents/skills, one
   * entry per id, with owner and location. A project copy hides the packaged
   * one of the same id and names it in `shadows`.
   */
  skills?: {
    list(): DaemonSkill[];
  };

  /**
   * The surface a client would be handed under a context strategy and a set
   * of categories (feature "context.projection"), the same answer as GET
   * /v1/context/projection. `project` throws on an unknown strategy or
   * category. `call` runs one call to a projected tool (a category, lean's
   * catalog or a category's describe, micro's tools gateway, or flow) exactly
   * as the MCP server would, through the dispatch callAction uses; a tool or a
   * gateway call outside the scope is refused with an error result.
   */
  context?: {
    project(opts: ContextProjectionOptions): ContextProjection;
    call(tool: string, input: Record<string, unknown>, opts?: ContextCallOptions): Promise<ActionResult>;
  };

  /**
   * Every enabled action's effect and destructive flag (feature
   * "actions.effects"), the same list as GET /v1/actions. The effect is read
   * from the graph the server dispatches, plugin actions included.
   * `destructive` is what the action declares, else (for mutate and unknown)
   * whether its name leads with a destructive verb; the verb list is a
   * fallback. `categories` narrows the list; an unknown name throws.
   */
  actions?: {
    effects(categories?: string[]): ActionEffectInfo[];
  };

  /**
   * This extension's versions (feature "extension.versions", #1619): the one
   * loaded, the one this project records, and every one installed on the
   * machine, oldest first. Versions install side by side and each project
   * loads its recorded one, so another project can install a newer one at any
   * time; `ue-mcp extension use <name> <version> <project>` moves a project.
   */
  versions?(): { loaded: string; recorded: string | null; installed: string[] };

  editors(): EditorStatus[];
  /** Start, stop, restart or rebuild the editor, with the cause announced first. */
  editorOperation(op: "start" | "stop" | "restart" | "rebuild"): Promise<Record<string, unknown>>;

  /** Report whether this extension has work in progress; the daemon does not idle-exit while it does. */
  setBusy(busy: () => boolean): void;

  ui: {
    /**
     * Trust an Ed25519 public key (PEM) for UI bundles. A bundle named for a
     * loaded extension verifies only against that extension's keys (feature "ui.scopedKeys").
     */
    trustKey(pem: string): void;
    /** Verify a bundle directory and install it. Throws when it does not verify. */
    install(dir: string): UiManifest;
    /**
     * The bundle being served, or null. A bundle named for this extension is
     * served only at its version or older (`ueMcpExtension.ui`).
     */
    active(): UiManifest | null;
    /**
     * This extension's own bundle, newest in its version window, which the
     * daemon serves at /ui/ext/<name>/ (feature "ui.extensions"), or null.
     */
    own?(): UiManifest | null;
  };

  /**
   * The ue-mcp account this machine signed in with (`ue-mcp login`), or null.
   * The token authenticates the user to ue-mcp services; never log or display it.
   */
  registryAccount(): Promise<{ login: string; token: string; registry: string } | null>;

  /**
   * How feedback and editor dialogs are handled for this project, the way
   * `ue-mcp feedback mode` and `ue-mcp dialog mode` set them with --editor
   * (feature "modes"). `get` gives the effective mode and whether this project
   * sets it; `set` with undefined goes back to the per-user preference.
   */
  modes?: {
    get(): {
      feedback: { mode: "interactive" | "auto-approve" | "defer"; project: boolean };
      dialog: { mode: "interactive" | "auto" | "defer" | null; project: boolean };
      /**
       * Who starts playtest runs (feature "modes.playtest"), as `ue-mcp playtest
       * mode` sets it. null is unset: each client decides. Under manual, an MCP
       * session's pie(script_run) and pie(script_replay) are refused; calls made
       * through callAction are not, so an extension applies its own rule.
       */
      playtest: { mode: "manual" | "automatic" | null; project: boolean };
    };
    setFeedback(mode: "interactive" | "auto-approve" | "defer" | undefined): void;
    setDialog(mode: "interactive" | "auto" | "defer" | undefined): void;
    /** Set this project's playtest mode; undefined goes back to the per-user preference (feature "modes.playtest"). */
    setPlaytest(mode: "manual" | "automatic" | undefined): void;
  };

  log: {
    info(message: string): void;
    warn(message: string, detail?: unknown): void;
  };
}

export interface DaemonExtension {
  activate(api: DaemonExtensionApi): void | { dispose?: () => void | Promise<void> } | Promise<void | { dispose?: () => void | Promise<void> }>;
}

/** GET /v1/health. */
export interface DaemonHealth {
  ok: true;
  pid: number;
  version: string;
  /** DAEMON_API_VERSION: the HTTP and shim protocol, which a UI bundle's range is checked against. */
  apiVersion: number;
  projectRoot: string;
  startedAt: string;
  editors: EditorStatus[];
  /** Open MCP sessions. */
  clients: number;
  /** Requests received from MCP sessions and not yet answered. */
  inflight: number;
  lastEventId: number;
  extensions: Array<{ name: string; version: string }>;
  /** Extensions this daemon refused to load, with why; the client offers the update that fixes it. */
  refusedExtensions?: RefusedExtension[];
  /** Version of the UI bundle being served at /ui/, or null. */
  ui: string | null;
  /**
   * Each loaded extension's own bundle, in load order: name, version, `base`
   * (/ui/ext/<name>/) and the signed `module` it declares (feature "ui.extensions").
   */
  uiBundles?: UiBundleInfo[];
  /**
   * What the HTTP API offers beyond apiVersion, for feature detection ("checkpoints", "skills",
   * "context.projection", "actions.effects", and:
   *   "install.pendingBuild"  GET /v1/install carries plugin.pendingBuild, a source bridge deployed
   *                           and not built yet, which the next start through ue-mcp builds first
   *   "editor.rebuild"        POST /v1/editor/rebuild stops the editor, builds, and starts it again
   *                           only when the build passed; a failed step carries `reason`
   *   "ui.scopedKeys"         a bundle named for a loaded extension is installed and served only
   *                           when signed by a key that extension trusted
   *   "ui.extensions"         GET /ui/ext/<name>/ serves each loaded extension's own bundle, listed
   *                           in `uiBundles`; `ueMcpExtension.ui.root: false` keeps one off /ui/
   *   "sessions.access"       GET /v1/sessions lists each MCP session with its access mode, and
   *                           PUT /v1/sessions/<id>/access sets one to read-only or read-write).
   * Absent on a daemon older than the field.
   */
  features?: readonly string[];
  /** Open MCP sessions with their access mode (feature "sessions.access"). */
  sessions?: McpSessionInfo[];
}

/** One open MCP session on the daemon (feature "sessions.access"). */
export interface McpSessionInfo {
  /** Stable for the life of the socket; also sent as `x-ue-mcp-session` on the WebSocket upgrade. */
  id: string;
  /** The MCP client's name once it has introduced itself, else what its shim said. */
  client: string;
  connectedAt: string;
  /** `read-only` refuses every call whose declared effect is `mutate` or `unknown`. */
  access: "read-write" | "read-only";
}
