// Regression: #725 — Enhanced Input trigger subobjects could not be authored via
// set_mapping_modifiers (only {type} worked, {class} rejected), and a failed
// shape could leave a null Triggers entry that trips AssetCheck on save.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, resultArray, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  for (const a of ["IA_TrigTest", "IMC_TrigTest"]) {
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/${a}` }).catch(() => {});
  }
  disconnectBridge();
});

describe("gameplay — Enhanced Input triggers (#725)", () => {
  beforeAll(async () => {
    await callBridge(bridge, "create_input_action", { name: "IA_TrigTest", packagePath: TEST_PREFIX });
    await callBridge(bridge, "create_input_mapping_context", { name: "IMC_TrigTest", packagePath: TEST_PREFIX });
    await callBridge(bridge, "add_imc_mapping", {
      imcPath: `${TEST_PREFIX}/IMC_TrigTest`,
      inputActionPath: `${TEST_PREFIX}/IA_TrigTest`,
      key: "SpaceBar",
    });
  });

  it("constructs a Hold trigger via full class path with its property", async () => {
    const r = await callBridge(bridge, "set_mapping_modifiers", {
      imcPath: `${TEST_PREFIX}/IMC_TrigTest`,
      mappingIndex: 0,
      triggers: [{ class: "/Script/EnhancedInput.InputTriggerHold", HoldTimeThreshold: 1.0 }],
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(Number(result.triggerCount)).toBe(1);
    expect(result.failedTriggers).toBeUndefined();

    const read = await callBridge(bridge, "read_imc", { imcPath: `${TEST_PREFIX}/IMC_TrigTest` });
    const mappings = (resultArray(read.result, "mappings") ?? []) as Array<Record<string, unknown>>;
    const triggers = (mappings[0]?.triggers ?? []) as unknown[];
    expect(triggers.length).toBe(1);
    // No null entry landed in the array.
    expect(triggers.every((t) => t != null)).toBe(true);
  });

  it("constructs a Hold trigger via short type name", async () => {
    const r = await callBridge(bridge, "set_mapping_modifiers", {
      imcPath: `${TEST_PREFIX}/IMC_TrigTest`,
      mappingIndex: 0,
      triggers: [{ type: "Hold", HoldTimeThreshold: 0.5 }],
    });
    expect(r.ok, r.error).toBe(true);
    expect(Number((r.result as Record<string, unknown>).triggerCount)).toBe(1);
  });

  it("reports an unresolvable trigger instead of appending a null entry", async () => {
    const r = await callBridge(bridge, "set_mapping_modifiers", {
      imcPath: `${TEST_PREFIX}/IMC_TrigTest`,
      mappingIndex: 0,
      triggers: [{ type: "NotARealTrigger" }],
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(Number(result.triggerCount)).toBe(0);
    expect((resultArray(result, "failedTriggers") ?? []).length).toBeGreaterThanOrEqual(1);
  });
});
