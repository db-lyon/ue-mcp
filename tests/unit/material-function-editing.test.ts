import { describe, expect, it } from "vitest";
import { parseParams } from "../../src/action-schema.js";
import { materialTool } from "../../src/tools/material.js";
import { handlerSpecs } from "../../src/tools/specs/material.generated.js";

describe("material function expression editing (#1138)", () => {
  it("documents functionPath on the expression edit actions", () => {
    for (const action of ["set_expression_value", "set_custom_expression", "delete_expression"]) {
      const params = parseParams(materialTool.actions[action].description ?? "").params.map((p) => p.name);
      expect(params, `${action} documents functionPath`).toContain("functionPath");
      expect(params, `${action} still documents materialPath`).toContain("materialPath");
    }
    expect(materialTool.schema.functionPath.safeParse("/Game/MF_Example").success).toBe(true);
  });

  it("passes functionPath straight through, with materialFunctionPath as the spec's alias (#1057)", () => {
    for (const [action, method] of [
      ["set_expression_value", "set_expression_value"],
      ["set_custom_expression", "set_custom_expression"],
      ["delete_expression", "delete_material_expression"],
    ]) {
      expect(materialTool.actions[action].mapParams, action).toBeUndefined();
      const functionPath = handlerSpecs[method].params.find((p) => p.name === "functionPath");
      expect(functionPath?.aliases, action).toEqual(["materialFunctionPath"]);
      expect(handlerSpecs[method].choices?.[0].branches, action).toEqual([["materialPath"], ["functionPath"]]);
    }
  });
});
