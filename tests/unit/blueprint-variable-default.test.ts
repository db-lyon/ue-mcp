import { describe, expect, it, vi } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import { declaredActionEffect } from "../../src/surface/action-effects.js";
import type { ToolContext } from "../../src/core/types.js";

describe("blueprint.get_variable_default (#902)", () => {
  it("forwards assetPath and the variable name unchanged", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await blueprintTool.handler(ctx, {
      action: "get_variable_default",
      assetPath: "/Game/Test/BP_Test",
      name: "TestHealth",
    });

    expect(call).toHaveBeenCalledWith(
      "get_blueprint_variable_default",
      { assetPath: "/Game/Test/BP_Test", name: "TestHealth" },
      undefined,
    );
  });

  it("is a read, so a multi-editor caller is not forced to name a target for it", () => {
    expect(declaredActionEffect("blueprint", "get_variable_default")).toBe("read");
  });
});

describe("blueprint.list_variables includeValues (#902)", () => {
  it("declares includeValues and forwards it", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    expect(blueprintTool.schema.includeValues.safeParse(true).success).toBe(true);
    expect(blueprintTool.schema.includeValues.safeParse("yes").success).toBe(false);

    await blueprintTool.handler(ctx, {
      action: "list_variables",
      assetPath: "/Game/Test/BP_Test",
      includeValues: true,
    });

    expect(call).toHaveBeenCalledWith(
      "list_blueprint_variables",
      { assetPath: "/Game/Test/BP_Test", includeValues: true },
      undefined,
    );
  });

  it("leaves includeValues undefined when the caller does not ask, so the default payload is unchanged", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await blueprintTool.handler(ctx, {
      action: "list_variables",
      assetPath: "/Game/Test/BP_Test",
    });

    expect(call).toHaveBeenCalledWith(
      "list_blueprint_variables",
      { assetPath: "/Game/Test/BP_Test" },
      undefined,
    );
  });
});
