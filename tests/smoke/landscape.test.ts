import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
let hasLandscape = false;

beforeAll(async () => {
  bridge = await getBridge();
  const r = await callBridge(bridge, "get_landscape_info");
  hasLandscape = r.ok && r.result != null && !(r.result as any)?.error;
});
afterAll(() => disconnectBridge());

describe("landscape — read (requires landscape in level)", () => {
  it("get_landscape_info", async ({ skip }) => {
    if (!hasLandscape) skip();
    const r = await callBridge(bridge, "get_landscape_info");
    expect(r.ok, r.error).toBe(true);
  });

  it("list_landscape_layers", async ({ skip }) => {
    if (!hasLandscape) skip();
    const r = await callBridge(bridge, "list_landscape_layers");
    expect(r.ok, r.error).toBe(true);
  });

  it("sample_landscape", async ({ skip }) => {
    if (!hasLandscape) skip();
    const r = await callBridge(bridge, "sample_landscape", { point: { x: 0, y: 0, z: 0 } });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_landscape_splines", async ({ skip }) => {
    if (!hasLandscape) skip();
    const r = await callBridge(bridge, "list_landscape_splines");
    expect(r.ok, r.error).toBe(true);
  });

  it("get_landscape_component", async ({ skip }) => {
    if (!hasLandscape) skip();
    const r = await callBridge(bridge, "get_landscape_component", { componentIndex: 0 });
    expect(r.ok, r.error).toBe(true);
  });
});
