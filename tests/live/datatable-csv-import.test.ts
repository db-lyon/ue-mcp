// @live-editor headless
/**
 * A DataTable from CSV without the DataTable Options dialog.
 *
 * Dropping a .csv on the Content Browser asks for a row type, Ignore Extra
 * Fields, Ignore Missing Fields and Import Key Field in a modal. create_datatable
 * and reimport_datatable take csvString or a .csv filePath with the same three
 * options and read the text through the engine's CSV importer; import_file
 * takes rowStruct for a .csv and never raises the modal unattended. The
 * importer's problems come back as `problems`, never only in the log.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/DataTableCsv`;
const ROW_STRUCT = "/Script/GameplayTags.GameplayTagTableRow";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;
let scratch = "";

async function raw(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok, `${method}: ${r.error}`).toBe(true);
  return body(r);
}

async function call(method: string, params: Json): Promise<Json> {
  const out = await raw(method, params);
  expect(out.success, `${method}: ${JSON.stringify(out)}`).not.toBe(false);
  return out;
}

async function rows(assetPath: string): Promise<Array<Record<string, unknown>>> {
  const read = await call("read_datatable", { assetPath });
  return (read.rows ?? []) as Array<Record<string, unknown>>;
}

async function property(assetPath: string, propertyName: string): Promise<unknown> {
  const read = await call("get_asset_properties", { assetPath, propertyName, valueFormat: "json" });
  return read.value;
}

function rowNamed(list: Array<Record<string, unknown>>, name: string): Record<string, unknown> | undefined {
  return list.find((r) => r.Name === name || r.name === name || r.RowName === name);
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = mkdtempSync(join(tmpdir(), "ue-mcp-dt-csv-"));
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_folder", { path: DIR });
}, 120_000);

afterAll(async () => {
  if (scratch) rmSync(scratch, { recursive: true, force: true });
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("create_datatable with CSV rows", () => {
  it("fills the new table from csvString through the engine importer", async () => {
    const created = await call("create_datatable", {
      name: "DT_CsvCreate",
      packagePath: DIR,
      rowStruct: ROW_STRUCT,
      csvString: "---,Tag,DevComment\nAlpha,Test.Csv.Alpha,first row\nBeta,Test.Csv.Beta,second row\n",
    });
    expect(created.created).toBe(true);
    expect(created.rowsImported).toBe(true);
    expect(created.rowCount).toBe(2);
    expect(created.problems).toEqual([]);

    const read = await rows(`${DIR}/DT_CsvCreate`);
    expect(read).toHaveLength(2);
    const beta = rowNamed(read, "Beta");
    expect(beta, JSON.stringify(read)).toBeDefined();
    expect(JSON.stringify(beta)).toContain("Test.Csv.Beta");
    expect(JSON.stringify(beta)).toContain("second row");
  }, 120_000);

  it("reports the importer's problems instead of hiding them", async () => {
    const created = await raw("create_datatable", {
      name: "DT_CsvProblems",
      packagePath: DIR,
      rowStruct: ROW_STRUCT,
      csvString: "---,Tag,DevComment,NotAField\nAlpha,Test.Csv.Alpha,first,extra\n",
    });
    expect(created.success).toBe(false);
    const problems = created.problems as string[];
    expect(problems.length, JSON.stringify(created)).toBeGreaterThan(0);
    expect(problems.join("\n")).toContain("NotAField");
    // The table exists, so the inverse still deletes it.
    expect(created.created).toBe(true);
  }, 120_000);
});

describe("reimport_datatable from CSV", () => {
  it("replaces the rows from a .csv filePath with the dialog's three options", async () => {
    await call("create_datatable", { name: "DT_CsvReimport", packagePath: DIR, rowStruct: ROW_STRUCT });
    const table = `${DIR}/DT_CsvReimport`;
    const csv = join(scratch, "reimport.csv");
    writeFileSync(csv, "Key,Tag,DevComment,Unused\nGamma,Test.Csv.Gamma,third,x\nDelta,Test.Csv.Delta,fourth,y\n");

    const reimported = await call("reimport_datatable", {
      assetPath: table,
      filePath: csv,
      ignoreExtraFields: true,
      ignoreMissingFields: true,
      importKeyField: "Key",
    });
    expect(reimported.format).toBe("csv");
    expect(reimported.rowCount).toBe(2);
    expect(reimported.problems).toEqual([]);
    expect(reimported.importOptions).toEqual({ ignoreExtraFields: true, ignoreMissingFields: true, importKeyField: "Key" });

    // The options are the table's own now, as the dialog leaves them.
    expect(await property(table, "bIgnoreExtraFields")).toBe(true);
    expect(await property(table, "bIgnoreMissingFields")).toBe(true);
    expect(await property(table, "ImportKeyField")).toBe("Key");
    const read = await rows(table);
    // With an import key field the export names the row column after it.
    expect(read.map((r) => r.Key ?? r.Name).sort()).toEqual(["Delta", "Gamma"]);
  }, 120_000);

  it("refuses an extra column when ignoreExtraFields is off, and says which", async () => {
    await call("create_datatable", { name: "DT_CsvStrict", packagePath: DIR, rowStruct: ROW_STRUCT });
    const out = await raw("reimport_datatable", {
      assetPath: `${DIR}/DT_CsvStrict`,
      csvString: "---,Tag,DevComment,Stray\nEpsilon,Test.Csv.Epsilon,fifth,z\n",
      ignoreExtraFields: false,
    });
    expect(out.success).toBe(false);
    expect((out.problems as string[]).join("\n")).toContain("Stray");
  }, 120_000);
});

describe("import_file of a .csv", () => {
  it("imports into a DataTable with rowStruct and no dialog", async () => {
    const csv = join(scratch, "DT_CsvFile.csv");
    writeFileSync(csv, "---,Tag,DevComment\nZeta,Test.Csv.Zeta,sixth\n");
    const imported = await call("import_file", {
      filePath: csv,
      packagePath: DIR,
      rowStruct: ROW_STRUCT,
      ignoreMissingFields: true,
    });
    expect(imported.importedCount).toBe(1);
    expect(String(imported.rowStruct)).toContain("GameplayTagTableRow");
    expect(imported.problems).toEqual([]);
    const table = `${DIR}/DT_CsvFile`;
    const read = await rows(table);
    expect(rowNamed(read, "Zeta"), JSON.stringify(read)).toBeDefined();
    expect(await property(table, "bIgnoreMissingFields")).toBe(true);
  }, 120_000);

  it("refuses an unattended .csv import with no row type instead of producing nothing", async () => {
    const csv = join(scratch, "DT_NoStruct.csv");
    writeFileSync(csv, "---,Tag\nEta,Test.Csv.Eta\n");
    const out = await raw("import_file", { filePath: csv, packagePath: DIR });
    expect(out.success).toBe(false);
    expect(String(out.error)).toContain("rowStruct");
    const listed = await call("list_assets", { directory: DIR, recursive: false });
    expect(JSON.stringify(listed)).not.toContain("DT_NoStruct");
  }, 120_000);
});
