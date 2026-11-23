// @session independent: The value every route hands the call pipeline and the order its gates run in; holds nothing per editor.
/**
 * One call, whichever way it arrived.
 *
 * An MCP tool call, a micro gateway call, a flow run, a flow HTTP request and
 * an in-process `callAction` each read their own wire shape at their own
 * boundary and turn it into one `ActionCall`. From there every route goes
 * through the same gates, in GATE_ORDER, each once, and comes back as one
 * `CallOutcome`. Only the outermost adapter turns that outcome into MCP
 * content, an HTTP body or a CLI line, so nothing between the gates and the
 * adapter reads or writes response text.
 */
import { ErrorCode } from "../core/errors.js";
import type { ElicitFn } from "../core/elicit.js";
import type { ProgressFn } from "../core/progress.js";

/**
 * The gates, in the order they run. Each is a middleware over the rest: it
 * answers in place of them, or runs them and may shape what they answered.
 *
 *  - route: the editor's own surface is built, so the call runs against that
 *    editor's registry and never another project's.
 *  - untargeted: beyond one editor, a change has to name its editor (#817).
 *  - read-only: a read-only session runs reads only (#1484).
 *  - playtest: an MCP client does not start playtests a person runs by hand.
 *  - dialog: nothing runs past a blocking modal, and what failed under one is
 *    reported as the dialog it was.
 *  - consent: stopping, restarting, rebuilding or patching the editor waits
 *    for the other sessions using it to agree (#2075). After the dialog gate,
 *    so a modal is reported as the modal rather than opening a request.
 *  - disabled: the project's `disable:` turned the action off.
 *  - known: the editor provides the action and the category has it.
 *  - engine: the attached engine has the engine tool the action wraps.
 *  - withheld: the running plugin registered the method behind it (6.4).
 *  - lease: no other MCP session holds what the call would change.
 *  - journal: a change inside a journal group is recorded (#1481). It runs
 *    outside `lock`: journaled calls queue one at a time, so a call holding an
 *    asset lock while it waited for the queue could wait on a call that needs
 *    that same lock.
 *  - lock: the assets the call changes are locked in its editor.
 *  - run: the task, or the flow, runs.
 */
export const GATE_ORDER = [
  "route",
  "untargeted",
  "read-only",
  "playtest",
  "dialog",
  "consent",
  "disabled",
  "known",
  "engine",
  "withheld",
  "lease",
  "journal",
  "lock",
  "run",
] as const;

export type GateName = (typeof GATE_ORDER)[number];

/** How the caller addressed an editor. */
export interface CallRouting {
  /** True when the caller named the editor rather than taking the active one. */
  targeted: boolean;
}

/** Who sent the call, and so who may be asked while it runs. */
export interface Asker {
  /**
   * Where the call came from. An MCP client's playtest runs follow the
   * project's playtest mode; a process-local caller (an extension, the CLI)
   * enforces its own rules; an HTTP request has nobody to ask.
   */
  origin: "mcp" | "in-process" | "http";
  /** The caller's elicitation gate, when it has one. */
  elicit?: ElicitFn;
  /** False when nobody can be asked on this route, whatever the guard's own client supports. */
  canElicit?: boolean;
  /** The MCP client that sent the call. */
  client?: { name: string; version?: string };
  /** The caller's access mode, read when a gate asks. Absent is read-write. */
  access?: () => "read-write" | "read-only";
  /**
   * Who the call's asset leases are held under, which is also the session its
   * activity is counted for. `parent` is the session that started it, when the
   * client says so.
   */
  leaseHolder: { id: string; label: string; parent?: string };
  onProgress?: ProgressFn;
  /** Aborted when the caller cancels the call. */
  signal?: AbortSignal;
}

/** What runs a call once every gate let it through. */
export type CallTarget =
  /**
   * A task from the editor's registry. `registryTask` and `registryParams`
   * are what the registry is asked for, which differ from the call's own
   * task and parameters only for a call made through the micro gateway.
   */
  | { kind: "task"; registryTask: string; registryParams: Record<string, unknown> }
  /**
   * The flow tool, which addresses one editor for the whole run. `input` is
   * the flow call as the tool reads it, `action` included; `approval` is the
   * step approver an HTTP run named (flow/step-approval.ts).
   */
  | { kind: "flow"; input: Record<string, unknown>; approval?: { approver: string; token: string } };

export interface ActionCall<S> {
  /** `category.action`, the action that actually runs. */
  task: string;
  /** The action's own parameters: no `action`, no `editor`. */
  params: Record<string, unknown>;
  routing: CallRouting;
  /** The editor the call runs in. */
  session: S;
  asker: Asker;
  target: CallTarget;
}

/**
 * How a call ended. A success carries `data` and no `code`; a refusal carries
 * `code: REFUSED` and the refusal payload as `data`; any other failure carries
 * its error code and `message`, and `details` when the error had a
 * machine-readable companion (#799).
 */
export interface CallOutcome {
  code?: string;
  data?: unknown;
  message?: string;
  details?: Record<string, unknown>;
  /** Said before the result, such as a dialog answered while the call waited. */
  notes: string[];
  /** Images and audio the result carried, taken out of `data`. */
  media: Array<{ type: "image" | "audio"; data: string; mimeType: string }>;
  /** Whether the answer names the editor that served it (beyond one editor). */
  attributed: boolean;
}

/** Image or audio a result carries, as MCP content rather than base64 inside its JSON text. */
export type MediaBlock = CallOutcome["media"][number];

/**
 * A result's `media` entries ({type, mimeType, data}) become MCP image and
 * audio content. Each entry stays in the JSON without its data and says which
 * content block holds it, so the text still describes the result whole.
 */
export function splitMedia(data: unknown): { data: unknown; blocks: MediaBlock[] } {
  if (!data || typeof data !== "object" || Array.isArray(data)) return { data, blocks: [] };
  const media = (data as Record<string, unknown>).media;
  if (!Array.isArray(media)) return { data, blocks: [] };
  const blocks: MediaBlock[] = [];
  const kept = media.map((entry) => {
    if (!entry || typeof entry !== "object") return entry;
    const { data: payload, ...rest } = entry as Record<string, unknown>;
    const type = rest.type;
    if ((type !== "image" && type !== "audio") || typeof payload !== "string" || typeof rest.mimeType !== "string") return entry;
    blocks.push({ type, data: payload, mimeType: rest.mimeType });
    return { ...rest, contentBlock: blocks.length };
  });
  return blocks.length ? { data: { ...(data as Record<string, unknown>), media: kept }, blocks } : { data, blocks };
}

export function succeeded(data: unknown, notes: string[], attributed: boolean): CallOutcome {
  const { data: shown, blocks } = splitMedia(data);
  return { data: shown, notes, media: blocks, attributed };
}

export function failed(code: string, message: string, attributed: boolean, details?: Record<string, unknown>): CallOutcome {
  return { code, message, ...(details ? { details } : {}), notes: [], media: [], attributed };
}

export function refused(refusal: unknown, attributed: boolean): CallOutcome {
  return { code: ErrorCode.REFUSED, data: refusal, notes: [], media: [], attributed };
}

export function isFailure(outcome: CallOutcome): boolean {
  return outcome.code !== undefined;
}

/** One gate: answer in place of the rest, or run them with `next`. */
export type Gate<S> = (call: ActionCall<S>, next: () => Promise<CallOutcome>) => Promise<CallOutcome>;

/** Every gate, keyed by name, so a gate cannot be left out or given twice. */
export type Gates<S> = Record<GateName, Gate<S>>;

/** Run a call through its gates in GATE_ORDER. The run gate is the last and never calls next. */
export function runGates<S>(call: ActionCall<S>, gates: Gates<S>): Promise<CallOutcome> {
  const step = (i: number): Promise<CallOutcome> => {
    if (i >= GATE_ORDER.length) return Promise.reject(new Error("the run gate passed the call on, and nothing follows it"));
    return gates[GATE_ORDER[i]](call, () => step(i + 1));
  };
  return step(0);
}
