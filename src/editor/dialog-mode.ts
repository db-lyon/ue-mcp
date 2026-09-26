import { getDialogMode, getUserStatePath, type DialogMode } from "../config/user-state.js";
import type { ElicitFn } from "../core/types.js";
import { readEnv } from "../core/env.js";

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
 * Resolve the dialog handling mode. Precedence (highest wins), the same shape
 * the feedback approval mode uses:
 *
 *   1. UE_MCP_DIALOG_MODE env var         - per-process override
 *   2. ~/.ue-mcp/state.json, this project - per-project, `dialog.mode`
 *   3. ~/.ue-mcp/state.json preference    - per-user-per-device, `dialog.mode`
 *   4. default: "interactive" when the connected client advertised MCP
 *      elicitation, otherwise "defer"
 *
 * THE DEFAULT NEVER RESOLVES TO "auto". A dialog is a question for a person,
 * and with no channel to that person the safe answer is to suspend and say so,
 * never to let something decide it because asking was inconvenient. "auto" is
 * reachable only by being named, in the env var or in the stored preference.
 *
 * An unrecognised env value is ignored rather than guessed at, and the fact
 * that it was ignored travels in `source` so a typo does not silently change
 * how dialogs are handled.
 *
 * Mode is NOT read from ue-mcp.yml, for the reason the feedback mode is not:
 * whether a person is at the keyboard to answer a modal is a property of the
 * machine and the session, not project policy a collaborator should inherit.
 */
export function resolveDialogMode(opts: {
  projectDir?: string | null;
  /** Whether the connected MCP client advertised the elicitation capability. */
  canElicit: boolean;
  env?: NodeJS.ProcessEnv;
}): ResolvedDialogMode {
  const raw = (readEnv("dialogMode", opts.env) ?? "").trim().toLowerCase();
  if (raw === "interactive" || raw === "auto" || raw === "defer") {
    return { mode: raw, source: `UE_MCP_DIALOG_MODE=${raw}` };
  }
  const ignored = raw === "" ? "" : ` (UE_MCP_DIALOG_MODE="${raw}" names no mode and was ignored)`;

  const stored = getDialogMode(opts.projectDir ?? null);
  if (stored) {
    return { mode: stored, source: `dialog.mode in ${getUserStatePath()}${ignored}` };
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
