// @live-editor headless
/**
 * Data table, curve table and string table row edits, each read back through a
 * second action, plus a string table CSV import and a migrate to a scratch
 * Content folder on disk.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AssetB/Tables";
const DT = `${DIR}/DT_BCovB`;
const CT = `${DIR}/CT_BCovB`;
const ST = `${DIR}/ST_BCovB`;
// A native row struct with two plain fields, always loaded in the editor.
const ROW_STRUCT = "/Script/GameplayTags.GameplayTagTableRow";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let scratch = "";

async function call(method: string, params: Json, timeoutMs?: number): Promise<Json> {
  // An explicit budget goes straight to the bridge, which callBridge does not forward.
  const r: TestResult = timeoutMs === undefined
    ? await callBridge(bridge, method, params)
    : await bridge.call(method, params, timeoutMs).then(
        (result) => ({ method, ok: true, ms: 0, result }),
        (e: unknown) => ({ method, ok: false, ms: 0, error: e instanceof Error ? e.message : String(e) }),
      );
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function dataTableRowNames(): Promise<string[]> {
  const read = await call("read_datatable", { assetPath: DT });
  return (read.rowNames as unknown[]).map(String);
}

async function dataTableField(rowName: string, field: string): Promise<string> {
  const row = await call("get_datatable_row", { assetPath: DT, rowName });
  return String((row.fields as Json)[field]);
}

async function curveRows(): Promise<Array<{ name: string; keys: Array<{ time: number; value: number }> }>> {
  const read = await call("read_curvetable", { assetPath: CT });
  return read.rows as Array<{ name: string; keys: Array<{ time: number; value: number }> }>;
}

async function stringTableKeys(): Promise<string[]> {
  const listed = await call("list_stringtable_keys", { assetPath: ST });
  return (listed.keys as unknown[]).map(String);
}

async function stringTableSource(key: string): Promise<string | undefined> {
  const read = await call("read_stringtable", { assetPath: ST });
  const entry = (read.entries as Array<{ key: string; sourceString: string }>).find((e) => e.key === key);
  return entry?.sourceString;
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = mkdtempSync(join(tmpdir(), "ue-mcp-bcov-asset-b-"));
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_datatable", { name: "DT_BCovB", packagePath: DIR, rowStruct: ROW_STRUCT });
  await call("create_curvetable", { name: "CT_BCovB", packagePath: DIR });
  await call("add_curvetable_row", { assetPath: CT, rowName: "Speed", curveType: "rich" });
  await call("add_curvetable_row", { assetPath: CT, rowName: "Damage", curveType: "rich" });
  await call("create_stringtable", { name: "ST_BCovB", packagePath: DIR });
}, 120_000);

afterAll(async () => {
  if (scratch) rmSync(scratch, { recursive: true, force: true });
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("data table rows", () => {
  it("set_datatable_row appends a row and update_datatable_row merges into it", async () => {
    const set = await call("set_datatable_row", { assetPath: DT, rowName: "Alpha", row: { Tag: "BCov.Alpha", DevComment: "first" } });
    expect(set.rowName).toBe("Alpha");
    expect(await dataTableRowNames()).toContain("Alpha");
    expect(await dataTableField("Alpha", "DevComment")).toContain("first");

    await call("update_datatable_row", { assetPath: DT, rowName: "Alpha", row: { DevComment: "second" } });
    expect(await dataTableField("Alpha", "DevComment")).toContain("second");
    // A field the update did not name keeps its value.
    expect(await dataTableField("Alpha", "Tag")).toContain("BCov.Alpha");
  }, 120_000);

  it("rename_datatable_row moves the row to its new name", async () => {
    const renamed = await call("rename_datatable_row", { assetPath: DT, oldName: "Alpha", newName: "Beta" });
    expect(renamed.newName).toBe("Beta");
    const names = await dataTableRowNames();
    expect(names).toContain("Beta");
    expect(names).not.toContain("Alpha");
    expect(await dataTableField("Beta", "DevComment")).toContain("second");
  }, 120_000);

  it("remove_datatable_row drops the row", async () => {
    await call("remove_datatable_row", { assetPath: DT, rowName: "Beta" });
    expect(await dataTableRowNames()).not.toContain("Beta");
  }, 120_000);

  it("reimport_datatable replaces the table from JSON", async () => {
    await call("set_datatable_row", { assetPath: DT, rowName: "Stale", row: { DevComment: "old" } });
    const json = JSON.stringify([
      { Name: "FromJsonA", Tag: "BCov.JsonA", DevComment: "json a" },
      { Name: "FromJsonB", Tag: "BCov.JsonB", DevComment: "json b" },
    ]);
    const reimported = await call("reimport_datatable", { assetPath: DT, jsonString: json });
    expect(reimported.rowCount).toBe(2);
    const names = await dataTableRowNames();
    expect(names.sort()).toEqual(["FromJsonA", "FromJsonB"]);
    expect(await dataTableField("FromJsonB", "DevComment")).toContain("json b");
  }, 120_000);
});

describe("curve table rows", () => {
  it("set_curvetable_keys replaces a row's keys", async () => {
    const set = await call("set_curvetable_keys", {
      assetPath: CT,
      rowName: "Speed",
      keys: [{ time: 0, value: 1 }, { time: 2, value: 7.5 }],
    });
    expect(set.keyCount).toBe(2);
    const speed = (await curveRows()).find((r) => r.name === "Speed")!;
    expect(speed.keys).toHaveLength(2);
    expect(speed.keys[1].time).toBeCloseTo(2, 4);
    expect(speed.keys[1].value).toBeCloseTo(7.5, 4);
  }, 120_000);

  it("list_curvetable_rows lists every row and honours rowFilter", async () => {
    const all = await call("list_curvetable_rows", { assetPath: CT });
    expect((all.rowNames as string[]).sort()).toEqual(["Damage", "Speed"]);
    const filtered = await call("list_curvetable_rows", { assetPath: CT, rowFilter: "spe" });
    expect(filtered.rowNames).toEqual(["Speed"]);
    expect(filtered.totalRowCount).toBe(2);
  }, 120_000);

  it("rename_curvetable_row and remove_curvetable_row change the row set", async () => {
    await call("rename_curvetable_row", { assetPath: CT, oldName: "Damage", newName: "Health" });
    let names = (await curveRows()).map((r) => r.name);
    expect(names).toContain("Health");
    expect(names).not.toContain("Damage");

    const removed = await call("remove_curvetable_row", { assetPath: CT, rowName: "Health" });
    expect(removed.rowCount).toBe(1);
    names = (await curveRows()).map((r) => r.name);
    expect(names).toEqual(["Speed"]);
  }, 120_000);
});

describe("string table entries", () => {
  it("set_stringtable_entry writes an entry that read_stringtable and list_stringtable_keys report", async () => {
    await call("set_stringtable_entry", { assetPath: ST, key: "Greeting", sourceString: "Hello there" });
    expect(await stringTableKeys()).toContain("Greeting");
    expect(await stringTableSource("Greeting")).toBe("Hello there");

    await call("set_stringtable_entry", { assetPath: ST, key: "Greeting", sourceString: "Hello again" });
    expect(await stringTableSource("Greeting")).toBe("Hello again");
    const filtered = await call("list_stringtable_keys", { assetPath: ST, keyFilter: "greet" });
    expect(filtered.keys).toEqual(["Greeting"]);
  }, 120_000);

  it("remove_stringtable_entry drops the entry", async () => {
    await call("set_stringtable_entry", { assetPath: ST, key: "Doomed", sourceString: "bye" });
    expect(await stringTableKeys()).toContain("Doomed");
    await call("remove_stringtable_entry", { assetPath: ST, key: "Doomed" });
    expect(await stringTableKeys()).not.toContain("Doomed");
  }, 120_000);

  it("import_stringtable_csv merges the CSV's entries into the table", async () => {
    const csv = join(scratch, "bcov_strings.csv");
    writeFileSync(csv, 'Key,SourceString\nCsvOne,"First from csv"\nCsvTwo,"Second from csv"\n');
    const imported = await call("import_stringtable_csv", { assetPath: ST, csvPath: csv, expectedKeys: ["CsvOne", "CsvTwo"] });
    expect((imported.addedKeys as string[]).sort()).toEqual(["CsvOne", "CsvTwo"]);
    expect(await stringTableSource("CsvOne")).toBe("First from csv");
    expect(await stringTableSource("CsvTwo")).toBe("Second from csv");
    // Not replaceExisting, so the entry written earlier survives.
    expect(await stringTableKeys()).toContain("Greeting");
  }, 120_000);
});

describe("migrate", () => {
  it("copies a saved package into another project's Content folder", async () => {
    await call("save_asset", { assetPath: ST, force: true });
    // Migrate refuses while anything is unsaved, so start from a saved level: demo_go_home creates and saves
    // MCP_Home on a fresh project, which does not check it in.
    await call("demo_go_home", {});
    await call("load_level", { levelPath: "/Game/MCP_Home", discardUnsaved: true });
    await call("save_all_dirty", {});
    const content = join(scratch, "OtherProject", "Content");
    mkdirSync(content, { recursive: true });
    // The engine migrates only into a project's Content folder.
    writeFileSync(join(scratch, "OtherProject", "OtherProject.uproject"), JSON.stringify({ FileVersion: 3 }));
    const migrated = await call("migrate", { assetPaths: [ST], destinationContentDir: content, includeDependencies: false }, 110_000);
    expect(migrated.packageCount).toBe(1);
    expect(migrated.packagesPresentAtDestination).toBe(1);
    expect(existsSync(join(content, "MCPTest", "BCov", "AssetB", "Tables", "ST_BCovB.uasset"))).toBe(true);
  }, 180_000);
});
