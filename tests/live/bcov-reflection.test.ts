// @live-editor headless
/**
 * Reflection: enum authoring read back through reflect_enum, class and instance
 * reflection over throwaway Blueprints, load probes, gameplay tag listing and a
 * save slot written by the test. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Reflection";
const ENUM_NAME = "E_BCovReflection";
const ENUM = `${DIR}/${ENUM_NAME}`;
const BASE = `${DIR}/BP_BCov_Reflection_Base`;
const BASE_CLASS = `${BASE}.BP_BCov_Reflection_Base_C`;
const CHILD = `${DIR}/BP_BCov_Reflection_Child`;
const CHILD_CLASS = `${CHILD}.BP_BCov_Reflection_Child_C`;
const SAVE = `${DIR}/BP_BCov_Reflection_Save`;
const SAVE_CLASS = `${SAVE}.BP_BCov_Reflection_Save_C`;
const SLOT = "BCov_Reflection_Slot";
const VAR = "BCovHealth";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function python(code: string): Promise<string> {
  const res = await call("execute_python", { code });
  return String(res.output ?? "");
}

const deleteSlot = `import unreal\nif unreal.GameplayStatics.does_save_game_exist("${SLOT}", 0):\n    unreal.GameplayStatics.delete_game_in_slot("${SLOT}", 0)\nprint("BCOV_SLOT_CLEARED")`;

async function cleanup(): Promise<void> {
  for (const path of [CHILD, BASE, SAVE, ENUM]) {
    await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  }
  await callBridge(bridge, "execute_python", { code: deleteSlot });
}

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
  await call("create_blueprint", { assetPath: BASE, parentClass: "Actor" });
  await call("add_variable", { assetPath: BASE, name: VAR, varType: "float" });
  await call("compile_blueprint", { assetPath: BASE });
  await call("create_blueprint", { assetPath: CHILD, parentClass: BASE_CLASS });
  await call("compile_blueprint", { assetPath: CHILD });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await cleanup();
    disconnectBridge();
  }
}, 120_000);

const displayNames = (res: Json): string[] => (res.values as Json[]).map((v) => String(v.displayName));

describe("enum authoring", () => {
  it("create_enum seeds entries and set_enum_entries replaces them, both read back through reflect_enum", async () => {
    const created = await call("create_enum", { name: ENUM_NAME, packagePath: DIR, entries: ["Alpha", { name: "Beta", displayName: "Beta Shown" }] });
    expect(created.entriesAdded).toBe(2);
    const enumPath = String(created.assetPath);
    expect(enumPath.startsWith(ENUM)).toBe(true);

    const first = await call("reflect_enum", { enumName: enumPath });
    expect(first.userDefined).toBe(true);
    expect(first.enumPath).toBe(enumPath);
    expect(displayNames(first)).toEqual(["Alpha", "Beta Shown"]);

    const replaced = await call("set_enum_entries", { assetPath: enumPath, entries: ["Gamma", "Delta", "Epsilon"] });
    expect(replaced.entries).toBe(3);
    expect((replaced.previousEntries as Json[]).map((e) => e.displayName)).toEqual(["Alpha", "Beta Shown"]);

    const second = await call("reflect_enum", { enumName: enumPath });
    expect(second.valueCount).toBe(3);
    expect(displayNames(second)).toEqual(["Gamma", "Delta", "Epsilon"]);
  }, 120_000);

  it("reflect_enum describes a native enum by short name", async () => {
    const res = await call("reflect_enum", { enumName: "ENetDormancy" });
    expect(res.userDefined).toBe(false);
    const names = (res.values as Json[]).map((v) => String(v.name));
    expect(names.some((n) => n.endsWith("DORM_DormantAll"))).toBe(true);
  }, 120_000);
});

describe("class and instance reflection", () => {
  it("reflect_class lists the Blueprint's own variable and its parent chain", async () => {
    const res = await call("reflect_class", { className: BASE_CLASS });
    expect(res.className).toBe("BP_BCov_Reflection_Base_C");
    expect(res.parentClass).toBe("Actor");
    expect(res.parentChain as string[]).toContain("Object");
    const names = (res.properties as Json[]).map((p) => p.name);
    expect(names).toContain(VAR);
    // Own properties only by default, so nothing inherited from Actor.
    expect(names).not.toContain("bReplicates");
  }, 120_000);

  it("list_classes with a Blueprint parentFilter returns that class and its Blueprint child only", async () => {
    const res = await call("list_classes", { parentFilter: BASE_CLASS, limit: 50 });
    const rows = res.classes as Json[];
    const names = rows.map((c) => c.name);
    expect(names).toContain("BP_BCov_Reflection_Base_C");
    expect(names).toContain("BP_BCov_Reflection_Child_C");
    expect(names).not.toContain("Actor");
    const child = rows.find((c) => c.name === "BP_BCov_Reflection_Child_C")!;
    expect(child.parent).toBe("BP_BCov_Reflection_Base_C");
    expect(String(child.path)).toBe(CHILD_CLASS);
  }, 120_000);

  it("reflect_instance reads a Blueprint default written through set_cdo_property", async () => {
    await call("set_cdo_property", { className: BASE_CLASS, propertyName: VAR, value: 42.5 });
    const res = await call("reflect_instance", { objectPath: BASE, filter: VAR });
    const rows = res.properties as Json[];
    expect(rows.map((r) => r.name)).toEqual([VAR]);
    expect(Number(rows[0].value)).toBeCloseTo(42.5, 3);
    expect(String(res.classPath)).toBe(BASE_CLASS);
  }, 120_000);

  it("is_class_loaded separates a loaded native class, a loaded Blueprint class and a missing one", async () => {
    const native = await call("is_class_loaded", { className: "StaticMeshActor" });
    expect(native.loaded).toBe(true);
    expect(native.module).toBe("Engine");
    expect(native.moduleLoaded).toBe(true);

    const bp = await call("is_class_loaded", { className: BASE_CLASS });
    expect(bp.exists).toBe(true);
    expect(bp.resolvedPath).toBe(BASE_CLASS);

    const missing = await call("is_class_loaded", { className: "BCovReflectionNoSuchClass" });
    expect(missing.loaded).toBe(false);
    expect(missing.exists).toBe(false);
  }, 120_000);
});

describe("gameplay tags and save games", () => {
  it("list_gameplay_tags filters by prefix over the project's declared tags", async () => {
    const res = await call("list_gameplay_tags", { filter: "MCPLive." });
    const tags = res.tags as string[];
    expect(tags).toContain("MCPLive.NotACue");
    expect(tags.every((t) => t.startsWith("MCPLive."))).toBe(true);
    expect(tags).not.toContain("GameplayCue.MCPLive.Probe");

    const cue = await call("list_gameplay_tags", { filter: "GameplayCue.MCPLive" });
    expect(cue.tags as string[]).toContain("GameplayCue.MCPLive.Probe");
  }, 120_000);

  it("inspect_save_game loads a slot the test wrote and reports its class", async () => {
    await call("create_blueprint", { assetPath: SAVE, parentClass: "SaveGame" });
    await call("compile_blueprint", { assetPath: SAVE });
    const out = await python(
      `import unreal\ncls = unreal.EditorAssetLibrary.load_blueprint_class("${SAVE}")\nobj = unreal.GameplayStatics.create_save_game_object(cls)\nprint("BCOV_SAVED", unreal.GameplayStatics.save_game_to_slot(obj, "${SLOT}", 0))`,
    );
    expect(out).toContain("BCOV_SAVED True");

    const res = await call("inspect_save_game", { slotName: SLOT });
    expect(res.slotName).toBe(SLOT);
    expect(res.className).toBe("BP_BCov_Reflection_Save_C");
    expect(res.classPath).toBe(SAVE_CLASS);
    expect(typeof res.propertyCount).toBe("number");
  }, 120_000);
});
