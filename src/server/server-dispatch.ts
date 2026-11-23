// @session per-session: Turns each route's call into the pipeline's one call value, and its outcome into what that route answers.
/**
 * The boundaries of the call pipeline. A category tool call (or a micro
 * gateway call) and a flow call are each read off their wire shape into one
 * `ActionCall`, run through the gates in call-gates.ts, and the outcome is
 * turned into MCP content here, and only here.
 */
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { McpError as SdkMcpError } from "@modelcontextprotocol/sdk/types.js";
import type { EditorSession, SessionRegistry } from "../sessions/session.js";
import type { ElicitFn } from "../core/elicit.js";
import type { ProgressFn, ProgressUpdate } from "../core/progress.js";
import type { ToolDef } from "../surface/tool-types.js";
import { McpError, ErrorCode } from "../core/errors.js";
import { debug } from "../core/log.js";
import type { SessionAccessMode } from "../flow/session-access.js";
import { unwrapArgsEnvelope, validateCategoryParams } from "../surface/context/call-envelope.js";
import { microFlowCall } from "../surface/context/micro-context.js";
import { callSubject, editorAttribution, resolveEditorTarget, takeEditorTarget } from "../dispatch/editor-gate.js";
import { isDirectiveResponse } from "../core/directive.js";
import { EDITOR_TARGET_PARAM, stripAction } from "../surface/routing-params.js";
import { stripEditorTarget } from "../surface/target-params.js";
import { connectedEditorOf } from "../editor/editor-control.js";
import { elicitationUnrenderedNote } from "../dispatch/client-quirks.js";
import { contestedProject } from "../editor/project-holders.js";
import { SESSION_ID } from "../dispatch/lock-owner.js";
import { failed, type ActionCall, type Asker, type CallOutcome, type CallTarget, type MediaBlock } from "../dispatch/call.js";
import { machineDetails, runCall, type GateDeps } from "./call-gates.js";

export { splitMedia, type MediaBlock } from "../dispatch/call.js";

type TextBlock = { type: "text"; text: string };
export type ContentBlock = TextBlock | MediaBlock;

/** What a tool callback hands back to the MCP SDK. */
export interface ToolResult {
  content: ContentBlock[];
  isError?: true;
}

/** The request extras a tool callback receives, as far as dispatch reads them. */
export interface CallExtra {
  sendNotification?: (n: never) => Promise<void>;
  _meta?: { progressToken?: string | number };
  /** Aborted by the SDK when the client sends notifications/cancelled for this request. */
  signal?: AbortSignal;
}

/** The one error shape every tool answers with: `Error [CODE]: message`, then any tail. */
export function errorResult(code: string, message: string, tail: TextBlock[] = []): ToolResult {
  return {
    content: [{ type: "text" as const, text: `Error [${code}]: ${message}` }, ...tail],
    isError: true,
  };
}

/**
 * Turn an MCP request's progress token into a reporter the tools can call.
 *
 * `notifications/progress` is the one channel clients render live: stderr
 * from an MCP server goes to a log file the user never opens. It only exists
 * when the caller supplied a token.
 */
export function makeProgressReporter(extra: CallExtra): ProgressFn | undefined {
  const token = extra?._meta?.progressToken;
  // The SDK types sendNotification against its own ServerNotification union,
  // and the progress params carry a token the compiler cannot narrow here.
  const send = extra?.sendNotification as unknown as
    | ((n: { method: string; params: Record<string, unknown> }) => Promise<void>)
    | undefined;

  // A missing token is silent by design and indistinguishable from a client
  // that discards what we send, so record which it was.
  debug("progress", token === undefined ? "no progressToken on this request - client did not ask for progress" : `progressToken present (${String(token)})`);

  if (token === undefined || typeof send !== "function") return undefined;

  return (update: ProgressUpdate): void => {
    // Fire and forget: a progress update must never fail the call it describes.
    void Promise.resolve(
      send({
        method: "notifications/progress",
        params: {
          progressToken: token,
          progress: update.progress,
          ...(update.total !== undefined ? { total: update.total } : {}),
          message: update.message,
        },
      }),
    ).catch(() => undefined);
  };
}

/**
 * A person answers a form when they get to it, not within the SDK's 60 second
 * request default. An answer given after that was discarded (#1076).
 */
const ELICIT_TIMEOUT_MS = 24 * 60 * 60 * 1000;

/**
 * The elicitation gate for a server. Built before any client has connected,
 * so the capability and the client are read live at call time.
 */
export function buildElicit(mcp: McpServer): ElicitFn {
  const elicit: ElicitFn = async (params) => {
    const caps = mcp.server.getClientCapabilities();
    if (!caps?.elicitation) {
      // A JSON-RPC-style error so callers can tell "user declined" from
      // "client has no UI for this".
      throw new McpError(
        ErrorCode.UNKNOWN_ACTION,
        "Connected MCP client did not advertise the `elicitation` capability - cannot obtain a deterministic user approval. Upgrade your client (Claude Code >= 2.1.76) or run the action from a client that supports MCP elicitation.",
      );
    }
    // Advertised but never drawn: the request would wait on a form nobody sees.
    const unrendered = elicitationUnrenderedNote(mcp.server.getClientVersion());
    if (unrendered) throw new McpError(ErrorCode.UNKNOWN_ACTION, unrendered);
    const result = await mcp.server.elicitInput(params, { timeout: ELICIT_TIMEOUT_MS });
    return result as Awaited<ReturnType<ElicitFn>>;
  };
  // The function exists whatever the client supports, so its presence proves
  // nothing. Callers deciding whether the user CAN be asked read this, and a
  // client known to drop the forms it advertises cannot be asked (#1459).
  elicit.clientAdvertisesElicitation = () =>
    !!mcp.server.getClientCapabilities()?.elicitation &&
    elicitationUnrenderedNote(mcp.server.getClientVersion()) === null;
  elicit.client = () => mcp.server.getClientVersion();
  return elicit;
}

/** What one route hands the pipeline beside the gates' own dependencies: who is calling. */
export interface DispatchDeps extends GateDeps {
  /** Where this route's calls come from; see Asker.origin. */
  origin: Asker["origin"];
  elicit: () => ElicitFn | undefined;
  client: () => { name: string; version?: string } | undefined;
  /**
   * The calling session's access mode, read on every call. Absent is
   * read-write. In read-only mode an action whose declared effect is `mutate`
   * or `unknown` is refused, and every bridge call it would make is refused
   * by the read-only guard (#1484).
   */
  access?: () => SessionAccessMode;
  /**
   * The MCP session calls are attributed to for asset leases. The daemon
   * gives each connection its own; absent is this process.
   */
  leaseHolder?: () => { id: string; label: string; parent?: string };
}

/** The caller of one call, read off the route's deps and the request. */
function askerOf(deps: DispatchDeps, extra: CallExtra = {}): Asker {
  const client = deps.client();
  return {
    origin: deps.origin,
    ...(deps.origin === "http" ? { canElicit: false } : {}),
    elicit: deps.elicit(),
    client,
    ...(deps.access ? { access: deps.access } : {}),
    leaseHolder: deps.leaseHolder?.() ?? { id: SESSION_ID, label: client?.name ?? "this server" },
    onProgress: makeProgressReporter(extra),
    signal: extra?.signal,
  };
}

/** The serving editor, appended to a response only beyond one editor (5.3). */
export function attribution(sessions: SessionRegistry, session: EditorSession): TextBlock[] {
  const line = editorAttribution(
    {
      name: session.name,
      projectPath: session.project.projectPath,
      pid: connectedEditorOf(session.bridge)?.pid,
    },
    sessions.size,
    contestedProject(session.project.projectPath) !== null,
  );
  return line ? [{ type: "text" as const, text: line }] : [];
}

/** A result as one text block: a string as itself, anything else as indented JSON. */
function stringify(v: unknown): string {
  return typeof v === "string" ? v : JSON.stringify(v, null, 2);
}

/** The MACHINE_ERROR block a client branches on without matching prose (#799). */
function machineErrorBlocks(details: Record<string, unknown> | undefined): TextBlock[] {
  return details ? [{ type: "text" as const, text: "MACHINE_ERROR=" + JSON.stringify(details) }] : [];
}

/** An outcome as MCP content: the one place a call's answer becomes text. */
export function toToolResult(outcome: CallOutcome, sessions: SessionRegistry, session: EditorSession | undefined): ToolResult {
  const served = outcome.attributed && session ? attribution(sessions, session) : [];
  if (outcome.code === ErrorCode.REFUSED) {
    return { content: [{ type: "text" as const, text: JSON.stringify(outcome.data, null, 2) }, ...served], isError: true };
  }
  if (outcome.code !== undefined) {
    return errorResult(outcome.code, outcome.message ?? "", [...machineErrorBlocks(outcome.details), ...served]);
  }
  const notes: TextBlock[] = outcome.notes.map((text) => ({ type: "text" as const, text }));
  // Directive responses (execute_python workaround tracking) emit the prose
  // directive, a MACHINE_DIRECTIVE block for clients that strip prose, and
  // the actual tool result.
  if (isDirectiveResponse(outcome.data)) {
    const blocks: TextBlock[] = [...notes, { type: "text" as const, text: outcome.data.directive }];
    if (outcome.data.machine) {
      blocks.push({ type: "text" as const, text: "MACHINE_DIRECTIVE=" + JSON.stringify(outcome.data.machine) });
    }
    blocks.push({ type: "text" as const, text: stringify(outcome.data.result) });
    return { content: [...blocks, ...served] };
  }
  return { content: [...notes, { type: "text" as const, text: stringify(outcome.data) }, ...outcome.media, ...served] };
}

/** An outcome, and the editor that served it, when the call got as far as one. */
export interface Served {
  outcome: CallOutcome;
  session?: EditorSession;
}

/**
 * Read one call to a category tool (or the micro gateway) into the pipeline's
 * call value and run it. `envelope` says the tool is advertised as `action` +
 * `args`, so the flat shape is validated here rather than by the SDK; a
 * validation failure is thrown to the SDK, which words it as its own.
 */
export async function categoryCallOutcome(
  deps: DispatchDeps,
  tool: ToolDef,
  envelope: boolean,
  callArgs: Record<string, unknown>,
  extra: CallExtra = {},
): Promise<Served> {
  let taken: { target: unknown; params: Record<string, unknown> };
  let flowInput: Record<string, unknown> | null;
  try {
    taken = takeEditorTarget(tool, envelope ? unwrapArgsEnvelope(tool, callArgs) : callArgs);
    if (envelope) taken = { ...taken, params: validateCategoryParams(tool, taken.params) };
    flowInput = microFlowCall(tool, taken.params);
  } catch (e) {
    if (e instanceof SdkMcpError) throw e;
    const code = e instanceof McpError ? e.code : ErrorCode.INVALID_PARAMS;
    return { outcome: failed(code, e instanceof Error ? e.message : String(e), false) };
  }
  // The micro gateway reaches flows as a category; the flow route runs them.
  if (flowInput) return flowCallOutcome(deps, { ...flowInput, ...(taken.target === undefined ? {} : { [EDITOR_TARGET_PARAM]: taken.target }) }, extra);

  let routed: { session: EditorSession; targeted: boolean };
  try {
    routed = resolveEditorTarget(deps.sessions, taken.target);
  } catch (e) {
    return { outcome: failed(ErrorCode.NOT_FOUND, e instanceof Error ? e.message : String(e), false) };
  }
  const params = taken.params;
  // The action that actually runs: the gateway's target in micro mode.
  const subject = callSubject(tool, params);
  const call: ActionCall<EditorSession> = {
    task: subject.taskName,
    params: subject.params,
    routing: { targeted: routed.targeted },
    session: routed.session,
    asker: askerOf(deps, extra),
    target: { kind: "task", registryTask: `${tool.name}.${String(params.action)}`, registryParams: stripAction(params) },
  };
  return { outcome: await runCall(deps, call), session: routed.session };
}

/**
 * Read one call to the flow tool into the pipeline's call value and run it. A
 * flow addresses one editor for the whole run, so the target is read off the
 * call and stripped from both the call and the nested `params` forwarded into
 * every step.
 */
export async function flowCallOutcome(
  deps: DispatchDeps,
  rawParams: Record<string, unknown>,
  extra: CallExtra = {},
  approval?: Extract<CallTarget, { kind: "flow" }>["approval"],
): Promise<Served> {
  const flowTool = deps.flow.tool;
  const nested = rawParams.params && typeof rawParams.params === "object"
    ? (rawParams.params as Record<string, unknown>)
    : undefined;
  // A name is resolved strictly at one editor too, so a stale one refuses
  // rather than running the flow against the active editor (#1278).
  const routable = flowTool.injectedEditorParam || !(EDITOR_TARGET_PARAM in flowTool.schema);
  const target = routable ? rawParams[EDITOR_TARGET_PARAM] ?? nested?.[EDITOR_TARGET_PARAM] : undefined;
  let session: EditorSession;
  try {
    session = routable ? deps.sessions.resolve(target) : deps.sessions.active;
  } catch (e) {
    const code = e instanceof McpError ? e.code : ErrorCode.UNKNOWN;
    return { outcome: failed(code, e instanceof Error ? e.message : String(e), false, machineDetails(e)) };
  }
  const input = stripEditorTarget(rawParams);
  if (nested) input.params = stripEditorTarget(nested);
  const call: ActionCall<EditorSession> = {
    task: `${flowTool.name}.${String(input.action ?? "")}`,
    params: stripAction(input),
    routing: { targeted: typeof target === "string" && target.trim() !== "" },
    session,
    asker: askerOf(deps, extra),
    target: { kind: "flow", input, ...(approval ? { approval } : {}) },
  };
  return { outcome: await runCall(deps, call), session };
}

/** One call to a category tool (or the micro gateway), answered as MCP content. */
export async function dispatchCategoryCall(
  deps: DispatchDeps,
  tool: ToolDef,
  envelope: boolean,
  callArgs: Record<string, unknown>,
  extra: CallExtra,
): Promise<ToolResult> {
  const { outcome, session } = await categoryCallOutcome(deps, tool, envelope, callArgs, extra);
  return toToolResult(outcome, deps.sessions, session);
}

/** One call to the flow tool, answered as MCP content. */
export async function dispatchFlowCall(deps: DispatchDeps, rawParams: Record<string, unknown>, extra: CallExtra = {}): Promise<ToolResult> {
  const { outcome, session } = await flowCallOutcome(deps, rawParams, extra);
  return toToolResult(outcome, deps.sessions, session);
}
