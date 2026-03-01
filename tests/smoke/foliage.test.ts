import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("foliage — read", () => {
  it("list_foliage_types", async () => {
    const r = await callBridge(bridge, "list_foliage_types");
    expect(r.ok, r.error).toBe(true);
  });

  it("sample_foliage", async () => {
    const r = await callBridge(bridge, "sample_foliage", {
      center: { x: 0, y: 0, z: 0 }, radius: 10000,
    });
    expect(r.ok, r.error).toBe(true);
  });
});
