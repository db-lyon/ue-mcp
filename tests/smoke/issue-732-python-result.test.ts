// Regression: #732 — execute_python / run_python_file had no first-class result
// channel, forcing print() as transport. resultVariable returns a named
// top-level variable as `result`, separate from logs.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("editor — execute_python result channel (#732)", () => {
  it("returns a named variable as `result` without using print()", async () => {
    const r = await callBridge(bridge, "execute_python", {
      code: "mcp_result = 'finished-scoring-82-assets'",
      resultVariable: "mcp_result",
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.result).toBe("finished-scoring-82-assets");
    expect(result.resultVariableResolved).toBe(true);
    // No print() was used, so the diagnostic output stays empty.
    expect(String(result.output ?? "")).toBe("");
  });

  it("reports resultVariableResolved=false for an undefined variable", async () => {
    const r = await callBridge(bridge, "execute_python", {
      code: "x = 1",
      resultVariable: "does_not_exist",
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.resultVariableResolved).toBe(false);
  });
});
