// Regression: #730 - audio.list dropped the bridge connection and ignored the
// directory. list_sound_assets now filters by directory, paginates, and returns
// count/total/hasMore so a large project cannot overflow one response.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("audio - list_sound_assets directory + pagination (#730)", () => {
  it("returns pagination metadata and keeps the bridge alive", async () => {
    const r = await callBridge(bridge, "list_sound_assets", { directory: "/Game", recursive: true });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(Array.isArray(result.assets)).toBe(true);
    expect(typeof result.total).toBe("number");
    expect(typeof result.hasMore).toBe("boolean");
    expect(result.directory).toBe("/Game");
    // Bridge is still usable after the (potentially large) list.
    const still = await callBridge(bridge, "get_build_status");
    expect(still.ok, still.error).toBe(true);
  });

  it("honors maxResults as a page cap", async () => {
    const r = await callBridge(bridge, "list_sound_assets", { directory: "/Game", maxResults: 1 });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect((result.assets as unknown[]).length).toBeLessThanOrEqual(1);
    expect(result.maxResults).toBe(1);
  });

  it("scopes results to a non-existent directory (empty page, no error)", async () => {
    const r = await callBridge(bridge, "list_sound_assets", { directory: "/Game/__NoSuchDir__" });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect((result.assets as unknown[]).length).toBe(0);
  });
});
