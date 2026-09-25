import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, checkFeature, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
let hasEQS = false;
let hasStateTree = false;
let hasSmartObjects = false;

const testAssets = [
  `${TEST_PREFIX}/IA_SmokeTest`,
  `${TEST_PREFIX}/IA_MappableSettings`,
  `${TEST_PREFIX}/IMC_SmokeTest`,
  `${TEST_PREFIX}/BB_SmokeTest`,
  `${TEST_PREFIX}/BT_SmokeTest`,
  `${TEST_PREFIX}/EQS_SmokeTest`,
  `${TEST_PREFIX}/ST_SmokeTest`,
  `${TEST_PREFIX}/SOD_SmokeTest`,
  `${TEST_PREFIX}/GM_SmokeTest`,
  `${TEST_PREFIX}/GS_SmokeTest`,
  `${TEST_PREFIX}/PC_SmokeTest`,
  `${TEST_PREFIX}/PS_SmokeTest`,
  `${TEST_PREFIX}/HUD_SmokeTest`,
];

beforeAll(async () => {
  bridge = await getBridge();
  [hasEQS, hasStateTree, hasSmartObjects] = await Promise.all([
    checkFeature(bridge, "EQS"),
    checkFeature(bridge, "StateTree"),
    checkFeature(bridge, "SmartObjects"),
  ]);
}, 60_000);
afterAll(async () => {
  for (const assetPath of testAssets) {
    if (assetPath.includes("EQS") && !hasEQS) continue;
    if (assetPath.includes("ST") && !hasStateTree) continue;
    if (assetPath.includes("SOD") && !hasSmartObjects) continue;
    await callBridge(bridge, "delete_asset", { assetPath }).catch(() => {});
  }
  disconnectBridge();
});

describe("gameplay - read / query", () => {
  it("get_navmesh_info", async () => {
    const r = await callBridge(bridge, "get_navmesh_info");
    expect(r.ok, r.error).toBe(true);
  });

  it("get_game_framework_info", async () => {
    const r = await callBridge(bridge, "get_game_framework_info");
    expect(r.ok, r.error).toBe(true);
  });

  it("list_input_assets", async () => {
    const r = await callBridge(bridge, "list_input_assets", { recursive: true });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_behavior_trees", async () => {
    const r = await callBridge(bridge, "list_behavior_trees", { recursive: true });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_eqs_queries", async ({ skip }) => {
    if (!hasEQS) skip();
    const r = await callBridge(bridge, "list_eqs_queries");
    expect(r.ok, r.error).toBe(true);
  });

  it("list_state_trees", async ({ skip }) => {
    if (!hasStateTree) skip();
    const r = await callBridge(bridge, "list_state_trees");
    expect(r.ok, r.error).toBe(true);
  });

  it("project_point_to_navigation", async () => {
    const r = await callBridge(bridge, "project_point_to_navigation", {
      location: { x: 0, y: 0, z: 0 },
    });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("gameplay - create assets (with cleanup)", () => {
  it("create_input_action", async () => {
    const r = await callBridge(bridge, "create_input_action", {
      name: "IA_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_input_action with valueType=Axis2D (#50)", async () => {
    const r = await callBridge(bridge, "create_input_action", {
      name: "IA_SmokeTestAxis2D", packagePath: TEST_PREFIX, valueType: "Axis2D",
    });
    expect(r.ok, r.error).toBe(true);
    // Verify the value type was actually applied via Python introspection
    const verify = await callBridge(bridge, "execute_python", {
      code: `import unreal\nia = unreal.load_asset('${TEST_PREFIX}/IA_SmokeTestAxis2D')\nprint("VALUETYPE:" + str(ia.value_type))`,
    });
    expect(verify.ok, verify.error).toBe(true);
    const output = JSON.stringify(verify.result);
    expect(output).toContain("AXIS2D");
    // Cleanup
    await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/IA_SmokeTestAxis2D` });
  });

  it("create_input_mapping_context", async () => {
    const r = await callBridge(bridge, "create_input_mapping_context", {
      name: "IMC_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_blackboard", async () => {
    const r = await callBridge(bridge, "create_blackboard", {
      name: "BB_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_behavior_tree", async () => {
    const r = await callBridge(bridge, "create_behavior_tree", {
      name: "BT_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_eqs_query", async ({ skip }) => {
    if (!hasEQS) skip();
    const r = await callBridge(bridge, "create_eqs_query", {
      name: "EQS_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_state_tree", async ({ skip }) => {
    if (!hasStateTree) skip();
    const r = await callBridge(bridge, "create_state_tree", {
      name: "ST_SmokeTest", packagePath: TEST_PREFIX,
    });
    // StateTree factory may fail in some UE5 versions
    if (r.error?.includes("Failed to create StateTree")) skip();
    expect(r.ok, r.error).toBe(true);
  });

  it("create_smart_object_definition", async ({ skip }) => {
    if (!hasSmartObjects) skip();
    const r = await callBridge(bridge, "create_smart_object_definition", {
      name: "SOD_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_game_mode", async () => {
    const r = await callBridge(bridge, "create_game_mode", {
      name: "GM_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_game_state", async () => {
    const r = await callBridge(bridge, "create_game_state", {
      name: "GS_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_player_controller", async () => {
    const r = await callBridge(bridge, "create_player_controller", {
      name: "PC_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_player_state", async () => {
    const r = await callBridge(bridge, "create_player_state", {
      name: "PS_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("create_hud", async () => {
    const r = await callBridge(bridge, "create_hud", {
      name: "HUD_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("gameplay - set_player_mappable_settings", () => {
  const assetName = "IA_MappableSettings";
  const assetPath = `${TEST_PREFIX}/${assetName}`;

  it("creates settings, reads them back, is idempotent, and rejects invalid mapping names", async () => {
    await callBridge(bridge, "delete_asset", { assetPath }).catch(() => {});
    const createdAction = await callBridge(bridge, "create_input_action", {
      name: assetName, packagePath: TEST_PREFIX,
    });
    expect(createdAction.ok, createdAction.error).toBe(true);

    const created = await callBridge(bridge, "set_player_mappable_settings", {
      inputActionPath: assetPath,
      mappingName: "Jump",
      displayName: "Jump",
      displayCategory: "Movement",
    });
    expect(created.ok, created.error).toBe(true);
    const createdResult = created.result as Record<string, unknown>;
    expect(createdResult.success, String(createdResult.error)).not.toBe(false);
    expect(createdResult.created).toBe(true);
    expect(createdResult.unchanged).toBe(false);
    expect(createdResult.mappingName).toBe("Jump");
    expect(createdResult.displayName).toBe("Jump");
    expect(createdResult.displayCategory).toBe("Movement");
    expect(String(createdResult.playerMappableKeySettings)).not.toBe("None");
    expect(createdResult.saved).toBe(true);
    expect(createdResult.persisted).toBe(true);
    expect(createdResult.packageDirty).toBe(false);

    const reloaded = await callBridge(bridge, "force_reload_asset", { assetPath });
    expect(reloaded.ok, reloaded.error).toBe(true);
    expect((reloaded.result as Record<string, unknown>).reloaded).toBe(true);

    const read = await callBridge(bridge, "read_input_action", { inputActionPath: assetPath });
    expect(read.ok, read.error).toBe(true);
    const readResult = read.result as Record<string, unknown>;
    expect(String(readResult.playerMappableKeySettings)).toBe(String(createdResult.playerMappableKeySettings));

    const named = await callBridge(bridge, "get_property", {
      objectPath: String(readResult.playerMappableKeySettings),
      propertyName: "Name",
    });
    expect(named.ok, named.error).toBe(true);
    const namedResult = named.result as Record<string, unknown>;
    expect(namedResult.value).toBe("Jump");

    const replay = await callBridge(bridge, "set_player_mappable_settings", {
      inputActionPath: assetPath,
      mappingName: "Jump",
      displayName: "Jump",
      displayCategory: "Movement",
    });
    expect(replay.ok, replay.error).toBe(true);
    const replayResult = replay.result as Record<string, unknown>;
    expect(replayResult.success, String(replayResult.error)).not.toBe(false);
    expect(replayResult.unchanged).toBe(true);
    expect(replayResult.updated).toBe(false);
    expect(replayResult.mappingName).toBe("Jump");
    expect(replayResult.displayName).toBe("Jump");
    expect(replayResult.displayCategory).toBe("Movement");

    const caseOnly = await callBridge(bridge, "set_player_mappable_settings", {
      inputActionPath: assetPath,
      mappingName: "jump",
    });
    expect(caseOnly.ok, caseOnly.error).toBe(true);
    const caseResult = caseOnly.result as Record<string, unknown>;
    expect(caseResult.success).toBe(true);
    expect(caseResult.unchanged).toBe(false);
    expect(caseResult.mappingName).toBe("jump");
    const inverse = caseResult.rollback as { method: string; payload: Record<string, unknown> };
    expect(inverse.method).toBe("set_player_mappable_settings");
    expect(inverse.payload.mappingName).toBe("Jump");
    expect(inverse.payload.save).toBe(true);
    const undone = await callBridge(bridge, inverse.method, inverse.payload);
    expect(undone.ok, undone.error).toBe(true);
    expect((undone.result as Record<string, unknown>).mappingName).toBe("Jump");

    const updated = await callBridge(bridge, "set_player_mappable_settings", {
      inputActionPath: assetPath,
      mappingName: "Jump",
      displayName: "Leap",
    });
    expect(updated.ok, updated.error).toBe(true);
    const updatedResult = updated.result as Record<string, unknown>;
    expect(updatedResult.success, String(updatedResult.error)).not.toBe(false);
    expect(updatedResult.updated).toBe(true);
    expect(updatedResult.unchanged).toBe(false);
    expect(updatedResult.displayName).toBe("Leap");
    expect(updatedResult.displayCategory).toBe("Movement");

    const deferred = await callBridge(bridge, "set_player_mappable_settings", {
      inputActionPath: assetPath, mappingName: "Jump", displayName: "Unsaved", save: false,
    });
    expect(deferred.ok, deferred.error).toBe(true);
    const deferredResult = deferred.result as Record<string, unknown>;
    expect(deferredResult.saved).toBe(false);
    expect(deferredResult.persisted).toBe(false);
    expect(deferredResult.packageDirty).toBe(true);
    const deferredRollback = deferredResult.rollback as { method: string; payload: Record<string, unknown> };
    expect(deferredRollback.payload.save).toBe(false);
    expect(deferredResult.rollbackLossy).toBe(true);
    const restored = await callBridge(bridge, deferredRollback.method, deferredRollback.payload);
    expect(restored.ok, restored.error).toBe(true);
    expect((restored.result as Record<string, unknown>).displayName).toBe("Leap");
    const flushed = await callBridge(bridge, "set_player_mappable_settings", {
      inputActionPath: assetPath, mappingName: "Jump", displayName: "Leap",
    });
    expect(flushed.ok, flushed.error).toBe(true);
    expect((flushed.result as Record<string, unknown>).saved).toBe(true);

    const empty = await callBridge(bridge, "set_player_mappable_settings", {
      inputActionPath: assetPath,
      mappingName: "",
    });
    expect(empty.ok, empty.error).toBe(true);
    const emptyResult = empty.result as Record<string, unknown>;
    expect(emptyResult.success).toBe(false);
    expect(String(emptyResult.error)).toMatch(/mappingName/i);
    expect(String(emptyResult.error)).toMatch(/non-empty|empty/i);

    const none = await callBridge(bridge, "set_player_mappable_settings", {
      inputActionPath: assetPath,
      mappingName: "None",
    });
    expect(none.ok, none.error).toBe(true);
    const noneResult = none.result as Record<string, unknown>;
    expect(noneResult.success).toBe(false);
    expect(String(noneResult.error)).toMatch(/mappingName/i);
    expect(String(noneResult.error)).toMatch(/None/i);
  });
});
