import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, checkFeature, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
let hasMetaSounds = false;

beforeAll(async () => {
  bridge = await getBridge();
  hasMetaSounds = await checkFeature(bridge, "MetaSounds");
});
afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/SC_SmokeTest` });
  if (hasMetaSounds) {
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/MS_SmokeTest` });
  }
  disconnectBridge();
});

describe("audio — read", () => {
  it("list_sound_assets", async () => {
    const r = await callBridge(bridge, "list_sound_assets", { recursive: true });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("audio — create (with cleanup)", () => {
  it("create_sound_cue", async () => {
    const r = await callBridge(bridge, "create_sound_cue", {
      name: "SC_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_metasound_source", async ({ skip }) => {
    if (!hasMetaSounds) skip();
    const r = await callBridge(bridge, "create_metasound_source", {
      name: "MS_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });
});
