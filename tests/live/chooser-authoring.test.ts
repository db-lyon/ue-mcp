// @live-editor headless
/**
 * Chooser table authoring beyond rows (#1828), each action read back through
 * describe, list_rows, get_cell, get_property, get_signature or list_nested:
 * type discovery, the signature and parameters, property paths, column and
 * row restructuring, cells and the fallback, auto populate, nested choosers,
 * and the parameters added to the original row and column actions.
 * Refs #1828
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/ChooserAuthoring";
const NAME = "CT_Authoring";
const OTHER_NAME = "CT_AuthoringTarget";
const TABLE = `${DIR}/${NAME}`;
const OTHER = `${DIR}/${OTHER_NAME}`;
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const SPHERE = "/Engine/BasicShapes/Sphere.Sphere";
const CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder";
const CONE = "/Engine/BasicShapes/Cone.Cone";

type Json = Record<string, unknown>;
type Cell = { column: number; name: string; value?: string };
type Row = { index: number; disabled: boolean; output: { resultType: string; output?: string }; cells: Cell[] };
type Column = {
  index: number;
  name: string;
  columnType: string;
  disabled?: boolean;
  randomize: boolean;
  autoPopulates: boolean;
  hasFallbackValue: boolean;
  inputType?: string;
  binding?: { parameterIndex: number; propertyChain: string[]; displayName: string };
};
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const message = (r: TestResult): string => `${r.error ?? ""} ${JSON.stringify(r.result ?? {})}`;

let bridge: EditorBridge;

/**
 * Saving a chooser runs data validation, whose progress window can still be up
 * when the next call lands; the dialog gate refuses calls while it is. Wait it
 * out (it closes on its own) rather than fail on the timing.
 */
async function callPastValidation(method: string, params: Json): Promise<TestResult> {
  for (let attempt = 0; ; attempt++) {
    const r = await callBridge(bridge, method, params);
    const b = body(r);
    if (!(b.dialogBlocking === true && String(b.dialogMessage).startsWith("Validating Assets")) || attempt >= 40) return r;
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
}

async function call(method: string, params: Json): Promise<Json> {
  const r = await callPastValidation(method, params);
  expect(succeeded(r), `${method}: ${message(r)}`).toBe(true);
  return body(r);
}

async function refused(method: string, params: Json): Promise<string> {
  const r = await callPastValidation(method, params);
  expect(succeeded(r), `${method} should have been refused: ${message(r)}`).toBe(false);
  return message(r);
}

async function rows(table = TABLE, nested?: string): Promise<Row[]> {
  return (await call("chooser_list_rows", { table, ...(nested ? { nested } : {}) })).rows as Row[];
}

async function columns(table = TABLE): Promise<Column[]> {
  return (await call("chooser_describe", { table })).columns as Column[];
}

const outputs = (list: Row[]): Array<string | undefined> => list.map((r) => r.output.output);

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of [TABLE, OTHER]) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("chooser_create", { name: NAME, packagePath: DIR });
  await call("chooser_create", { name: OTHER_NAME, packagePath: DIR });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    for (const path of [TABLE, OTHER]) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
});

describe("chooser authoring (#1828)", () => {
  it("chooser_list_types lists column, result, parameter and per-column input types", async () => {
    const columnTypes = (await call("chooser_list_types", { kind: "column" })).types as string[];
    expect(columnTypes).toContain("/Script/Chooser.FloatRangeColumn");
    expect(columnTypes).toContain("/Script/Chooser.OutputFloatColumn");
    expect(columnTypes).not.toContain("/Script/Chooser.ChooserColumnBase");
    const resultTypes = (await call("chooser_list_types", { kind: "result" })).types as string[];
    expect(resultTypes).toContain("/Script/Chooser.AssetChooser");
    const parameterTypes = (await call("chooser_list_types", { kind: "parameter" })).types as string[];
    expect(parameterTypes).toContain("/Script/Chooser.ContextObjectTypeClass");

    await call("chooser_add_column", { table: TABLE, columnType: "FloatRangeColumn", inputStruct: "FloatContextProperty" });
    const inputTypes = (await call("chooser_list_types", { kind: "input", table: TABLE, column: 0 })).types as string[];
    expect(inputTypes).toContain("/Script/Chooser.FloatContextProperty");
    expect(await refused("chooser_list_types", { kind: "input" })).toMatch(/needs table and column/);
  }, 60_000);

  it("chooser_set_signature, chooser_add_parameter and chooser_remove_parameter change what chooser_get_signature reads", async () => {
    const before = await call("chooser_get_signature", { table: TABLE });
    expect(before.resultType).toBe("ObjectResult");
    expect(before.usingLocalSignature).toBe(true);

    const set = await call("chooser_set_signature", { table: TABLE, resultClass: "/Script/Engine.StaticMesh" });
    expect((set.rollback as Json).method).toBe("chooser_set_signature");
    expect((await call("chooser_get_signature", { table: TABLE })).resultClass).toBe("/Script/Engine.StaticMesh");

    await call("chooser_set_signature", { table: TABLE, resultType: "ClassResult" });
    expect((await call("chooser_get_signature", { table: TABLE })).resultType).toBe("ClassResult");
    await call("chooser_set_signature", { table: TABLE, resultType: "ObjectResult" });
    expect(await refused("chooser_set_signature", { table: TABLE, resultType: "NotAType" })).toMatch(/NotAType/);
    // A chooser table is never a signature asset, on any engine.
    expect(await refused("chooser_set_signature", { table: TABLE, signatureAsset: OTHER })).toMatch(/ChooserSignature|5\.9/);
    expect((await call("chooser_get_signature", { table: TABLE })).signatureAsset).toBe("");

    const added = await call("chooser_add_parameter", {
      table: TABLE,
      parameterType: "ContextObjectTypeClass",
      properties: { Class: "/Script/Engine.Actor" },
    });
    expect(added.index).toBe(0);
    const extra = await call("chooser_add_parameter", { table: TABLE, parameterType: "ContextObjectTypeClass" });
    expect(extra.index).toBe(1);
    let signature = await call("chooser_get_signature", { table: TABLE });
    const params = signature.parameters as Array<{ index: number; parameterType: string; class?: string }>;
    expect(signature.parameterCount).toBe(2);
    expect(params[0].parameterType).toBe("/Script/Chooser.ContextObjectTypeClass");
    expect(params[0].class).toBe("/Script/Engine.Actor");

    expect(await refused("chooser_add_parameter", { table: TABLE, parameterType: "ContextObjectTypeClass", properties: { NoSuchField: "1" } }))
      .toMatch(/NoSuchField/);
    expect((await call("chooser_get_signature", { table: TABLE })).parameterCount).toBe(2);

    const removed = await call("chooser_remove_parameter", { table: TABLE, index: 1 });
    expect((removed.rollback as Json).method).toBe("chooser_add_parameter");
    signature = await call("chooser_get_signature", { table: TABLE });
    expect(signature.parameterCount).toBe(1);
  }, 60_000);

  it("chooser_set_column retypes, binds and disables a column input, refusing a chain the parameter does not have", async () => {
    // A new input type starts from defaults, so it goes first and the binding after it.
    await call("chooser_set_column", { table: TABLE, column: 0, inputType: "FloatContextProperty" });
    expect((await columns())[0].inputType).toBe("/Script/Chooser.FloatContextProperty");

    const bound = await call("chooser_set_column", { table: TABLE, column: 0, parameterIndex: 0, propertyChain: ["CustomTimeDilation"] });
    expect((bound.rollback as Json).method).toBe("chooser_set_column");
    let column = (await columns())[0];
    expect(column.binding?.parameterIndex).toBe(0);
    expect(column.binding?.propertyChain).toEqual(["CustomTimeDilation"]);

    expect(await refused("chooser_set_column", { table: TABLE, column: 0, parameterIndex: 0, propertyChain: ["NoSuchProperty"] }))
      .toMatch(/NoSuchProperty/);
    column = (await columns())[0];
    expect(column.binding?.propertyChain).toEqual(["CustomTimeDilation"]);

    await call("chooser_set_column", { table: TABLE, column: 0, disabled: true });
    expect((await columns())[0].disabled).toBe(true);
    await call("chooser_set_column", { table: TABLE, column: 0, disabled: false });
    expect((await columns())[0].disabled).toBe(false);
    expect((await columns())[0].binding?.propertyChain).toEqual(["CustomTimeDilation"]);
  }, 60_000);

  it("chooser_add_row inserts at insertIndex and chooser_set_row changes a result type, which list_rows reads", async () => {
    await call("chooser_add_row", { table: TABLE, output: CUBE, cells: ["(Min=0,Max=10)"] });
    await call("chooser_add_row", { table: TABLE, output: CYLINDER, cells: ["(Min=20,Max=30)"] });
    const inserted = await call("chooser_add_row", { table: TABLE, output: SPHERE, insertIndex: 1, cells: ["(Min=10,Max=20)"] });
    expect(inserted.rowIndex).toBe(1);
    expect(outputs(await rows())).toEqual([CUBE, SPHERE, CYLINDER]);
    expect((await rows())[1].cells[0].value).toMatch(/Min=10(\.0+)?\b/);

    await call("chooser_add_row", { table: TABLE, resultType: "SoftAssetChooser" });
    let listed = await rows();
    expect(listed).toHaveLength(4);
    expect(listed[3].output.resultType).toBe("SoftAssetChooser");
    await call("chooser_set_row", { table: TABLE, index: 3, resultType: "AssetChooser" });
    listed = await rows();
    expect(listed[3].output.resultType).toBe("AssetChooser");

    const deleted = await call("chooser_delete_row", { table: TABLE, index: 3 });
    expect(((deleted.rollback as Json).payload as Json).insertIndex).toBe(3);
    expect(await rows()).toHaveLength(3);
  }, 60_000);

  it("chooser_get_property and chooser_set_property read and write paths on rows, columns and parameters", async () => {
    const asset = await call("chooser_get_property", { table: TABLE, target: "row", index: 0, propertyPath: "Asset" });
    expect(String(asset.value)).toContain("Cube");

    const set = await call("chooser_set_property", { table: TABLE, target: "row", index: 0, propertyPath: "Asset", value: CONE });
    expect(String(set.previousValue)).toContain("Cube");
    expect(((set.rollback as Json).payload as Json).value).toBe(set.previousValue);
    expect((await rows())[0].output.output).toBe(CONE);

    await call("chooser_set_property", { table: TABLE, target: "column", index: 0, propertyPath: "DefaultRowValue", value: "(Min=1,Max=2)" });
    const defaults = await call("chooser_get_property", { table: TABLE, target: "column", index: 0, propertyPath: "DefaultRowValue" });
    expect(String(defaults.value)).toMatch(/Min=1(\.0+)?\b/);

    const binding = await call("chooser_get_property", { table: TABLE, target: "column", index: 0, propertyPath: "InputValue.Binding.PropertyBindingChain" });
    expect(String(binding.value)).toContain("CustomTimeDilation");

    const param = await call("chooser_get_property", { table: TABLE, target: "parameter", index: 0, propertyPath: "Class" });
    expect(String(param.value)).toContain("Actor");

    expect(await refused("chooser_set_property", { table: TABLE, target: "row", index: 0, propertyPath: "NoSuchField", value: "1" })).toMatch(/NoSuchField/);
    expect((await rows())[0].output.output).toBe(CONE);
  }, 60_000);

  it("chooser_set_cell and chooser_get_cell edit row cells and an output column's fallback cell", async () => {
    const set = await call("chooser_set_cell", { table: TABLE, column: 0, row: 2, value: "(Min=25,Max=35)" });
    expect(String(set.previousValue)).toMatch(/Min=20(\.0+)?\b/);
    const cell = await call("chooser_get_cell", { table: TABLE, column: 0, row: 2 });
    expect(String(cell.value)).toMatch(/Min=25(\.0+)?\b/);
    expect(cell.cellType).toBe("ChooserFloatRangeRowData");

    expect(await refused("chooser_get_cell", { table: TABLE, column: 0, fallback: true })).toMatch(/not an output column/);
    expect(await refused("chooser_set_cell", { table: TABLE, column: 0, row: 2, value: "(Nope=1)" })).toMatch(/Nope|member/);
    expect(String((await call("chooser_get_cell", { table: TABLE, column: 0, row: 2 })).value)).toMatch(/Min=25(\.0+)?\b/);

    const output = await call("chooser_add_column", { table: TABLE, columnType: "OutputFloatColumn" });
    const outputColumn = output.columnIndex as number;
    expect((await columns())[outputColumn].hasFallbackValue).toBe(true);
    await call("chooser_set_cell", { table: TABLE, column: outputColumn, fallback: true, value: "3.5" });
    expect(Number((await call("chooser_get_cell", { table: TABLE, column: outputColumn, fallback: true })).value)).toBeCloseTo(3.5);
    await call("chooser_set_cell", { table: TABLE, column: outputColumn, row: 1, value: "7" });
    expect(Number((await call("chooser_get_cell", { table: TABLE, column: outputColumn, row: 1 })).value)).toBeCloseTo(7);
  }, 60_000);

  it("chooser_set_fallback sets, retypes and removes the fallback result describe reports", async () => {
    await call("chooser_set_fallback", { table: TABLE, output: CUBE });
    let described = await call("chooser_describe", { table: TABLE });
    expect(described.hasFallback).toBe(true);
    expect((described.fallbackResult as Json).output).toBe(CUBE);

    const fallbackAsset = await call("chooser_get_property", { table: TABLE, target: "fallback", propertyPath: "Asset" });
    expect(String(fallbackAsset.value)).toContain("Cube");

    const removed = await call("chooser_set_fallback", { table: TABLE, remove: true });
    expect(((removed.rollback as Json).payload as Json).output).toBe(CUBE);
    described = await call("chooser_describe", { table: TABLE });
    expect(described.hasFallback).toBe(false);
    expect(await refused("chooser_set_fallback", { table: TABLE, remove: true })).toMatch(/no fallback/);
  }, 60_000);

  it("chooser_duplicate_column, chooser_move_column, chooser_copy_column and chooser_delete_column reshape the columns", async () => {
    const before = await columns();
    const duplicated = await call("chooser_duplicate_column", { table: TABLE, column: 0 });
    expect(duplicated.columnIndex).toBe(1);
    let after = await columns();
    expect(after).toHaveLength(before.length + 1);
    expect(after[1].columnType).toBe("FloatRangeColumn");
    expect(String((await call("chooser_get_cell", { table: TABLE, column: 1, row: 2 })).value)).toMatch(/Min=25(\.0+)?\b/);

    const moved = await call("chooser_move_column", { table: TABLE, column: 1, newIndex: after.length - 1 });
    expect(moved.columnIndex).toBe(after.length - 1);
    after = await columns();
    expect(after[after.length - 1].columnType).toBe("FloatRangeColumn");
    expect(after[1].columnType).toBe("OutputFloatColumn");

    const deleted = await call("chooser_delete_column", { table: TABLE, column: after.length - 1 });
    expect(deleted.deletedColumnType).toBe("FloatRangeColumn");
    expect(await columns()).toHaveLength(before.length);

    // The inverse is a snapshot; restoring it brings the column back with its cells.
    const undo = deleted.rollback as Json;
    expect(undo.method).toBe("chooser_restore_snapshot");
    await call("chooser_restore_snapshot", undo.payload as Json);
    expect(await columns()).toHaveLength(before.length + 1);
    expect(String((await call("chooser_get_cell", { table: TABLE, column: after.length - 1, row: 2 })).value)).toMatch(/Min=25(\.0+)?\b/);
    await call("chooser_delete_column", { table: TABLE, column: after.length - 1 });
    expect(await columns()).toHaveLength(before.length);

    await call("chooser_add_row", { table: OTHER, output: CUBE });
    await call("chooser_add_row", { table: OTHER, output: SPHERE });
    const copied = await call("chooser_copy_column", { table: TABLE, column: 0, targetTable: OTHER });
    expect(copied.columnIndex).toBe(0);
    const target = await columns(OTHER);
    expect(target[0].columnType).toBe("FloatRangeColumn");
    expect((await rows(OTHER))[1].cells[0].value).toMatch(/Min=10(\.0+)?\b/);
  }, 60_000);

  it("chooser_move_rows, chooser_duplicate_rows, chooser_delete_rows and chooser_copy_rows reorder, copy and remove rows", async () => {
    expect(outputs(await rows())).toEqual([CONE, SPHERE, CYLINDER]);
    const moved = await call("chooser_move_rows", { table: TABLE, rows: [2], targetIndex: 0 });
    expect(moved.firstRow).toBe(0);
    let listed = await rows();
    expect(outputs(listed)).toEqual([CYLINDER, CONE, SPHERE]);
    // Cells travel with their rows.
    expect(listed[0].cells[0].value).toMatch(/Min=25(\.0+)?\b/);
    const back = moved.rollback as Json;
    expect(back.method).toBe("chooser_move_rows");
    await call("chooser_move_rows", { table: TABLE, ...(back.payload as Json) });
    expect(outputs(await rows())).toEqual([CONE, SPHERE, CYLINDER]);

    const duplicated = await call("chooser_duplicate_rows", { table: TABLE, rows: [0, 1] });
    expect(duplicated.firstRow).toBe(2);
    listed = await rows();
    expect(outputs(listed)).toEqual([CONE, SPHERE, CONE, SPHERE, CYLINDER]);
    expect(listed[3].cells[0].value).toBe(listed[1].cells[0].value);

    const deleted = await call("chooser_delete_rows", { table: TABLE, rows: [3, 2] });
    expect(deleted.deletedRows).toEqual([2, 3]);
    expect(outputs(await rows())).toEqual([CONE, SPHERE, CYLINDER]);

    await call("chooser_set_fallback", { table: TABLE, output: CYLINDER });
    const copied = await call("chooser_copy_rows", { table: TABLE, rows: [1], targetTable: OTHER, insertIndex: 0, includeFallback: true });
    expect(copied.firstRow).toBe(0);
    const target = await rows(OTHER);
    expect(outputs(target)).toEqual([SPHERE, CUBE, SPHERE]);
    expect(target[0].cells[0].value).toMatch(/Min=10(\.0+)?\b/);
    expect(((await call("chooser_describe", { table: OTHER })).fallbackResult as Json).output).toBe(CYLINDER);
    // The output column had no match in the target, so it was added.
    expect((copied.addedColumns as number[]).length).toBe(1);
  }, 60_000);

  it("chooser_auto_populate fills only auto populating columns and refuses a column that is not one", async () => {
    expect(await refused("chooser_auto_populate", { table: TABLE, column: 0 })).toMatch(/does not support auto populate|5\.7/);
    const all = await call("chooser_auto_populate", { table: TABLE });
    expect(all.columns).toEqual([]);
    expect(outputs(await rows())).toEqual([CONE, SPHERE, CYLINDER]);
  }, 60_000);

  it("chooser_create_nested, chooser_list_nested, chooser_rename_nested and chooser_delete_nested manage nested choosers", async () => {
    const created = await call("chooser_create_nested", { table: TABLE, name: "Inner" });
    expect(created.name).toBe("Inner");
    expect(created.addedRow).toBe(true);
    const rowIndex = created.rowIndex as number;
    expect((await rows())[rowIndex].output.resultType).toBe("NestedChooser");

    let nested = await call("chooser_list_nested", { table: TABLE });
    expect((nested.nestedObjects as Json[]).map((o) => o.name)).toContain("Inner");

    await call("chooser_add_row", { table: TABLE, nested: "Inner", output: CUBE });
    expect(outputs(await rows(TABLE, "Inner"))).toEqual([CUBE]);
    expect((await call("chooser_describe", { table: TABLE, nested: "Inner" })).isNested).toBe(true);

    const renamed = await call("chooser_rename_nested", { table: TABLE, name: "Inner", newName: "Inner2" });
    expect(renamed.name).toBe("Inner2");
    expect(((renamed.rollback as Json).payload as Json).newName).toBe("Inner");
    nested = await call("chooser_list_nested", { table: TABLE });
    expect((nested.nestedObjects as Json[]).map((o) => o.name)).toEqual(["Inner2"]);
    expect(await refused("chooser_list_rows", { table: TABLE, nested: "Inner" })).toMatch(/no nested chooser named 'Inner'/);

    const deleted = await call("chooser_delete_nested", { table: TABLE, name: "Inner2" });
    nested = await call("chooser_list_nested", { table: TABLE });
    expect(nested.count).toBe(0);

    // Restoring the snapshot recreates the nested chooser with its row.
    const restored = await call("chooser_restore_snapshot", (deleted.rollback as Json).payload as Json);
    expect(restored.recreatedNested).toBe(1);
    expect(outputs(await rows(TABLE, "Inner2"))).toEqual([CUBE]);
    await call("chooser_delete_nested", { table: TABLE, name: "Inner2" });
    expect((await call("chooser_list_nested", { table: TABLE })).count).toBe(0);
    await call("chooser_delete_row", { table: TABLE, index: rowIndex });
    expect(await rows()).toHaveLength(3);
  }, 60_000);

  it("chooser_add_column honours insertIndex and chooser_remove_disabled deletes disabled rows and columns", async () => {
    const before = await columns();
    const added = await call("chooser_add_column", { table: TABLE, columnType: "BoolColumn", insertIndex: 0 });
    expect(added.columnIndex).toBe(0);
    expect((await columns())[0].columnType).toBe("BoolColumn");
    expect(((added.rollback as Json).method)).toBe("chooser_delete_column");

    await call("chooser_set_column", { table: TABLE, column: 0, disabled: true });
    await call("chooser_set_row", { table: TABLE, index: 1, disabled: true });
    const removed = await call("chooser_remove_disabled", { table: TABLE });
    expect(removed.removedColumns).toBe(1);
    expect(removed.removedRows).toBe(1);
    expect(await columns()).toHaveLength(before.length);
    expect(outputs(await rows())).toEqual([CONE, CYLINDER]);
  }, 60_000);
});
