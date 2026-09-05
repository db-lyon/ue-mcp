/**
 * The server half of the dialog gate.
 *
 * The plugin refuses every non-modal-safe bridge method while a modal is up
 * (BridgeServer.cpp), which covers anything that needs the editor. It cannot
 * cover what never reaches it: the offline actions served from this process,
 * and the handful the bridge answers without the game thread. Those kept
 * working while Unreal sat on a modal, so an agent could carry on reading
 * config and searching the tool graph and never notice the editor was stuck.
 *
 * This closes that. One latch per session, set the moment any bridge call
 * comes back refused, and every tool call is turned away until the dialog is
 * gone. The probe that clears it is `list_dialogs`, which is modal-safe by
 * construction, so the check works while the game thread is parked.
 */
import type { EditorSession } from "./session.js";

/** Actions that must stay reachable, or the dialog could never be answered. */
const ALWAYS_ALLOWED = new Set([
  "editor.list_dialogs",
  "editor.respond_to_dialog",
  "editor.get_dialog_policy",
  "editor.set_dialog_policy",
  "editor.clear_dialog_policy",
  // Reading engine state is how a caller works out what is going on, and it is
  // served without the game thread, so it answers while a modal is up.
  "editor.get_engine_state",
  "project.get_status",
]);

export interface BlockingDialogInfo {
  dialogTitle: string;
  dialogMessage: string;
  buttons: string[];
  choices: Array<{ buttonLabel: string; respondWith: string }>;
}

/** True for the refusal shape the plugin's gate returns. */
export function isDialogRefusal(v: unknown): v is BlockingDialogInfo & { dialogBlocking: true } {
  return (
    typeof v === "object"
    && v !== null
    && (v as Record<string, unknown>).dialogBlocking === true
  );
}

const latched = new WeakMap<EditorSession, BlockingDialogInfo>();

/** Remember that this editor is sitting on a modal. */
export function noteDialogBlocking(session: EditorSession, info: BlockingDialogInfo): void {
  latched.set(session, {
    dialogTitle: info.dialogTitle ?? "",
    dialogMessage: info.dialogMessage ?? "",
    buttons: Array.isArray(info.buttons) ? info.buttons : [],
    choices: Array.isArray(info.choices) ? info.choices : [],
  });
}

/** Forget it, once the dialog is gone. */
export function clearDialogBlocking(session: EditorSession): void {
  latched.delete(session);
}

export function latchedDialog(session: EditorSession): BlockingDialogInfo | undefined {
  return latched.get(session);
}

/**
 * The refusal a gated call gets, in the same shape the plugin emits so a
 * caller has one thing to branch on wherever the refusal came from.
 */
export function buildRefusal(taskName: string, info: BlockingDialogInfo): Record<string, unknown> {
  return {
    success: false,
    dialogBlocking: true,
    refusedMethod: taskName,
    dialogTitle: info.dialogTitle,
    dialogMessage: info.dialogMessage,
    buttons: info.buttons,
    choices: info.choices,
    error:
      `A modal dialog is blocking the editor, so '${taskName}' was refused without running. `
      + "Unreal cannot execute anything else until the dialog is answered. Read it in "
      + "dialogMessage, choose a button, and press it with the call beside it in choices. "
      + "Every other action returns this same refusal until then.",
  };
}

/**
 * Whether this call is gated, re-probing the editor so a dialog answered by
 * hand in the editor window does not leave the latch stuck on.
 *
 * `probe` runs `list_dialogs`. A probe that throws leaves the latch alone and
 * lets the call through: the editor being unreachable is a different failure,
 * and reporting it as a dialog would be a lie.
 */
export async function gateForDialog(
  session: EditorSession,
  taskName: string,
  probe: () => Promise<unknown>,
): Promise<Record<string, unknown> | null> {
  if (ALWAYS_ALLOWED.has(taskName)) return null;
  if (!latched.has(session)) return null;

  let live: unknown;
  try {
    live = await probe();
  } catch {
    return null;
  }

  const dialogs = (live as { dialogs?: unknown })?.dialogs;
  const first = Array.isArray(dialogs) ? dialogs[0] : undefined;
  if (!first) {
    clearDialogBlocking(session);
    return null;
  }

  const d = first as Record<string, unknown>;
  const refreshed: BlockingDialogInfo = {
    dialogTitle: typeof d.title === "string" ? d.title : "",
    dialogMessage: typeof d.message === "string" ? d.message : "",
    buttons: Array.isArray(d.buttons) ? (d.buttons as string[]) : [],
    choices: Array.isArray(d.choices)
      ? (d.choices as Array<{ buttonLabel: string; respondWith: string }>)
      : [],
  };
  noteDialogBlocking(session, refreshed);
  return buildRefusal(taskName, refreshed);
}
