// @session per-session: Resolves the dialog handling mode for the project it is handed, from that project's stored preference.
import { DIALOG_MODE, type DialogMode } from "../config/modes.js";
import { getUserStatePath } from "../config/state-store.js";
import type { ElicitFn } from "../core/elicit.js";

/**
 * The dialog handling mode that applied, and why it applied.
 *
 * The "why" is reported because the mode is resolved from three places and a
 * caller who sees a dialog handled differently than they expected needs to know
 * which one decided it, without reading the server's source.
 */
export interface ResolvedDialogMode {
  mode: DialogMode;
  /** Where the mode came from, in the reader's terms. */
  source: string;
}

/**
 * Whether the user behind this call can actually be shown a form.
 *
 * NOT "was an elicit function handed over". The shipped server builds that
 * function at startup, before a client has connected, so it is always present
 * and testing it for undefined answers a different question: it says the server
 * has a gate, not that the client has a UI. Reading it that way put every
 * client that advertised nothing into the interactive path and reported the
 * reason as "the client advertised elicitation", which was false.
 *
 * A gate built outside the server (tests, embedders) carries no probe and is
 * taken at face value: it was handed over deliberately.
 */
export function clientAdvertisesElicitation(elicit?: ElicitFn): boolean {
  if (!elicit) return false;
  if (typeof elicit.clientAdvertisesElicitation === "function") {
    return elicit.clientAdvertisesElicitation();
  }
  return true;
}

/**
 * Resolve the dialog handling mode with the scoped-mode precedence (the env
 * var, this project's stored mode, the per-user one), then the default:
 * "interactive" when the connected client advertised MCP elicitation,
 * otherwise "defer".
 *
 * THE DEFAULT NEVER RESOLVES TO "auto". A dialog is a question for a person,
 * and with no channel to that person the safe answer is to suspend and say so.
 * "auto" is reachable only by being named, in the env var or the stored mode.
 *
 * An unrecognised env value is ignored rather than guessed at, and that it was
 * ignored travels in `source` so a typo does not silently change how dialogs
 * are handled.
 */
export function resolveDialogMode(opts: {
  projectDir?: string | null;
  /** Whether the connected MCP client advertised the elicitation capability. */
  canElicit: boolean;
  env?: NodeJS.ProcessEnv;
}): ResolvedDialogMode {
  const resolved = DIALOG_MODE.resolve(opts.projectDir ?? null, opts.env);
  if (resolved.source === "env" && resolved.value) {
    return { mode: resolved.value, source: `${DIALOG_MODE.envName}=${resolved.value}` };
  }
  const ignored =
    resolved.ignoredEnv === undefined ? "" : ` (${DIALOG_MODE.envName}="${resolved.ignoredEnv.toLowerCase()}" names no mode and was ignored)`;

  if (resolved.value) {
    return { mode: resolved.value, source: `dialog.mode in ${getUserStatePath()}${ignored}` };
  }

  return opts.canElicit
    ? { mode: "interactive", source: `default, because this MCP client advertised elicitation${ignored}` }
    : {
        mode: "defer",
        source:
          "default, because this MCP client did not advertise elicitation, and the fallback is defer" +
          ` rather than auto${ignored}`,
      };
}
