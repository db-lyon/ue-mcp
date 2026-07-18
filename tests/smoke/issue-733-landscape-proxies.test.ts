// Regression: #733 — no native way to enumerate loaded World Partition landscape
// streaming proxies or resolve which proxy covers a world position.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("landscape — World Partition proxies (#733)", () => {
  it("list_proxies returns the shape even with zero proxies", async () => {
    const r = await callBridge(bridge, "list_landscape_proxies");
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(typeof result.loadedProxies).toBe("number");
    expect(typeof result.parentLandscapes).toBe("number");
    expect(Array.isArray(result.proxies)).toBe(true);
  });

  it("find_proxy_at returns a loaded/found verdict", async () => {
    const r = await callBridge(bridge, "find_landscape_proxy_at", { worldX: 0, worldY: 0 });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(typeof result.found).toBe("boolean");
    expect(typeof result.loaded).toBe("boolean");
  });

  it("find_proxy_at requires a position", async () => {
    const r = await callBridge(bridge, "find_landscape_proxy_at", {});
    const result = r.result as Record<string, unknown> | undefined;
    const failed = !r.ok || (result != null && result.success === false);
    expect(failed).toBeTruthy();
  });
});
