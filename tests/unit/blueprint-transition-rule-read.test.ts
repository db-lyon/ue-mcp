import { describe, expect, it, vi } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import { handlerSpecs } from "../../src/tools/specs/blueprint.generated.js";
import type { ToolContext } from "../../src/surface/tool-types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

describe("blueprint.read_graph on AnimBP transition rules (#1321)", () => {
  it("addresses a rule graph by transition guid or state pair", async () => {
    const names = handlerSpecs.read_blueprint_graph.params.map((p) => p.name);
    expect(names).toEqual(expect.arrayContaining(["transitionGuid", "fromState", "toState", "stateMachineName", "includeLinks"]));
    expect(blueprintTool.actions.read_graph.description).toContain("transitionGuid");

    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = { assetPath: "/Game/ABP_Hero", transitionGuid: "0123456789ABCDEF0123456789ABCDEF", includeLinks: true };
    await blueprintTool.handler(ctx, { action: "read_graph", ...params });
    expect(call.mock.calls[0][0]).toBe("read_blueprint_graph");
    expect(call.mock.calls[0][1]).toMatchObject(params);
  });

  it("refuses an ambiguous pair and reports property access paths", () => {
    const source = readHandlerFile("BlueprintHandlers_Graph.cpp");
    expect(source).toContain("transitions go from '%s' to '%s': %s. Pass transitionGuid or stateMachineName.");
    expect(source).toContain('NodeObj->SetStringField(TEXT("propertyAccessPath")');
    expect(source).toContain("NodeOptions.bLinks = bIncludeLinks;");
  });
});
