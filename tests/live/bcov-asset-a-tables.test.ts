// @live-editor headless
/**
 * Table and type authoring, each read back through a second action: curve
 * tables, string tables, a data table row, user-defined enums and structs,
 * data assets (single and bulk) and the primary asset ids they register.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AssetA/Tables";
const CT = `${DIR}/CT_BCov`;
const ST = `${DIR}/ST_BCov`;
const DT = `${DIR}/DT_BCov`;
const ENUM = `${DIR}/E_BCov`;
const STRUCT = `${DIR}/F_BCov`;
const INPUT_ACTION = "/Script/EnhancedInput.InputAction";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let scratch = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function readProperty(assetPath: string, propertyName: string): Promise<unknown> {
  const r = await call("get_asset_properties", { assetPath, propertyName, valueFormat: "json" });
  return r.value;
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = mkdtempSync(join(tmpdir(), "ue-mcp-bcov-tables-"));
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

describe("curve tables", () => {
  it("create_curvetable, add_curvetable_row, add_curvetable_key and get_curvetable_keys round-trip", async () => {
    const created = await call("create_curvetable", { name: "CT_BCov", packagePath: DIR });
    expect(created.created).toBe(true);
    expect(created.rowCount).toBe(0);

    await call("add_curvetable_row", { assetPath: CT, rowName: "Damage", curveType: "simple" });
    const rows = await call("read_curvetable", { assetPath: CT });
    expect(rows.rowNames).toContain("Damage");

    await call("add_curvetable_key", { assetPath: CT, rowName: "Damage", time: 0, value: 10 });
    await call("add_curvetable_key", { assetPath: CT, rowName: "Damage", time: 5, value: 42.5 });
    const keys = await call("get_curvetable_keys", { assetPath: CT, rowName: "Damage" });
    expect(keys.keyCount).toBe(2);
    const list = keys.keys as Array<{ time: number; value: number }>;
    expect(list.map((k) => k.time)).toEqual([0, 5]);
    expect(list[1].value).toBeCloseTo(42.5, 4);

    // A key at an existing time overwrites it rather than adding a third.
    await call("add_curvetable_key", { assetPath: CT, rowName: "Damage", time: 5, value: 7 });
    const after = await call("get_curvetable_keys", { assetPath: CT, rowName: "Damage" });
    expect(after.keyCount).toBe(2);
    expect((after.keys as Array<{ value: number }>)[1].value).toBeCloseTo(7, 4);
  }, 120_000);

  it("import_curvetable replaces the rows from CSV text", async () => {
    await call("create_curvetable", { name: "CT_BCovImport", packagePath: DIR });
    const imported = await call("import_curvetable", {
      assetPath: `${DIR}/CT_BCovImport`,
      csvString: "Name,0,1,2\nSpeed,1,2,4\nArmor,3,3,3\n",
    });
    expect(imported.rowCount).toBe(2);
    const read = await call("read_curvetable", { assetPath: `${DIR}/CT_BCovImport` });
    expect(read.rowNames).toEqual(expect.arrayContaining(["Speed", "Armor"]));
    const speed = await call("get_curvetable_keys", { assetPath: `${DIR}/CT_BCovImport`, rowName: "Speed" });
    expect((speed.keys as Array<{ time: number; value: number }>).map((k) => k.value)).toEqual([1, 2, 4]);
  }, 120_000);
});

describe("string tables", () => {
  it("create_stringtable, import_stringtable and get_stringtable_entry round-trip", async () => {
    const created = await call("create_stringtable", { name: "ST_BCov", packagePath: DIR, namespace: "BCov" });
    expect(created.created).toBe(true);

    const csv = join(scratch, "strings.csv");
    writeFileSync(csv, 'Key,SourceString\nGreeting,"Hello there"\nFarewell,"Goodbye"\n');
    const imported = await call("import_stringtable", { assetPath: ST, filePath: csv });
    expect(imported.entryCountBefore).toBe(0);
    expect(imported.entryCountAfter).toBe(2);

    const greeting = await call("get_stringtable_entry", { assetPath: ST, key: "Greeting" });
    expect(greeting.sourceString).toBe("Hello there");
    const farewell = await call("get_stringtable_entry", { assetPath: ST, key: "Farewell" });
    expect(farewell.sourceString).toBe("Goodbye");

    const missing = await callBridge(bridge, "get_stringtable_entry", { assetPath: ST, key: "NoSuchKey" });
    expect(succeeded(missing)).toBe(false);
  }, 120_000);
});

describe("data table rows", () => {
  it("add_datatable_row writes a row get_datatable_row reads back", async () => {
    await call("create_datatable", { name: "DT_BCov", packagePath: DIR, rowStruct: "/Script/GameplayTags.GameplayTagTableRow" });
    const added = await call("add_datatable_row", {
      assetPath: DT, rowName: "RowA", row: { Tag: "BCov.Tag.A", DevComment: "first row" },
    });
    expect(added.success).toBe(true);

    const read = await call("get_datatable_row", { assetPath: DT, rowName: "RowA" });
    expect(read.rowStruct).toBe("GameplayTagTableRow");
    const fields = read.fields as Record<string, string>;
    expect(fields.Tag).toContain("BCov.Tag.A");
    expect(fields.DevComment).toContain("first row");

    // Writing the same row again merges: an unnamed field keeps its value.
    await call("add_datatable_row", { assetPath: DT, rowName: "RowA", row: { DevComment: "edited" } });
    const edited = (await call("get_datatable_row", { assetPath: DT, rowName: "RowA" })).fields as Record<string, string>;
    expect(edited.DevComment).toContain("edited");
    expect(edited.Tag).toContain("BCov.Tag.A");
  }, 120_000);
});

describe("user-defined enums and structs", () => {
  it("create_user_defined_enum and edit_user_defined_enum change the enumerators", async () => {
    const created = await call("create_user_defined_enum", { name: "E_BCov", packagePath: DIR, values: ["Red", "Green"] });
    expect(created.count).toBe(2);
    expect((created.values as Array<{ displayName: string }>).map((v) => v.displayName)).toEqual(["Red", "Green"]);

    const added = await call("edit_user_defined_enum", { assetPath: ENUM, op: "add_value", displayName: "Blue" });
    expect(added.count).toBe(3);
    await call("edit_user_defined_enum", { assetPath: ENUM, op: "rename_value", name: "Green", displayName: "Lime" });
    await call("edit_user_defined_enum", { assetPath: ENUM, op: "remove_value", name: "Red" });

    const listed = await call("list_enum_values", { assetPath: ENUM });
    expect((listed.values as Array<{ displayName: string }>).map((v) => v.displayName)).toEqual(["Lime", "Blue"]);
  }, 120_000);

  it("create_user_defined_struct and edit_user_defined_struct change the members", async () => {
    const created = await call("create_user_defined_struct", {
      name: "F_BCov", packagePath: DIR, structFields: [{ name: "Health", type: "float" }, { name: "Label", type: "string" }],
    });
    expect(created.count).toBe(2);

    const added = await call("edit_user_defined_struct", { assetPath: STRUCT, op: "add_field", fieldName: "Count", type: "int" });
    expect((added.field as { friendlyName: string }).friendlyName).toBe("Count");
    await call("edit_user_defined_struct", { assetPath: STRUCT, op: "rename_field", fieldName: "Label", newDisplayName: "Title" });
    await call("edit_user_defined_struct", { assetPath: STRUCT, op: "set_field_type", fieldName: "Count", type: "name" });
    await call("edit_user_defined_struct", { assetPath: STRUCT, op: "remove_field", fieldName: "Health" });

    const listed = await call("list_struct_fields", { assetPath: STRUCT });
    const fields = listed.fields as Array<{ friendlyName: string; typeSpec: string }>;
    expect(fields.map((f) => f.friendlyName)).toEqual(["Title", "Count"]);
    expect(fields[1].typeSpec.toLowerCase()).toContain("name");
  }, 120_000);
});

describe("data assets", () => {
  it("create_data_asset writes the properties it is given", async () => {
    const created = await call("create_data_asset", {
      name: "IA_BCov", packagePath: DIR, className: INPUT_ACTION, properties: { bConsumeInput: false },
    });
    expect(created.propertiesSet).toBe(1);
    expect(created.className).toBe("InputAction");
    expect(await readProperty(`${DIR}/IA_BCov`, "bConsumeInput")).toBe(false);
  }, 120_000);

  it("bulk_upsert_data_assets creates, then updates, a batch", async () => {
    const items = ["IA_BCovBulkA", "IA_BCovBulkB"].map((name) => ({
      name, packagePath: DIR, className: INPUT_ACTION, properties: { bConsumeInput: false },
    }));
    const first = await call("bulk_upsert_data_assets", { items });
    expect(first.createdAssetCount).toBe(2);
    expect(await readProperty(`${DIR}/IA_BCovBulkB`, "bConsumeInput")).toBe(false);

    const second = await call("bulk_upsert_data_assets", {
      items: [{ ...items[1], properties: { bConsumeInput: true } }],
    });
    expect(second.updatedAssetCount).toBe(1);
    expect(await readProperty(`${DIR}/IA_BCovBulkB`, "bConsumeInput")).toBe(true);
    expect(await readProperty(`${DIR}/IA_BCovBulkA`, "bConsumeInput")).toBe(false);
  }, 120_000);

  it("get_primary_asset_ids lists a PrimaryAssetLabel created under /Game", async () => {
    await call("create_data_asset", { name: "PAL_BCov", packagePath: DIR, className: "PrimaryAssetLabel" });
    let ids: Array<{ name: string; type: string; assetPath: string }> = [];
    for (let i = 0; i < 20; i++) {
      const r = await call("get_primary_asset_ids", { type: "PrimaryAssetLabel", maxResults: 5000 });
      ids = r.primaryAssetIds as typeof ids;
      if (ids.some((id) => id.name === "PAL_BCov")) break;
      await new Promise((resolve) => setTimeout(resolve, 250));
    }
    const mine = ids.find((id) => id.name === "PAL_BCov");
    expect(mine, "the new label is registered with the AssetManager").toBeDefined();
    expect(mine!.type).toBe("PrimaryAssetLabel");
    expect(mine!.assetPath).toContain(`${DIR}/PAL_BCov`);
  }, 120_000);
});
