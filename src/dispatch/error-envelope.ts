/**
 * The error envelope every action answers a failure with, beside the fields
 * it always carried.
 *
 *   usage         the call as written cannot succeed: fix the parameters
 *   precondition  the editor or project is not in a state this call needs
 *   failure       the call ran and the operation itself failed
 *   internal      a defect or an unexpected fault on our side
 *
 * One ordered table maps the server's codes, the editor's `errorCode` tokens
 * and its message wording to a class. The first matching rule wins.
 */
import { ErrorCode } from "../core/errors.js";

export type ErrorClass = "usage" | "precondition" | "failure" | "internal";

export const ERROR_CLASSES: readonly ErrorClass[] = ["usage", "precondition", "failure", "internal"];

export interface ErrorEnvelope {
  class: ErrorClass;
  message: string;
  /** Closest valid names, when the failure named something that does not exist. */
  didYouMean?: string[];
  /** Whether the same call may succeed later without being changed. */
  retryable: boolean;
  /** Inside a flow: the path of the step that failed, e.g. `2/1`. */
  step?: string;
}

export interface ErrorRule {
  /** Server code (`ErrorCode`, `TASK_FAILED`, `UNKNOWN`) the rule applies to. */
  code?: string | RegExp;
  /** The editor's `errorCode` token. */
  editorCode?: string | RegExp;
  /** The message wording. */
  text?: RegExp;
  class: ErrorClass;
  retryable: boolean;
}

/** Ordered: the first rule whose every given matcher matches decides. */
export const ERROR_CLASS_TABLE: readonly ErrorRule[] = [
  // Server codes whose meaning does not depend on the wording.
  { code: ErrorCode.UNKNOWN_ACTION, class: "usage", retryable: false },
  { code: ErrorCode.INVALID_PARAMS, class: "usage", retryable: false },
  { code: ErrorCode.NOT_FOUND, class: "usage", retryable: false },
  { code: ErrorCode.NOT_CONNECTED, class: "precondition", retryable: true },
  { code: ErrorCode.CONNECTION_LOST, class: "precondition", retryable: true },
  { code: ErrorCode.PROJECT_NOT_LOADED, class: "precondition", retryable: false },
  { code: ErrorCode.ASSET_LOCKED, class: "precondition", retryable: true },
  { code: ErrorCode.WRITE_BLOCKED, class: "precondition", retryable: false },
  // The editor may have finished the call: read the state back before any retry.
  { code: ErrorCode.BRIDGE_TIMEOUT, class: "failure", retryable: false },
  { code: ErrorCode.NO_HANDLER, class: "internal", retryable: false },
  { code: "DIALOG_BLOCKED", class: "precondition", retryable: true },
  // A flow's declared check refused the step: the state it guards is not there.
  { code: "CHECK_FAILED", class: "precondition", retryable: false },
  // A method the loaded plugin does not register: it is older than the server.
  { code: ErrorCode.BRIDGE_ERROR, text: /unknown method|method not found|not registered/i, class: "precondition", retryable: false },
  { code: ErrorCode.BRIDGE_ERROR, class: "internal", retryable: false },

  // The editor's machine tokens.
  { editorCode: /^(invalid_|nope$)/, class: "usage", retryable: false },
  { editorCode: /_not_found$|^not_found$/, class: "usage", retryable: false },
  { editorCode: /^(not_ready|shutting_down|controller_unavailable)$/, class: "precondition", retryable: true },
  { editorCode: /^(protected_asset|unsupported_engine_version|auto_setup_unavailable|incompatible_rig|non_empty_rig|dangling_goal)$/, class: "precondition", retryable: false },
  { editorCode: /^timeout$/, class: "failure", retryable: false },
  { editorCode: /^(save_failed|mutation_failed)$/, class: "failure", retryable: false },

  // The editor's wording, for the handlers that return a message only.
  { text: /missing required (vector |rotator )?parameter|parameters are required|is required\b|must be (a|an|one of|between|>=|<=|non-?empty|positive)|must not be empty|invalid (value|parameter|enum|json)|unknown (option|parameter|property|mode|value|action)|does not take|expected (a|an|one of)|not a valid|unrecognized/i, class: "usage", retryable: false },
  { text: /did you mean/i, class: "usage", retryable: false },
  { text: /not found|does not exist|no such /i, class: "usage", retryable: false },
  { text: /modal dialog|dialog is (up|blocking|open)/i, class: "precondition", retryable: true },
  { text: /is (still )?compiling|asset registry .*(scan|loading)|not ready|try again|busy|in progress/i, class: "precondition", retryable: true },
  { text: /not connected|no editor is connected|editor (is )?not running|bridge .*not (running|reachable)/i, class: "precondition", retryable: true },
  { text: /requires unreal engine|plugin (is )?not (loaded|enabled|available)|class is unavailable|not available|no editor world|editor world not available|pie (session )?(is )?(already )?(active|running)|not in pie|no (active )?pie|already exists|already (running|stopped)|protected asset|read-?only|checked out|is dirty|has referencers/i, class: "precondition", retryable: false },
  { text: /timed out|timeout/i, class: "failure", retryable: false },
  { text: /exception|assert|null pointer|nullptr|unexpected|internal error|crash/i, class: "internal", retryable: false },

  // The server's catch-all for a throw with no code.
  { code: "UNKNOWN", class: "internal", retryable: false },
];

function matches(m: string | RegExp | undefined, value: string | undefined): boolean {
  if (m === undefined) return true;
  if (value === undefined) return false;
  return typeof m === "string" ? m === value : m.test(value);
}

/** The class and retryability of one failure. Anything unmatched ran and failed. */
export function classifyError(input: { code?: string; editorCode?: string; message: string }): {
  class: ErrorClass;
  retryable: boolean;
} {
  for (const rule of ERROR_CLASS_TABLE) {
    if (rule.code === undefined && rule.editorCode === undefined && rule.text === undefined) continue;
    if (rule.code !== undefined && !matches(rule.code, input.code)) continue;
    if (rule.editorCode !== undefined && !matches(rule.editorCode, input.editorCode)) continue;
    if (rule.text !== undefined && !matches(rule.text, input.message)) continue;
    return { class: rule.class, retryable: rule.retryable };
  }
  return { class: "failure", retryable: false };
}

/** Names offered in a message: `Did you mean: a, b?`, `Did you mean 'a'?` or `Closest: a, b`. */
export function suggestionsIn(message: string): string[] | undefined {
  const m = /did you mean[:\s]+([^?\n]+)\?/i.exec(message) ?? /closest:\s*([^.\n]+)/i.exec(message);
  if (!m) return undefined;
  const names = m[1]
    .split(/,\s*|\s+or\s+/)
    .map((s) => s.trim().replace(/^['"`]|['"`]$/g, ""))
    .filter((s) => s.length > 0 && !/\s/.test(s));
  return names.length ? names : undefined;
}

/** String suggestions an editor body carries under one of its usual keys. */
function suggestionsInBody(body: Record<string, unknown> | undefined): string[] | undefined {
  if (!body) return undefined;
  for (const key of ["didYouMean", "suggestions", "candidates", "closest"]) {
    const v = body[key];
    if (Array.isArray(v)) {
      const names = v.filter((s): s is string => typeof s === "string");
      if (names.length) return names;
    }
  }
  return undefined;
}

export interface EnvelopeInput {
  code?: string;
  editorCode?: string;
  message: string;
  step?: string;
  didYouMean?: string[];
  /** The editor's body, read for its suggestion lists. */
  body?: Record<string, unknown>;
}

export function errorEnvelope(input: EnvelopeInput): ErrorEnvelope {
  const { class: cls, retryable } = classifyError(input);
  const didYouMean = input.didYouMean ?? suggestionsInBody(input.body) ?? suggestionsIn(input.message);
  return {
    class: cls,
    message: input.message,
    ...(didYouMean?.length ? { didYouMean } : {}),
    retryable,
    ...(input.step !== undefined ? { step: input.step } : {}),
  };
}

/** The code an error object carries: `McpError.code`, or none. */
export function codeOf(e: unknown): string | undefined {
  if (e && typeof e === "object" && typeof (e as { code?: unknown }).code === "string") {
    return (e as { code: string }).code;
  }
  return undefined;
}

/** Error classes flowkit throws, by name, and the code each stands for. */
const ERROR_NAME_CODES: Record<string, string | undefined> = {
  TaskOptionsError: ErrorCode.INVALID_PARAMS,
  CheckFailedError: "CHECK_FAILED",
};

/** The envelope for a thrown or returned Error, as a flow step or a task reports it. */
export function envelopeOfError(e: unknown, step?: string, body?: unknown): ErrorEnvelope {
  const message = e instanceof Error ? e.message : String(e);
  const record = body && typeof body === "object" && !Array.isArray(body) ? (body as Record<string, unknown>) : undefined;
  const details = e && typeof e === "object" ? (e as { details?: Record<string, unknown> }).details : undefined;
  const didYouMean = suggestionsInBody(details);
  const editorCode = typeof record?.errorCode === "string" ? record.errorCode : undefined;
  return errorEnvelope({
    code: codeOf(e) ?? (e instanceof Error ? ERROR_NAME_CODES[e.name] : undefined),
    editorCode,
    message,
    step,
    didYouMean,
    body: record,
  });
}

/**
 * The envelope for a handler body that answered `success: false`, or undefined
 * for any other body. Keyed `errorEnvelope` on the body, since `error` there is
 * already the editor's message string.
 */
export function bodyEnvelope(body: unknown): ErrorEnvelope | undefined {
  if (!body || typeof body !== "object" || Array.isArray(body)) return undefined;
  const record = body as Record<string, unknown>;
  if (record.success !== false) return undefined;
  const message = ["error", "message", "reason"]
    .map((k) => record[k])
    .find((v): v is string => typeof v === "string" && v.trim().length > 0)
    ?? "the editor answered success: false without saying why";
  return errorEnvelope({
    code: "TASK_FAILED",
    editorCode: typeof record.errorCode === "string" ? record.errorCode : undefined,
    message,
    body: record,
  });
}

/** A body with its envelope attached, when it reports a failure and has none yet. */
export function withBodyEnvelope<T>(body: T): T {
  const envelope = bodyEnvelope(body);
  if (!envelope || "errorEnvelope" in (body as Record<string, unknown>)) return body;
  return { ...(body as Record<string, unknown>), errorEnvelope: envelope } as T;
}
