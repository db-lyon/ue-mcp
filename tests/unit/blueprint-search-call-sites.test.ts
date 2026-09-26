import { describe, expect, it, vi } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import { declaredActionEffect } from "../../src/action-effects.js";
import type { ToolContext } from "../../src/types.js";

describe("blueprint.search_call_sites (#945)", () => {
  it("forwards every narrowing, bounding and dump parameter to the native handler", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await blueprintTool.handler(ctx, {
      action: "search_call_sites",
      functionNames: ["FinishExecute", "FinishAbort"],
      className: "/Script/AIModule.BTTask_BlueprintBase",
      directory: "/Game/AI",
      includeNestedGraphs: false,
      includeLevelScripts: true,
      includeNeighbours: true,
      narrowByRegistry: false,
      offset: 200,
      limit: 50,
      maxBlueprints: 500,
      dumpToFile: true,
      outputPath: "UE_MCP/audit.json",
    });

    expect(call).toHaveBeenCalledWith(
      "search_blueprint_call_sites",
      {
        functionNames: ["FinishExecute", "FinishAbort"],
        className: "/Script/AIModule.BTTask_BlueprintBase",
        directory: "/Game/AI",
        includeNestedGraphs: false,
        includeLevelScripts: true,
        includeNeighbours: true,
        narrowByRegistry: false,
        offset: 200,
        limit: 50,
        maxBlueprints: 500,
        dumpToFile: true,
        outputPath: "UE_MCP/audit.json",
      },
      undefined,
    );
  });

  it("sends only functionNames when nothing else is given, so the handler defaults apply", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await blueprintTool.handler(ctx, {
      action: "search_call_sites",
      functionNames: ["ApplyDamage"],
    });

    const [, params] = call.mock.calls[0];
    expect(params.functionNames).toEqual(["ApplyDamage"]);
    for (const key of ["className", "directory", "includeNestedGraphs", "includeLevelScripts", "narrowByRegistry", "limit", "maxBlueprints"]) {
      expect(params[key]).toBeUndefined();
    }
  });

  it("declares its parameters with the right types", () => {
    expect(blueprintTool.schema.functionNames.safeParse(["A", "B"]).success).toBe(true);
    expect(blueprintTool.schema.functionNames.safeParse("A").success).toBe(false);
    expect(blueprintTool.schema.maxBlueprints.safeParse(500).success).toBe(true);
    expect(blueprintTool.schema.maxBlueprints.safeParse(0).success).toBe(false);
    expect(blueprintTool.schema.narrowByRegistry.safeParse(false).success).toBe(true);
    expect(blueprintTool.schema.includeNeighbours.safeParse(true).success).toBe(true);
    expect(blueprintTool.schema.includeLevelScripts.safeParse(true).success).toBe(true);
    expect(blueprintTool.schema.directory.safeParse("/Game/AI").success).toBe(true);
  });

  it("declares itself a read even though 'call' is a mutate verb", () => {
    // It loads packages and reads graphs; it authors nothing.
    expect(declaredActionEffect("blueprint", "search_call_sites")).toBe("read");
  });
});
