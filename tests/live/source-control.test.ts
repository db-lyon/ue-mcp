// @live-editor headless
/**
 * Source control category (#1357), against the test project's editor.
 *
 * tests/ue_mcp runs with no revision control provider, so this covers the
 * contract every action keeps there: get_provider reports the state instead
 * of failing, and every other action refuses once with
 * source_control_not_enabled rather than an error per file. The checkout
 * mode is per editor session and is written and read back.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;
let providerEnabled = false;

beforeAll(async () => {
  bridge = await getBridge();
  const provider = await callBridge(bridge, "source_control_get_provider", {});
  expect(provider.ok, provider.error).toBe(true);
  providerEnabled = body(provider).enabled === true;
}, 60_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "source_control_set_checkout_mode", { mode: "manual" });
    disconnectBridge();
  }
});

async function expectRefused(method: string, params: Json): Promise<void> {
  const refused = await callBridge(bridge, method, params);
  expect(refused.ok, refused.error).toBe(true);
  expect(body(refused).success).toBe(false);
  expect(body(refused).errorCode).toBe("source_control_not_enabled");
}

describe("source control with no provider (#1357)", () => {
  it("get_provider reports the provider state instead of failing", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    const provider = await callBridge(bridge, "source_control_get_provider", {});
    expect(body(provider).success).toBe(true);
    expect(body(provider).enabled).toBe(false);
    expect(body(provider).checkoutMode).toMatch(/^(manual|auto)$/);
  });

  it("get_status refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_get_status", { paths: ["/Game"] });
  });

  it("list_changes refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_list_changes", {});
  });

  it("checkout refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_checkout", { paths: ["/Game/MCPTest"] });
  });

  it("add refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_add", { paths: ["/Game/MCPTest"] });
  });

  it("revert refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_revert", { paths: ["/Game/MCPTest"], dryRun: true });
  });

  it("diff refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_diff", { path: "Config/DefaultEngine.ini" });
  });

  it("submit refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_submit", { paths: ["Config/DefaultEngine.ini"], description: "never submitted" });
  });

  it("mark_for_delete refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_mark_for_delete", { paths: ["Config/DefaultEngine.ini"] });
  });

  it("sync refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_sync", { paths: ["Config/DefaultEngine.ini"] });
  });

  it("history refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_history", { path: "Config/DefaultEngine.ini" });
  });

  it("connect refuses once with source_control_not_enabled", async (ctx) => {
    if (providerEnabled) ctx.skip("a provider is configured in this editor");
    await expectRefused("source_control_connect", {});
  });

  it("set_checkout_mode switches the session mode and reports the previous one", async () => {
    await callBridge(bridge, "source_control_set_checkout_mode", { mode: "manual" });
    const set = await callBridge(bridge, "source_control_set_checkout_mode", { mode: "auto" });
    expect(set.ok, set.error).toBe(true);
    expect(body(set).previousMode).toBe("manual");
    const provider = await callBridge(bridge, "source_control_get_provider", {});
    expect(body(provider).checkoutMode).toBe("auto");
  });
});
