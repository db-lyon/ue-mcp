import { describe, expect, it, vi } from "vitest";
import { materialTool } from "../../src/tools/material.js";
import { handlerSpecs } from "../../src/tools/specs/material.generated.js";
import type { ToolContext } from "../../src/core/types.js";

describe("material insertion and filtered summary", () => {
  it("refuses ambiguous targets before dispatch", async () => {
    const call = vi.fn();
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const request = { action: "insert_expression_between", materialPath: "/Game/M", expressionType: "Multiply" };
    await expect(materialTool.handler(ctx, request)).rejects.toThrow();
    await expect(materialTool.handler(ctx, { ...request, targetExpression: "Make", property: "BaseColor" })).rejects.toThrow();
    expect(call).not.toHaveBeenCalled();
  });

  it("forwards named attribute inputs and node properties intact", async () => {
    const reply = { success: true, expressionName: "Inserted", previousSource: { expressionName: "Tint", outputIndex: 2 } };
    const call = vi.fn().mockResolvedValue(reply);
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = { materialPath: "/Game/M", targetExpression: "Make", targetInput: "BaseColor", expressionType: "Multiply", inputForUpstream: "A", properties: { ConstB: 0.5 } };
    await materialTool.handler(ctx, { action: "insert_expression_between", ...params });
    expect(call.mock.calls[0].slice(0, 2)).toEqual(["insert_expression_between", params]);
    expect(materialTool.actions.insert_expression_between.effect).toBe("mutate");
    expect(handlerSpecs.insert_expression_between.choices).toEqual([{ mode: "exactlyOne", branches: [["targetExpression"], ["property"]] }]);
  });

  it("forwards summary filters and bounds the shape of the chain request", async () => {
    const call = vi.fn().mockResolvedValue({ success: true, countsByClass: { MaterialExpressionMultiply: 1 }, nodes: [] });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const params = { materialPath: "/Game/M", classes: ["Multiply"], parameterNames: ["Tint"], propertyChain: { targetExpression: "Make", targetInput: "BaseColor", depthLimit: 3 } };
    await materialTool.handler(ctx, { action: "read_summary", ...params });
    expect(call.mock.calls[0].slice(0, 2)).toEqual(["read_material_summary", params]);
    expect(materialTool.actions.read_summary.effect).toBe("read");
    expect(materialTool.schema.propertyChain.safeParse({ depthLimit: 1.5 }).success).toBe(false);
    expect(materialTool.schema.parameterNames.safeParse([42]).success).toBe(false);
  });
});
