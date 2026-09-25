import { describe, expect, it } from "vitest";
import { parseParamsClause } from "../../src/action-schema.js";
import { materialTool } from "../../src/tools/material.js";

describe("material function expression editing (#1138)", () => {
  it("documents functionPath on the expression edit actions", () => {
    for (const action of ["set_expression_value", "set_custom_expression", "delete_expression"]) {
      const params = parseParamsClause(materialTool.actions[action].description ?? "").map((p) => p.name);
      expect(params, `${action} documents functionPath`).toContain("functionPath");
      expect(params, `${action} still documents materialPath`).toContain("materialPath");
    }
    expect(materialTool.schema.functionPath.safeParse("/Game/MF_Example").success).toBe(true);
  });

  it("forwards functionPath through set_custom_expression's param map", () => {
    expect(materialTool.actions.set_custom_expression.mapParams?.({
      functionPath: "/Game/MF_Example",
      expressionIndex: 2,
      code: "return 1;",
    })).toMatchObject({ functionPath: "/Game/MF_Example", expressionIndex: 2, code: "return 1;" });

    expect(materialTool.actions.set_custom_expression.mapParams?.({
      materialFunctionPath: "/Game/MF_Alias",
      expressionIndex: 0,
    })).toMatchObject({ functionPath: "/Game/MF_Alias" });
  });

  it("passes functionPath straight through the unmapped actions", () => {
    expect(materialTool.actions.set_expression_value.mapParams).toBeUndefined();
    expect(materialTool.actions.delete_expression.mapParams).toBeUndefined();
  });
});
