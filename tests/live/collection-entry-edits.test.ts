// @live-editor headless
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

let bridge: EditorBridge;
const assetPath = `${TEST_PREFIX}/DA_Collection_${process.pid}`;
const tablePath = `${TEST_PREFIX}/DT_Collection_${process.pid}`;

async function call(method: string, params: Record<string, unknown>) {
  const response = await callBridge(bridge, method, params);
  expect(response.ok, response.error).toBe(true);
  const result = response.result as Record<string, any>;
  expect(result.success, result.error).not.toBe(false);
  return result;
}

beforeAll(async () => {
  bridge = await getBridge();
  await call("create_asset_by_class", { name: assetPath.split("/").at(-1), packagePath: TEST_PREFIX, className: "/Script/UE_MCP_Bridge.UEMCPCollectionEditTestAsset" });
  await call("create_datatable", { name: tablePath.split("/").at(-1), packagePath: TEST_PREFIX, rowStruct: "/Script/UE_MCP_Bridge.UEMCPCollectionEditRow" });
}, 60_000);

afterAll(async () => {
  for (const path of [assetPath, tablePath]) await callBridge(bridge, "delete_asset", { assetPath: path, force: true }).catch(() => {});
  disconnectBridge();
});

describe("native collection entry edits", () => {
  it("updates keyed array fields and clones with nested overrides, dry run and rollback", async () => {
    await call("set_asset_property", { assetPath, propertyName: "Profiles", value: [{ Id: "Rifle", Presentation: { CameraRelativeTransform: { Translation: { X: 1, Y: 2, Z: 3 }, Scale3D: { X: 2, Y: 3, Z: 4 } } } }], save: false });
    await call("set_asset_property", { assetPath, propertyName: "Profiles[Id=Rifle].Presentation.CameraRelativeTransform.Translation.X", value: 7, save: false });
    const params = { assetPath, propertyName: "Profiles", keyField: "Id", entryKey: "Rifle", newKey: "Carbine", overrides: { "Presentation.CameraRelativeTransform.Translation.Y": 82.5 }, save: false };
    const preview = await call("edit_collection_entry", { ...params, dryRun: true });
    expect(preview.proposedValue.Id).toBe("Carbine");
    expect((await call("read_asset_properties", { assetPath, propertyName: "Profiles", valueFormat: "json" })).value).toHaveLength(1);
    const clone = await call("edit_collection_entry", params);
    expect(clone.count).toBe(2);
    expect(clone.value.Id).toBe("Carbine");
    expect(clone.persisted).toBe(false);
    expect(clone.rollback.payload.save).toBe(false);
    const source = await call("read_asset_properties", { assetPath, propertyName: "Profiles[Id=Rifle].Presentation.CameraRelativeTransform.Translation", valueFormat: "json" });
    expect(source.value).toEqual({ x: 7, y: 2, z: 3 });
    const copy = await call("read_asset_properties", { assetPath, propertyName: "Profiles[Id=Carbine].Presentation.CameraRelativeTransform.Translation", valueFormat: "json" });
    expect(copy.value).toEqual({ x: 7, y: 82.5, z: 3 });
    expect(clone.value.Presentation.CameraRelativeTransform.scale).toEqual({ x: 2, y: 3, z: 4 });
    const duplicate = await callBridge(bridge, "edit_collection_entry", params);
    expect((duplicate.result as any).success).toBe(false);
    await call(clone.rollback.method, clone.rollback.payload);
    const restored = await call("read_asset_properties", { assetPath, propertyName: "Profiles", valueFormat: "json" });
    expect(restored.value).toHaveLength(1);
  });

  it("refuses missing or ambiguous array keys without changing the array", async () => {
    const profiles = [{ Id: "Duplicate" }, { Id: "Duplicate" }];
    await call("set_asset_property", { assetPath, propertyName: "Profiles", value: profiles, save: false });
    const before = await call("read_asset_properties", { assetPath, propertyName: "Profiles", valueFormat: "json" });
    for (const entryKey of ["Missing", "Duplicate"]) {
      const response = await callBridge(bridge, "edit_collection_entry", { assetPath, propertyName: "Profiles", keyField: "Id", entryKey, newKey: "Copy", save: false });
      expect((response.result as any).success).toBe(false);
    }
    expect((await call("read_asset_properties", { assetPath, propertyName: "Profiles", valueFormat: "json" })).value).toEqual(before.value);
  });

  it("clones and updates map values while preserving the source and refusing invalid patches", async () => {
    await call("set_asset_property", { assetPath, propertyName: "ProfileMap", value: { Rifle: { CameraRelativeTransform: { Translation: { X: 1, Y: 2, Z: 3 } } } }, save: false });
    const params = { assetPath, propertyName: "ProfileMap", entryKey: "Rifle", newKey: "Carbine", overrides: { "CameraRelativeTransform.Translation.Y": 82.5 }, save: false };
    const preview = await call("edit_collection_entry", { ...params, dryRun: true });
    expect(preview.proposedValue.CameraRelativeTransform.translation.y).toBe(82.5);
    expect(Object.keys((await call("read_asset_properties", { assetPath, propertyName: "ProfileMap", valueFormat: "json" })).value)).toEqual(["Rifle"]);
    const cloned = await call("edit_collection_entry", params);
    expect(cloned.count).toBe(2);
    expect(cloned.value.CameraRelativeTransform.translation).toEqual({ x: 1, y: 82.5, z: 3 });
    const updated = await call("edit_collection_entry", { assetPath, propertyName: "ProfileMap", entryKey: "Carbine", overrides: { "CameraRelativeTransform.Translation.Z": -3.8 }, save: true });
    expect(updated.count).toBe(2);
    expect(updated.persisted).toBe(true);
    expect(updated.value.CameraRelativeTransform.translation).toEqual({ x: 1, y: 82.5, z: -3.8 });
    const before = await call("read_asset_properties", { assetPath, propertyName: "ProfileMap", valueFormat: "json" });
    expect(before.value.Rifle.CameraRelativeTransform.translation).toEqual({ x: 1, y: 2, z: 3 });
    const invalid = await callBridge(bridge, "edit_collection_entry", { ...params, newKey: "Invalid", overrides: { MissingField: true } });
    expect((invalid.result as any).success).toBe(false);
    const after = await call("read_asset_properties", { assetPath, propertyName: "ProfileMap", valueFormat: "json" });
    expect(after.value).toEqual(before.value);
    await call(updated.rollback.method, updated.rollback.payload);
    await call(cloned.rollback.method, cloned.rollback.payload);
    const restored = await call("read_asset_properties", { assetPath, propertyName: "ProfileMap", valueFormat: "json" });
    expect(Object.keys(restored.value)).toEqual(["Rifle"]);
  });

  it("clones rows in a batch and rejects a bad later request before adding anything", async () => {
    await call("set_datatable_row", { assetPath: tablePath, rowName: "Source", row: { Count: 5, Presentation: { CameraRelativeTransform: { Translation: { X: 1, Y: 2, Z: 3 } } } } });
    const clones = [{ sourceRow: "Source", rowName: "First", overrides: { Count: 7 } }, { sourceRow: "Source", rowName: "Second", overrides: { "Presentation.CameraRelativeTransform.Translation.Y": 9 } }];
    const preview = await call("clone_datatable_rows", { assetPath: tablePath, clones, dryRun: true });
    expect(preview.rowCount).toBe(1);
    expect(preview.proposedRows).toHaveLength(2);
    const invalid = await callBridge(bridge, "clone_datatable_rows", { assetPath: tablePath, clones: [clones[0], { sourceRow: "Missing", rowName: "Bad" }] });
    expect((invalid.result as any).success).toBe(false);
    for (const request of [
      { sourceRow: "Source", rowName: "Bad", overrides: { MissingField: 5 } },
      { sourceRow: "Source", rowName: "first" },
      { sourceRow: "Source", rowName: "Source" },
    ]) {
      const rejected = await callBridge(bridge, "clone_datatable_rows", { assetPath: tablePath, clones: [clones[0], request] });
      expect((rejected.result as any).success).toBe(false);
    }
    const result = await call("clone_datatable_rows", { assetPath: tablePath, clones, save: false });
    expect(result.clonedCount).toBe(2);
    expect(result.rowCount).toBe(3);
    expect(result.rows[0].fields.Count).toBe(7);
    expect(result.rows[1].fields.Count).toBe(5);
    expect(result.persisted).toBe(false);
    const source = await call("get_datatable_row", { assetPath: tablePath, rowName: "Source" });
    expect(source.fields.Count).toBe("5");
    expect(result.rollback.payload.save).toBe(false);
    expect(result.rollback.method).toBe("remove_datatable_row");
    const removed = await call("remove_datatable_row", result.rollback.payload);
    expect(removed.removedRows).toEqual(["First", "Second"]);
    expect(removed.rowCount).toBe(1);
    expect(removed.persisted).toBe(false);
    expect((await call(result.rollback.method, result.rollback.payload)).alreadyDeleted).toBe(true);
    const saved = await call("clone_datatable_rows", { assetPath: tablePath, clones });
    expect(saved.saved).toBe(true);
    expect(saved.persisted).toBe(true);
  });

  it("invokes a static callable on a class outside the function-library hierarchy", async () => {
    const result = await call("invoke_static_function", { className: "/Script/UE_MCP_Bridge.UEMCPCollectionEditTestAsset", functionName: "DescribeEntry", args: { EntryId: "Rifle", Count: 3 } });
    expect(result.returnValues.ReturnValue).toBe("Rifle:3");
  });
});
