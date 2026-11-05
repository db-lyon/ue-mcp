// @live-editor headless
/**
 * Chooser table authoring read back through describe and list_rows: create,
 * columns, rows added, edited and deleted, and the object reference listing
 * and remap across the table's results.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Chooser";
const NAME = "CT_BCov_Chooser";
const TABLE = `${DIR}/${NAME}`;
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const SPHERE = "/Engine/BasicShapes/Sphere.Sphere";
const CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder";
const CONE = "/Engine/BasicShapes/Cone.Cone";

type Json = Record<string, unknown>;
type Cell = { column: number; name: string; value?: string };
type Row = { index: number; disabled: boolean; output: { resultType: string; output?: string }; cells: Cell[] };
type Ref = { chooser: string; location: string; structType: string; objectPath: string };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function rows(): Promise<Row[]> {
  const listed = await call("chooser_list_rows", { table: TABLE });
  return listed.rows as Row[];
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: TABLE, force: true });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: TABLE, force: true });
    disconnectBridge();
  }
});

describe("chooser authoring", () => {
  it("chooser_create makes an empty table chooser_describe reads", async () => {
    const created = await call("chooser_create", { name: NAME, packagePath: DIR });
    expect(String(created.path)).toContain(NAME);
    const described = await call("chooser_describe", { table: TABLE });
    expect(described.name).toBe(NAME);
    expect(described.rowCount).toBe(0);
    expect(described.columns).toEqual([]);
  }, 120_000);

  it("chooser_add_column adds a float range column chooser_describe lists", async () => {
    const added = await call("chooser_add_column", {
      table: TABLE,
      columnType: "FloatRangeColumn",
      inputStruct: "FloatContextProperty",
      boundProperty: "Speed",
    });
    expect(added.columnIndex).toBe(0);
    expect(added.columnType).toBe("FloatRangeColumn");
    const described = await call("chooser_describe", { table: TABLE });
    const columns = described.columns as Array<{ index: number; columnType: string; cellType?: string }>;
    expect(columns).toHaveLength(1);
    expect(columns[0].columnType).toBe("FloatRangeColumn");
    expect(columns[0].cellType).toBe("ChooserFloatRangeRowData");
  }, 120_000);

  it("chooser_add_row, chooser_set_row and chooser_delete_row reshape the rows chooser_list_rows reads", async () => {
    const first = await call("chooser_add_row", { table: TABLE, output: CUBE, cells: ["(Min=10,Max=20)"] });
    expect(first.rowIndex).toBe(0);
    expect(first.cellWarnings).toBeUndefined();
    await call("chooser_add_row", { table: TABLE, output: SPHERE, cells: ["(Min=20,Max=30)"] });

    let listed = await rows();
    expect(listed).toHaveLength(2);
    expect(listed[0].output.output).toBe(CUBE);
    expect(listed[0].cells[0].value).toMatch(/Min=10(\.0+)?\b/);
    expect(listed[1].output.output).toBe(SPHERE);

    const set = await call("chooser_set_row", { table: TABLE, index: 1, output: CYLINDER, cells: ["(Min=30,Max=40)"] });
    expect((set.priorOutput as Json).output).toBe(SPHERE);
    listed = await rows();
    expect(listed[1].output.output).toBe(CYLINDER);
    expect(listed[1].cells[0].value).toMatch(/Min=30(\.0+)?\b/);
    expect(listed[1].cells[0].value).toMatch(/Max=40(\.0+)?\b/);

    const deleted = await call("chooser_delete_row", { table: TABLE, index: 0 });
    expect(deleted.rowCount).toBe(1);
    expect((deleted.deletedOutput as Json).output).toBe(CUBE);
    listed = await rows();
    expect(listed).toHaveLength(1);
    expect(listed[0].output.output).toBe(CYLINDER);
  }, 180_000);

  it("chooser_list_object_references finds the row output and chooser_remap_object_references repoints it", async () => {
    const refs = await call("chooser_list_object_references", { assetPath: TABLE, pathFilter: "Cylinder" });
    const found = refs.references as Ref[];
    expect(found).toHaveLength(1);
    expect(found[0].objectPath).toBe(CYLINDER);
    expect(found[0].location).toContain("ResultsStructs[0]");

    const preview = await call("chooser_remap_object_references", { assetPath: TABLE, from: CYLINDER, to: CONE, dryRun: true });
    expect(preview.matched).toBe(1);
    expect(preview.rewritten).toBe(0);
    expect((await rows())[0].output.output).toBe(CYLINDER);

    const applied = await call("chooser_remap_object_references", { assetPath: TABLE, from: CYLINDER, to: CONE, dryRun: false });
    expect(applied.rewritten).toBe(1);
    expect((await rows())[0].output.output).toBe(CONE);
    const after = await call("chooser_list_object_references", { assetPath: TABLE, pathFilter: "Cone" });
    expect(after.referenceCount).toBe(1);
    const gone = await call("chooser_list_object_references", { assetPath: TABLE, pathFilter: "Cylinder" });
    expect(gone.referenceCount).toBe(0);
  }, 120_000);
});
