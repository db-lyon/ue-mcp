/**
 * Material function nodes are addressable by name.
 *
 * add_expression_in_function labels any node with `name`, and
 * connect_expressions_in_function resolves that name. A name used to read as
 * index 0, so every name-based connection targeted the first node.
 */
import { afterAll, describe, expect, it } from "vitest";
import { closeLiveBridges, liveBridge, liveTarget } from "./harness.js";

await liveTarget();

const PACKAGE = "/Game/MCP_LiveMaterialFunctionRefs";
const FUNCTION = `${PACKAGE}/MF_Refs`;

type Result = Record<string, unknown> & { success?: boolean; error?: string };

async function call(method: string, params: Record<string, unknown>): Promise<Result> {
  const bridge = await liveBridge();
  return (await bridge.call(method, params, 30_000)) as Result;
}

afterAll(async () => {
  await call("delete_asset", { assetPath: FUNCTION, force: true }).catch(() => undefined);
  await closeLiveBridges();
});

describe("material function node references", () => {
  it("connects nodes by the names they were given", async () => {
    await call("delete_asset", { assetPath: FUNCTION, force: true }).catch(() => undefined);
    const created = await call("create_material_function", { name: "MF_Refs", packagePath: PACKAGE });
    expect(created.success, created.error).toBe(true);

    const alpha = await call("add_expression_in_function", { functionPath: FUNCTION, expressionType: "Constant", name: "Alpha" });
    const beta = await call("add_expression_in_function", { functionPath: FUNCTION, expressionType: "Multiply", name: "Beta" });
    expect(alpha.success, alpha.error).toBe(true);
    expect(beta.success, beta.error).toBe(true);

    const listed = await call("list_expressions_in_function", { functionPath: FUNCTION });
    const descriptions = (listed.expressions as Array<{ description: string }>).map((e) => e.description);
    expect(descriptions).toEqual(["Alpha", "Beta"]);

    const byName = await call("connect_expressions_in_function", {
      functionPath: FUNCTION,
      sourceExpression: "Alpha",
      targetExpression: "Beta",
      targetInput: "A",
    });
    expect(byName.success, byName.error).toBe(true);
  });

  it("refuses a name that matches no node instead of using the first one", async () => {
    const missing = await call("connect_expressions_in_function", {
      functionPath: FUNCTION,
      sourceExpression: "NoSuchNode",
      targetExpression: "Beta",
      targetInput: "B",
    });
    expect(missing.success).toBe(false);
    expect(missing.error).toContain("sourceExpression not found");
  });
});
