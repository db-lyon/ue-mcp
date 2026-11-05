// @live-editor headless
/**
 * `material(list_parameters)` loading a MaterialInstanceConstant (#1183).
 *
 * A .uasset copied or renamed outside the editor keeps the object name it was saved with, so its package path no
 * longer names it. The bare package path is the form an agent has, and it answered "Failed to load material" for an
 * instance the Asset Registry listed. It now resolves to the package's one asset; an explicit wrong object name still
 * fails, with the registry's object path in the error.
 *
 * Runs only against the dedicated disposable test project.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX } from "../setup.js";
import { TEST_PROJECT_DIR } from "../../scripts/bridge-target.mjs";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const FOLDER = `${TEST_PREFIX}/MatLoad1183`;
const SOURCE = `${FOLDER}/MI_Source1183`;
const COPY = `${FOLDER}/MI_Copy1183`;
// The copy's object keeps the source's name, and that object path is what deletes it.
const COPY_OBJECT = `${COPY}.MI_Source1183`;
const CONTENT_DIR = path.join(TEST_PROJECT_DIR, "Content", ...FOLDER.replace(/^\/Game\//, "").split("/"));

type Json = Record<string, unknown>;
let bridge: EditorBridge;

beforeAll(async () => {
  bridge = await getBridge();
  for (const p of [COPY_OBJECT, SOURCE]) await callBridge(bridge, "delete_asset", { assetPath: p, force: true });
  const created = await callBridge(bridge, "create_material_instance", {
    parentPath: "/Engine/BasicShapes/BasicShapeMaterial",
    name: "MI_Source1183",
    packagePath: FOLDER,
  });
  expect(created.ok, created.error).toBe(true);
  expect((created.result as Json).success, JSON.stringify(created.result)).toBe(true);

  // The copy is made on disk, the way a file manager or a version control move makes one.
  fs.copyFileSync(path.join(CONTENT_DIR, "MI_Source1183.uasset"), path.join(CONTENT_DIR, "MI_Copy1183.uasset"));
  const scanned = await callBridge(bridge, "execute_python", {
    code: `import unreal\nunreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous(['${COPY}'], True)\n`,
  });
  expect(scanned.ok, scanned.error).toBe(true);
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  for (const p of [COPY_OBJECT, SOURCE]) await callBridge(bridge, "delete_asset", { assetPath: p, force: true });
  disconnectBridge();
}, 120_000);

describe("list_material_parameters on a MaterialInstanceConstant (#1183)", () => {
  it("loads an instance whose object is not named after its package, by the package path", async () => {
    const listed = await callBridge(bridge, "list_material_parameters", { assetPath: COPY });
    expect(listed.ok, listed.error).toBe(true);
    const body = listed.result as Json;
    expect(body.success, JSON.stringify(body)).toBe(true);
    expect(body.assetType).toBe("MaterialInstanceConstant");
    expect(body.path).toBe(COPY_OBJECT);
    expect(body.parentPath).toBe("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
    expect(Array.isArray(body.scalarParameters)).toBe(true);
  });

  it("still refuses a wrong explicit object name, naming the path the registry has", async () => {
    const refused = await callBridge(bridge, "list_material_parameters", { assetPath: `${COPY}.MI_Copy1183` });
    expect(refused.ok, refused.error).toBe(true);
    const body = refused.result as Json;
    expect(body.success).toBe(false);
    expect(body.registryObjectPath).toBe(COPY_OBJECT);
  });
});
