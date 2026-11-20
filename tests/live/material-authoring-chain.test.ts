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
 * - set_custom_expression carries each wire to the input of the same name (#1907).
 * - A Custom node's whole spec goes on its build_material_graph node, applied
 *   before that call's connections (#1948).
 * - An export imported with replace into an empty material exports the same
 *   (#1951). material-graph-replace.test.ts covers a replace that does not
 *   compile, which needs an editor that compiles shaders.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { closeLiveBridges, liveBridge, liveTarget } from "./harness.js";

await liveTarget();

const DIR = "/Game/MCP_LiveMaterialAuthoring";
const MPC = `${DIR}/MPC_Chain`;
const M_MPC = `${DIR}/M_ChainMpc`;
const M_CUSTOM = `${DIR}/M_ChainCustom`;
const M_CUSTOM_BUILD = `${DIR}/M_ChainCustomBuild`;
const M_RT_SRC = `${DIR}/M_ChainRoundTripSource`;
const M_RT_DST = `${DIR}/M_ChainRoundTripTarget`;
const ALL_ASSETS = [M_RT_DST, M_RT_SRC, M_CUSTOM_BUILD, M_CUSTOM, M_MPC, MPC];

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
  await call("create_material", { name: "M_ChainCustom", packagePath: DIR });
  await call("create_material", { name: "M_ChainCustomBuild", packagePath: DIR });
  await call("create_material", { name: "M_ChainRoundTripSource", packagePath: DIR });
  await call("create_material", { name: "M_ChainRoundTripTarget", packagePath: DIR });
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

type InputRow = { inputName: string; connectedExpressionIndex?: number };
async function customInputs(materialPath: string, index: number): Promise<InputRow[]> {
  const listed = await call("list_material_expressions", { materialPath, includeInputs: true, limit: 100 });
  const row = (listed.expressions as Array<{ nodeId: string; inputs: InputRow[] }>).find((e) => e.nodeId === String(index));
  return row?.inputs ?? [];
}

describe("set_custom_expression keeps wires by input name (#1907)", () => {
  it("an input that keeps its name keeps its wire, and a dropped one is reported", async () => {
    const built = await call("build_material_graph", {
      assetPath: M_CUSTOM,
      nodes: [
        { name: "One", class: "Constant", value: 1 },
        { name: "Two", class: "Constant", value: 2 },
        { name: "Mix", class: "Custom" },
      ],
      propertyConnections: [{ property: "EmissiveColor", from: "Mix" }],
    });
    const nodes = built.nodes as Array<{ name: string; expressionName: string; expressionIndex: number }>;
    const mix = nodes.find((n) => n.name === "Mix")!;
    const one = nodes.find((n) => n.name === "One")!;
    await call("set_custom_expression", { materialPath: M_CUSTOM, expressionIndex: mix.expressionIndex, code: "return A + B;", inputs: ["A", "B"] });
    await call("build_material_graph", {
      assetPath: M_CUSTOM, nodes: [],
      connections: [{ from: "One", to: mix.expressionName, toInput: "A" }, { from: "Two", to: mix.expressionName, toInput: "B" }],
    });

    const edited = await call("set_custom_expression", {
      materialPath: M_CUSTOM, expressionIndex: mix.expressionIndex, code: "return A * C;", inputs: ["A", "C"],
    });
    expect(edited.keptConnections).toEqual(["A"]);
    expect((edited.disconnectedInputs as Json[]).map((d) => d.input)).toEqual(["B"]);
    expect(edited.rollbackLossy).toBe(true);

    const inputs = await customInputs(M_CUSTOM, mix.expressionIndex);
    expect(inputs.map((i) => i.inputName)).toEqual(["A", "C"]);
    expect(inputs[0].connectedExpressionIndex).toBe(one.expressionIndex);
    expect(inputs[1].connectedExpressionIndex ?? -1).toBe(-1);

    const wiped = await call("set_custom_expression", {
      materialPath: M_CUSTOM, expressionIndex: mix.expressionIndex, inputs: ["A", "C"], preserveConnections: false,
    });
    expect((wiped.disconnectedInputs as Json[]).map((d) => d.input)).toEqual(["A"]);
    expect((await customInputs(M_CUSTOM, mix.expressionIndex))[0].connectedExpressionIndex ?? -1).toBe(-1);
  }, 180_000);
});

describe("a Custom node is built whole in one call (#1948)", () => {
  it("build_material_graph sets code, inputs, outputs, defines and includes before wiring by input name", async () => {
    const built = await call("build_material_graph", {
      assetPath: M_CUSTOM_BUILD,
      nodes: [
        { name: "Base", class: "Constant3Vector", value: { r: 0.2, g: 0.4, b: 0.6 } },
        { name: "Gain", class: "Constant", value: 2 },
        {
          name: "Grade", class: "Custom",
          code: "Extra = InColor.r;\nreturn InColor * Amount * GRADE_SCALE;",
          description: "Grade", outputType: "float3",
          inputs: ["InColor", { name: "Amount" }],
          additionalOutputs: [{ name: "Extra", type: "float1" }],
          additionalDefines: [{ name: "GRADE_SCALE", value: "1.0" }, "GRADE_MODE=2"],
          includeFilePaths: ["/Engine/Private/Common.ush"],
        },
        { name: "Mask", class: "Multiply" },
      ],
      connections: [
        { from: "Base", to: "Grade", toInput: "InColor" },
        { from: "Gain", to: "Grade", toInput: "Amount" },
        { from: "Grade", fromOutput: "Extra", to: "Mask", toInput: "A" },
      ],
      propertyConnections: [{ property: "EmissiveColor", from: "Grade" }],
    });
    expect(built.wiresMade).toBe(3);
    const grade = (built.nodes as Array<{ name: string; expressionIndex: number }>).find((n) => n.name === "Grade")!;

    const read = await call("set_custom_expression", { materialPath: M_CUSTOM_BUILD, expressionIndex: grade.expressionIndex });
    expect(read.unchanged).toBe(true);
    expect(read.code).toContain("GRADE_SCALE");
    expect(read.description).toBe("Grade");
    expect(read.inputs).toEqual(["InColor", "Amount"]);
    expect(read.additionalOutputs).toEqual([{ name: "Extra", type: "float1" }]);
    expect(read.additionalDefines).toEqual([{ name: "GRADE_SCALE", value: "1.0" }, { name: "GRADE_MODE", value: "2" }]);
    expect(read.includeFilePaths).toEqual(["/Engine/Private/Common.ush"]);
    const inputs = await customInputs(M_CUSTOM_BUILD, grade.expressionIndex);
    expect(inputs.every((i) => typeof i.connectedExpressionIndex === "number" && i.connectedExpressionIndex >= 0)).toBe(true);

    const edited = await call("set_custom_expression", {
      materialPath: M_CUSTOM_BUILD, expressionIndex: grade.expressionIndex, includeFilePaths: [], additionalDefines: ["GRADE_SCALE=0.5"],
    });
    expect(edited.includeFilePaths).toEqual([]);
    expect(((edited.rollback as Json).payload as Json).includeFilePaths).toEqual(["/Engine/Private/Common.ush"]);
  }, 180_000);

  it("refuses Custom fields on a node of another class before anything is built", async () => {
    const refused = await attempt("build_material_graph", {
      assetPath: M_CUSTOM_BUILD, nodes: [{ name: "NotCustom", class: "Multiply", code: "return 1;" }],
    });
    expect(refused.success).toBe(false);
    expect(String(refused.error)).toContain("code applies to a Custom node");
  }, 120_000);
});

describe("export_material_graph and import_material_graph round-trip (#1951)", () => {
  const withoutPath = (exported: Json): Json => {
    const { assetPath: _path, success: _ok, ...rest } = exported;
    return rest;
  };
  let firstExport: Json = {};

  it("an export imported with replace into an empty material exports identically", async () => {
    await call("build_material_graph", {
      assetPath: M_RT_SRC,
      materialSettings: { TwoSided: true, bUsedWithSkeletalMesh: true, BlendMode: "BLEND_Masked" },
      nodes: [
        { name: "Bravo", class: "CollectionParameter", posX: -900, properties: { Collection: `${MPC}.MPC_Chain`, ParameterName: "Bravo" } },
        { name: "Tint", class: "VectorParameter", parameterName: "Tint", group: "Look", sortPriority: 3, defaultValue: { r: 0.1, g: 0.2, b: 0.3, a: 1 }, posX: -900, posY: 200 },
        { name: "UseTint", class: "StaticSwitchParameter", parameterName: "UseTint", properties: { DefaultValue: true }, posX: -600 },
        { name: "Red", class: "ComponentMask", channels: { r: true, g: false, b: false, a: false }, posX: -600, posY: 300 },
        {
          name: "Grade", class: "Custom", posX: -300,
          code: "Extra = InColor.g;\nreturn InColor * Amount;", outputType: "float3",
          inputs: ["InColor", "Amount"], additionalOutputs: [{ name: "Extra", type: "float1" }],
          additionalDefines: [{ name: "GRADE_MODE", value: "1" }],
        },
      ],
      connections: [
        { from: "Tint", to: "UseTint", toInput: "True" },
        { from: "Tint", to: "UseTint", toInput: "False" },
        { from: "UseTint", to: "Grade", toInput: "InColor" },
        { from: "Bravo", to: "Grade", toInput: "Amount" },
        { from: "Tint", to: "Red", toInput: "Input" },
      ],
      propertyConnections: [
        { property: "BaseColor", from: "Grade" },
        { property: "OpacityMask", from: "Grade", output: "Extra" },
        { property: "AmbientOcclusion", from: "Red" },
      ],
    });

    firstExport = await call("export_material_graph", { assetPath: M_RT_SRC });
    const nodes = firstExport.nodes as Json[];
    const custom = nodes.find((n) => n.class === "MaterialExpressionCustom")!;
    expect(custom.inputs).toEqual(["InColor", "Amount"]);
    expect(custom.additionalOutputs).toEqual([{ name: "Extra", type: "float1" }]);
    const collection = nodes.find((n) => n.class === "MaterialExpressionCollectionParameter")!;
    expect(String((collection.properties as Json).Collection)).toContain("MPC_Chain");
    expect((collection.properties as Json).ParameterName).toBe("Bravo");
    expect((nodes.find((n) => n.name === "UseTint")!.properties as Json).DefaultValue).toBe(true);
    expect((nodes.find((n) => n.name === "Tint")!.properties as Json).Group).toBe("Look");
    expect((firstExport.propertyConnections as Json[]).map((p) => p.property).sort()).toEqual(["AmbientOcclusion", "BaseColor", "OpacityMask"]);
    const settings = firstExport.materialSettings as Json;
    expect(settings.TwoSided).toBe(true);
    expect(settings.bUsedWithSkeletalMesh).toBe(true);
    expect(String(settings.BlendMode)).toContain("Masked");

    const imported = await call("import_material_graph", { assetPath: M_RT_DST, replace: true, ...withoutPath(firstExport) });
    expect(imported.replaced).toBe(true);
    expect(imported.compileErrors).toEqual([]);
    expect((imported.rollback as Json).method).toBe("import_material_graph");

    const secondExport = await call("export_material_graph", { assetPath: M_RT_DST });
    expect(withoutPath(secondExport)).toEqual(withoutPath(firstExport));
  }, 300_000);
});
