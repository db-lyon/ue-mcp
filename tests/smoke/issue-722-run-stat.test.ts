// Regression: #722 — run_stat name="unit" executed `stat fps` instead of `stat unit`.
// The handler only read "command" and defaulted to "stat fps"; a bare stat name
// passed as "name" was ignored.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("editor — run_stat name mapping (#722)", () => {
  it("name=unit resolves to 'stat unit'", async () => {
    const r = await callBridge(bridge, "run_stat_command", { name: "unit" });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as Record<string, unknown>).command).toBe("stat unit");
  });

  it("an explicit command is still honored verbatim", async () => {
    const r = await callBridge(bridge, "run_stat_command", { command: "stat gpu" });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as Record<string, unknown>).command).toBe("stat gpu");
  });

  it("no args still defaults to 'stat fps'", async () => {
    const r = await callBridge(bridge, "run_stat_command", {});
    expect(r.ok, r.error).toBe(true);
    expect((r.result as Record<string, unknown>).command).toBe("stat fps");
  });
});
