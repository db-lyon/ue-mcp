import { describe, expect, it, vi } from "vitest";
import { widgetTool } from "../../src/tools/widget.js";

describe("widget inspect_runtime_instances", () => {
  it("forwards multi-instance filters and reflected property selection", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    await widgetTool.handler(
      { bridge: { call } } as never,
      {
        action: "inspect_runtime_instances",
        classFilter: "Hero",
        propertyNames: ["MemberID", "BuffDynamic"],
        includeSubtree: true,
        childClassFilter: "BuffSlot",
        world: "pie",
        pieInstance: 2,
        maxInstances: 8,
      },
    );

    expect(call).toHaveBeenCalledWith("inspect_runtime_instances", {
      widgetName: undefined,
      classFilter: "Hero",
      propertyNames: ["MemberID", "BuffDynamic"],
      includeSubtree: true,
      childName: undefined,
      childClassFilter: "BuffSlot",
      viewportOnly: undefined,
      world: "pie",
      pieInstance: 2,
      maxInstances: 8,
      maxNodesPerInstance: undefined,
    }, undefined);
  });

  it("exposes bounded multi-client schema fields", () => {
    expect(widgetTool.schema.propertyNames.safeParse(["MemberID"]).success).toBe(true);
    expect(widgetTool.schema.pieInstance.safeParse(1).success).toBe(true);
    expect(widgetTool.schema.maxInstances.safeParse(501).success).toBe(false);
    expect(widgetTool.schema.maxNodesPerInstance.safeParse(0).success).toBe(false);
  });

  it("advertises the runtime world scopes without gating on them", () => {
    for (const scope of ["pie", "game", "auto"]) {
      expect(widgetTool.schema.world.safeParse(scope).success).toBe(true);
    }
    // A strict enum here would be validated by the MCP SDK BEFORE the tool
    // callback runs, so a misspelled scope came back as a transport schema dump
    // instead of the handler's own answer. The handler refuses anything that
    // does not resolve to a live PIE/Game world and names the scopes it takes,
    // so the parse has to let the value through for that message to be reached.
    expect(widgetTool.schema.world.safeParse("editor").success).toBe(true);
    expect(widgetTool.schema.world.safeParse("pei").success).toBe(true);
    expect(widgetTool.schema.world.safeParse(7).success).toBe(false);
    const described = widgetTool.schema.world.description ?? "";
    for (const scope of ["pie", "game", "auto"]) {
      expect(described).toContain(scope);
    }
  });
});
