/**
 * A session's access mode: whether the calls one client sends may change
 * anything (#1484).
 *
 * The mode belongs to the MCP session, not to the editor or the project: two
 * clients on one editor can hold different modes, so it travels with each call
 * in its `CallAsker` rather than living on the shared guard pipeline.
 *
 * It is enforced at two seams, both reading the same effect declarations:
 * dispatch refuses an action whose declared effect is `mutate` or `unknown`
 * before it runs (which covers actions served in this process), and the
 * bridge pipeline runs `readOnlyGuard` ahead of every other guard, which
 * covers a bridge call made from inside a declared read, a flow step or a
 * batch whose arguments decide its effect.
 */
import { McpError, ErrorCode } from "../core/errors.js";
import type { ActionEffect, ToolDef } from "../core/types.js";
import { bridgeMethodEffect, mayChangeState, taskEffect } from "../surface/action-effects.js";
import { currentCallAsker } from "./call-elicit.js";
import { mutationScope, type BridgeGuard, type CallContext } from "./guard.js";

/** The modes a session can be in. `read-write` is what a session starts in. */
export const SESSION_ACCESS_MODES = ["read-write", "read-only"] as const;
export type SessionAccessMode = (typeof SESSION_ACCESS_MODES)[number];
export const DEFAULT_SESSION_ACCESS: SessionAccessMode = "read-write";

/** The mode named by `value`, or undefined when it names none. */
export function parseSessionAccessMode(value: unknown): SessionAccessMode | undefined {
  return SESSION_ACCESS_MODES.find((m) => m === value);
}

/** One session's mode, read per call so a change applies to the next call. */
export class SessionAccess {
  private current: SessionAccessMode;

  constructor(mode: SessionAccessMode = DEFAULT_SESSION_ACCESS) {
    this.current = mode;
  }

  get mode(): SessionAccessMode {
    return this.current;
  }

  /** Set the mode. True when it changed. */
  set(mode: SessionAccessMode): boolean {
    if (mode === this.current) return false;
    this.current = mode;
    return true;
  }
}

/** True when the call in flight was sent by a session in read-only mode. */
export function callIsReadOnly(): boolean {
  return currentCallAsker()?.access?.() === "read-only";
}

/** `blueprint(create)` from `blueprint.create`. */
function label(taskName: string): string {
  const dot = taskName.indexOf(".");
  return dot > 0 ? `${taskName.slice(0, dot)}(${taskName.slice(dot + 1)})` : taskName;
}

function refusal(what: string, effect: ActionEffect): string {
  const why = effect === "unknown"
    ? "its parameters decide what it does, so it counts as a change"
    : "it is declared to change state";
  return `read-only session mode refuses ${what}: ${why}. Reads still run. `
    + "The session's access mode is set through the daemon (PUT /v1/sessions/<id>/access).";
}

/**
 * The refusal for running `taskName` in read-only mode, or null when it reads.
 *
 * Reads the effect the action declares in `graph`; a name the graph does not
 * carry is `mutate`, so it is refused.
 */
export function readOnlyRefusal(taskName: string, graph?: readonly ToolDef[]): string | null {
  const { effect } = taskEffect(taskName, graph);
  return mayChangeState(effect) ? refusal(label(taskName), effect) : null;
}

/**
 * The bridge half: refuses a bridge call that may change state when the call
 * in flight came from a read-only session. The pipeline runs it first, so no
 * other guard acts (a checkout, say) on a call that is about to be refused.
 */
export const readOnlyGuard: BridgeGuard = {
  name: "read-only",
  order: Number.MIN_SAFE_INTEGER,
  appliesTo: (cc: CallContext) => callIsReadOnly() && mutationScope(cc),
  before: async (cc: CallContext): Promise<void> => {
    const subject = currentCallAsker()?.subject;
    const what = subject ? `${label(subject)} (bridge method ${cc.method})` : `bridge method ${cc.method}`;
    const { effect } = bridgeMethodEffect(cc.method, cc.params, cc.session?.toolGraph);
    throw new McpError(ErrorCode.WRITE_BLOCKED, refusal(what, effect));
  },
};
