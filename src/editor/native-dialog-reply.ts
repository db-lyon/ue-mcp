// @session independent: Builds a reply from the dialog guard it is handed; holds nothing per editor.
/**
 * list_dialogs and respond_to_dialog, answered in this process while the
 * editor process shows a native Win32 dialog.
 *
 * The plugin cannot see such a box and cannot answer while it is up, so both
 * would wait out their timeout. The window scan sees it from outside, and the
 * press goes to that window by handle. The guarded bridge asks this module for
 * a reply on every call and sends the call on when there is none, so the
 * bridge wrapper itself names no dialog method.
 */
import type { DialogGuard } from "./dialog-guard.js";

/** A press keyword, as respond_to_dialog takes it, to the labels that carry its meaning. */
const RESPONSE_LABELS: Record<string, string[]> = {
  ok: ["OK"],
  yes: ["Yes", "OK"],
  no: ["No", "Cancel"],
  cancel: ["Cancel"],
  retry: ["Retry", "Try Again"],
  continue: ["Continue"],
  ignore: ["Ignore"],
  abort: ["Abort"],
};

/** Bridge methods a native dialog can be answered for from outside the editor. */
export function repliesToNativeDialog(method: string): boolean {
  return method === "list_dialogs" || method === "respond_to_dialog";
}

/** The method lists dialogs, so a native dialog that appears mid-call is its answer rather than a refusal. */
export function listsDialogs(method: string): boolean {
  return method === "list_dialogs";
}

/** The reply to `method` while a native dialog is up, or undefined when none is (or the method is not answered here). */
export async function nativeDialogReply(
  guard: DialogGuard,
  method: string,
  params: Record<string, unknown> | undefined,
): Promise<Record<string, unknown> | undefined> {
  if (!repliesToNativeDialog(method)) return undefined;
  const dialog = guard.nativeDialog();
  if (!dialog?.native) return undefined;
  if (listsDialogs(method)) {
    return {
      success: true,
      native: true,
      dialogs: [{
        title: dialog.title,
        message: dialog.message,
        messageTruncated: false,
        buttons: dialog.buttons,
        choices: dialog.choices,
        blocksGameThread: true,
        detectedBy: "nativeWindow",
        native: dialog.native,
      }],
      windows: [],
    };
  }
  const p = params ?? {};
  const buttonsList = dialog.buttons.map((b) => `"${b}"`).join(", ");
  if (typeof p.dialogAction === "string" && p.dialogAction !== "") {
    return {
      success: false,
      error: `"${dialog.title}" is a native Windows dialog, which is answered with one of its own buttons (${buttonsList}), not closed. Pass buttonLabel.`,
    };
  }
  let label: string | undefined;
  if (typeof p.buttonLabel === "string" && p.buttonLabel !== "") label = p.buttonLabel;
  else if (typeof p.buttonIndex === "number") label = dialog.buttons[p.buttonIndex];
  else if (typeof p.response === "string") {
    const wanted = RESPONSE_LABELS[p.response.toLowerCase()] ?? [p.response];
    label = wanted.map((w) => dialog.buttons.find((b) => b.toLowerCase() === w.toLowerCase())).find((b) => b !== undefined);
  }
  if (!label) return { success: false, error: `Name one of the dialog's buttons with buttonLabel: ${buttonsList}.` };
  const r = await guard.pressNativeButton(dialog, label, null);
  if (!r.pressed) return { success: false, answered: true, native: true, error: r.error ?? "The press did not reach the dialog." };
  return {
    success: true,
    answered: true,
    native: true,
    pressed: r.button ?? label,
    closed: r.closed,
    ...(dialog.native.kind === "gpu-crash"
      ? { note: "The editor's GPU device was lost, so the editor exits now. Start it again once it has gone." }
      : {}),
  };
}
