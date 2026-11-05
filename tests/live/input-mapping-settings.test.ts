// @live-editor headless
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

let bridge: EditorBridge;
let fixtureIndex = 0;
let inputActionPath: string;
let imcPath: string;
let duplicatePath: string;

async function call(method: string, params: Record<string, unknown>) {
  const response = await callBridge(bridge, method, params);
  expect(response.ok, response.error).toBe(true);
  const result = response.result as Record<string, any>;
  expect(result.success, result.error).not.toBe(false);
  return result;
}

beforeEach(async () => {
  const suffix = `${process.pid}_${++fixtureIndex}`;
  inputActionPath = `${TEST_PREFIX}/IA_Settings_${suffix}`;
  imcPath = `${TEST_PREFIX}/IMC_Settings_${suffix}`;
  duplicatePath = `${TEST_PREFIX}/IA_SettingsCopy_${suffix}`;
  bridge = await getBridge();
  await call("create_input_action", { name: inputActionPath.split("/").at(-1), packagePath: TEST_PREFIX, valueType: "Axis1D" });
  await call("create_input_mapping_context", { name: imcPath.split("/").at(-1), packagePath: TEST_PREFIX });
  await call("add_imc_mapping", { imcPath, inputActionPath, key: "Gamepad_LeftX" });
  await call("add_imc_mapping", { imcPath, inputActionPath, key: "A" });
  await call("set_player_mappable_settings", { inputActionPath, mappingName: "Move", displayName: "Move action", save: false });
}, 60_000);

afterEach(async () => {
  for (const path of [imcPath, duplicatePath, inputActionPath]) await callBridge(bridge, "delete_asset", { assetPath: path, force: true }).catch(() => {});
  disconnectBridge();
});

describe("per-mapping player settings", () => {
  it("reports inheritance and edits one row with preview, rollback and clear", async () => {
    const initial = await call("read_imc", { imcPath });
    expect(initial.mappings[0].settingBehavior).toBe("InheritSettingsFromAction");
    expect(initial.mappings[0].playerMappableKeySettings).toBeNull();
    expect(initial.mappings[0].effectivePlayerMappableKeySettings.properties.Name).toBe("Move");
    for (const mappingIndex of [-1, 0.5, initial.mappings.length]) {
      const invalid = await callBridge(bridge, "set_player_mappable_settings", { imcPath, mappingIndex, settingBehavior: "IgnoreSettings", save: false });
      expect((invalid.result as any).success).toBe(false);
    }
    const params = { imcPath, mappingIndex: 0, settingBehavior: "OverrideSettings", playerMappableKeySettings: { Name: "ControllerMove", DisplayName: "Controller movement", DisplayCategory: "Movement" }, save: false };
    const preview = await call("set_player_mappable_settings", { ...params, dryRun: true });
    expect(preview.proposedMapping.settingBehavior).toBe("OverrideSettings");
    expect(preview.proposedMapping.playerMappableKeySettings.properties.Name).toBe("ControllerMove");
    expect(preview.proposedMapping.effectivePlayerMappableKeySettings).toEqual(preview.proposedMapping.playerMappableKeySettings);
    expect((await call("read_imc", { imcPath })).mappings).toEqual(initial.mappings);
    const written = await call("set_player_mappable_settings", params);
    expect(written.settingBehavior).toBe("OverrideSettings");
    expect(written.playerMappableKeySettings.properties.Name).toBe("ControllerMove");
    expect(written.effectivePlayerMappableKeySettings).toEqual(written.playerMappableKeySettings);
    expect(written.persisted).toBe(false);
    expect(written.rollback.payload.settingBehavior).toBe("InheritSettingsFromAction");
    expect(written.rollback.payload.save).toBe(false);
    const read = await call("read_imc", { imcPath });
    expect(read.mappings[1]).toEqual(initial.mappings[1]);
    await call(written.rollback.method, written.rollback.payload);
    expect((await call("read_imc", { imcPath })).mappings).toEqual(initial.mappings);
    await call("set_player_mappable_settings", params);
    const ignored = await call("set_player_mappable_settings", { imcPath, mappingIndex: 0, settingBehavior: "IgnoreSettings", save: false });
    expect(ignored.settingBehavior).toBe("IgnoreSettings");
    expect(ignored.effectivePlayerMappableKeySettings).toBeNull();
    expect(ignored.playerMappableKeySettings.properties.Name).toBe("ControllerMove");
    const invalid = await callBridge(bridge, "set_player_mappable_settings", { ...params, playerMappableKeySettings: { MissingField: true } });
    expect((invalid.result as any).success).toBe(false);
    const cleared = await call("set_player_mappable_settings", { imcPath, mappingIndex: 0, settingBehavior: "InheritSettingsFromAction", playerMappableKeySettings: null, save: false });
    expect(cleared.settingBehavior).toBe("InheritSettingsFromAction");
    expect(cleared.playerMappableKeySettings).toBeNull();
    expect(cleared.effectivePlayerMappableKeySettings.properties.Name).toBe("Move");
    const saved = await call("set_player_mappable_settings", { ...params, save: true });
    expect(saved.persisted).toBe(true);
  });

  it("round-trips default settings fields when rolling back an override patch", async () => {
    const params = { imcPath, mappingIndex: 1, save: false };
    const created = await call("set_player_mappable_settings", { ...params, settingBehavior: "OverrideSettings", mappingName: "KeyboardMove" });
    const before = (await call("read_imc", { imcPath })).mappings;
    expect(before[1].playerMappableKeySettings.properties.SupportedKeyProfiles).toEqual([]);
    const patched = await call("set_player_mappable_settings", { ...params, playerMappableKeySettings: { Metadata: inputActionPath, DisplayCategory: "Movement" } });
    expect(patched.playerMappableKeySettings.properties.Metadata).not.toEqual(before[1].playerMappableKeySettings.properties.Metadata);
    expect(patched.playerMappableKeySettings.properties.DisplayCategory).toBe("Movement");
    expect(patched.rollback.payload.playerMappableKeySettings.SupportedKeyProfiles).toEqual([]);
    await call(patched.rollback.method, patched.rollback.payload);
    expect((await call("read_imc", { imcPath })).mappings).toEqual(before);
    await call(created.rollback.method, created.rollback.payload);
    const restored = (await call("read_imc", { imcPath })).mappings[1];
    expect(restored.settingBehavior).toBe("InheritSettingsFromAction");
    expect(restored.playerMappableKeySettings).toBeNull();
    expect(restored.effectivePlayerMappableKeySettings.properties.Name).toBe("Move");
  });

  it("owns independent override subobjects on two rows of the same context", async () => {
    const first = await call("set_player_mappable_settings", { imcPath, mappingIndex: 0, settingBehavior: "OverrideSettings", mappingName: "ControllerMove", save: false });
    const second = await call("set_player_mappable_settings", { imcPath, mappingIndex: 1, settingBehavior: "OverrideSettings", mappingName: "KeyboardMove", save: false });
    const outerPath = `${imcPath}.${imcPath.split("/").at(-1)}:`;
    expect(first.playerMappableKeySettings.objectPath.startsWith(outerPath)).toBe(true);
    expect(second.playerMappableKeySettings.objectPath.startsWith(outerPath)).toBe(true);
    expect(second.playerMappableKeySettings.objectPath).not.toBe(first.playerMappableKeySettings.objectPath);
    const before = (await call("read_imc", { imcPath })).mappings;
    expect(before[0].playerMappableKeySettings).toEqual(first.playerMappableKeySettings);
    expect(before[1].playerMappableKeySettings).toEqual(second.playerMappableKeySettings);
    const patched = await call("set_player_mappable_settings", { imcPath, mappingIndex: 1, mappingName: "EditedKeyboardMove", save: false });
    const after = (await call("read_imc", { imcPath })).mappings;
    expect(after[0]).toEqual(before[0]);
    expect(after[1].effectivePlayerMappableKeySettings.properties.Name).toBe("EditedKeyboardMove");
    expect(after[1].playerMappableKeySettings.objectPath).toBe(second.playerMappableKeySettings.objectPath);
    await call(patched.rollback.method, patched.rollback.payload);
    expect((await call("read_imc", { imcPath })).mappings).toEqual(before);
  });

  it("duplicates triggers, modifiers and mappable settings as independent subobjects", async () => {
    await call("set_action_triggers", { inputActionPath, triggers: [{ type: "Hold", HoldTimeThreshold: 0.75 }], modifiers: [{ type: "Negate" }] });
    const original = await call("read_input_action", { inputActionPath });
    await call("duplicate_asset", { sourcePath: inputActionPath, destinationPath: duplicatePath, onConflict: "error" });
    const copied = await call("read_input_action", { inputActionPath: duplicatePath });
    expect(copied.triggerCount).toBe(1);
    expect(copied.modifierCount).toBe(1);
    expect(copied.triggers[0].properties).toEqual(original.triggers[0].properties);
    expect(copied.triggers[0].objectPath).not.toBe(original.triggers[0].objectPath);
    expect(copied.modifiers[0].properties).toEqual(original.modifiers[0].properties);
    expect(copied.modifiers[0].objectPath).not.toBe(original.modifiers[0].objectPath);
    expect(original.playerMappableKeySettings).not.toBe("None");
    expect(copied.playerMappableKeySettings).not.toBe("None");
    expect(copied.playerMappableKeySettings).not.toBe(original.playerMappableKeySettings);
    const copiedName = await call("read_asset_properties", { assetPath: duplicatePath, propertyName: "PlayerMappableKeySettings.Name", valueFormat: "json" });
    expect(copiedName.value).toBe("Move");
    await call("set_player_mappable_settings", { inputActionPath: duplicatePath, mappingName: "CopiedMove", save: false });
    const originalName = await call("read_asset_properties", { assetPath: inputActionPath, propertyName: "PlayerMappableKeySettings.Name", valueFormat: "json" });
    expect(originalName.value).toBe("Move");
    const editedCopyName = await call("read_asset_properties", { assetPath: duplicatePath, propertyName: "PlayerMappableKeySettings.Name", valueFormat: "json" });
    expect(editedCopyName.value).toBe("CopiedMove");
    const read = await call("read_imc", { imcPath });
    expect(read.mappings[1].effectivePlayerMappableKeySettings.properties.Name).toBe("Move");
  });

  it("validates InputAction previews without changing the existing settings", async () => {
    const before = await call("read_input_action", { inputActionPath });
    const preview = await call("set_player_mappable_settings", { inputActionPath, mappingName: "PreviewMove", displayName: "Preview movement", dryRun: true });
    expect(preview.dryRun).toBe(true);
    expect(preview.mappingName).toBe("PreviewMove");
    expect(await call("read_input_action", { inputActionPath })).toEqual(before);
  });
});
