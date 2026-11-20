/**
 * Bounds on the request ids a caller names for PIE input and remote-player requests.
 *
 * A caller may choose the id of a hold or tape (injection_id) and reads a relayed request back by id
 * (request_id). The editor keeps every id it is sent in its own maps and relays it to the remote player's
 * process, so an id of any length is refused here before it leaves this process. The editor enforces the
 * same bound (FMCPPIERemoteReports::MaxIdLength in MCPPIERemoteControl.h); keep the two equal.
 */
import type { BridgeActionSpec, ToolContext } from "../surface/tool-types.js";

/** Longest id a caller may choose or name. Equal to FMCPPIERemoteReports::MaxIdLength. */
export const MAX_REQUEST_ID_LENGTH = 128;

/** The parameters that carry a request id, in the spellings a call may use. */
const ID_PARAMS = ["injection_id", "request_id"] as const;

// Control characters end up in the editor's log lines and in the remote player's.
// eslint-disable-next-line no-control-regex
const CONTROL_CHARS = /[\u0000-\u001f\u007f]/;

/** Why the id in `params[key]` is refused, or null when it is acceptable or absent. */
export function requestIdProblem(key: string, value: unknown): string | null {
  if (typeof value !== "string") return null;
  if (value.length > MAX_REQUEST_ID_LENGTH) {
    return `${key} is ${value.length} characters; an id is at most ${MAX_REQUEST_ID_LENGTH}. `
      + "Use the id a call returned, or leave injection_id out to have one minted.";
  }
  if (CONTROL_CHARS.test(value)) {
    return `${key} contains a control character; use the id a call returned, or leave injection_id out to have one minted.`;
  }
  return null;
}

/** Throws the first refusal among the id parameters of a call; returns undefined so the call goes ahead. */
export async function interceptRequestIds(_ctx: ToolContext, params: Record<string, unknown>): Promise<undefined> {
  for (const key of ID_PARAMS) {
    const problem = requestIdProblem(key, params[key]);
    if (problem) throw new Error(problem);
  }
  return undefined;
}

/** The action with its request ids checked before the call is sent. */
export function boundedRequestIds(spec: BridgeActionSpec): BridgeActionSpec {
  return { ...spec, intercept: interceptRequestIds };
}
