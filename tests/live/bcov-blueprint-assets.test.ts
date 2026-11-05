// @live-editor headless
/**
 * Whole-asset Blueprint actions and user-defined types, read back: duplicate,
 * diff, reparent and dependencies; enum order and tooltips; struct order,
 * defaults and metadata.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Blueprint";
const SOURCE = `${DIR}/BP_BCovSource`;
const COPY = `${DIR}/BP_BCovCopy`;
const ENUM = `${DIR}/E_BCovEnum`;
const STRUCT = `${DIR}/S_BCovStruct`;
const ALL = [COPY, SOURCE, ENUM, STRUCT];

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (j: Json, key: string): Row[] => (Array.isArray(j[key]) ? (j[key] as Row[]) : []);
const strings = (v: unknown): string[] => (Array.isArray(v) ? v.map(String) : []);

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function variableNames(path: string): Promise<string[]> {
  return rows(await call("list_blueprint_variables", { assetPath: path }), "variables").map((v) => String(v.name));
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const p of ALL) await callBridge(bridge, "delete_asset", { assetPath: p, force: true });
  await call("create_blueprint", { assetPath: SOURCE, parentClass: "Actor" });
  await call("add_variable", { assetPath: SOURCE, name: "BCovShared", varType: "int" });
  await call("create_user_defined_enum", { name: "E_BCovEnum", packagePath: DIR, values: ["Alpha", "Beta", "Gamma"] });
  await call("create_user_defined_struct", {
    name: "S_BCovStruct", packagePath: DIR,
    structFields: [{ name: "BCovHealth", type: "float" }, { name: "BCovLabel", type: "string" }],
  });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    for (const p of ALL) await callBridge(bridge, "delete_asset", { assetPath: p, force: true });
    disconnectBridge();
  }
});

describe("whole Blueprint assets", () => {
  it("duplicate_blueprint copies members, and diff_blueprint reports what changed on the copy", async () => {
    const dup = await call("duplicate_blueprint", { sourcePath: SOURCE, destinationPath: COPY });
    expect(String(dup.destinationPath)).toContain("BP_BCovCopy");
    expect(await variableNames(COPY)).toContain("BCovShared");

    await call("add_variable", { assetPath: COPY, name: "BCovCopyOnly", varType: "bool" });
    const diff = await call("diff_blueprint", { assetPath: SOURCE, otherPath: COPY });
    expect(diff.identical).toBe(false);
    const vars = (diff.variables ?? {}) as Json;
    expect(strings(vars.added)).toContain("BCovCopyOnly");
    expect(strings(vars.removed)).toEqual([]);
  }, 180_000);

  it("reparent_blueprint changes the parent the dependency and override readers see", async () => {
    const before = await call("get_blueprint_dependencies", { assetPath: COPY });
    expect(strings(before.classes)).toContain("/Script/Engine.Actor");

    const res = await call("reparent_blueprint", { assetPath: COPY, parentClass: "Pawn" });
    expect(res.parentClass).toBe("/Script/Engine.Pawn");
    expect(res.previousParent).toBe("/Script/Engine.Actor");

    const after = await call("get_blueprint_dependencies", { assetPath: COPY });
    expect(strings(after.classes)).toContain("/Script/Engine.Pawn");
    expect(strings(after.classes)).not.toContain("/Script/Engine.Actor");
    const overridable = await call("list_overridable_functions", { assetPath: COPY });
    expect(overridable.parentClass).toBe("Pawn");
    expect(await variableNames(COPY)).toContain("BCovShared");
  }, 180_000);

  it("get_blueprint_dependencies lists a class an object variable references", async () => {
    await call("add_variable", { assetPath: SOURCE, name: "BCovMeshRef", varType: "object:/Script/Engine.StaticMesh" });
    const deps = await call("get_blueprint_dependencies", { assetPath: SOURCE });
    expect(strings(deps.classes)).toContain("/Script/Engine.StaticMesh");
    expect(deps.classCount).toBe(strings(deps.classes).length);
  }, 120_000);
});

describe("user-defined enum", () => {
  it("reorder_enum_values and set_enum_metadata show up in read_user_defined_enum", async () => {
    const initial = await call("read_user_defined_enum", { assetPath: ENUM });
    expect(initial.isUserDefined).toBe(true);
    expect(rows(initial, "values").map((v) => v.displayName)).toEqual(["Alpha", "Beta", "Gamma"]);

    const reordered = await call("reorder_enum_values", { assetPath: ENUM, order: ["Gamma", "Alpha", "Beta"] });
    expect(Number(reordered.moves)).toBeGreaterThan(0);
    let read = await call("read_user_defined_enum", { assetPath: ENUM });
    expect(rows(read, "values").map((v) => v.displayName)).toEqual(["Gamma", "Alpha", "Beta"]);

    const meta = await call("set_enum_metadata", { assetPath: ENUM, entries: [{ name: "Alpha", tooltip: "The first letter" }] });
    expect(meta.tooltipsWritten).toBe(1);
    read = await call("read_user_defined_enum", { assetPath: ENUM });
    expect(rows(read, "values").find((v) => v.displayName === "Alpha")?.tooltip).toBe("The first letter");
  }, 120_000);
});

describe("user-defined struct", () => {
  it("reorder_struct_fields reverses the members read_user_defined_struct lists", async () => {
    const initial = await call("read_user_defined_struct", { assetPath: STRUCT });
    const before = rows(initial, "fields").map((f) => String(f.name));
    expect(before).toEqual(expect.arrayContaining(["BCovHealth", "BCovLabel"]));

    const reversed = [...before].reverse();
    const res = await call("reorder_struct_fields", { assetPath: STRUCT, order: reversed });
    expect(res.complete).toBe(true);
    const after = await call("read_user_defined_struct", { assetPath: STRUCT });
    expect(rows(after, "fields").map((f) => String(f.name))).toEqual(reversed);
  }, 120_000);

  it("set_struct_field_default stores a member default", async () => {
    const res = await call("set_struct_field_default", { assetPath: STRUCT, fieldName: "BCovHealth", defaultValue: "42" });
    expect(res.fieldName).toBe("BCovHealth");
    const read = await call("read_user_defined_struct", { assetPath: STRUCT });
    const field = rows(read, "fields").find((f) => f.name === "BCovHealth");
    expect(Number(field?.defaultValue)).toBe(42);
  }, 120_000);

  it("edit_struct_metadata writes the struct and member tooltips", async () => {
    const res = await call("edit_struct_metadata", {
      assetPath: STRUCT, tooltip: "BCov struct tip",
      fields: [{ fieldName: "BCovLabel", tooltip: "Display label", saveGame: true }],
    });
    expect(res.fieldsEdited).toBe(1);
    const read = await call("read_user_defined_struct", { assetPath: STRUCT });
    expect(read.tooltip).toBe("BCov struct tip");
    const label = rows(read, "fields").find((f) => f.name === "BCovLabel");
    expect(label?.tooltip).toBe("Display label");
    expect(label?.saveGame).toBe(true);
  }, 120_000);
});
