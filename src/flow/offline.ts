// @session independent: Classifies whichever tool graph it is handed as offline or editor-bound, and builds the editor-down message from a context handed to it; holds nothing per editor.
/**
 * What this server can still do with no editor running, and what it says when
 * something needs one (T16).
 *
 * The claim that ue-mcp "works offline" was true and unmeasurable at the same
 * time. Every category tool is advertised in full whether or not an editor is
 * attached, which `tests/golden/editor-down.json` records: the startup contract
 * does not shrink when the editor is down. What shrinks is the set of calls
 * that can succeed, and nothing reported which set that was. An agent starting
 * a session against a stopped editor could see 949 actions, pick one, and learn
 * the difference one refusal at a time.
 *
 * There are exactly two ways an action reaches its work:
 *
 *   `spec.bridge`    a method name dispatched over the WebSocket to the editor.
 *                    It cannot run without one, so the answer is mechanical and
 *                    covers the overwhelming majority of the surface.
 *   `spec.handler`   a closure that runs inside this Node process. Whether it
 *                    then talks to the editor is a property of the code, so
 *                    each one declares it: `offline` or `editorBound`, with
 *                    the reason.
 *
 * The declaration is deliberate. Deriving it by scanning `handler.toString()`
 * for a bridge reference looks cheaper and is wrong: `asset(migrate)` calls the
 * bridge from a module-level helper the closure only names, so a source scan
 * reads it as editor-free and an agent is told a migrate will work with the
 * editor down. `tests/unit/offline.test.ts` fails on a handler action that
 * declares neither.
 */
import type { ActionSpec, ToolDef } from "../surface/tool-types.js";
import { McpError, ErrorCode } from "../core/errors.js";
import { STATUS_STALE_AFTER_MS } from "../editor/dialog-guard.js";
import { readLogState, readEngineSnapshot } from "../editor/engine-observer.js";

/**
 * Whether an action can run with no editor attached.
 *
 * `always` is not a promise that the call succeeds. It is a promise that a
 * missing editor is not what stops it: `project(read_config)` still needs the
 * INI to exist, and `editor(stop_editor)` still needs a process to ask.
 */
export type ActionAvailability = "always" | "editor" | "unknown";

export interface ActionVerdict {
  tool: string;
  action: string;
  availability: ActionAvailability;
  /** Why, in one sentence a caller can act on. */
  reason: string;
  /** The bridge method an editor-bound action dispatches to. */
  bridgeMethod?: string;
}

/**
 * Whether one action can run with no editor: a bridge action never can, and a
 * handler-backed action says so in its `offline` or `editorBound` declaration.
 * One that declares neither reports `unknown` and says so, because guessing
 * "offline" for an unclassified action is the failure this module exists to
 * remove.
 */
export function classifyAvailability(tool: string, action: string, spec: ActionSpec): ActionVerdict {
  if (spec.bridge) {
    return {
      tool,
      action,
      availability: "editor",
      reason: `Dispatches to the '${spec.bridge}' bridge method, which only a running editor answers.`,
      bridgeMethod: spec.bridge,
    };
  }

  if (spec.offline) return { tool, action, availability: "always", reason: spec.offline };
  if (spec.editorBound) return { tool, action, availability: "editor", reason: spec.editorBound };

  return {
    tool,
    action,
    availability: "unknown",
    reason:
      "Runs in the server process, and nothing declares whether it then calls the editor. "
      + "Injected actions from a plugin land here. Treat it as needing an editor until it is classified.",
  };
}

/** Every action in a tool graph, classified. Declaration order is preserved. */
export function classifyGraph(graph: readonly ToolDef[]): ActionVerdict[] {
  const out: ActionVerdict[] = [];
  for (const tool of graph) {
    for (const [action, spec] of Object.entries(tool.actions)) {
      out.push(classifyAvailability(tool.name, action, spec));
    }
  }
  return out;
}

/** The `tool.action` names that run with no editor, for a graph. */
export function offlineActionNames(graph: readonly ToolDef[]): string[] {
  return classifyGraph(graph)
    .filter((v) => v.availability === "always")
    .map((v) => `${v.tool}.${v.action}`);
}

export interface AvailabilityReportOptions {
  /** True when an editor is attached right now. */
  editorConnected: boolean;
  /** Narrow the whole report to one category. */
  category?: string;
  /** Which side of the line to list. */
  state?: "available" | "blocked" | "all";
  /** Include the per-action list, not just the counts. */
  names?: boolean;
}

export interface AvailabilityReport {
  editorConnected: boolean;
  total: number;
  availableNow: number;
  blocked: number;
  /** Why the blocked ones are blocked, or undefined when nothing is. */
  reason?: string;
  byCategory: Array<{ category: string; total: number; availableNow: number; blocked: number }>;
  actions?: Array<{
    tool: string;
    action: string;
    availability: ActionAvailability;
    availableNow: boolean;
    reason: string;
    bridgeMethod?: string;
  }>;
}

/**
 * What this server can serve right now.
 *
 * With an editor attached every action is available, and the classification is
 * still worth reporting: it is the answer to "which of these keep working after
 * I stop the editor to rebuild", which is the moment the question is actually
 * asked.
 */
export function availabilityReport(
  graph: ToolDef[],
  options: AvailabilityReportOptions,
): AvailabilityReport {
  const { editorConnected, category, state = "available", names = false } = options;
  const wanted = category?.trim().toLowerCase();

  const verdicts = classifyGraph(graph).filter((v) => !wanted || v.tool === wanted);
  const isAvailable = (v: ActionVerdict): boolean => editorConnected || v.availability === "always";

  const byCategory = new Map<string, { category: string; total: number; availableNow: number; blocked: number }>();
  for (const v of verdicts) {
    const row = byCategory.get(v.tool) ?? { category: v.tool, total: 0, availableNow: 0, blocked: 0 };
    row.total += 1;
    if (isAvailable(v)) row.availableNow += 1;
    else row.blocked += 1;
    byCategory.set(v.tool, row);
  }

  const availableNow = verdicts.filter(isAvailable).length;
  const blocked = verdicts.length - availableNow;

  const listed = names
    ? verdicts
        .filter((v) => (state === "all" ? true : state === "available" ? isAvailable(v) : !isAvailable(v)))
        .map((v) => ({
          tool: v.tool,
          action: v.action,
          availability: v.availability,
          availableNow: isAvailable(v),
          reason: v.reason,
          bridgeMethod: v.bridgeMethod,
        }))
    : undefined;

  return {
    editorConnected,
    total: verdicts.length,
    availableNow,
    blocked,
    reason: blocked > 0
      ? "No editor is connected, so every action that dispatches to a bridge method has nothing to dispatch to."
      : undefined,
    byCategory: [...byCategory.values()],
    actions: listed,
  };
}

export interface EditorDownContext {
  /** The bridge method the caller was trying to reach. */
  method: string;
  /** The .uproject this connection belongs to, when one is loaded. */
  projectPath: string | null;
  /** The port that was tried, and where the number came from. */
  port: number;
  portSource?: string;
  /** What the underlying transport said, quoted rather than paraphrased. */
  cause?: string;
  /** The editor's own account of itself, when a log or snapshot could be read. */
  phase?: string;
  /** True when the phase says a human has to answer something first. */
  blocking?: boolean;
  /** A modal dialog title the plugin's status file reported. */
  modal?: string;
  /** How many actions still work with no editor. */
  offlineActionCount?: number;
}

/**
 * The message a caller gets when the editor is not there.
 *
 * The transport's own answer is fast and exact ("connect ECONNREFUSED" comes
 * back in single-digit milliseconds, so this is never the timeout case) and it
 * is also unactionable on its own: it names a port and nothing else. It does
 * not say which project the port belongs to, whether the editor is stopped or
 * still starting, what to call to fix it, or that a large part of the surface
 * does not need an editor at all.
 */
export function editorDownMessage(ctx: EditorDownContext): string {
  const parts: string[] = [];
  const project = ctx.projectPath ? ` for ${ctx.projectPath}` : "";
  parts.push(
    `'${ctx.method}' needs a running editor and none answered on port ${ctx.port}`
    + `${ctx.portSource ? ` (${ctx.portSource})` : ""}${project}.`,
  );
  if (ctx.cause) parts.push(`The connection attempt said: ${ctx.cause}`);

  if (ctx.modal) {
    parts.push(
      `An editor IS running and is blocked on the dialog "${ctx.modal}". `
      + `Answer it with editor(action='list_dialogs') then editor(action='respond_to_dialog'), `
      + `and this call will go through without restarting anything.`,
    );
  } else if (ctx.phase === "crashed") {
    parts.push(
      `The editor's own log ends in a crash, so there is nothing to reconnect to. `
      + `editor(action='check_for_crashes') reports the callstack, and `
      + `editor(action='start_editor') brings a new one up.`,
    );
  } else if (ctx.phase?.startsWith("waiting on rebuild prompt")) {
    // The bridge plugin is C++, and a project whose modules are out of date
    // never gets far enough to open one. This is the same failure T17 exists
    // to diagnose, so send the caller straight at the diagnosis.
    parts.push(
      `The editor is up and asking to rebuild modules that are missing or out of date, `
      + `so it will not finish starting until someone answers. `
      + `project(action='check_install') reports which module is out of date and how to build it.`,
    );
  } else if (ctx.blocking) {
    parts.push(
      `The editor's own log says it is ${ctx.phase}, which is waiting on a human rather than on time. `
      + `editor(action='get_engine_state') reports what it is waiting for.`,
    );
  } else if (ctx.phase && ctx.phase !== "editor exited") {
    parts.push(
      `The editor's own log says it is ${ctx.phase}, so it is coming up rather than absent. `
      + `Wait for it, or call editor(action='start_editor'), which blocks until the bridge answers.`,
    );
  } else {
    parts.push("Start one with editor(action='start_editor'), which blocks until the bridge answers.");
  }

  if (ctx.offlineActionCount && ctx.offlineActionCount > 0) {
    parts.push(
      `${ctx.offlineActionCount} actions do not need an editor at all, including the engine symbol index, `
      + `the C++ correctness checks, the project config and source readers, and the build. `
      + `project(action='list_available_actions') lists them.`,
    );
  }

  return parts.join(" ");
}

/**
 * Replace a bare connection failure with the account above.
 *
 * Returns the error untouched unless it is a connection failure, so a timeout
 * on a live socket keeps its own message: that one already explains that the
 * outcome is unknown, which is the opposite of what this says.
 */
export function explainEditorDown(error: unknown, ctx: EditorDownContext): unknown {
  if (!(error instanceof McpError)) return error;
  if (error.code !== ErrorCode.NOT_CONNECTED) return error;
  return new McpError(
    ErrorCode.NOT_CONNECTED,
    editorDownMessage({ ...ctx, cause: ctx.cause ?? error.message }),
    {
      // The call never reached a socket, so unlike a bridge timeout there is
      // nothing for the caller to reconcile. Saying so is the point of the
      // machine block: a client can retry this one safely.
      outcome: "failed",
      method: ctx.method,
      editorConnected: false,
      port: ctx.port,
      projectPath: ctx.projectPath ?? undefined,
      phase: ctx.phase,
      offlineActionCount: ctx.offlineActionCount,
    },
  );
}

/**
 * The same explanation, with the evidence gathered for you.
 *
 * Reads the editor's own log and the plugin's status snapshot (both plain file
 * reads) and counts the offline surface out of the graph the caller hands in.
 * All of it happens on a failure path that has already given up, so the cost
 * is paid only by a call that was going to fail anyway.
 */
export async function explainEditorDownWithEvidence(
  error: unknown,
  ctx: Omit<EditorDownContext, "phase" | "blocking" | "modal" | "offlineActionCount"> & {
    /** The graph the failed call was dispatched from. Omitted, no count is given. */
    toolGraph?: readonly ToolDef[];
  },
): Promise<unknown> {
  if (!(error instanceof McpError) || error.code !== ErrorCode.NOT_CONNECTED) return error;

  let phase: string | undefined;
  let blocking: boolean | undefined;
  let modal: string | undefined;
  try {
    const logState = readLogState(ctx.projectPath, 25);
    const snapshot = readEngineSnapshot(ctx.projectPath);
    // "unknown" is what the log reader says when there is no log to read, or
    // when nothing in it matched a marker. Quoting that back as the editor's
    // phase would turn "no editor has ever run here" into "it is starting".
    const fromLog = logState.phase === "unknown" ? undefined : logState.phase;
    phase = snapshot?.phase ?? fromLog;
    // A status file outlives the editor that wrote it, so a modal recorded in
    // a stale one is not a live modal. Claiming otherwise told a caller "an
    // editor IS running and is blocked" in the same breath as the guard
    // correctly refusing to say so, from the same file, under two policies.
    const snapshotIsFresh =
      snapshot !== null
      && (snapshot.ageSeconds === undefined
        || snapshot.ageSeconds * 1000 <= STATUS_STALE_AFTER_MS);
    const liveModal = snapshotIsFresh ? snapshot?.modal : undefined;
    blocking = logState.blocking || Boolean(liveModal);
    modal = liveModal?.title;
  } catch {
    // No log, no snapshot, no project. The message degrades to the generic
    // "start one" branch, which is still the right advice.
  }

  let offlineActionCount: number | undefined;
  try {
    if (ctx.toolGraph) offlineActionCount = offlineActionNames(ctx.toolGraph).length;
  } catch {
    // Counting the surface is a nicety. Never let it swallow the real error.
  }

  const { toolGraph: _graph, ...context } = ctx;
  return explainEditorDown(error, { ...context, phase, blocking, modal, offlineActionCount });
}
