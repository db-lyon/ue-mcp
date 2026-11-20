// @live-editor render
/**
 * import_material_graph with replace saves only a graph that compiles (#1951).
 * A -nullrhi editor compiles no shaders, so this file needs a rendering one.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { closeLiveBridges, liveBridge, liveTarget } from "./harness.js";

await liveTarget();

const DIR = "/Game/MCP_LiveMaterialReplace";
const M = `${DIR}/M_Replace`;

type Json = Record<string, unknown>;

async function attempt(method: string, params: Json): Promise<Json> {
  const bridge = await liveBridge();
  return (await bridge.call(method, params, 300_000)) as Json;
}

async function call(method: string, params: Json): Promise<Json> {
  const result = await attempt(method, params);
  expect(result.success, `${method}: ${String(result.error ?? JSON.stringify(result))}`).not.toBe(false);
  return result;
}

const graphOf = async (): Promise<Json> => {
  const { assetPath: _path, success: _ok, ...rest } = await call("export_material_graph", { assetPath: M });
  return rest;
};

beforeAll(async () => {
  await attempt("delete_asset", { assetPath: M, force: true });
  await call("create_material", { name: "M_Replace", packagePath: DIR });
}, 180_000);

afterAll(async () => {
  await attempt("delete_asset", { assetPath: M, force: true }).catch(() => undefined);
  await closeLiveBridges();
}, 120_000);

describe("import_material_graph replace (#1951)", () => {
  it("replaces the graph when the spec compiles, and its rollback restores the previous one", async () => {
    await call("build_material_graph", {
      assetPath: M,
      nodes: [{ name: "Half", class: "Constant", value: 0.5 }],
      propertyConnections: [{ property: "Roughness", from: "Half" }],
    });
    const original = await graphOf();

    const replaced = await call("import_material_graph", {
      assetPath: M, replace: true,
      nodes: [{ name: "Red", class: "Constant3Vector", value: { r: 1, g: 0, b: 0 } }],
      propertyConnections: [{ property: "BaseColor", from: "Red" }],
    });
    expect(replaced.compileChecked).toBe(true);
    expect(replaced.compileErrors).toEqual([]);
    const after = await graphOf();
    expect((after.nodes as Json[]).map((n) => n.name)).toEqual(["Red"]);

    const rollback = replaced.rollback as { method: string; payload: Json };
    await call(rollback.method, rollback.payload);
    expect(await graphOf()).toEqual(original);
  }, 300_000);

  it("a spec that does not compile leaves the material as it was and returns the errors", async () => {
    const before = await graphOf();
    const broken = await attempt("import_material_graph", {
      assetPath: M, replace: true,
      nodes: [{ name: "Loose", class: "Custom", code: "return Missing;", outputType: "float3", inputs: ["Missing"] }],
      propertyConnections: [{ property: "BaseColor", from: "Loose" }],
    });
    expect(broken.success).toBe(false);
    expect((broken.compileErrors as string[]).length).toBeGreaterThan(0);
    expect(await graphOf()).toEqual(before);
  }, 300_000);
});
