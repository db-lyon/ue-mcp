import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, checkFeature, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
let hasNiagara = false;

beforeAll(async () => {
  bridge = await getBridge();
  hasNiagara = await checkFeature(bridge, "Niagara");
});
afterAll(async () => {
  if (hasNiagara) {
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/NS_SmokeTest` });
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/NE_SmokeTest` });
  }
  disconnectBridge();
});

describe("niagara — read / list", () => {
  it("list_niagara_systems", async ({ skip }) => {
    if (!hasNiagara) skip();
    const r = await callBridge(bridge, "list_niagara_systems", { recursive: true });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_niagara_modules", async ({ skip }) => {
    if (!hasNiagara) skip();
    const r = await callBridge(bridge, "list_niagara_modules");
    expect(r.ok, r.error).toBe(true);
  });
});

describe("niagara — create (with cleanup)", () => {
  it("create_niagara_system", async ({ skip }) => {
    if (!hasNiagara) skip();
    const r = await callBridge(bridge, "create_niagara_system", {
      name: "NS_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("get_niagara_info", async ({ skip }) => {
    if (!hasNiagara) skip();
    const r = await callBridge(bridge, "get_niagara_info", {
      assetPath: `${TEST_PREFIX}/NS_SmokeTest`,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_emitters_in_system", async ({ skip }) => {
    if (!hasNiagara) skip();
    const r = await callBridge(bridge, "list_emitters_in_system", {
      systemPath: `${TEST_PREFIX}/NS_SmokeTest`,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_niagara_emitter", async ({ skip }) => {
    if (!hasNiagara) skip();
    const r = await callBridge(bridge, "create_niagara_emitter", {
      name: "NE_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });
});
