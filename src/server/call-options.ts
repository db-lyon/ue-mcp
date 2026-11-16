// @session independent: In-process call options, as type declarations. No runtime code.
import type { ElicitFn } from "../core/elicit.js";
import type { DialogPromptFn } from "../contract/daemon.js";

/** Options for one action run in process, by a daemon extension. */
export interface InProcessCallOptions {
  /**
   * Who a blocking dialog this call meets is put to, in place of an MCP
   * elicitation form. Without it nobody is asked, and a mode that wants a
   * person (interactive) applies as defer.
   */
  dialogPrompt?: DialogPromptFn;
  /**
   * How long this call waits on an open prompt before it returns the dialog
   * refusal, in milliseconds. The prompt stays up either way, and whatever it
   * answers later is still pressed. Default 60000.
   */
  dialogWaitMs?: number;
  /**
   * Who a form this call raises is put to, such as feedback approval, in place
   * of an MCP client's elicitation. Without it nothing is elicited, and a mode
   * that wants a person applies as it does for a client without elicitation.
   */
  elicit?: ElicitFn;
  /**
   * The access mode this call runs under. `read-only` refuses an action whose
   * declared effect is `mutate` or `unknown`, and every state-changing bridge
   * call it would make. Default read-write.
   */
  access?: "read-write" | "read-only";
}
