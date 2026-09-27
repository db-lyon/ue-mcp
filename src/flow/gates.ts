/**
 * The server's gates, declared once as flowkit checks.
 *
 * Each is a `when` over facts: `session.*` and `editor.*` from the host
 * namespaces, `call.*` for the call being routed, `step.*` for the task being
 * run. A plan reads the facts without acting (flow/preflight.ts). A live call
 * or flow step reads them live, evaluates the same `when`, and refuses in the
 * shape its route always used.
 */
import { evaluateCondition } from "./condition.js";
import type { GuardDecision } from "../editor/dialog-guard.js";

export type GateId = "untargeted" | "editor" | "dialog" | "python";

export interface GateDeclaration {
  /** `flow` is checked once per call or run, `step` per task. */
  on: "flow" | "step";
  when: string;
  /** What a plan reports. The untargeted gate words its own, naming the editors. */
  message?: string;
}

export const GATES: Readonly<Record<GateId, GateDeclaration>> = {
  untargeted: {
    on: "flow",
    when: "session.count > 1 && !call.targeted && call.needs_target",
  },
  editor: {
    on: "step",
    when: "step.availability == 'editor' && !editor.connected",
    message: "No editor is connected, and this step dispatches to one.",
  },
  dialog: {
    on: "step",
    when: "step.dialog_blocked",
    message: "A modal dialog is blocking the editor, and this step would be refused until it is answered.",
  },
  python: {
    on: "step",
    when: "step.name == 'editor.execute_python' && step.python_refused",
    message: "execute_python's gate would refuse this step: give it a taskSummary and rule out, in ruledOut, every "
      + "candidate action a search for it returns. editor(execute_python) with the same taskSummary lists them.",
  },
};

export const GATE_IDS = Object.keys(GATES) as GateId[];

/** Whether gate `id` fires over these facts. */
export function gateFires(id: GateId, facts: Record<string, unknown>): boolean {
  return evaluateCondition(GATES[id].when, facts);
}

/**
 * The dialog gate for a live call or step. The guard reads the modal, and may
 * answer it first under its mode; the declaration decides from what it found.
 */
export async function dialogGate(
  guard: { check(subject: string, kind: "bridge" | "action", opts?: { canElicit?: boolean }): Promise<GuardDecision> },
  subject: string,
  kind: "bridge" | "action",
  opts?: { canElicit?: boolean },
): Promise<Record<string, unknown> | null> {
  const decision = opts === undefined ? await guard.check(subject, kind) : await guard.check(subject, kind, opts);
  const refusal = decision.allow ? undefined : decision.refusal;
  return gateFires("dialog", { step: { dialog_blocked: refusal !== undefined } }) ? refusal! : null;
}
