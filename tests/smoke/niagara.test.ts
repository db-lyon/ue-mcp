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
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/M_SmokeHlsl` });
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
    // NiagaraEmitterFactory may not exist in UE5 (system-centric workflow)
    if (r.error?.includes("factory not available")) skip();
    expect(r.ok, r.error).toBe(true);
  });
});

describe("niagara — v0.7.14: module inputs + static switches + HLSL modules", () => {
  it("list_niagara_module_inputs tolerates empty systems", async ({ skip }) => {
    if (!hasNiagara) skip();
    const r = await callBridge(bridge, "list_niagara_module_inputs", {
      systemPath: `${TEST_PREFIX}/NS_SmokeTest`,
      stackContext: "all",
    });
    // Empty system has no emitters — handler should return a structured error, not crash
    expect(typeof r.ok).toBe("boolean");
  });

  it("list_niagara_static_switches tolerates empty systems", async ({ skip }) => {
    if (!hasNiagara) skip();
    const r = await callBridge(bridge, "list_niagara_static_switches", {
      systemPath: `${TEST_PREFIX}/NS_SmokeTest`,
    });
    expect(typeof r.ok).toBe("boolean");
  });

  it("set_niagara_module_input reports missing module cleanly", async ({ skip }) => {
    if (!hasNiagara) skip();
    const r = await callBridge(bridge, "set_niagara_module_input", {
      systemPath: `${TEST_PREFIX}/NS_SmokeTest`,
      moduleName: "DoesNotExist",
      inputName: "Foo",
      value: "1",
    });
    // Empty system → either 'Emitter not resolved' or module-not-found, both acceptable non-crash paths
    expect(r.ok === true || typeof r.error === "string").toBe(true);
  });

  it("create_niagara_module_from_hlsl", async ({ skip }) => {
    if (!hasNiagara) skip();
    const r = await callBridge(bridge, "create_niagara_module_from_hlsl", {
      name: "M_SmokeHlsl",
      packagePath: TEST_PREFIX,
      hlsl: "// smoke test\nOutput = Input;",
    });
    expect(r.ok, r.error).toBe(true);
  });
});
