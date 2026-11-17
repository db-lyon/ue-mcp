// @session per-session: Routes a call to an editor and refuses an untargeted change.
/**
 * Write gating and response attribution beyond one editor (#817, plan 5.2/5.3).
 *
 * Both are inert while the server drives one editor, and that is a startup
 * fact rather than a runtime one: with a single session nothing here is
 * consulted, so the response bytes and the refusal behaviour are exactly what
 * they were before sessions existed.
 *
 * The gate answers one question: an untargeted call arrived, and it would fall
 * through to whichever session happens to be active. Is that safe? For a read
 * it always is. For anything that changes an editor, its project on disk, or
 * its process, falling through means editing a project the caller never named,
 * which is the failure multi-editor exists to prevent. Those calls are refused
 * with the editors listed, so the caller picks rather than the server guessing.
 *
 * Attribution answers the other half: once a response can have come from any of
 * several editors, a response that does not say which one is unreadable. The
 * name rides along in the same machine-readable block shape the error and
 * directive envelopes already use.
 */
import { requiresExplicitEditor, type ActionClass } from "../surface/action-class.js";
import { actionSpecOf, taskEffect } from "../surface/action-effects.js";
import type { ToolDef } from "../surface/tool-types.js";
import { stripEditorTarget } from "../surface/target-params.js";
import { EDITOR_TARGET_PARAM, stripAction } from "../surface/routing-params.js";
import { MICRO_GATEWAY_TOOL, MICRO_GATEWAY_CALL, microCallParams } from "../surface/context/micro-context.js";
import type { EditorSession, SessionRegistry } from "../sessions/session.js";

export interface UntargetedCall {
  /** `category.action`, already resolved through any gateway indirection. */
  taskName: string;
  /** Every registered editor, in registration order. */
  editors: string[];
  /** The editor an untargeted call would land in. */
  activeEditor: string;
  /** The parameter name a caller uses to address an editor. */
  targetParam: string;
  /** The graph whose declarations decide the effect. Omitted, the pristine one. */
  graph?: readonly ToolDef[];
}

/**
 * Why this untargeted call cannot be routed, or null when it can.
 *
 * Never called at one editor: a single-session server has nothing to choose
 * between, so there is nothing to refuse.
 */
export function refuseUntargetedCall(call: UntargetedCall): string | null {
  // Its subject is the session registry or a service outside every editor
  // (add_editor, feedback(submit)). It may still declare `mutate`, which is
  // what locking and the guards should see; it just cannot land in the wrong
  // editor, so demanding a target would be a riddle rather than a safeguard.
  if (actionSpecOf(call.taskName, call.graph)?.addressesServer) return null;
  // The action's own declaration, not a reading of its name. A name is only
  // consulted for a task the tool graph does not carry, and the answer there
  // is a flat `mutate` rather than a second opinion about the name.
  const { effect: cls } = taskEffect(call.taskName, call.graph);
  if (!requiresExplicitEditor(cls)) return null;

  const others = call.editors.filter((n) => n !== call.activeEditor);
  return (
    `${describeWhy(call.taskName, cls)} This server drives ${call.editors.length} editors ` +
    `(${call.editors.join(", ")}), so it will not pick one for you: an untargeted run would go to ` +
    `'${call.activeEditor}'${others.length > 0 ? `, leaving ${others.join(", ")} untouched` : ""}. ` +
    `Re-send it with ${call.targetParam}="<name>". ` +
    `project(action='list_editors') reports what each one is. Reads do not need this.`
  );
}

function describeWhy(taskName: string, cls: ActionClass): string {
  return cls === "unknown"
    ? `'${taskName}' does whatever its parameters say, so it is treated as a change.`
    : `'${taskName}' changes the editor it runs in.`;
}

/** The editor a call named, taken off its parameters. */
export interface TakenTarget {
  /** The name, project or path the caller gave; undefined when it gave none. */
  target: unknown;
  /** Params with the routing instruction removed, at both nesting levels. */
  params: Record<string, unknown>;
}

/**
 * Take the editor a call names off its parameters, before anything validates
 * them, so validation never sees or strips a routing instruction (#1278).
 *
 * `editor` is a routing instruction only on a tool that had the parameter
 * injected, or on one that does not declare it (one editor: not advertised,
 * but a name a caller still holds resolves strictly). On a tool that declares
 * its own `editor` it is that tool's parameter and is left alone. The micro
 * gateway carries every real parameter inside `args`, so both levels are read
 * and both stripped.
 */
export function takeEditorTarget(tool: ToolDef, params: Record<string, unknown>): TakenTarget {
  if (!tool.injectedEditorParam && EDITOR_TARGET_PARAM in tool.schema) return { target: undefined, params };
  const nested = params.args && typeof params.args === "object"
    ? (params.args as Record<string, unknown>)
    : undefined;
  const target = params[EDITOR_TARGET_PARAM] ?? nested?.[EDITOR_TARGET_PARAM];
  if (!tool.injectedEditorParam && (target === undefined || target === null || target === "")) return { target: undefined, params };
  const stripped = stripEditorTarget(params);
  if (nested) stripped.args = stripEditorTarget(nested);
  return { target, params: stripped };
}

/**
 * The editor a taken target names, and whether the caller named one. No name
 * is the active editor; a name that matches nothing throws rather than falling
 * through to it.
 */
export function resolveEditorTarget(sessions: SessionRegistry, target: unknown): { session: EditorSession; targeted: boolean } {
  return {
    session: sessions.resolve(target),
    targeted: typeof target === "string" && target.trim() !== "",
  };
}

/**
 * The `category.action` a call actually runs, seen past any gateway.
 *
 * In micro mode every call arrives as `tools(call, category, method)`, so
 * classifying the tool the client named would classify the gateway instead of
 * the thing being invoked, and let every mutation through as one read.
 */
export function effectiveTaskName(tool: ToolDef, params: Record<string, unknown>): string {
  const action = typeof params.action === "string" ? params.action : "";
  if (tool.name === MICRO_GATEWAY_TOOL && action === MICRO_GATEWAY_CALL) {
    const category = typeof params.category === "string" ? params.category : "";
    const method = typeof params.method === "string" ? params.method : "";
    return `${category}.${method}`;
  }
  return `${tool.name}.${action}`;
}

/**
 * The task a call runs and the parameters that task reads, seen past any
 * gateway. Asset locks are classified from this, so a micro call locks exactly
 * what the direct call to the same action locks.
 */
export function callSubject(
  tool: ToolDef,
  params: Record<string, unknown>,
): { taskName: string; params: Record<string, unknown> } {
  const taskName = effectiveTaskName(tool, params);
  const viaGateway = tool.name === MICRO_GATEWAY_TOOL && params.action === MICRO_GATEWAY_CALL;
  return { taskName, params: viaGateway ? microCallParams(params) : stripAction(params) };
}

/**
 * The gate, against a live session registry.
 *
 * Returns null at one editor without classifying anything, which is what keeps
 * a single-editor server on exactly the path it was on before.
 */
export function refuseUntargetedInRegistry(
  sessions: SessionRegistry,
  taskName: string,
  targeted: boolean,
  graph?: readonly ToolDef[],
): string | null {
  if (sessions.size <= 1 || targeted) return null;
  return refuseUntargetedCall({
    taskName,
    editors: sessions.list().map((s) => s.name),
    activeEditor: sessions.active.name,
    targetParam: EDITOR_TARGET_PARAM,
    graph,
  });
}

/** The machine-readable block naming the editor that served a response. */
export const EDITOR_ATTRIBUTION_PREFIX = "MACHINE_EDITOR=";

/**
 * Name the editor a response came from.
 *
 * Returns null at one editor, which is what keeps a single-editor response
 * byte-identical, unless several processes hold that editor's project; then
 * the answering pid is the point (#1150). The prose of the result is never touched: this is an extra
 * content block, the same way MACHINE_ERROR and MACHINE_DIRECTIVE are, so a
 * client that only reads the first block sees exactly what it saw before.
 */
export function editorAttribution(
  editor: { name: string; projectPath: string | null; pid?: number | null },
  editorCount: number,
  /** Several processes hold this session's project (#1150). */
  contested = false,
): string | null {
  if (editorCount <= 1 && !contested) return null;
  return (
    EDITOR_ATTRIBUTION_PREFIX +
    JSON.stringify({ editor: editor.name, project: editor.projectPath, pid: editor.pid ?? undefined })
  );
}
