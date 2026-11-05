// @live-editor headless
/**
 * `editor(set_dialog_policy)` refusing a button label passed as the response (#1421).
 *
 * The shutdown Save Content prompt's button reads "Don't Save", and an agent naturally passes that as the response.
 * The refusal has to name the keyword that presses it and the buttonLabel spelling, and arm nothing.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const PATTERN = "MCPLive1421 never matches";

type Json = Record<string, unknown>;
let bridge: EditorBridge;

const policyPatterns = async (): Promise<string[]> => {
  const got = await callBridge(bridge, "get_dialog_policy", {});
  expect(got.ok, got.error).toBe(true);
  const policies = ((got.result as Json).policies ?? []) as Array<{ pattern: string }>;
  return policies.map((p) => p.pattern);
};

beforeAll(async () => {
  bridge = await getBridge();
});

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "clear_dialog_policy", { pattern: PATTERN });
  disconnectBridge();
});

describe("set_dialog_policy response refusal (#1421)", () => {
  it("names the keyword and the buttonLabel spelling for a button label, and arms nothing", async () => {
    const refused = await callBridge(bridge, "set_dialog_policy", { pattern: PATTERN, response: "Don't Save" });
    expect(refused.ok, refused.error).toBe(true);
    const body = refused.result as Json;
    expect(body.success).toBe(false);
    expect(body.error).toContain("response 'no' presses it");
    expect(body.error).toContain("buttonLabel 'Don't Save'");
    expect(await policyPatterns()).not.toContain(PATTERN);
  });

  it("points an unrelated value at buttonLabel", async () => {
    const refused = await callBridge(bridge, "set_dialog_policy", { pattern: PATTERN, response: "Frobnicate" });
    expect(refused.ok, refused.error).toBe(true);
    const body = refused.result as Json;
    expect(body.success).toBe(false);
    expect(body.error).toContain("pass buttonLabel instead");
  });

  it("arms the policy when the label is passed as buttonLabel", async () => {
    const armed = await callBridge(bridge, "set_dialog_policy", { pattern: PATTERN, buttonLabel: "Don't Save" });
    expect(armed.ok, armed.error).toBe(true);
    expect((armed.result as Json).success, JSON.stringify(armed.result)).toBe(true);
    expect(await policyPatterns()).toContain(PATTERN);
    await callBridge(bridge, "clear_dialog_policy", { pattern: PATTERN });
    expect(await policyPatterns()).not.toContain(PATTERN);
  });
});
