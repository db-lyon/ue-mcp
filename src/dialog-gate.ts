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
import type { ElicitFn } from "./types.js";
import type { DialogMode } from "./user-state.js";

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
export function buildRefusal(
  taskName: string,
  info: BlockingDialogInfo,
  mode: DialogMode = "auto",
): Record<string, unknown> {
  const common = {
    success: false,
    dialogBlocking: true,
    refusedMethod: taskName,
    dialogMode: mode,
    dialogTitle: info.dialogTitle,
    dialogMessage: info.dialogMessage,
    buttons: info.buttons,
  };
  const preamble =
    `A modal dialog is blocking the editor, so '${taskName}' was refused without running. `
    + "Unreal cannot execute anything else until the dialog is answered. ";

  if (mode === "defer") {
    return {
      ...common,
      error:
        preamble
        + "Dialog mode is defer, so this reports the dialog for recognition rather than "
        + "actuation: answer it in the Unreal Editor window. To answer it from here instead, "
        + "read it with editor(list_dialogs) and press with editor(respond_to_dialog), which "
        + "is a decision you make rather than one this payload makes for you.",
    };
  }
  return {
    ...common,
    choices: info.choices,
    error:
      preamble
      + "Read it in dialogMessage, choose a button, and press it with the call beside it in "
      + "choices. Every other action returns this same refusal until then.",
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
export interface GateOptions {
  /** How this machine wants a blocking dialog handled. */
  mode: DialogMode;
  /** Reads the live dialog list. Modal-safe, so it answers while parked. */
  probe: () => Promise<unknown>;
  /** Presses one button by label. */
  press: (buttonLabel: string) => Promise<unknown>;
  /** Present when the connected client advertised elicitation. */
  elicit?: ElicitFn;
}

export async function gateForDialog(
  session: EditorSession,
  taskName: string,
  opts: GateOptions,
): Promise<Record<string, unknown> | null> {
  if (ALWAYS_ALLOWED.has(taskName)) return null;
  if (!latched.has(session)) return null;

  let live: unknown;
  try {
    live = await opts.probe();
  } catch {
    return null;
  }

  const refreshed = readDialog(live);
  if (!refreshed) {
    clearDialogBlocking(session);
    return null;
  }
  noteDialogBlocking(session, refreshed);

  // interactive: the dialog is a question, so it goes to the person, and only
  // the button THEY pick is pressed. Answering it clears the way, so the call
  // that tripped the gate goes on to run rather than making them ask twice.
  if (opts.mode === "interactive" && opts.elicit) {
    const pressed = await askUser(refreshed, opts);
    if (pressed) {
      clearDialogBlocking(session);
      return null;
    }
  }

  // defer reports the dialog for recognition rather than actuation, so the
  // press calls are withheld, here as in stop_editor, or defer would differ
  // from auto in wording only.
  return buildRefusal(taskName, refreshed, opts.mode);
}

/** The dialog the probe found, or null when the editor reports none. */
function readDialog(live: unknown): BlockingDialogInfo | null {
  const dialogs = (live as { dialogs?: unknown })?.dialogs;
  const first = Array.isArray(dialogs) ? dialogs[0] : undefined;
  if (!first) return null;
  const d = first as Record<string, unknown>;
  return {
    dialogTitle: typeof d.title === "string" ? d.title : "",
    dialogMessage: typeof d.message === "string" ? d.message : "",
    buttons: Array.isArray(d.buttons) ? (d.buttons as string[]) : [],
    choices: Array.isArray(d.choices)
      ? (d.choices as Array<{ buttonLabel: string; respondWith: string }>)
      : [],
  };
}

/**
 * Put the dialog to the person and press what they choose.
 *
 * Returns the label pressed, or null when nothing was. Declining, leaving it
 * open, a client with no elicitation UI, or a press the editor refused all come
 * back null, and the caller then reports the dialog rather than pretending it
 * is gone.
 */
async function askUser(dialog: BlockingDialogInfo, opts: GateOptions): Promise<string | null> {
  if (!opts.elicit || dialog.buttons.length === 0) return null;
  const LEAVE_OPEN = "Leave the dialog open";
  let answer;
  try {
    answer = await opts.elicit({
      message: [
        "Unreal Editor is blocked on a modal dialog and is waiting for an answer.",
        "Nothing else can run until it is answered.",
        "",
        "Title: " + dialog.dialogTitle,
        dialog.dialogMessage === "" ? "(no message text)" : dialog.dialogMessage,
        "",
        "Choose the button to press. Nothing is pressed unless you choose it.",
      ].join("\n"),
      requestedSchema: {
        type: "object",
        properties: {
          button: {
            type: "string",
            title: "Button",
            description: "The dialog's own buttons, in the order it lays them out.",
            enum: [...dialog.buttons, LEAVE_OPEN],
          },
        },
        required: ["button"],
      },
    });
  } catch {
    return null;
  }
  if (answer.action !== "accept") return null;
  const chosen = answer.content?.button;
  if (typeof chosen !== "string" || chosen === LEAVE_OPEN) return null;
  if (!dialog.buttons.includes(chosen)) return null;
  try {
    await opts.press(chosen);
  } catch {
    return null;
  }
  return chosen;
}

/**
 * Turn the plugin's own refusal into the one this machine's mode calls for.
 *
 * The plugin gate fires first for anything needing the editor and knows
 * nothing about the mode, so without this the first blocked call would get its
 * wording and the next, caught by the latch, would get the mode's. Same
 * decision either way.
 *
 * Returns the refusal to send back, or null when the person answered the
 * dialog through the elicitation form, in which case the editor is free again
 * and the caller is told to retry.
 */
export async function reshapeDialogRefusal(
  session: EditorSession,
  taskName: string,
  info: BlockingDialogInfo,
  opts: GateOptions,
): Promise<Record<string, unknown> | null> {
  if (opts.mode === "interactive" && opts.elicit) {
    const pressed = await askUser(info, opts);
    if (pressed) {
      clearDialogBlocking(session);
      return null;
    }
  }
  return buildRefusal(taskName, info, opts.mode);
}
