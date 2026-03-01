import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, checkFeature, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
let hasPCG = false;

beforeAll(async () => {
  bridge = await getBridge();
  hasPCG = await checkFeature(bridge, "PCG");
});
afterAll(async () => {
  if (hasPCG) {
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/PCG_SmokeTest` });
  }
  disconnectBridge();
});

describe("pcg — read / list", () => {
  it("list_pcg_graphs", async ({ skip }) => {
    if (!hasPCG) skip();
    const r = await callBridge(bridge, "list_pcg_graphs", { recursive: true });
    expect(r.ok, r.error).toBe(true);
  });

  it("get_pcg_components", async ({ skip }) => {
    if (!hasPCG) skip();
    const r = await callBridge(bridge, "get_pcg_components");
    expect(r.ok, r.error).toBe(true);
  });
});

describe("pcg — create (with cleanup)", () => {
  it("create_pcg_graph", async ({ skip }) => {
    if (!hasPCG) skip();
    const r = await callBridge(bridge, "create_pcg_graph", {
      name: "PCG_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("read_pcg_graph", async ({ skip }) => {
    if (!hasPCG) skip();
    const r = await callBridge(bridge, "read_pcg_graph", {
      assetPath: `${TEST_PREFIX}/PCG_SmokeTest`,
    });
    expect(r.ok, r.error).toBe(true);
  });
});
