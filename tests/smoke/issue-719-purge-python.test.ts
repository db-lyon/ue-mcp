// Regression: #719 - no native way to purge cached embedded-Python modules by
// prefix, so tool-dev iteration required execute_python.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, resultArray } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("editor - purge_python_modules (#719)", () => {
  it("purges modules matching a prefix and reports them", async () => {
    // Seed a fake cached module.
    await callBridge(bridge, "execute_python", {
      code: [
        "import sys, types",
        "sys.modules['mcp_test_purge_alpha'] = types.ModuleType('mcp_test_purge_alpha')",
        "sys.modules['mcp_test_purge_beta'] = types.ModuleType('mcp_test_purge_beta')",
      ].join("\n"),
    });
    const r = await callBridge(bridge, "purge_python_modules", { prefix: "mcp_test_purge" });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    const purged = (resultArray(result, "purged") ?? []).map(String);
    expect(purged).toContain("mcp_test_purge_alpha");
    expect(purged).toContain("mcp_test_purge_beta");
    expect(Number(result.count)).toBeGreaterThanOrEqual(2);
  });

  it("rejects an empty prefix", async () => {
    const r = await callBridge(bridge, "purge_python_modules", { prefix: "" });
    const result = r.result as Record<string, unknown> | undefined;
    const failed = !r.ok || (result != null && result.success === false);
    expect(failed).toBeTruthy();
  });
});
