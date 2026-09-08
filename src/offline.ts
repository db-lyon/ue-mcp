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
 *                    then talks to the editor is a property of the code, not of
 *                    the declaration, so those are enumerated below by hand.
 *
 * The hand-written half is deliberate. Deriving it by scanning
 * `handler.toString()` for a bridge reference looks cheaper and is wrong:
 * `asset(migrate)` calls the bridge from a module-level helper the closure only
 * names, so a source scan reads it as editor-free and an agent is told a
 * migrate will work with the editor down. `LOCAL_ACTIONS` records the answer
 * once, and `tests/unit/offline.test.ts` fails when an action is added, removed
 * or renamed without updating it, so the table cannot silently drift away from
 * the graph the server dispatches from.
 */
import type { ActionSpec, ToolDef } from "./types.js";
import { McpError, ErrorCode } from "./errors.js";
import { STATUS_STALE_AFTER_MS } from "./dialog-guard.js";

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
 * Every action whose work happens in this process rather than in the editor,
 * with the reason each one is in the list.
 *
 * Keyed `tool.action`. An entry omitted here for an action that has a handler
 * is not assumed either way: it reports `unknown` and says so, because guessing
 * "offline" for an unclassified action is the failure this module exists to
 * remove.
 */
export const LOCAL_ACTIONS: Record<string, string> = {
  // Session and connection bookkeeping. These describe the connection rather
  // than using it, which is exactly what a caller needs while it is down.
  "project.get_status": "Reports the connection state, and reads the editor's own log and status snapshot off disk when there is no live bridge to ask.",
  "project.set_project": "Retargets path resolution and the bridge socket, then reports whether the new target answered.",
  "project.list_editors": "Lists registered sessions and probes each bridge port from outside.",
  "project.use_editor": "Changes which registered session is the default.",
  "project.add_editor": "Registers a session and attaches the plugin on disk. The connection attempt is allowed to fail.",
  "project.drop_editor": "Forgets a session and closes its socket. The editor process is left running.",

  // The project on disk: its descriptor, its INI tree and its C++ source.
  "project.get_info": "Reads the .uproject file.",
  "project.read_config": "Reads INI files from the project's Config directory.",
  "project.search_config": "Searches the project's INI files on disk.",
  "project.list_config_tags": "Builds the gameplay tag tree from the project's INI files.",
  "project.read_cpp_header": "Reads a header from the project's Source tree.",
  "project.read_module": "Reads a module's headers from the project's Source tree.",
  "project.list_modules": "Enumerates the modules declared under the project's Source roots.",
  "project.search_cpp": "Searches the project's own C++ on disk.",
  "project.write_cpp_file": "Writes a file under the project's Source tree.",
  "project.read_cpp_source": "Reads a .cpp from the project's Source tree.",
  "project.write_source_file": "Writes into a named module's Public or Private folder.",
  "project.read_source_file": "Reads from a named module's folder.",
  "project.add_module_dependency": "Edits a Build.cs dependency array on disk.",
  "project.add_cpp_member": "Edits a header on disk.",
  "project.list_files": "Lists files on disk under a directory.",
  "project.list_content_assets": "Walks the package files under a mount path on disk. It is the one asset query that needs no editor, and it says which questions it cannot answer without one.",

  // The engine tree and the symbol index over it (T14). These read installed
  // engine headers, which are there whether or not an editor is running.
  "project.read_engine_header": "Reads an engine header from the installed engine tree.",
  "project.find_engine_symbol": "Searches the installed engine headers.",
  "project.list_engine_modules": "Enumerates modules in the installed engine tree.",
  "project.search_engine_cpp": "Searches the installed engine sources.",
  "project.build_engine_index": "Scans the installed engine headers into the on-disk symbol index.",
  "project.verify_symbols": "Answers from the engine symbol index on disk.",
  "project.suggest_build_deps": "Answers from the engine symbol index and reads the target Build.cs.",
  "project.find_example_usage": "Searches the installed engine sources.",
  "project.lint_cpp_header": "Checks a header on disk against the engine symbol index.",
  "project.class_hierarchy": "Answers from the engine symbol index on disk.",
  "project.find_references": "Searches the installed engine sources and the project's own.",
  "project.find_callers": "Searches the installed engine sources and the project's own.",
  "project.find_callees": "Reads the symbol's own body out of the engine sources.",
  "project.symbol_context": "Reads the declaration and its surrounding lines off disk.",

  // The surface describing itself. Reads the tool graph this process holds.
  "project.search_tools": "Searches the tool graph this server holds in memory.",
  "project.describe_action": "Reads the parameter schema out of the tool graph this server holds in memory.",
  "project.execute_python_report": "Reads this session's own recorded calls.",
  "project.list_available_actions": "Classifies the tool graph this server holds in memory.",
  "project.check_install": "Inspects the project, the engine tree and the deployed plugin on disk.",

  // Process lifecycle. Starting an editor is the one thing that must work
  // without one, and building requires the editor to be stopped.
  "editor.start_editor": "Launches the editor process, then waits for its bridge. It exists for the case where nothing is running.",
  "editor.stop_editor": "Asks the editor that published this project's port lockfile to quit. With no lockfile it refuses and names the file it checked.",
  "editor.restart_editor": "Stops whatever holds this project open and starts it again.",
  "editor.get_engine_state": "Probes the process table, the editor's log and the plugin's status file from outside. The bridge is consulted only when it is already connected.",
  "editor.build_project": "Runs UnrealBuildTool out of process, which requires the editor to be STOPPED because it cannot link while an editor holds the module DLLs.",
  "project.build": "Runs UnrealBuildTool out of process, which requires the editor to be STOPPED because it cannot link while an editor holds the module DLLs.",

  // Reporting and introspection that never touch the editor.
  "feedback.submit": "Posts to a GitHub tracker. It needs the network and a user approval prompt, not an editor.",
  "feedback.route": "Answers where an issue would land from the plugin registry on disk.",
  "plugins.list": "Reads the plugin set this server loaded.",
  "plugins.describe": "Reads one loaded plugin's manifest.",
};

/**
 * Actions that have a handler but still reach the editor, with the reason.
 *
 * Kept separate from `LOCAL_ACTIONS` so both halves are stated rather than one
 * being inferred from the other's absence. Between them they must cover every
 * handler-backed action in the graph, which is what the unit test enforces.
 */
export const EDITOR_BOUND_LOCAL_ACTIONS: Record<string, string> = {
  "asset.search": "Wraps the search_assets bridge method, resolving content roots first.",
  "asset.migrate": "Calls the migrate bridge method from a module-level helper, then rescans the destination editor.",
  "asset.lock": "Calls the acquire_lock bridge method. The lock registry lives in the editor.",
  "asset.unlock": "Calls the release_lock bridge method. The lock registry lives in the editor.",
  "asset.unlock_all": "Calls the release_session_locks bridge method. The lock registry lives in the editor.",
  "blueprint.author": "Runs a sequence of bridge calls to create and populate a Blueprint.",
  "niagara.batch": "Runs a sequence of bridge calls against one Niagara system.",
  "editor.execute_python": "Runs Python inside the editor process.",
  "editor.play_in_editor_ignore_blueprint_errors": "Drives Play In Editor through the pie_control bridge method.",
  "editor.request_editor_shutdown": "Asks the running editor to shut itself down over the bridge.",
};

/** Where a handler-backed action's classification comes from. */
export function classifyAction(tool: string, action: string, spec: ActionSpec): ActionVerdict {
  const key = `${tool}.${action}`;

  if (spec.bridge) {
    return {
      tool,
      action,
      availability: "editor",
      reason: `Dispatches to the '${spec.bridge}' bridge method, which only a running editor answers.`,
      bridgeMethod: spec.bridge,
    };
  }

  const local = LOCAL_ACTIONS[key];
  if (local) return { tool, action, availability: "always", reason: local };

  const bound = EDITOR_BOUND_LOCAL_ACTIONS[key];
  if (bound) return { tool, action, availability: "editor", reason: bound };

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
export function classifyGraph(graph: ToolDef[]): ActionVerdict[] {
  const out: ActionVerdict[] = [];
  for (const tool of graph) {
    for (const [action, spec] of Object.entries(tool.actions)) {
      out.push(classifyAction(tool.name, action, spec));
    }
  }
  return out;
}

/** The `tool.action` names that run with no editor, for a graph. */
export function offlineActionNames(graph: ToolDef[]): string[] {
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
 * reads) and counts the offline surface out of the live tool graph. All of it
 * happens on a failure path that has already given up, so the cost is paid
 * only by a call that was going to fail anyway. The tool graph is imported
 * dynamically because this module sits underneath it: a static import would
 * close the cycle from `tools.ts` back to itself.
 */
export async function explainEditorDownWithEvidence(
  error: unknown,
  ctx: Omit<EditorDownContext, "phase" | "blocking" | "modal" | "offlineActionCount">,
): Promise<unknown> {
  if (!(error instanceof McpError) || error.code !== ErrorCode.NOT_CONNECTED) return error;

  let phase: string | undefined;
  let blocking: boolean | undefined;
  let modal: string | undefined;
  try {
    const { readLogState, readEngineSnapshot } = await import("./engine-observer.js");
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
    const { getLiveToolGraph } = await import("./tools.js");
    offlineActionCount = offlineActionNames(getLiveToolGraph()).length;
  } catch {
    // Counting the surface is a nicety. Never let it swallow the real error.
  }

  return explainEditorDown(error, { ...ctx, phase, blocking, modal, offlineActionCount });
}
