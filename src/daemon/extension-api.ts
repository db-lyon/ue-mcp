/**
 * The contract between the daemon and its extensions. Public: an extension
 * built against one EXTENSION_API_VERSION keeps working until that number
 * changes, so anything here changes additively or bumps it.
 */
import type { DialogPromptFn, ElicitParams, ElicitResult } from "../core/types.js";
import type { DisconnectCause } from "./disconnect.js";
import type { DaemonEvent } from "./events.js";
import type { UiManifest } from "./ui-bundles.js";
import type { ModelProvider } from "../flow/model-provider.js";
import type { ChangesResult, CheckpointResult, CheckpointState, RestoreResult } from "../checkpoints/service.js";
import type { DaemonSkill } from "../extensions/skills-list.js";

export type { DaemonSkill, DaemonSkillOwner, ShadowedSkill, ProjectSkillFolder } from "../extensions/skills-list.js";

export type { ChangeEntry, CheckpointInfo, ChangesResult, CheckpointResult, CheckpointState, RestoreRefusal, RestoreResult } from "../checkpoints/service.js";

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

/** The text and error flag of an action's result. */
export interface ActionResult {
  isError: boolean;
  content: Array<{ type: string; text?: string }>;
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
   *   "modes"                    `modes` reads and sets this project's feedback and dialog modes
   *   "events.dialog"            dialog.opened, dialog.answered, dialog.delegated and
   *                              dialog.closed on the event stream (see DialogEventData)
   *   "models.provider"          `models.register` supplies the model flow agent steps run on
   *   "events.script"            script.run_started, script.step_started, script.step_acted,
   *                              script.step_passed, script.step_failed, script.verdict_requested,
   *                              script.verdict and script.run_finished on the event stream (see ScriptEvent)
   *   "checkpoints"              `checkpoints` drives each session's shadow history, and
   *                              checkpoint.created, checkpoint.restored, checkpoints.toggled and
   *                              checkpoints.cleared are on the event stream (API 3)
   *   "skills"                   `skills.list` lists ue-mcp's packaged skills and the project's own
   * Absent on a daemon older than the field.
   */
  features?: readonly string[];

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
    changes(from: string, to?: string, opts?: { editor?: string }): Promise<ChangesResult>;
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

  editors(): EditorStatus[];
  /** Start, stop, restart or rebuild the editor, with the cause announced first. */
  editorOperation(op: "start" | "stop" | "restart" | "rebuild"): Promise<Record<string, unknown>>;

  /** Report whether this extension has work in progress; the daemon does not idle-exit while it does. */
  setBusy(busy: () => boolean): void;

  ui: {
    /** Trust an Ed25519 public key (PEM) for UI bundles. */
    trustKey(pem: string): void;
    /** Verify a bundle directory and install it. Throws when it does not verify. */
    install(dir: string): UiManifest;
    /** The bundle being served, or null. */
    active(): UiManifest | null;
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
    get(): { feedback: { mode: "interactive" | "auto-approve" | "defer"; project: boolean }; dialog: { mode: "interactive" | "auto" | "defer" | null; project: boolean } };
    setFeedback(mode: "interactive" | "auto-approve" | "defer" | undefined): void;
    setDialog(mode: "interactive" | "auto" | "defer" | undefined): void;
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
  /** Version of the UI bundle being served, or null. */
  ui: string | null;
  /** What the HTTP API offers beyond apiVersion, for feature detection ("checkpoints", "skills"). Absent on a daemon older than the field. */
  features?: readonly string[];
}
