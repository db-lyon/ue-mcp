// @live-editor headless
/**
 * Epic's AI Assistant API (UAIAssistantInstance, UE 5.9+), reached by reflection.
 *
 * Before 5.9 the class does not exist and every call refuses with
 * unsupported_engine_version; on 5.9 with the AI Assistant plugin off, with
 * PLUGIN_NOT_AVAILABLE. Those exact refusals are asserted there. With the
 * plugin on, the calls run for real; sending a message and reading the reply
 * needs the editor's Epic sign-in, so that half runs when
 * UE_MCP_AI_ASSISTANT_SIGNED_IN=1.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const SIGNED_IN = process.env.UE_MCP_AI_ASSISTANT_SIGNED_IN === "1";
const SUBJECT = "Epic's AI Assistant API (UAIAssistantInstance)";

let bridge: EditorBridge;
let status: Json = {};

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

function before59(version: string): boolean {
  const [major, minor] = version.split(".").map(Number);
  return major === 5 && minor < 9;
}

/** The refusal every call gives while the class is absent, matched to the letter. */
async function expectRefused(method: string, params: Json): Promise<void> {
  const r = await callBridge(bridge, method, params);
  const refused = body(r);
  expect(succeeded(r), JSON.stringify(refused)).toBe(false);
  const version = String(status.engineVersion);
  if (before59(version)) {
    expect(refused.errorCode).toBe("unsupported_engine_version");
    expect(refused.error).toBe(`${SUBJECT} requires Unreal Engine 5.9 or newer. This editor is ${version}`);
  } else {
    expect(refused.errorCode).toBe("PLUGIN_NOT_AVAILABLE");
    expect(refused.error).toBe(
      `AI Assistant plugin not available: ${SUBJECT} is not loaded. Enable it with project(enable_plugin, pluginName="AIAssistant") and restart the editor.`,
    );
  }
  expect(refused.requiredPlugin).toBe("AIAssistant");
  expect(refused.requiredClass).toBe("/Script/AIAssistant.AIAssistantInstance");
  expect(refused.engineVersion).toBe(version);
}

/** Poll get_request until the request leaves pending. */
async function settle(requestId: number, timeoutMs: number): Promise<Json> {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const r = await callBridge(bridge, "epic_ai_assistant_get_request", { requestId });
    const answer = body(r);
    if (answer.state !== "pending") return answer;
    if (Date.now() > deadline) throw new Error(`request ${requestId} still pending after ${timeoutMs}ms`);
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
}

beforeAll(async () => {
  bridge = await getBridge();
  status = body(await callBridge(bridge, "epic_ai_assistant_status", {}));
}, 60_000);

afterAll(() => {
  if (bridge) disconnectBridge();
});

describe("epic AI Assistant", () => {
  it("epic_ai_assistant_status reports whether UAIAssistantInstance is loaded, and why not", async () => {
    const status = await call("epic_ai_assistant_status", {});
    const version = String(status.engineVersion);
    expect(version).toMatch(/^\d+\.\d+$/);
    if (status.available === true) {
      expect(before59(version)).toBe(false);
      expect(status.functions).toEqual(["Get", "SendTextMessage", "SendTextMessageAndGetReply", "GetConversationJson"]);
      expect(status.missingFunctions).toEqual([]);
      expect(typeof status.listening).toBe("boolean");
      expect(typeof status.pendingRequests).toBe("number");
      return;
    }
    expect(status.available).toBe(false);
    expect(status.requiredPlugin).toBe("AIAssistant");
    if (before59(version)) {
      expect(status.errorCode).toBe("unsupported_engine_version");
      expect(status.reason).toBe(`${SUBJECT} requires Unreal Engine 5.9 or newer. This editor is ${version}`);
    } else {
      expect(status.errorCode).toBe("PLUGIN_NOT_AVAILABLE");
    }
  }, 60_000);

  it("epic_ai_assistant_get_request refuses a requestId that is not open", async () => {
    const r = await callBridge(bridge, "epic_ai_assistant_get_request", { requestId: 987654321 });
    const refused = body(r);
    expect(succeeded(r)).toBe(false);
    expect(refused.errorCode).toBe("ai_assistant_request_not_found");
    expect(refused.requestId).toBe(987654321);
    expect(refused.error).toBe(
      "No AI Assistant request 987654321 is open. A request is released once its complete or failed state has been returned.",
    );
  }, 60_000);

  it("epic_ai_assistant_get_events lists recorded conversation updates, or refuses without the API", async () => {
    if (status.available !== true) {
      await expectRefused("epic_ai_assistant_get_events", {});
      return;
    }
    const events = await call("epic_ai_assistant_get_events", {});
    expect(typeof events.listening).toBe("boolean");
    expect(Array.isArray(events.events)).toBe(true);
    expect(typeof events.nextSeq).toBe("number");
    const later = await call("epic_ai_assistant_get_events", { sinceSeq: events.nextSeq });
    expect(later.events).toEqual([]);
  }, 60_000);

  it("epic_ai_assistant_send_message and get_conversation drive the assistant, or refuse without the API", async () => {
    if (status.available !== true) {
      await expectRefused("epic_ai_assistant_send_message", { message: "Hello", wait: false });
      await expectRefused("epic_ai_assistant_send_message", { message: "Hello", reply: false, wait: false });
      await expectRefused("epic_ai_assistant_get_conversation", { wait: false });
      return;
    }
    if (!SIGNED_IN) {
      // Answers need the editor's Epic sign-in; without it only the API surface is checked (status test).
      expect(status.missingFunctions).toEqual([]);
      return;
    }

    // SendTextMessage: a new conversation, resolved with its id.
    const sent = await call("epic_ai_assistant_send_message", { message: "Reply with the single word: pong", reply: false, wait: false });
    expect(sent.changed).toBe(true);
    expect(sent.rollbackPossible).toBe(false);
    const bound = sent.state === "pending" ? await settle(Number(sent.requestId), 120_000) : sent;
    expect(bound.state, JSON.stringify(bound)).toBe("complete");
    const conversationId = String(bound.conversationId);
    expect(conversationId.length).toBeGreaterThan(0);

    // GetConversationJson: the conversation the message landed in holds it.
    const read = await call("epic_ai_assistant_get_conversation", { conversationId, wait: false });
    const conversation = read.state === "pending" ? await settle(Number(read.requestId), 60_000) : read;
    expect(conversation.state, JSON.stringify(conversation)).toBe("complete");
    expect(JSON.stringify(conversation.conversation)).toContain("pong");

    // SendTextMessageAndGetReply: the same conversation, resolved with the assistant's text.
    const asked = await call("epic_ai_assistant_send_message", { message: "Reply with the single word: ping", conversationId, wait: false });
    const replied = asked.state === "pending" ? await settle(Number(asked.requestId), 300_000) : asked;
    expect(replied.state, JSON.stringify(replied)).toBe("complete");
    expect(String(replied.reply).length).toBeGreaterThan(0);
    expect(replied.conversationId).toBe(conversationId);

    // OnConversationUpdated: the reply's generation was recorded against that conversation.
    const events = await call("epic_ai_assistant_get_events", { conversationId });
    expect(events.listening).toBe(true);
    const types = (events.events as Json[]).map((e) => e.updateType);
    expect(types).toContain("Complete");
  }, 600_000);
});
