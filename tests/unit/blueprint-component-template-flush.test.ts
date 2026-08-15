import { describe, expect, it, vi } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import type { ToolContext } from "../../src/types.js";

describe("blueprint.flush_component_templates", () => {
  it("routes one Blueprint path to the native maintenance handler", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await blueprintTool.handler(ctx, {
      action: "flush_component_templates",
      assetPath: "/Game/Blueprints/BP_Actor",
    });

    expect(call).toHaveBeenCalledWith(
      "flush_blueprint_component_templates",
      { path: "/Game/Blueprints/BP_Actor" },
      120_000,
    );
  });
});
