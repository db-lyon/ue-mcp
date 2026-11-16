import type { ElicitParams, ElicitResult } from "../core/elicit.js";

/**
 * One block of an editor dialog's body, in reading order (#1891).
 *
 * The flat `message` is every text widget of the dialog on its own line,
 * buttons and table cells included. `blocks` is the same body as it reads:
 *
 *   paragraph  text outside any table and any button; texts laid side by side
 *              in the dialog are one paragraph.
 *   table      a list or tree view: its visible column headings and one row of
 *              cells per row. `checked` (when any row has a checkbox) holds each
 *              row's state, null for a row without one, and `items` the index of
 *              that row in the dialog's `items`, which respond_to_dialog takes.
 *
 * The dialog's title and its button labels are never part of it.
 */
export type DialogBlock =
  | { type: "paragraph"; text: string }
  | {
      type: "table";
      columns: string[];
      rows: string[][];
      checked?: Array<boolean | null>;
      items?: Array<number | null>;
    };

/**
 * A blocking dialog put to a daemon extension, for a call that extension made.
 *
 * The same form an MCP client is elicited with (`message`, `requestedSchema`,
 * answered with `content.button` and, for a dialog with rows, `content.items`),
 * plus the dialog itself so the extension can draw it rather than parse it.
 */
export interface DialogPromptRequest extends ElicitParams {
  dialog: {
    /** The editor showing it. */
    editor: string;
    title: string;
    message: string;
    /** In the order the dialog lays them out. */
    buttons: string[];
    /** The rows a person can tick, for a dialog that asks per item. */
    items?: Array<{ index: number; label: string; cells: string[]; checked: boolean }>;
    /** The body as paragraphs and tables, when the plugin reports it; `message` otherwise. */
    blocks?: DialogBlock[];
    /**
     * Set for a native Win32 dialog of the editor process (a GPU device loss,
     * an assert box): its window handle, process, kind, a plain summary, and
     * any log lines that explain it.
     */
    native?: {
      hwnd: string;
      pid: number;
      kind: "gpu-crash" | "assert" | "ensure" | "missing-modules" | "crash" | "message";
      summary: string;
      details?: string[];
    };
    /** The `category.action` whose call met the dialog. */
    subject: string;
    /** An answer sooner than this many milliseconds is not taken as a person's. */
    minHumanMs: number;
  };
}

/**
 * The form's three answers, or `delegate`: the person handed this one dialog
 * to the agent, which may then answer it as it would in auto mode.
 */
export type DialogPromptResult = ElicitResult | { action: "delegate" };

export type DialogPromptFn = (request: DialogPromptRequest) => Promise<DialogPromptResult>;
