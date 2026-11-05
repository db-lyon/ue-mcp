import { describe, expect, it } from "vitest";
import { levelTool } from "../../src/tools/level.js";
import { pieTool } from "../../src/tools/pie.js";
import { declaredActionEffect } from "../../src/surface/action-effects.js";
import { z } from "zod";
import { paramZod } from "../../src/surface/handler-spec.js";
import type { ActionSpec } from "../../src/core/types.js";

function parseTraceParams(action: ActionSpec, params: Record<string, unknown>) {
  if (action.kind !== "bridge" || !action.paramSpec) throw new Error("Missing parameter spec");
  return z.object(Object.fromEntries(action.paramSpec.map((p) =>
    [p.name, p.required ? paramZod(p) : paramZod(p).optional()]))).parse(params);
}

describe("native view and shape traces", () => {
  it("exposes read actions with generated parameter specs and no remapping", () => {
    for (const [tool, name, method] of [
      [pieTool, "view_trace", "pie_view_trace"],
      [levelTool, "shape_trace", "shape_trace"],
    ] as const) {
      expect(tool.schema.action.safeParse(name).success).toBe(true);
      const action = tool.actions[name];
      expect(action.bridge).toBe(method);
      expect(action.mapParams).toBeUndefined();
      expect(declaredActionEffect(tool.name, name)).toBe("read");
      expect(action.kind === "bridge" && action.paramSpec?.length).toBeGreaterThan(0);
    }
  });

  it("accepts PIE player selection, renamed channels and optional sphere sweep", () => {
    const params = { playerIndex: 1, pieInstance: 2, distance: 5000, channel: "Dismantle",
      sweepRadius: 20, traceComplex: true, ignoreActors: ["Cable", "/Game/Map.Map:PersistentLevel.Pawn_0"], ignorePawn: false };
    expect(parseTraceParams(pieTool.actions.view_trace, params)).toEqual(params);
    expect(pieTool.schema.playerIndex.safeParse(0.5).success).toBe(false);
    expect(pieTool.schema.ignorePawn.safeParse("false").success).toBe(false);
  });

  it("accepts each shape's dimensions and both ray forms", () => {
    const start = { x: 0, y: 0, z: 100 };
    for (const dimensions of [
      { shape: "sphere", radius: 10 },
      { shape: "box", halfExtent: { x: 10, y: 20, z: 30 } },
      { shape: "capsule", radius: 10, halfHeight: 30 },
    ]) {
      const params = { ...dimensions, start, direction: { x: 1, y: 0, z: 0 }, distance: 1000,
        world: "pie", pieInstance: 2, multi: true, channel: "Visibility", traceComplex: false, ignoreActors: ["Player"] };
      expect(parseTraceParams(levelTool.actions.shape_trace, params)).toEqual(params);
      expect(parseTraceParams(levelTool.actions.shape_trace, { ...dimensions, start, end: start })).toEqual({ ...dimensions, start, end: start });
    }
    expect(levelTool.schema.halfExtent.safeParse({ x: 10 }).success).toBe(false);
    expect(() => parseTraceParams(levelTool.actions.shape_trace, { radius: 10, start })).toThrow();
  });

  it("exposes collision projection on component queries", () => {
    const params = { componentClass: "BoxComponent", actorLabelContains: "Cable", world: "pie",
      includeCollision: true, collisionChannel: "Dismantle" };
    expect(parseTraceParams(levelTool.actions.query_components, params)).toEqual(params);
    expect(levelTool.schema.includeCollision.safeParse(1).success).toBe(false);
    expect(levelTool.schema.collisionChannel.safeParse(1).success).toBe(false);
  });

  it("accepts multi on line traces and on each bulk item (#1564)", () => {
    const start = { x: 0, y: 0, z: 1100 };
    const end = { x: 0, y: 0, z: -500 };
    expect(parseTraceParams(levelTool.actions.line_trace, { start, end, multi: true })).toEqual({ start, end, multi: true });
    const bulk = { traces: [{ start, end, multi: true }, { start, end }], world: "editor" };
    expect(parseTraceParams(levelTool.actions.bulk_line_trace, bulk)).toEqual(bulk);
    expect(levelTool.schema.multi.safeParse("yes").success).toBe(false);
  });

});
