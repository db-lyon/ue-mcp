import { describe, expect, it, vi } from "vitest";
import { assetTool } from "../../src/tools/asset.js";
import { gameplayTool } from "../../src/tools/gameplay.js";
import { editorTool } from "../../src/tools/editor.js";
import { reflectionTool } from "../../src/tools/reflection.js";
import { actionSchema } from "../../src/surface/action-schema.js";
import { contractViolation } from "../../src/surface/handler-spec.js";
import { searchToolGraph } from "../../src/surface/context/tool-search.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { handlerSpecs as assetSpecs } from "../../src/tools/specs/asset.generated.js";
import { handlerSpecs as inputSpecs } from "../../src/tools/specs/gameplay.generated.js";
import type { ToolContext } from "../../src/core/types.js";

describe("collection and input edits", () => {
  it.each([
    { action: "set_property", method: "set_asset_property", params: { assetPath: "/Game/Data/DA_Profiles", propertyName: "Profiles[Id=Rifle].Presentation.PrimaryUse", value: null, save: false } },
    { action: "edit_collection_entry", method: "edit_collection_entry", params: { assetPath: "/Game/Data/DA_Profiles", propertyName: "Profiles", keyField: "Id", entryKey: "Rifle", newKey: "Carbine", overrides: { "Presentation.PrimaryUse": null }, dryRun: true, save: false } },
    { action: "edit_collection_entry", method: "edit_collection_entry", params: { assetPath: "/Game/Data/DA_Profiles", propertyName: "ProfileMap", entryKey: "Rifle", overrides: { CameraRelativeTransform: { Translation: { X: 1, Y: 2, Z: 3 } } }, save: false } },
    { action: "clone_datatable_rows", method: "clone_datatable_rows", params: { assetPath: "/Game/Data/DT_Items", clones: [{ sourceRow: "Rifle", rowName: "Carbine", overrides: { Count: 3 } }], dryRun: true, save: false } },
    { action: "duplicate", method: "duplicate_asset", params: { sourcePath: "/Game/Input/IA_Look", destinationPath: "/Game/Input/IA_ControllerLook", onConflict: "error" } },
  ])("forwards $action without losing nested values or persistence policy", async ({ action, method, params }) => {
    const response = { success: true, value: { Count: 3 }, persisted: false };
    const call = vi.fn().mockResolvedValue(response);
    const result = await assetTool.handler({ bridge: { call } } as unknown as ToolContext, { action, ...params });
    expect(call).toHaveBeenCalledWith(method, params, undefined);
    expect(result).toBe(response);
  });

  it("describes and validates structured row clone requests", () => {
    const schema = actionSchema(assetTool, "clone_datatable_rows");
    expect(schema.params.find(p => p.name === "clones")).toMatchObject({ required: true, type: "object[]" });
    expect(contractViolation(assetSpecs.clone_datatable_rows, { clones: [{ sourceRow: "A" }] })).toContain("rowName");
    expect(contractViolation(assetSpecs.clone_datatable_rows, { clones: [{ sourceRow: "A", rowName: "B", overrides: { Count: 2 } }] })).toBeUndefined();
  });

  it("replays a row-clone inverse without saving an unsaved batch", async () => {
    const payload = { assetPath: "/Game/Data/DT_Items", rowNames: ["First", "Second"], save: false };
    const response = { success: true, rollback: { method: "remove_datatable_row", payload } };
    const call = vi.fn().mockResolvedValueOnce(response).mockResolvedValueOnce({ success: true, persisted: false });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const result = await assetTool.handler(ctx, { action: "clone_datatable_rows", assetPath: payload.assetPath, clones: [{ sourceRow: "Source", rowName: "First" }, { sourceRow: "Source", rowName: "Second" }], save: false }) as typeof response;
    await assetTool.handler(ctx, { action: "remove_datatable_row", ...result.rollback.payload });
    expect(call).toHaveBeenLastCalledWith("remove_datatable_row", payload, undefined);
    expect(contractViolation(assetSpecs.remove_datatable_row, { ...payload, rowName: "Third" })).toBeDefined();
  });

  it.each([
    { imcPath: "/Game/Input/IMC_Test", mappingIndex: 0, settingBehavior: "OverrideSettings", playerMappableKeySettings: { Name: "Move", DisplayName: "Movement" }, save: false },
    { imcPath: "/Game/Input/IMC_Test", mappingIndex: 1, settingBehavior: "OverrideSettings", playerMappableKeySettings: { Name: "KeyboardMove", Metadata: null, SupportedKeyProfiles: [], DisplayCategory: "" }, save: false },
    { imcPath: "/Game/Input/IMC_Test", mappingIndex: 0, settingBehavior: "InheritSettingsFromAction", playerMappableKeySettings: null, dryRun: true },
    { inputActionPath: "/Game/Input/IA_Move", mappingName: "Move", displayName: "Movement", save: false },
  ])("preserves input settings payloads and read-back", async params => {
    const response = { success: true, settingBehavior: "OverrideSettings", playerMappableKeySettings: { properties: { Name: "Move" } } };
    const call = vi.fn().mockResolvedValue(response);
    expect(await gameplayTool.handler({ bridge: { call } } as unknown as ToolContext, { action: "set_player_mappable_settings", ...params })).toBe(response);
    expect(call).toHaveBeenCalledWith("set_player_mappable_settings", params, undefined);
  });

  it("requires one complete input target and refuses conflicting targets", () => {
    const spec = inputSpecs.set_player_mappable_settings;
    expect(contractViolation(spec, { imcPath: "/Game/Input/IMC_Test" })).toBeDefined();
    expect(contractViolation(spec, { imcPath: "/Game/Input/IMC_Test", mappingIndex: 0, inputActionPath: "/Game/Input/IA_Move" })).toBeDefined();
    expect(contractViolation(spec, { imcPath: "/Game/Input/IMC_Test", mappingIndex: 0, playerMappableKeySettings: null })).toBeUndefined();
  });

  it.each([
    { mappingIndex: 0, settingBehavior: "Unknown" },
    { mappingIndex: 0, playerMappableKeySettings: [] },
  ])("rejects invalid mapping settings in the action contract", params => {
    expect(contractViolation(inputSpecs.set_player_mappable_settings, {
      imcPath: "/Game/Input/IMC_Test", ...params,
    })).toBeDefined();
  });

  it("dispatches a static function on a component class with struct arguments", async () => {
    const params = { className: "/Script/Game.WeaponTuningComponent", functionName: "SeedProfile", args: { SourceId: "Rifle", DestinationId: "Carbine", Muzzle: { Translation: { X: 0, Y: 82.5, Z: -3.8 } } } };
    const response = { success: true, returnValues: { ReturnValue: "True" } };
    const call = vi.fn().mockResolvedValue(response);
    expect(await editorTool.handler({ bridge: { call } } as unknown as ToolContext, { action: "invoke_static_function", ...params })).toBe(response);
    expect(call).toHaveBeenCalledWith("invoke_static_function", params, undefined);
  });

  it("makes covered operations discoverable in action descriptions", () => {
    expect(assetTool.actions.set_property.description).toContain("edit_collection_entry");
    expect(assetTool.actions.duplicate.description).toContain("InputAction");
    expect(gameplayTool.actions.read_imc.description).toContain("SettingBehavior");
    expect(gameplayTool.actions.read_imc.description).toContain("PlayerMappableKeySettings");
    expect(editorTool.actions.invoke_static_function.description).toContain("actor component");
    expect(reflectionTool.actions.reflect_class.description).toContain("invoke_static_function");
  });

  it.each([
    ["update struct array element by WeaponId", "asset.set_property"],
    ["clone TMap entry nested overrides", "asset.edit_collection_entry"],
    ["clone datatable rows", "asset.clone_datatable_rows"],
    ["InputMappingContext SettingBehavior PlayerMappableKeySettings", "gameplay.set_player_mappable_settings"],
    ["duplicate InputAction triggers modifiers", "asset.duplicate"],
    ["call project static BlueprintCallable function class arguments return", "editor.invoke_static_function"],
  ])("finds %s among the first five catalog results", (query, action) => {
    const hits = searchToolGraph(ALL_TOOLS, query, 5).map(hit => `${hit.tool}.${hit.action}`);
    expect(hits).toContain(action);
  });
});
