// Regression: #718 - no native action closed the open Level Sequence editor.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/LS_CloseTest` }).catch(() => {});
  disconnectBridge();
});

describe("editor - close_sequence (#718)", () => {
  it("closes an open sequence and reports it", async () => {
    await callBridge(bridge, "create_level_sequence", { name: "LS_CloseTest", packagePath: TEST_PREFIX });
    await callBridge(bridge, "open_asset", { assetPath: `${TEST_PREFIX}/LS_CloseTest` });
    const r = await callBridge(bridge, "close_sequence");
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(typeof result.wasOpen).toBe("boolean");
  });

  it("is a no-op (no error) when nothing is open", async () => {
    const r = await callBridge(bridge, "close_sequence");
    expect(r.ok, r.error).toBe(true);
    expect((r.result as Record<string, unknown>).wasOpen).toBe(false);
  });
});
