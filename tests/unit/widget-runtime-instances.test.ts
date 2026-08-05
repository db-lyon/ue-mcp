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

  it("keeps the editor world out of the runtime world scope", () => {
    for (const scope of ["pie", "game", "auto"]) {
      expect(widgetTool.schema.world.safeParse(scope).success).toBe(true);
    }
    expect(widgetTool.schema.world.safeParse("editor").success).toBe(false);
  });
});
