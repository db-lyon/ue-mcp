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
import { classifyActionClass, requiresExplicitEditor, splitTaskName, type ActionClass } from "./action-class.js";
import { EDITOR_TARGET_PARAM, stripEditorTarget, type ToolDef } from "./types.js";
import { MICRO_GATEWAY_TOOL, MICRO_GATEWAY_CALL } from "./lean-context.js";
import type { EditorSession, SessionRegistry } from "./session.js";

export interface UntargetedCall {
  /** `category.action`, already resolved through any gateway indirection. */
  taskName: string;
  /** Every registered editor, in registration order. */
  editors: string[];
  /** The editor an untargeted call would land in. */
  activeEditor: string;
  /** The parameter name a caller uses to address an editor. */
  targetParam: string;
}

/**
 * Why this untargeted call cannot be routed, or null when it can.
 *
 * Never called at one editor: a single-session server has nothing to choose
 * between, so there is nothing to refuse.
 */
export function refuseUntargetedCall(call: UntargetedCall): string | null {
  const { class: cls } = classifyActionClass(...toPair(call.taskName));
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

function toPair(taskName: string): [string, string] {
  const { tool, action } = splitTaskName(taskName);
  return [tool, action];
}

function describeWhy(taskName: string, cls: ActionClass): string {
  return cls === "unknown"
    ? `'${taskName}' does whatever its parameters say, so it is treated as a change.`
    : `'${taskName}' changes the editor it runs in.`;
}

export interface RoutedCall {
  /** The editor this call runs in. */
  session: EditorSession;
  /** Params with every routing instruction removed, at both nesting levels. */
  params: Record<string, unknown>;
  /** True when the caller named the editor rather than taking the default. */
  targeted: boolean;
}

/**
 * Route one call to the editor it addressed.
 *
 * `editor` is a routing instruction only on a tool that had the parameter
 * injected; on any other tool it is that tool's own parameter and is left
 * alone. It is stripped here, at both nesting levels, so no dispatch path can
 * forward it into a bridge call.
 */
export function routeEditorCall(
  tool: ToolDef,
  params: Record<string, unknown>,
  sessions: SessionRegistry,
): RoutedCall {
  if (!tool.injectedEditorParam) return { session: sessions.active, params, targeted: false };
  // The micro gateway carries every real parameter inside `args`, so a target
  // arrives there rather than at the top level. Read both, and strip both.
  const nested = params.args && typeof params.args === "object"
    ? (params.args as Record<string, unknown>)
    : undefined;
  const target = params[EDITOR_TARGET_PARAM] ?? nested?.[EDITOR_TARGET_PARAM];
  const stripped = stripEditorTarget(params);
  if (nested) stripped.args = stripEditorTarget(nested);
  return {
    session: sessions.resolve(target),
    params: stripped,
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
 * The gate, against a live session registry.
 *
 * Returns null at one editor without classifying anything, which is what keeps
 * a single-editor server on exactly the path it was on before.
 */
export function refuseUntargetedInRegistry(
  sessions: SessionRegistry,
  taskName: string,
  targeted: boolean,
): string | null {
  if (sessions.size <= 1 || targeted) return null;
  return refuseUntargetedCall({
    taskName,
    editors: sessions.list().map((s) => s.name),
    activeEditor: sessions.active.name,
    targetParam: EDITOR_TARGET_PARAM,
  });
}

/** The machine-readable block naming the editor that served a response. */
export const EDITOR_ATTRIBUTION_PREFIX = "MACHINE_EDITOR=";

/**
 * Name the editor a response came from.
 *
 * Returns null at one editor, which is what keeps a single-editor response
 * byte-identical. The prose of the result is never touched: this is an extra
 * content block, the same way MACHINE_ERROR and MACHINE_DIRECTIVE are, so a
 * client that only reads the first block sees exactly what it saw before.
 */
export function editorAttribution(
  editor: { name: string; projectPath: string | null },
  editorCount: number,
): string | null {
  if (editorCount <= 1) return null;
  return (
    EDITOR_ATTRIBUTION_PREFIX +
    JSON.stringify({ editor: editor.name, project: editor.projectPath })
  );
}
