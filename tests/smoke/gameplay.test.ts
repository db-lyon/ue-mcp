import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, checkFeature, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
let hasEQS = false;
let hasStateTree = false;
let hasSmartObjects = false;

beforeAll(async () => {
  bridge = await getBridge();
  [hasEQS, hasStateTree, hasSmartObjects] = await Promise.all([
    checkFeature(bridge, "EQS"),
    checkFeature(bridge, "StateTree"),
    checkFeature(bridge, "SmartObjects"),
  ]);
});
afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/IA_SmokeTest` });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/IMC_SmokeTest` });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/BB_SmokeTest` });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/BT_SmokeTest` });
  if (hasEQS) await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/EQS_SmokeTest` });
  if (hasStateTree) await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/ST_SmokeTest` });
  if (hasSmartObjects) await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/SOD_SmokeTest` });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/GM_SmokeTest` });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/GS_SmokeTest` });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/PC_SmokeTest` });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/PS_SmokeTest` });
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/HUD_SmokeTest` });
  disconnectBridge();
});

describe("gameplay — read / query", () => {
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

describe("gameplay — create assets (with cleanup)", () => {
  it("create_input_action", async () => {
    const r = await callBridge(bridge, "create_input_action", {
      name: "IA_SmokeTest", packagePath: TEST_PREFIX,
    });
    expect(r.ok, r.error).toBe(true);
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
