// @session independent: Carries the caller and its consent through async context and says which calls disrupt the editor; the registry decides.
/**
 * Where the consent rule of disruption.ts meets dispatch (#2075).
 *
 * The caller of the call in flight rides async context, set where dispatch
 * starts a call (call-gates.ts), so the calls made in process on its behalf
 * (flow steps, a daemon editor operation's steps) are attributed to it and
 * not to this process. An approved operation runs under a grant in the same
 * context, so its own steps do not ask again.
 *
 * Three places ask, so that no route runs a disruptive operation unasked:
 * the `consent` dispatch gate (the clean refusal for an MCP call), the
 * handlers of the process-level actions (stop_editor, restart_editor,
 * request_editor_shutdown, build), and the guarded bridge for the methods
 * that patch or quit a running editor. Every route ends in one of the last
 * two.
 */
import { AsyncLocalStorage } from "node:async_hooks";
import { McpError, ErrorCode } from "../core/errors.js";
import { SESSION_ID } from "./lock-owner.js";
import { findLiveInstanceRecord } from "../bridge/editor-target.js";
import {
  describeAnswers,
  describeOp,
  type DisruptionCaller,
  type DisruptionRegistry,
  type DisruptionView,
  type DisruptiveOp,
  type SubagentAttestation,
  SUBAGENT_ATTESTATIONS,
} from "./disruption.js";

interface Scope {
  caller: DisruptionCaller;
  /** The request whose approval the work in this scope runs under. */
  grant?: string;
  /** The calls this work runs inside, which a request it makes does not count against itself. */
  calls: number[];
  /** A person started this work (an HTTP route), so no subagent attestation is owed. */
  person?: boolean;
  /** What the caller said about its own subagents, carried to the steps of an operation. */
  subagents?: SubagentAttestation;
}

const store = new AsyncLocalStorage<Scope>();

/** This process as a caller: a stdio server's only session, or the daemon's own work. */
export function processCaller(label = "this server"): DisruptionCaller {
  return { id: SESSION_ID, label };
}

/**
 * Run `fn` as `caller`. A caller that is only this process does not replace
 * a caller already in scope: a call made in process on someone's behalf is
 * theirs.
 */
export function withDisruptionCaller<T>(caller: DisruptionCaller, fn: () => T, extra: { person?: boolean; subagents?: SubagentAttestation } = {}): T {
  const outer = store.getStore();
  if (outer && caller.id === SESSION_ID) {
    return extra.person || extra.subagents ? store.run({ ...outer, ...extra }, fn) : fn();
  }
  return store.run({ ...outer, caller, calls: outer?.calls ?? [], ...extra }, fn);
}

/** The caller of the call in flight, or this process outside any call. */
export function currentDisruptionCaller(fallback?: DisruptionCaller): DisruptionCaller {
  return store.getStore()?.caller ?? fallback ?? processCaller();
}

/** Run `fn` as part of the call `token`, so a request made inside it does not wait on that call. */
export function withDisruptionCall<T>(token: number, fn: () => T): T {
  const outer = store.getStore();
  return store.run({ caller: outer?.caller ?? processCaller(), ...outer, calls: [...(outer?.calls ?? []), token] }, fn);
}

/** Run `fn` under an approved request, so the operation's own steps do not ask again. */
export function withDisruptionGrant<T>(requestId: string, fn: () => T): T {
  const outer = store.getStore();
  return store.run({ caller: outer?.caller ?? processCaller(), calls: [], ...outer, grant: requestId }, fn);
}

/** The consent options a handler reads off its own parameters: the caller's attestation. */
export function consentOptions(params: Record<string, unknown> | undefined): { subagents?: SubagentAttestation } {
  const subagents = attestationOf(params);
  return subagents ? { subagents } : {};
}

/** Read a subagent attestation off call parameters (`subagents`), or undefined. */
export function attestationOf(params: Record<string, unknown> | undefined): SubagentAttestation | undefined {
  const v = params?.subagents;
  return SUBAGENT_ATTESTATIONS.includes(v as SubagentAttestation) ? (v as SubagentAttestation) : undefined;
}

/** The approved request the work in flight runs under, if any. */
export function currentDisruptionGrant(): string | undefined {
  return store.getStore()?.grant;
}

// ── What disrupts ──────────────────────────────────────────────────

/** A console command that closes the editor. */
export function isQuitCommand(command: unknown): boolean {
  return typeof command === "string" && /^\s*(quit|exit|quit_editor)\b/i.test(command);
}

export interface DisruptiveCall {
  op: DisruptiveOp;
  /** Disruptive only while an editor of the project is running (a build). */
  whileRunning?: boolean;
}

/** The operation a `category.action` call would perform, or null when it takes nothing away. */
export function disruptiveCallOf(task: string, params: Record<string, unknown>): DisruptiveCall | null {
  switch (task) {
    case "editor.stop_editor":
    case "editor.request_editor_shutdown":
      return { op: "stop" };
    case "editor.restart_editor":
      return { op: "restart" };
    case "editor.build_project":
    case "project.build":
      return { op: "build", whileRunning: true };
    case "editor.hot_reload":
    case "project.live_coding_compile":
      return { op: "live_coding" };
    case "editor.execute_command":
      return isQuitCommand(params.command) ? { op: "stop" } : null;
    default:
      return null;
  }
}

/**
 * Whether an editor of the project is running: its bridge is connected, or a
 * live editor published its instance record. A build is disruptive only then.
 */
export function projectEditorRunning(projectDir: string | null | undefined, bridge: { isConnected: boolean } | undefined): boolean {
  if (bridge?.isConnected) return true;
  return !!projectDir && findLiveInstanceRecord(projectDir) !== null;
}

/** The operation a bridge method call would perform, or null. */
export function disruptiveBridgeOp(method: string, params: Record<string, unknown> | undefined): DisruptiveOp | null {
  if (method === "live_coding_compile" || method === "hot_reload") return "live_coding";
  if (method === "execute_command" && isQuitCommand(params?.command)) return "stop";
  return null;
}

// ── Asking ─────────────────────────────────────────────────────────

/** What the requesting session is told while its request is not approved. */
export function consentRefusalMessage(view: DisruptionView): string {
  const what = `request ${view.id} to ${describeOp(view.op)}`;
  if (view.state === "confirm") {
    const others = describeAnswers(view);
    return [
      `Not run yet: ${what} needs you to account for your own subagents first.`,
      "ue-mcp cannot see subagents that share this session's connection, and stopping the editor under them loses their work too. "
        + "Check every subagent you started. Tell any that use the editor to stop and wait until their calls have returned.",
      `Then run the same action again with subagents="paused" (or subagents="none" if you started none), `
        + `or call editor(action="disruption_confirm", requestId="${view.id}", subagents="paused" | "none").`,
      ...(view.self.inflight.length ? [`Calls still in flight on this connection hold the request whatever you answer: ${view.self.inflight.join(", ")}.`] : []),
      ...(others.length ? ["The other sessions have to agree as well. Their answers so far:", ...others] : []),
    ].join("\n");
  }
  const head = view.state === "blocked"
    ? `Not run: another session blocked ${what}.`
    : view.state === "denied"
      ? `Not run: a person denied ${what}.`
      : view.answers.some((a) => a.answer !== "go")
        ? `Not run yet: ${what} is waiting on the other sessions using this editor.`
        : `Not run yet: ${what} is waiting on calls still in flight on this session's own connection.`;
  const answers = describeAnswers(view);
  return [
    head,
    ...(view.self.inflight.length ? [`In flight on this connection (your subagents, typically): ${view.self.inflight.join(", ")}. They hold the request until they return.`] : []),
    ...(answers.length ? ["Their answers:", ...answers] : []),
    "Tell your user this, with each session's answer. This session cannot override it.",
    view.state === "denied"
      ? "Ask again later by running the same action, which opens a new request."
      : `Run the same action again to check (it re-reads the same request), or read editor(action="disruption_status", requestId="${view.id}"). `
        + `A session that waits answers go once it calls editor(action="release"); an idle one answers go by itself. `
        + `Your user can approve it now with \`ue-mcp editor approve ${view.id}\` or from Studio. It expires at ${view.expiresAt}.`,
  ].join("\n");
}

/** The machine-readable companion of a refusal. */
export function consentDetails(view: DisruptionView): Record<string, unknown> {
  return {
    outcome: "failed",
    requestId: view.id,
    op: view.op,
    state: view.state,
    self: view.self,
    answers: view.answers.map((a) => ({ session: a.session, label: a.label, answer: a.answer, reason: a.reason, ...(a.parent ? { parent: a.parent } : {}) })),
  };
}

export type ConsentDecision =
  | { proceed: true; request?: DisruptionView }
  | { proceed: false; request: DisruptionView; message: string };

export interface ConsentOptions {
  caller?: DisruptionCaller;
  reason?: string;
  /** The requester's own subagents: none, or paused. Read from scope when absent. */
  subagents?: SubagentAttestation;
  /** A person asked. Read from scope when absent. */
  person?: boolean;
}

/** Whether `op` may run now for the caller in scope. An approved request is returned for completion. */
export function askConsent(
  registry: DisruptionRegistry | undefined,
  op: DisruptiveOp,
  action: string,
  opts: ConsentOptions = {},
): ConsentDecision {
  if (!registry?.enforcing || currentDisruptionGrant()) return { proceed: true };
  const scope = store.getStore();
  const subagents = opts.subagents ?? scope?.subagents;
  const person = opts.person ?? scope?.person;
  const view = registry.request(opts.caller ?? currentDisruptionCaller(), op, action, {
    ...(opts.reason ? { reason: opts.reason } : {}),
    ...(subagents ? { subagents } : {}),
    ...(person ? { person } : {}),
    ownCalls: scope?.calls ?? [],
  });
  if (view.state === "approved") return { proceed: true, request: view };
  return { proceed: false, request: view, message: consentRefusalMessage(view) };
}

/** The refusal as an error, the shape a handler or the guarded bridge throws. */
export function consentError(decision: Extract<ConsentDecision, { proceed: false }>): McpError {
  return new McpError(ErrorCode.EDITOR_IN_USE, decision.message, consentDetails(decision.request));
}

/**
 * Run `fn` once `op` is agreed, under the grant, and spend the request after.
 * Throws EDITOR_IN_USE otherwise. With no registry or the gate off it just runs.
 */
export async function withEditorConsent<T>(
  registry: DisruptionRegistry | undefined,
  op: DisruptiveOp,
  action: string,
  fn: () => Promise<T>,
  opts: ConsentOptions = {},
): Promise<T> {
  const decision = askConsent(registry, op, action, opts);
  if (!decision.proceed) throw consentError(decision);
  const request = decision.request;
  if (!request) return fn();
  try {
    return await withDisruptionGrant(request.id, fn);
  } finally {
    registry!.complete(request.id);
  }
}
