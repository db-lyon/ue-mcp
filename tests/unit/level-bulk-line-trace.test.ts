import { describe, expect, it } from "vitest";
import { declaredActionEffect } from "../../src/surface/action-effects.js";
import { levelTool } from "../../src/tools/level.js";

describe("level.bulk_line_trace", () => {
  it("is exposed through the level action schema", () => {
    expect(levelTool.schema.action.safeParse("bulk_line_trace").success).toBe(true);
  });

  it("accepts a bounded traces batch with the same fields as line_trace", () => {
    const traces = [
      { start: { x: 0, y: 0, z: 100 }, end: { x: 0, y: 0, z: 0 } },
      {
        start: { x: 0, y: 0, z: 100 },
        direction: { x: 0, y: 0, z: -1 },
        distance: 200000,
        traceComplex: true,
        channel: "Visibility",
        ignoreActors: ["Player"],
      },
    ];
    expect(levelTool.schema.traces.safeParse(traces).success).toBe(true);
  });

  it("rejects malformed entries; the handler refuses an empty or oversized batch", () => {
    expect(levelTool.schema.traces.safeParse([{ start: "origin" }]).success).toBe(false);
    expect(levelTool.schema.traces.safeParse([{ end: { x: 0, y: 0, z: 1 } }]).success).toBe(false);
  });

  it("takes its parameters from its spec and forwards the bag as sent", () => {
    const action = levelTool.actions.bulk_line_trace;
    expect(action.bridge).toBe("bulk_line_trace");
    expect(action.mapParams).toBeUndefined();
    expect(action.kind === "bridge" && action.paramSpec?.map((p) => p.name)).toEqual(["traces", "world", "pieInstance"]);
  });

  it("documents the cap and ordered per-item contract", () => {
    const description = levelTool.actions.bulk_line_trace.description ?? "";
    expect(description).toContain("traces");
    expect(description).toContain("256");
    expect(description).toMatch(/order/);
  });

  it("is a read, like line_trace", () => {
    expect(declaredActionEffect("level", "bulk_line_trace")).toBe("read");
  });
});
