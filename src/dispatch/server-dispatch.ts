/**
 * The per-call pipeline behind every advertised tool: route the call to an
 * editor, gate it, check the dialog guard, run the addressed session's task
 * under asset locks, and shape the result or refusal the client sees.
 */
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { McpError as SdkMcpError } from "@modelcontextprotocol/sdk/types.js";
import type { EditorSession, SessionRegistry } from "../sessions/session.js";
import type { ElicitFn, ProgressFn, ProgressUpdate, ToolContext, ToolDef } from "../core/types.js";
import { McpError, ErrorCode } from "../core/errors.js";
import { debug } from "../core/log.js";
import { consumeUpgradeNotice } from "../version-check.js";
import { unwrapArgsEnvelope, validateCategoryParams } from "../surface/context/call-envelope.js";
import {
  routeEditorCall,
  callSubject,
  refuseUntargetedInRegistry,
  editorAttribution,
  type RoutedCall,
} from "./editor-gate.js";
import { isDirectiveResponse } from "../core/directive.js";
import { EDITOR_TARGET_PARAM, stripAction } from "../surface/routing-params.js";
import { stripEditorTarget, sessionContext } from "../surface/target-params.js";
import {
  DialogGuard,
  type GuardDecision,
  isDialogRefusal,
  stampBlockedEditor,
} from "../editor/dialog-guard.js";
import { connectedEditorOf } from "../editor/editor-control.js";
import { clientAdvertisesElicitation } from "../editor/dialog-mode.js";
import { contestedProject } from "../editor/project-holders.js";
import { withAssetLocks, type LockingConfig } from "./locking.js";
import { unknownActionMessage } from "../surface/action-schema.js";
import { explainMissingAction } from "../sessions/session-surface.js";
import type { FlowContext } from "../flow/context.js";
import type { SessionLoad, SessionLoads } from "../sessions/session-load.js";

type TextBlock = { type: "text"; text: string };

/** What a tool callback hands back to the MCP SDK. */
export interface ToolResult {
  content: TextBlock[];
  isError?: true;
}

/** The request extras a tool callback receives, as far as dispatch reads them. */
export interface CallExtra {
  sendNotification?: (n: never) => Promise<void>;
  _meta?: { progressToken?: string | number };
}

function withUpgradeNotice(content: TextBlock[]): TextBlock[] {
  const notice = consumeUpgradeNotice();
  return notice ? [{ type: "text" as const, text: notice }, ...content] : content;
}

/**
 * Structured tail for an error a caller has to make a decision about (#799).
 * A bridge timeout is not a failed call: the editor may have finished it. The
 * prose says so, and this block says so in a form a client can branch on
 * without matching strings.
 */
function machineErrorBlock(e: unknown): TextBlock[] {
  if (!(e instanceof McpError) || !e.details) return [];
  return [{
    type: "text" as const,
    text: "MACHINE_ERROR=" + JSON.stringify({ code: e.code, ...e.details }),
  }];
}

/** The one error shape every tool answers with: `Error [CODE]: message`, then any tail. */
export function errorResult(code: string, message: string, tail: TextBlock[] = []): ToolResult {
  return {
    content: withUpgradeNotice([{ type: "text" as const, text: `Error [${code}]: ${message}` }, ...tail]),
    isError: true,
  };
}

/** A refusal payload, returned as the error result it is. */
function refusalResult(refusal: unknown, tail: TextBlock[] = []): ToolResult {
  return {
    content: withUpgradeNotice([{ type: "text" as const, text: JSON.stringify(refusal, null, 2) }, ...tail]),
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
    const result = await mcp.server.elicitInput(params, { timeout: ELICIT_TIMEOUT_MS });
    return result as Awaited<ReturnType<ElicitFn>>;
  };
  // The function exists whatever the client supports, so its presence proves
  // nothing. Callers deciding whether the user CAN be asked read this.
  elicit.clientAdvertisesElicitation = () => !!mcp.server.getClientCapabilities()?.elicitation;
  elicit.client = () => mcp.server.getClientVersion();
  return elicit;
}

/** What the pipeline needs from the server that owns it. */
export interface DispatchDeps {
  sessions: SessionRegistry;
  loads: SessionLoads;
  lockingCfg: LockingConfig;
  /** The one dialog guard per editor, created on first use. */
  dialogGuardFor: (session: EditorSession, canElicit?: boolean) => DialogGuard;
  elicit: () => ElicitFn | undefined;
  client: () => { name: string; version?: string } | undefined;
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

/**
 * One call to a category tool (or the micro gateway). `envelope` says the
 * tool is advertised as `action` + `args`, so the flat shape is validated here
 * rather than by the SDK.
 */
export async function dispatchCategoryCall(
  deps: DispatchDeps,
  tool: ToolDef,
  envelope: boolean,
  callArgs: Record<string, unknown>,
  extra: CallExtra,
): Promise<ToolResult> {
  const { sessions, loads } = deps;
  let rawParams: Record<string, unknown>;
  try {
    rawParams = envelope ? validateCategoryParams(tool, unwrapArgsEnvelope(tool, callArgs)) : callArgs;
  } catch (e) {
    // Thrown to the SDK, which words it exactly as its own validation did.
    if (e instanceof SdkMcpError) throw e;
    const msg = e instanceof Error ? e.message : String(e);
    const code = e instanceof McpError ? e.code : ErrorCode.INVALID_PARAMS;
    return errorResult(code, msg);
  }
  let routed: RoutedCall;
  try {
    routed = routeEditorCall(tool, rawParams, sessions);
  } catch (e) {
    return errorResult("NOT_FOUND", e instanceof Error ? e.message : String(e));
  }
  const session = routed.session;
  const params = routed.params;
  const action = params.action as string;
  const taskName = `${tool.name}.${action}`;
  // The action actually run: the gateway's target in micro mode.
  const subject = callSubject(tool, params);
  const effectiveTask = subject.taskName;

  // Refuse an untargeted change while more than one editor is registered
  // (#817, plan 5.2). Returns null at one editor without classifying anything.
  const untargeted = refuseUntargetedInRegistry(sessions, effectiveTask, routed.targeted, loads.dispatchUnion.tools);
  if (untargeted) return errorResult("INVALID_PARAMS", untargeted);

  // One guard per editor, shared by every route. The mode is read per call,
  // so changing it takes effect without restarting anything.
  const guard = deps.dialogGuardFor(session, clientAdvertisesElicitation(deps.elicit()));

  // Actions served in this process never reach the bridge, so the bridge
  // boundary cannot refuse them. Same guard, same decision.
  const preflight = await guard.check(effectiveTask, "action");
  if (!preflight.allow) return refusalResult(preflight.refusal);

  const taskParams = stripAction(params);
  const flowCtx: FlowContext = {
    ...loads.contextFor(session),
    elicit: deps.elicit(),
    onProgress: makeProgressReporter(extra),
    client: deps.client(),
  };

  // The addressed session's own registry, built on demand rather than falling
  // back to the first project's (D1). A call for an action this session does
  // not provide is refused here, naming the editors that do.
  let sessionLoad: SessionLoad;
  try {
    sessionLoad = await loads.ensure(session);
  } catch (e) {
    return errorResult(
      "NOT_CONNECTED",
      `Editor '${session.name}' has no tool surface of its own, so this call cannot ` +
        `be dispatched to it. Running it through another project's registry would execute that project's ` +
        `tasks in this editor, which is exactly what must not happen. Cause: ` +
        `${e instanceof Error ? e.message : String(e)}`,
    );
  }
  const sessionRegistry = sessionLoad.registry ?? loads.loadFor(undefined).registry!;
  const refusal = sessions.size > 1
    ? explainMissingAction(
        loads.dispatchUnion,
        effectiveTask,
        session.name,
        sessionRegistry.listRegistered().includes(effectiveTask),
      )
    : null;
  if (refusal) return errorResult("NOT_FOUND", refusal);

  // `action` is advertised as an enum but parsed as a string, so a typo is
  // refused here with the closest spellings rather than by the MCP layer
  // with every action named twice.
  if (!sessionRegistry.listRegistered().includes(taskName)) {
    return errorResult("NOT_FOUND", unknownActionMessage(action, tool.name, Object.keys(tool.actions)));
  }

  const served = attribution(sessions, session);
  try {
    const task = await sessionRegistry.create(taskName, flowCtx, taskParams);
    // Locks are taken in the editor the call runs in, through the GUARDED
    // bridge, so a lock request made while a modal is up is refused as a dialog.
    const result = await withAssetLocks(
      session.guarded,
      deps.lockingCfg,
      effectiveTask,
      subject.params,
      () => task.run(),
      session.lockOwnerId,
      loads.dispatchUnion.tools,
    );

    // A task that failed for its own reasons reports that, not a dialog. A
    // failure DURING a modal is usually caused by it, and the runner returns
    // no data on a throw, so ask the guard. The decision is kept so the
    // refusal below reports what the guard actually did.
    let postRunDecision: GuardDecision | null = null;
    const failedUnderDialog = !result.success
      && !isDialogRefusal(result.data)
      && !DialogGuard.actionAllowed(effectiveTask)
      && (postRunDecision = await guard.check(effectiveTask, "action")).allow === false;
    if (!result.success && !isDialogRefusal(result.data) && !failedUnderDialog) {
      const msg = result.error?.message ?? `Task ${taskName} failed`;
      return errorResult("TASK_FAILED", msg, [...machineErrorBlock(result.error), ...served]);
    }

    // An allow-listed read still SAYS a dialog is up: get_status must never
    // report a healthy editor while the game thread is parked.
    if (DialogGuard.actionAllowed(effectiveTask)) {
      // respond_to_dialog may have just cleared it. refresh, not check: check
      // applies the mode and could press a button on the next prompt unasked.
      if (effectiveTask === "editor.respond_to_dialog" && result.success) {
        await guard.refresh();
      }
      const seen = guard.current;
      if (result.success) stampBlockedEditor(result.data, seen, guard.mode);
    }

    // Whatever the route, the caller gets ONE refusal shape, built here.
    // Actions allowed through a modal are exempt.
    if (!DialogGuard.actionAllowed(effectiveTask)) {
      const fromPlugin = isDialogRefusal(result.data)
        ? (result.data as Record<string, unknown>)
        : null;
      if (fromPlugin) guard.observe("__refused__", fromPlugin);
      const blocking = guard.current;
      if (blocking) {
        const decision = postRunDecision
          ?? (await guard.check(effectiveTask, "action"));
        const refusalForReturn = decision.allow === false
          ? decision.refusal
          : guard.refusal(effectiveTask, blocking);
        // A refusal means nothing ran. Anything else had already started when
        // the dialog appeared, so it may have applied part of its work.
        const started = fromPlugin === null;
        return refusalResult(
          started
            ? {
                ...refusalForReturn,
                partiallyApplied: true,
                note:
                  `'${taskName}' had already started when the dialog appeared, so it may `
                  + "have applied some of its changes. Answer the dialog, then read the "
                  + "state back before deciding whether to run it again.",
                // A mutation that completed still has the path it created in here.
                partialResult: result.data ?? null,
              }
            : refusalForReturn,
          served,
        );
      }
    }

    const stringify = (v: unknown) =>
      typeof v === "string" ? v : JSON.stringify(v, null, 2);

    // Directive responses (execute_python workaround tracking) emit the prose
    // directive, a MACHINE_DIRECTIVE block for clients that strip prose, and
    // the actual tool result.
    if (isDirectiveResponse(result.data)) {
      const blocks: TextBlock[] = [
        { type: "text" as const, text: result.data.directive },
      ];
      if (result.data.machine) {
        blocks.push({
          type: "text" as const,
          text: "MACHINE_DIRECTIVE=" + JSON.stringify(result.data.machine),
        });
      }
      blocks.push({ type: "text" as const, text: stringify(result.data.result) });
      return { content: withUpgradeNotice([...blocks, ...served]) };
    }

    return {
      content: withUpgradeNotice([
        { type: "text" as const, text: stringify(result.data) },
        ...served,
      ]),
    };
  } catch (e) {
    // A refusal can arrive as a throw (taking an asset lock is a bridge call),
    // and is shaped through the guard so the payload matches the result route.
    const thrownRefusal = e instanceof McpError && isDialogRefusal(e.details)
      ? (e.details as unknown as Record<string, unknown>)
      : null;
    if (thrownRefusal) {
      guard.observe("__refused__", thrownRefusal);
      const blocking = guard.current;
      if (blocking) return refusalResult(guard.refusal(effectiveTask, blocking), served);
    }
    const msg = e instanceof Error ? e.message : String(e);
    const code = e instanceof McpError ? e.code : "UNKNOWN";
    return errorResult(code, msg, [...machineErrorBlock(e), ...served]);
  }
}

/**
 * One call to the flow tool. A flow addresses one editor for the whole run,
 * so the target is read off the call and stripped from both the call and the
 * nested `params` forwarded into every step.
 */
export async function dispatchFlowCall(
  deps: DispatchDeps,
  flowTool: ToolDef,
  baseCtx: ToolContext,
  rawParams: Record<string, unknown>,
): Promise<ToolResult> {
  const { sessions } = deps;
  try {
    const nested = rawParams.params && typeof rawParams.params === "object"
      ? (rawParams.params as Record<string, unknown>)
      : undefined;
    const target = flowTool.injectedEditorParam
      ? rawParams[EDITOR_TARGET_PARAM] ?? nested?.[EDITOR_TARGET_PARAM]
      : undefined;
    const session = flowTool.injectedEditorParam ? sessions.resolve(target) : sessions.active;
    const params = stripEditorTarget(rawParams);
    if (nested) params.params = stripEditorTarget(nested);

    // A flow is whatever its steps are, so an untargeted run is gated like
    // any other change. `plan` and `list` read and are not.
    const untargeted = refuseUntargetedInRegistry(
      sessions,
      `${flowTool.name}.${String(params.action ?? "")}`,
      typeof target === "string" && target.trim() !== "",
      deps.loads.dispatchUnion.tools,
    );
    if (untargeted) return errorResult("INVALID_PARAMS", untargeted);

    // Steps are refused individually at the guarded bridge. This covers a run
    // STARTED while a modal is up, and flow(list)/flow(plan), which never
    // touch the bridge at all.
    const flowGuard = deps.dialogGuardFor(session, clientAdvertisesElicitation(deps.elicit()));
    const flowCheck = await flowGuard.check(
      `${flowTool.name}.${String(params.action ?? "")}`,
      "action",
    );
    if (!flowCheck.allow) return refusalResult(flowCheck.refusal, attribution(sessions, session));
    const result = await flowTool.handler(sessionContext(baseCtx, session), params);
    const text = typeof result === "string" ? result : JSON.stringify(result, null, 2);
    return { content: withUpgradeNotice([{ type: "text" as const, text }, ...attribution(sessions, session)]) };
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    const code = e instanceof McpError ? e.code : "UNKNOWN";
    return errorResult(code, msg, machineErrorBlock(e));
  }
}
