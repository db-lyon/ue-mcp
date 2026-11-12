/**
 * Waiting for Epic's AI Assistant (UE 5.9+).
 *
 * UAIAssistantInstance answers through a UToolCallAsyncResult that completes on later editor ticks, so the bridge
 * returns a requestId and the server polls epic_ai_assistant_get_request until the result is complete or failed.
 */
import type { ToolContext } from "../core/types.js";

/** Injectable for tests. */
export interface WaitClock {
  now(): number;
  sleep(ms: number): Promise<void>;
}

const realClock: WaitClock = {
  now: () => Date.now(),
  sleep: (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
};

type Body = Record<string, unknown>;

/**
 * The follow-up send_message and get_conversation run: unless wait=false, poll the request they opened until it
 * completes. A timeout fails the call and keeps the requestId, so the caller can poll get_request later.
 */
export function waitForAssistant(defaultTimeoutSeconds: number, clock: WaitClock = realClock) {
  return async (ctx: ToolContext, params: Record<string, unknown>, answered: unknown): Promise<unknown> => {
    if (params.wait === false || !answered || typeof answered !== "object") return answered;
    let body = answered as Body;
    if (body.success === false || body.state !== "pending" || typeof body.requestId !== "number") return answered;
    const requestId = body.requestId;
    const timeoutSeconds = typeof params.timeoutSeconds === "number" ? params.timeoutSeconds : defaultTimeoutSeconds;
    const timeoutMs = Math.max(0, timeoutSeconds * 1000);
    const started = clock.now();
    let pollMs = 250;
    let polls = 0;
    const settled = { ...body };

    for (;;) {
      if (clock.now() - started >= timeoutMs) {
        return {
          ...settled,
          success: false,
          state: "timed_out",
          errorCode: "ai_assistant_timed_out",
          error: `The AI Assistant did not answer within ${timeoutSeconds}s. The request is still open: poll get_request with requestId ${requestId}.`,
          polls,
        };
      }
      await clock.sleep(pollMs);
      pollMs = Math.min(pollMs * 2, 2000);
      polls++;
      body = (await ctx.bridge.call("epic_ai_assistant_get_request", { requestId })) as Body;
      if (body?.state !== "pending") {
        // Fields only the first answer carried (changed, rollbackNote) stay on the result.
        return { ...settled, ...body, state: body?.state ?? "failed", polls };
      }
    }
  };
}
