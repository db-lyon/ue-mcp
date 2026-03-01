import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, checkFeature, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
let hasGAS = false;

beforeAll(async () => {
  bridge = await getBridge();
  hasGAS = await checkFeature(bridge, "GameplayAbilities");
});
afterAll(async () => {
  if (hasGAS) {
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/GA_SmokeTest` });
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/GE_SmokeTest` });
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/AS_SmokeTest` });
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/GC_SmokeTest` });
  }
  disconnectBridge();
});

describe("gas — query", () => {
  it("get_gas_info (no target, should handle gracefully)", async ({ skip }) => {
    if (!hasGAS) skip();
    const r = await callBridge(bridge, "get_gas_info", { blueprintPath: "/Game/NonExistent" });
    expect(r.method).toBe("get_gas_info");
  });
});

describe("gas — create (with cleanup)", () => {
  it("create_gameplay_ability", async ({ skip }) => {
    if (!hasGAS) skip();
    const r = await callBridge(bridge, "create_gameplay_ability", {
      name: "GA_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_gameplay_effect", async ({ skip }) => {
    if (!hasGAS) skip();
    const r = await callBridge(bridge, "create_gameplay_effect", {
      name: "GE_SmokeTest", packagePath: TEST_PREFIX, durationPolicy: "Instant",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_attribute_set", async ({ skip }) => {
    if (!hasGAS) skip();
    const r = await callBridge(bridge, "create_attribute_set", {
      name: "AS_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_gameplay_cue", async ({ skip }) => {
    if (!hasGAS) skip();
    const r = await callBridge(bridge, "create_gameplay_cue", {
      name: "GC_SmokeTest", packagePath: TEST_PREFIX, cueType: "Static",
    });
    expect(r.ok, r.error).toBe(true);
  });
});
