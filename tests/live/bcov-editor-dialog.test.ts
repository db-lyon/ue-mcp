// @live-editor headless
/**
 * Dialog policies: set_dialog_policy, get_dialog_policy and clear_dialog_policy,
 * read back through get_dialog_policy. The pattern matches no real prompt.
 * Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const PATTERN = "BCov_Editor_NoSuchDialog_7f3a";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function policyFor(pattern: string): Promise<Json | undefined> {
  const listed = await call("get_dialog_policy", {});
  return (listed.policies as Json[]).find((p) => p.pattern === pattern);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "clear_dialog_policy", { pattern: PATTERN });
}, 60_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "clear_dialog_policy", { pattern: PATTERN });
    disconnectBridge();
  }
});

describe("dialog policies", () => {
  it("set_dialog_policy arms a policy get_dialog_policy lists and clear_dialog_policy removes", async () => {
    expect(await policyFor(PATTERN)).toBeUndefined();

    const armed = await call("set_dialog_policy", { pattern: PATTERN, response: "cancel" });
    expect(armed.pattern).toBe(PATTERN);
    expect(armed.response).toBe("cancel");

    const listed = await call("get_dialog_policy", {});
    expect(listed.hookInstalled).toBe(true);
    const policy = await policyFor(PATTERN);
    expect(policy?.response).toBe("cancel");
    expect(policy?.source).toBe("caller");

    const cleared = await call("clear_dialog_policy", { pattern: PATTERN });
    expect(cleared.removed).toBe(1);
    expect((cleared.cleared as Json[])[0].response).toBe("cancel");
    expect(await policyFor(PATTERN)).toBeUndefined();

    const again = await call("clear_dialog_policy", { pattern: PATTERN });
    expect(again.unchanged).toBe(true);
  }, 60_000);
});
