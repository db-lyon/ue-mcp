// Regression: #727 - no native action opened editor settings/UI tabs, so
// producing visual settings evidence required the Python escape hatch.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("editor - open_tab / open_settings (#727)", () => {
  it("open_tab opens a registered tab by ID", async () => {
    const r = await callBridge(bridge, "open_tab", { tabId: "OutputLog" });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as Record<string, unknown>).opened).toBe(true);
  });

  it("open_tab reports failure for an unknown tab", async () => {
    const r = await callBridge(bridge, "open_tab", { tabId: "NoSuchTabXYZ" });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as Record<string, unknown>).opened).toBe(false);
  });

  it("open_settings navigates to a Project Settings section", async () => {
    const r = await callBridge(bridge, "open_settings", { section: "Engine.Physics" });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.category).toBe("Engine");
    expect(result.section).toBe("Physics");
  });
});
