// @live-editor headless
/**
 * Asset-level operations read back through a second action: texture import and
 * settings, enum and struct listing, moves, searches, the dirty-package sweep,
 * the bulk data asset rollback, input mapping reads and graph node removal.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { callBridge, closeEditorTabWindows, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AssetB/Misc";
const TEX = `${DIR}/T_BCovB_Single`;
const ENUM = `${DIR}/E_BCovB`;
const STRUCT = `${DIR}/F_BCovB`;
const IA = `${DIR}/IA_BCovB`;
const IMC = `${DIR}/IMC_BCovB`;
const CO = `${DIR}/CO_BCovB`;
const INPUT_ACTION = "/Script/EnhancedInput.InputAction";

/** A 1x1 PNG; only a valid image file is needed. */
const ONE_PIXEL_PNG = Buffer.from(
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==",
  "base64",
);

type Json = Record<string, unknown>;
type Row = { path: string; name: string; className?: string; class?: string };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let scratch = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function exists(assetPath: string): Promise<boolean> {
  return succeeded(await callBridge(bridge, "read_asset", { assetPath }));
}

async function textureInfo(assetPath: string): Promise<Json> {
  return call("get_texture_info", { assetPath });
}

function png(name: string): string {
  const file = join(scratch, name);
  writeFileSync(file, ONE_PIXEL_PNG);
  return file;
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = mkdtempSync(join(tmpdir(), "ue-mcp-bcov-asset-b-misc-"));
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}, 120_000);

afterAll(async () => {
  if (bridge) await closeEditorTabWindows(bridge);
  if (scratch) rmSync(scratch, { recursive: true, force: true });
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("textures", () => {
  it("import_texture creates a texture whose import source read_import_sources names", async () => {
    const file = png("bcov_single.png");
    const imported = await call("import_texture", { filePath: file, packagePath: DIR, name: "T_BCovB_Single" });
    expect(imported.importedCount).toBe(1);
    expect(await exists(TEX)).toBe(true);
    const info = await textureInfo(TEX);
    expect(info.sizeX).toBe(1);

    const sources = await call("read_import_sources", { assetPath: TEX });
    expect(sources.hasImportData).toBe(true);
    const first = (sources.sources as Array<{ relativeFilename: string; absolutePath?: string }>)[0];
    expect(`${first.relativeFilename} ${first.absolutePath ?? ""}`).toContain("bcov_single.png");
  }, 120_000);

  it("import_texture_batch imports every item", async () => {
    const batch = await call("import_texture_batch", {
      items: [
        { filePath: png("bcov_batch_a.png"), packagePath: DIR, name: "T_BCovB_BatchA" },
        { filePath: png("bcov_batch_b.png"), packagePath: DIR, name: "T_BCovB_BatchB" },
      ],
    });
    expect(batch.imported).toBe(2);
    expect(batch.failed).toBe(0);
    expect(await exists(`${DIR}/T_BCovB_BatchA`)).toBe(true);
    expect(await exists(`${DIR}/T_BCovB_BatchB`)).toBe(true);
  }, 180_000);

  it("set_texture_settings writes the settings get_texture_info reports", async () => {
    await call("set_texture_settings", { assetPath: TEX, compressionSettings: "Grayscale", lodGroup: "UI", sRGB: false, neverStream: true });
    const info = await textureInfo(TEX);
    expect(info.compressionSettings).toBe("Grayscale");
    expect(info.lodGroup).toBe("UI");
    expect(info.sRGB).toBe(false);
    expect(info.neverStream).toBe(true);
  }, 120_000);

  it("set_texture_settings_by_type applies each profile's compression, sRGB and LOD group", async () => {
    const set = await call("set_texture_settings_by_type", {
      groups: { normal: [`${DIR}/T_BCovB_BatchA`], baseColor: [`${DIR}/T_BCovB_BatchB`] },
    });
    expect(set.updatedCount).toBe(2);
    const normal = await textureInfo(`${DIR}/T_BCovB_BatchA`);
    expect(normal.compressionSettings).toBe("Normalmap");
    expect(normal.sRGB).toBe(false);
    expect(normal.lodGroup).toBe("CharacterNormalMap");
    const base = await textureInfo(`${DIR}/T_BCovB_BatchB`);
    expect(base.compressionSettings).toBe("Default");
    expect(base.sRGB).toBe(true);
    expect(base.lodGroup).toBe("Character");
  }, 120_000);
});

describe("user-defined types", () => {
  it("list_enum_values lists the enumerators an enum was created with", async () => {
    await call("create_user_defined_enum", { name: "E_BCovB", packagePath: DIR, values: ["Red", "Green"] });
    const listed = await call("list_enum_values", { assetPath: ENUM });
    expect(listed.isUserDefined).toBe(true);
    expect((listed.values as Array<{ displayName: string }>).map((v) => v.displayName)).toEqual(["Red", "Green"]);
  }, 120_000);

  it("rename_struct_field renames a member list_struct_fields reports, keeping its guid", async () => {
    await call("create_user_defined_struct", { name: "F_BCovB", packagePath: DIR, structFields: [{ name: "Speed", type: "float" }] });
    type Field = { friendlyName: string; guid: string };
    const before = (await call("list_struct_fields", { assetPath: STRUCT })).fields as Field[];
    const speed = before.find((f) => f.friendlyName === "Speed")!;
    expect(speed).toBeDefined();

    await call("rename_struct_field", { assetPath: STRUCT, fieldName: "Speed", newDisplayName: "Velocity" });
    const after = (await call("list_struct_fields", { assetPath: STRUCT })).fields as Field[];
    expect(after.map((f) => f.friendlyName)).not.toContain("Speed");
    expect(after.find((f) => f.friendlyName === "Velocity")?.guid).toBe(speed.guid);
  }, 120_000);
});

describe("moving assets", () => {
  it("move_asset puts the asset at its new path", async () => {
    const src = `${DIR}/MoveSrc/E_BCovB_Move`;
    const dst = `${DIR}/MoveDst/E_BCovB_Move`;
    await call("create_user_defined_enum", { name: "E_BCovB_Move", packagePath: `${DIR}/MoveSrc`, values: ["One"] });
    await call("move_asset", { sourcePath: src, destinationPath: dst });
    const listed = await call("list_enum_values", { assetPath: dst });
    expect((listed.values as Array<{ displayName: string }>).map((v) => v.displayName)).toEqual(["One"]);
    // What is left at the old path, if anything, is a redirector rather than the enum.
    const left = await call("search_assets", { query: "E_BCovB_Move", directory: `${DIR}/MoveSrc` });
    expect((left.results as Row[]).filter((r) => r.className === "UserDefinedEnum")).toHaveLength(0);
  }, 120_000);

  it("move_folder moves every asset under the folder", async () => {
    await call("create_user_defined_enum", { name: "E_BCovB_Folder", packagePath: `${DIR}/FolderSrc`, values: ["A"] });
    const moved = await call("move_folder", { sourcePath: `${DIR}/FolderSrc`, destinationPath: `${DIR}/FolderDst` });
    expect(moved.renamedCount).toBe(1);
    expect(moved.allSucceeded).toBe(true);
    const listed = await call("list_enum_values", { assetPath: `${DIR}/FolderDst/E_BCovB_Folder` });
    expect(listed.count).toBe(1);
  }, 120_000);
});

describe("searching", () => {
  it("search_assets finds an asset by name in a directory", async () => {
    await call("create_user_defined_enum", { name: "E_BCovB_SearchProbe", packagePath: `${DIR}/Search`, values: ["X"] });
    const found = await call("search_assets", { query: "SearchProbe", directory: `${DIR}/Search` });
    const rows = found.results as Row[];
    expect(rows.map((r) => r.path)).toContain(`${DIR}/Search/E_BCovB_SearchProbe`);
    expect(rows.find((r) => r.name === "E_BCovB_SearchProbe")?.className).toBe("UserDefinedEnum");
    const none = await call("search_assets", { query: "NoSuchBCovProbe", directory: `${DIR}/Search` });
    expect(none.results).toEqual([]);
  }, 120_000);

  it("reindex_assets_fts rescans a folder and search_assets_fts ranks its asset", async () => {
    await call("create_user_defined_enum", { name: "E_BCovBFtsProbe", packagePath: `${DIR}/Fts`, values: ["X"] });
    const reindexed = await call("reindex_assets_fts", { directory: `${DIR}/Fts` });
    expect(Number(reindexed.indexedCount)).toBeGreaterThanOrEqual(1);
    const found = await call("search_assets_fts", { query: "E_BCovBFtsProbe", classFilter: "UserDefinedEnum" });
    const rows = found.results as Row[];
    expect(rows.length).toBeGreaterThan(0);
    expect(rows[0].path).toBe(`${DIR}/Fts/E_BCovBFtsProbe`);
    expect(rows[0].class).toBe("UserDefinedEnum");
  }, 120_000);
});

describe("saving and rollback", () => {
  it("save_all_dirty writes a package left dirty by an unsaved edit", async () => {
    await call("create_data_asset", { name: "IA_BCovB_Dirty", packagePath: DIR, className: INPUT_ACTION });
    const path = `${DIR}/IA_BCovB_Dirty`;
    await call("set_asset_property", { assetPath: path, propertyName: "bConsumeInput", value: false, save: false });

    // A dirty /Temp/ package (the untitled map's external actors) has no file.
    // A plugin built before the sweep skipped those opens a modal Save As and
    // blocks the editor, so an empty sweep detects the build first.
    const isTemp = (name: string) => /^\/(Temp|Memory)\//.test(name);
    const dirty = await call("list_dirty_packages", {});
    const tempDirty = [...(dirty.content as Array<{ package: string }>), ...(dirty.maps as Array<{ package: string }>)]
      .map((d) => d.package)
      .filter(isTemp);
    const probe = await call("save_all_dirty", { saveMapPackages: false, saveContentPackages: false });
    expect(
      Array.isArray(probe.skippedUnsaveable) || tempDirty.length === 0,
      `this plugin build would open Save As for ${tempDirty.join(", ")} (pending rebuild)`,
    ).toBe(true);

    const swept = await call("save_all_dirty", { saveMapPackages: false });
    const saved = (swept.saved as Array<{ package: string; stillDirty: boolean }>).map((s) => s.package);
    expect(saved).toContain(path);
    expect(saved.filter(isTemp)).toEqual([]);
    expect((await call("list_dialogs", {})).count).toBe(0);
    const after = await call("save_asset", { assetPath: path });
    expect(after.wasDirty).toBe(false);
  }, 180_000);

  it("bulk_restore_data_assets replays an upsert's rollback, restoring updates and deleting creations", async () => {
    const existing = `${DIR}/IA_BCovB_Bulk`;
    await call("create_data_asset", { name: "IA_BCovB_Bulk", packagePath: DIR, className: INPUT_ACTION, properties: { bConsumeInput: true } });
    const upsert = await call("bulk_upsert_data_assets", {
      items: [
        { name: "IA_BCovB_Bulk", packagePath: DIR, className: INPUT_ACTION, properties: { bConsumeInput: false } },
        { name: "IA_BCovB_BulkNew", packagePath: DIR, className: INPUT_ACTION, properties: { bConsumeInput: false } },
      ],
    });
    expect(upsert.updatedAssetCount).toBe(1);
    expect(upsert.createdAssetCount).toBe(1);
    expect(await exists(`${DIR}/IA_BCovB_BulkNew`)).toBe(true);
    const rollback = upsert.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("bulk_restore_data_assets");

    const restored = await call("bulk_restore_data_assets", rollback.payload);
    expect(restored.restoredAssetCount).toBe(1);
    expect(restored.deletedAssetCount).toBe(1);
    expect(await exists(`${DIR}/IA_BCovB_BulkNew`)).toBe(false);
    const value = (await call("get_asset_properties", { assetPath: existing, propertyName: "bConsumeInput", valueFormat: "json" })).value;
    expect(String(value)).toBe("true");
  }, 180_000);
});

describe("input mapping and graphs", () => {
  it("read_imc reports a mapping added to the context", async () => {
    await call("create_input_action", { name: "IA_BCovB", packagePath: DIR });
    await call("create_input_mapping_context", { name: "IMC_BCovB", packagePath: DIR });
    await call("add_imc_mapping", { imcPath: IMC, inputActionPath: IA, key: "SpaceBar" });
    const read = await call("read_imc", { imcPath: IMC });
    expect(read.count).toBe(1);
    const mapping = (read.mappings as Array<{ key: string; inputActionName: string }>)[0];
    expect(mapping.key).toBe("SpaceBar");
    expect(mapping.inputActionName).toBe("IA_BCovB");
  }, 120_000);

  it("remove_graph_node deletes a node read_asset_graph no longer lists", async () => {
    await call("create_customizable_object", { name: "CO_BCovB", packagePath: DIR, save: false });
    const added = await call("add_graph_node", { assetPath: CO, nodeClass: "CONodeComponentSkeletalMesh", posX: -400, posY: 0, save: false });
    const guid = String(added.nodeGuid);
    const nodeGuids = async (): Promise<string[]> => {
      const read = await call("read_asset_graph", { assetPath: CO });
      return (read.graphs as Array<{ nodes: Array<{ nodeGuid: string }> }>).flatMap((g) => g.nodes.map((n) => n.nodeGuid));
    };
    expect(await nodeGuids()).toContain(guid);
    await call("remove_graph_node", { assetPath: CO, node: guid, save: false });
    expect(await nodeGuids()).not.toContain(guid);
  }, 120_000);
});
