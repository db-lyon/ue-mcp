// @live-editor headless
/**
 * Material authoring through the graph-build path, each case read back from
 * the asset it changed.
 *
 * - build_material_graph runs each node property as a details-panel edit, so a
 *   CollectionParameter built from {Collection, ParameterName} carries the
 *   collection parameter's id (#1946).
 * - Collection parameters are added, renamed and removed by name, and each
 *   keeps the id its readers bind to, through set_property as well (#1950).
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { closeLiveBridges, liveBridge, liveTarget } from "./harness.js";

await liveTarget();

const DIR = "/Game/MCP_LiveMaterialAuthoring";
const MPC = `${DIR}/MPC_Chain`;
const M_MPC = `${DIR}/M_ChainMpc`;
const ALL_ASSETS = [M_MPC, MPC];

type Json = Record<string, unknown>;
type MpcParam = { name: string; id: string; defaultValue: unknown };

async function call(method: string, params: Json): Promise<Json> {
  const bridge = await liveBridge();
  const result = (await bridge.call(method, params, 120_000)) as Json;
  expect(result.success, `${method}: ${String(result.error ?? JSON.stringify(result))}`).not.toBe(false);
  return result;
}

async function attempt(method: string, params: Json): Promise<Json> {
  const bridge = await liveBridge();
  return (await bridge.call(method, params, 120_000)) as Json;
}

const normalizeGuid = (guid: unknown): string => String(guid).replace(/[^0-9a-f]/gi, "").toUpperCase();

async function scalarParams(): Promise<MpcParam[]> {
  return (await call("read_material_parameter_collection", { assetPath: MPC })).scalarParameters as MpcParam[];
}

async function nodeProperty(materialPath: string, expressionName: string, propertyName: string): Promise<string> {
  const assetName = materialPath.slice(materialPath.lastIndexOf("/") + 1);
  const read = await call("get_object_properties", {
    objectPath: `${materialPath}.${assetName}:${expressionName}`,
    propertyNames: [propertyName],
  });
  return String((read.properties as Json)[propertyName]);
}

beforeAll(async () => {
  for (const assetPath of ALL_ASSETS) await attempt("delete_asset", { assetPath, force: true });
  await call("create_asset_by_class", { name: "MPC_Chain", className: "MaterialParameterCollection", packagePath: DIR });
  await call("set_asset_property", { assetPath: MPC, propertyName: "ScalarParameters", value: [{ ParameterName: "Amount", DefaultValue: 0 }] });
  await call("create_material", { name: "M_ChainMpc", packagePath: DIR });
}, 180_000);

afterAll(async () => {
  for (const assetPath of ALL_ASSETS) await attempt("delete_asset", { assetPath, force: true }).catch(() => undefined);
  await closeLiveBridges();
}, 120_000);

describe("node properties run their edit notifications (#1946)", () => {
  it("a CollectionParameter built from Collection and ParameterName carries the parameter's id", async () => {
    const amount = (await scalarParams()).find((p) => p.name === "Amount");
    expect(amount, "the collection holds Amount").toBeDefined();

    // ParameterName before Collection: the id lookup must see both.
    const built = await call("build_material_graph", {
      assetPath: M_MPC,
      nodes: [{ name: "Amount", class: "CollectionParameter", properties: { ParameterName: "Amount", Collection: `${MPC}.MPC_Chain` } }],
      propertyConnections: [{ property: "EmissiveColor", from: "Amount" }],
    });
    const node = (built.nodes as Array<{ name: string; expressionName: string }>)[0];

    const parameterId = await nodeProperty(M_MPC, node.expressionName, "ParameterId");
    expect(normalizeGuid(parameterId)).not.toMatch(/^0+$/);
    expect(normalizeGuid(parameterId)).toBe(normalizeGuid(amount!.id));
  }, 180_000);
});

describe("collection parameters are edited by name (#1950)", () => {
  it("set_collection_parameter adds parameters with fresh ids and refuses a duplicate name", async () => {
    const added = await call("set_collection_parameter", { assetPath: MPC, parameterName: "Bravo", defaultValue: 2 });
    expect(added.created).toBe(true);
    expect((added.rollback as Json).method).toBe("remove_collection_parameter");
    const tint = await call("set_collection_parameter", { assetPath: MPC, parameterName: "Tint", type: "vector", defaultValue: { r: 1, g: 0.5, b: 0, a: 1 } });
    expect((tint.parameter as Json).type).toBe("vector");

    const read = await call("read_material_parameter_collection", { assetPath: MPC });
    const scalars = read.scalarParameters as MpcParam[];
    expect(scalars.map((p) => p.name)).toEqual(["Amount", "Bravo"]);
    expect(new Set(scalars.map((p) => normalizeGuid(p.id))).size).toBe(2);
    expect(scalars.find((p) => p.name === "Bravo")?.defaultValue).toBeCloseTo(2, 3);
    const vector = (read.vectorParameters as MpcParam[]).find((p) => p.name === "Tint");
    expect((vector?.defaultValue as { g: number }).g).toBeCloseTo(0.5, 3);

    const duplicate = await attempt("set_collection_parameter", { assetPath: MPC, parameterName: "Bravo", newName: "Tint" });
    expect(duplicate.success).toBe(false);
    expect(String(duplicate.error)).toContain("already has a parameter named 'Tint'");
  }, 180_000);

  it("set_property re-sending the scalar list in another order keeps each id with its name", async () => {
    const before = new Map((await scalarParams()).map((p) => [p.name, normalizeGuid(p.id)]));
    await call("set_asset_property", {
      assetPath: MPC, propertyName: "ScalarParameters",
      value: [{ ParameterName: "Bravo", DefaultValue: 2 }, { ParameterName: "Amount", DefaultValue: 0 }],
    });
    const after = await scalarParams();
    expect(after.map((p) => p.name)).toEqual(["Bravo", "Amount"]);
    for (const p of after) expect(normalizeGuid(p.id)).toBe(before.get(p.name));
  }, 180_000);

  it("a rename keeps the id the material's node reads, and a removal names that node and rolls back under the same id", async () => {
    const amountId = normalizeGuid((await scalarParams()).find((p) => p.name === "Amount")!.id);
    const renamed = await call("set_collection_parameter", { assetPath: MPC, parameterName: "Amount", newName: "Alpha", defaultValue: 0.25 });
    expect(normalizeGuid((renamed.parameter as Json).id)).toBe(amountId);
    expect((renamed.readers as Json[]).some((r) => String(r.assetPath).includes("M_ChainMpc"))).toBe(true);
    const alpha = (await scalarParams()).find((p) => p.name === "Alpha");
    expect(alpha?.defaultValue).toBeCloseTo(0.25, 3);

    const removed = await call("remove_collection_parameter", { assetPath: MPC, parameterName: "Alpha" });
    expect((removed.readers as Json[]).map((r) => String(r.assetPath))).toEqual([expect.stringContaining("M_ChainMpc")]);
    expect((await scalarParams()).map((p) => p.name)).toEqual(["Bravo"]);

    const rollback = removed.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("set_collection_parameter");
    await call(rollback.method, rollback.payload);
    const restored = (await scalarParams()).find((p) => p.name === "Alpha");
    expect(normalizeGuid(restored?.id)).toBe(amountId);
  }, 180_000);
});
