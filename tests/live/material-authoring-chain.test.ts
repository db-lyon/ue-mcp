// @live-editor headless
/**
 * Material authoring through the graph-build path, each case read back from
 * the asset it changed.
 *
 * - build_material_graph runs each node property as a details-panel edit, so a
 *   CollectionParameter built from {Collection, ParameterName} carries the
 *   collection parameter's id (#1946).
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
