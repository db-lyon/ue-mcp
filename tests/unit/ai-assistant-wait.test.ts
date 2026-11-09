import { describe, expect, it } from "vitest";
import type { ToolContext } from "../../src/core/types.js";
import { epicTool } from "../../src/tools/epic.js";
import { waitForAssistant, type WaitClock } from "../../src/tools/ai-assistant-wait.js";

/** A bridge that answers epic_ai_assistant_get_request from a script, one answer per poll (the last repeats). */
function scripted(answers: Array<Record<string, unknown>>) {
  const calls: Array<{ method: string; params: Record<string, unknown> | undefined }> = [];
  let i = 0;
  const ctx = {
    bridge: {
      call: async (method: string, params?: Record<string, unknown>) => {
        calls.push({ method, params });
        const answer = answers[Math.min(i, answers.length - 1)];
        i++;
        return answer;
      },
    },
  } as unknown as ToolContext;
  return { ctx, calls };
}

function fakeClock(): WaitClock {
  let t = 0;
  return { now: () => t, sleep: async (ms) => { t += ms; } };
}

const pending = { success: true, state: "pending", requestId: 7, function: "SendTextMessageAndGetReply", changed: true, rollbackPossible: false };

describe("AI Assistant wait", () => {
  it("polls the request until it completes and keeps the send's own fields", async () => {
    const { ctx, calls } = scripted([
      { success: true, state: "pending", requestId: 7 },
      { success: true, state: "complete", requestId: 7, reply: "Hello", conversationId: "c1" },
    ]);
    const result = await waitForAssistant(300, fakeClock())(ctx, { message: "Hi" }, pending);
    expect(result).toMatchObject({ success: true, state: "complete", reply: "Hello", conversationId: "c1", changed: true, rollbackPossible: false, polls: 2 });
    expect(calls.every((c) => c.method === "epic_ai_assistant_get_request" && c.params?.requestId === 7)).toBe(true);
  });

  it("returns a failed request as the failure the bridge reported", async () => {
    const { ctx } = scripted([{ success: false, state: "failed", errorCode: "ai_assistant_failed", assistantError: "stopped" }]);
    const result = await waitForAssistant(300, fakeClock())(ctx, {}, pending);
    expect(result).toMatchObject({ success: false, state: "failed", errorCode: "ai_assistant_failed" });
  });

  it("fails with timed_out after timeoutSeconds and keeps the requestId", async () => {
    const { ctx } = scripted([{ success: true, state: "pending", requestId: 7 }]);
    const result = await waitForAssistant(300, fakeClock())(ctx, { timeoutSeconds: 3 }, pending) as Record<string, unknown>;
    expect(result).toMatchObject({ success: false, state: "timed_out", errorCode: "ai_assistant_timed_out", requestId: 7 });
    expect(String(result.error)).toContain("3s");
  });

  it("does not poll with wait=false, for a completed answer, or for a refusal", async () => {
    const { ctx, calls } = scripted([]);
    const wait = waitForAssistant(300, fakeClock());
    expect(await wait(ctx, { wait: false }, pending)).toBe(pending);
    const complete = { success: true, state: "complete", requestId: 1, reply: "x" };
    expect(await wait(ctx, {}, complete)).toBe(complete);
    const refused = { success: false, errorCode: "unsupported_engine_version" };
    expect(await wait(ctx, {}, refused)).toBe(refused);
    expect(calls).toHaveLength(0);
  });

  it("reports a request the bridge no longer holds as failed", async () => {
    const { ctx } = scripted([{ success: false, errorCode: "ai_assistant_request_not_found" }]);
    const result = await waitForAssistant(300, fakeClock())(ctx, {}, pending);
    expect(result).toMatchObject({ success: false, state: "failed", errorCode: "ai_assistant_request_not_found" });
  });

  it("is wired to send_message and get_conversation, and only those", () => {
    const actions = epicTool.actions as Record<string, { followUp?: unknown; bridge?: string }>;
    expect(typeof actions.ai_assistant_send_message.followUp).toBe("function");
    expect(typeof actions.ai_assistant_get_conversation.followUp).toBe("function");
    expect(actions.ai_assistant_get_request.followUp).toBeUndefined();
    expect(actions.ai_assistant_send_message.bridge).toBe("epic_ai_assistant_send_message");
  });
});
