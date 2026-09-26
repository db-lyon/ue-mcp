import { describe, expect, it } from "vitest";
import { parseParams } from "../../src/action-schema.js";
import { materialTool } from "../../src/tools/material.js";
import { handlerSpecs } from "../../src/tools/specs/material.generated.js";

describe("material graph read surface", () => {
  it("publishes the graph read and optional expression-input schema", () => {
    expect(materialTool.schema.action.safeParse("read_graph").success).toBe(true);
    expect(materialTool.schema.includeInputs.safeParse(true).success).toBe(true);
    expect(materialTool.schema.includeInputs.safeParse(false).success).toBe(true);
    expect(materialTool.schema.includeInputs.safeParse(undefined).success).toBe(true);
    expect(materialTool.schema.includeInputs.safeParse("true").success).toBe(false);

    const readGraphParams = parseParams(materialTool.actions.read_graph.description ?? "").params.map((p) => p.name);
    expect(readGraphParams, "read_graph documents expressionIndex").toContain("expressionIndex");
    expect(materialTool.schema.expressionIndex.safeParse(3).success).toBe(true);

    for (const action of ["list_expressions", "read_graph"]) {
      const params = parseParams(materialTool.actions[action].description ?? "").params.map((p) => p.name);
      expect(params, `${action} documents cursor`).toContain("cursor");
      expect(params, `${action} documents limit`).toContain("limit");
    }
  });

  it("forwards the bag as sent, with the path aliases in the C++ spec (#1057)", () => {
    for (const [action, method] of [["list_expressions", "list_material_expressions"], ["read_graph", "read_material_graph"]] as const) {
      expect(materialTool.actions[action].mapParams, action).toBeUndefined();
      const params = handlerSpecs[method].params;
      const materialPath = params.find((p) => p.name === "materialPath");
      expect(materialPath?.required, method).toBe(true);
      expect(materialPath?.aliases, method).toEqual(["path", "assetPath"]);
      expect(params.map((p) => p.name), method).toEqual(expect.arrayContaining(["cursor", "limit"]));
    }
    expect(handlerSpecs.read_material_graph.params.map((p) => p.name)).toContain("expressionIndex");
    expect(handlerSpecs.list_material_expressions.params.map((p) => p.name)).toContain("includeInputs");
  });
});
