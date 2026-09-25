import { describe, expect, it } from "vitest";
import { parseParamsClause } from "../../src/action-schema.js";
import { materialTool } from "../../src/tools/material.js";

describe("material designer layer stacks (#1131)", () => {
  const cases = [
    ["create_designer", "create_material_designer", "mutate"],
    ["read_designer", "read_material_designer", "read"],
    ["set_designer_value", "set_material_designer_value", "mutate"],
    ["add_designer_layer", "add_material_designer_layer", "mutate"],
    ["remove_designer_layer", "remove_material_designer_layer", "mutate"],
  ] as const;

  it.each(cases)("routes %s to %s", (action, bridge, effect) => {
    const spec = materialTool.actions[action];
    expect(spec.kind === "bridge" && spec.bridge).toBe(bridge);
    expect(spec.effect).toBe(effect);
  });

  it("declares every documented parameter so the MCP layer does not strip it", () => {
    for (const [action] of cases) {
      const names = parseParamsClause(materialTool.actions[action].description ?? "").map((p) => p.name);
      for (const name of names) expect(materialTool.schema[name], `${action}: ${name}`).toBeDefined();
    }
  });

  it("accepts a slot index or a material property name for designerSlot", () => {
    const schema = materialTool.schema.designerSlot;
    expect(schema.safeParse(0).success).toBe(true);
    expect(schema.safeParse("Base").success).toBe(true);
  });
});
