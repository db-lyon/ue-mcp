// @live-editor headless
/**
 * Game framework Blueprints, the world's default game mode, AI perception,
 * StateTree and SmartObject authoring, each write read back through another
 * action (read_blueprint, get_world_settings, read_perception, the StateTree
 * runtime reader and list_smart_object_slots).
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Gameplay/Framework";
const GM = `${DIR}/GM_BCov`;
const AI_BP = `${DIR}/BP_BCovAIController`;
const ST = `${DIR}/ST_BCov`;
const ST_BP = `${DIR}/BP_BCovStateTreeActor`;
const SO_BP = `${DIR}/BP_BCovSmartObjectActor`;
const SOD = `${DIR}/SOD_BCov`;
const ST_ACTOR = "BCov_Gameplay_StateTreeActor";
/** Ships in the SmartObjects plugin's own UncookedOnly module, as tests/smoke/asset-array-append.test.ts relies on. */
const BEHAVIOR = "/Script/SmartObjectsTestSuite.SmartObjectTestBehaviorDefinition";

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Row[] => (Array.isArray(v) ? (v as Row[]) : []);

let bridge: EditorBridge;
/** The world's DefaultGameMode before this file touched it, restored in afterAll. */
let priorGameMode: string | null = null;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function componentClasses(blueprintPath: string): Promise<string[]> {
  const bp = await call("read_blueprint", { assetPath: blueprintPath });
  return rows(bp.components).map((c) => String(c.class));
}

async function restoreGameMode(): Promise<void> {
  if (priorGameMode === null) return;
  await callBridge(bridge, "set_world_settings", { defaultGameMode: priorGameMode });
  priorGameMode = null;
}

async function wipe(): Promise<void> {
  await callBridge(bridge, "delete_actor", { actorLabel: ST_ACTOR });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await wipe();
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await restoreGameMode();
    await wipe();
    disconnectBridge();
  }
});

describe("game framework Blueprints", () => {
  it("create_game_mode, create_game_state, create_player_controller, create_player_state and create_hud make Blueprints of the right parent", async () => {
    const parentOf = async (name: string) => (await call("read_blueprint", { assetPath: `${DIR}/${name}` })).parentClass;
    expect(String((await call("create_game_mode", { name: "GM_BCov", packagePath: DIR })).path)).toContain("GM_BCov");
    expect(await parentOf("GM_BCov")).toBe("GameModeBase");
    expect(String((await call("create_game_state", { name: "GS_BCov", packagePath: DIR })).path)).toContain("GS_BCov");
    expect(await parentOf("GS_BCov")).toBe("GameStateBase");
    expect(String((await call("create_player_controller", { name: "PC_BCov", packagePath: DIR })).path)).toContain("PC_BCov");
    expect(await parentOf("PC_BCov")).toBe("PlayerController");
    expect(String((await call("create_player_state", { name: "PS_BCov", packagePath: DIR })).path)).toContain("PS_BCov");
    expect(await parentOf("PS_BCov")).toBe("PlayerState");
    expect(String((await call("create_hud", { name: "HUD_BCov", packagePath: DIR })).path)).toContain("HUD_BCov");
    expect(await parentOf("HUD_BCov")).toBe("HUD");
    const customParent = await call("create_game_mode", { name: "GM_BCovFull", packagePath: DIR, parentClass: "GameMode" });
    expect(String(customParent.path)).toContain("GM_BCovFull");
    expect(await parentOf("GM_BCovFull")).toBe("GameMode");
  }, 180_000);

  it("set_world_game_mode writes the WorldSettings default game mode get_world_settings reads", async () => {
    const before = await call("get_world_settings");
    priorGameMode = String(before.defaultGameMode ?? "None");
    try {
      const set = await call("set_world_game_mode", { gameModeClass: `${GM}.GM_BCov_C` });
      expect(String(set.gameModeClass)).toContain("GM_BCov_C");
      const after = await call("get_world_settings");
      expect(String(after.defaultGameMode)).toContain("GM_BCov_C");
    } finally {
      await restoreGameMode();
    }
    const restored = await call("get_world_settings");
    expect(String(restored.defaultGameMode)).not.toContain("GM_BCov_C");
  }, 120_000);

  it("get_game_framework_info reports the editor world, which has no authority game mode or game state", async () => {
    const info = await call("get_game_framework_info");
    expect(info.gameMode).toBe("none");
    expect(info.gameState).toBe("none");
    expect(info.playerControllerClass).toBeUndefined();
  }, 60_000);
});

describe("AI perception", () => {
  it("add_perception_component and configure_ai_perception_sense author senses read_perception reports", async () => {
    await call("create_blueprint", { name: "BP_BCovAIController", packagePath: DIR, parentClass: "AIController" });
    const added = await call("add_perception_component", { blueprintPath: AI_BP, senses: ["Sight"] });
    expect(added.created).toBe(true);
    expect(await componentClasses(AI_BP)).toContain("AIPerceptionComponent");

    let perception = await call("read_perception", { blueprintPath: AI_BP });
    expect(rows(perception.senses).map((s) => s.senseType)).toEqual(["Sight"]);

    const configured = await call("configure_ai_perception_sense", {
      blueprintPath: AI_BP, senseType: "Hearing", settings: { HearingRange: 1234 },
    });
    expect(String(configured.senseConfig)).toContain("Hearing");
    await call("configure_ai_perception_sense", { blueprintPath: AI_BP, senseType: "Sight", settings: { SightRadius: 2345 } });

    perception = await call("read_perception", { blueprintPath: AI_BP });
    const senses = rows(perception.senses);
    expect(senses.map((s) => s.senseType).sort()).toEqual(["Hearing", "Sight"]);
    const hearing = senses.find((s) => s.senseType === "Hearing")!;
    expect(String((hearing.parameters as Row).HearingRange)).toMatch(/^1234(\.0*)?$/);
    const sight = senses.find((s) => s.senseType === "Sight")!;
    expect(String((sight.parameters as Row).SightRadius)).toMatch(/^2345(\.0*)?$/);
  }, 180_000);
});

describe("StateTree", () => {
  it("create_state_tree compiles an asset list_state_trees lists", async () => {
    const created = await call("create_state_tree", { name: "ST_BCov", packagePath: DIR });
    expect(created.compiled).toBe(true);
    const listed = await call("list_state_trees", { limit: 2000 });
    expect(rows(listed.stateTrees).some((t) => String(t.path).startsWith(ST))).toBe(true);
  }, 120_000);

  it("add_state_tree_component adds a component get_state_tree_runtime reads its tree from", async () => {
    await call("create_blueprint", { name: "BP_BCovStateTreeActor", packagePath: DIR, parentClass: "Actor" });
    const added = await call("add_state_tree_component", { blueprintPath: ST_BP });
    expect(added.component).toBe("StateTreeComp");
    expect(await componentClasses(ST_BP)).toContain("StateTreeComponent");

    await call("place_actor", { actorClass: ST_BP, label: ST_ACTOR, location: { x: 0, y: -6000, z: 100 } });
    await call("set_component_property", {
      actorLabel: ST_ACTOR, componentName: "StateTreeComp", propertyName: "StateTreeRef.StateTree", value: `${ST}.ST_BCov`,
    });
    const runtime = await call("get_state_tree_runtime", { actorLabel: ST_ACTOR, world: "editor" });
    expect(runtime.component).toBe("StateTreeComp");
    expect(String(runtime.stateTree)).toContain("ST_BCov");
    // Nothing runs a StateTree in the editor world.
    expect(runtime.running).toBe(false);
  }, 180_000);
});

describe("SmartObjects", () => {
  const slots = async (): Promise<Json> => call("list_smart_object_slots", { assetPath: SOD });

  it("create_smart_object_definition and the slot actions author slots list_smart_object_slots reads back", async () => {
    const created = await call("create_smart_object_definition", { name: "SOD_BCov", packagePath: DIR, defaultBehaviorClass: BEHAVIOR });
    expect(created.defaultBehaviorCount).toBe(1);

    const a = await call("add_smart_object_slot", { assetPath: SOD, name: "SlotA", offset: { x: 10, y: 20, z: 30 }, behaviorClass: BEHAVIOR });
    expect(a.slotIndex).toBe(0);
    const b = await call("add_smart_object_slot", { assetPath: SOD, name: "SlotB", offset: { x: 1, y: 2, z: 3 } });
    expect(b.slotIndex).toBe(1);

    let listed = await slots();
    expect(listed.slotCount).toBe(2);
    let entries = rows(listed.slots);
    expect(entries[0].offset).toEqual({ x: 10, y: 20, z: 30 });
    expect(String(entries[0].raw)).toContain("SlotA");
    expect(String(entries[0].raw)).toContain("SmartObjectTestBehaviorDefinition");
    expect(String(entries[1].raw)).not.toContain("SmartObjectTestBehaviorDefinition");

    await call("set_smart_object_slot", { assetPath: SOD, slotIndex: 1, offset: { x: -50, y: 0, z: 5 }, rotation: { pitch: 0, yaw: 90, roll: 0 } });
    listed = await slots();
    entries = rows(listed.slots);
    expect(entries[1].offset).toEqual({ x: -50, y: 0, z: 5 });
    expect(Number((entries[1].rotation as Row).yaw)).toBeCloseTo(90, 3);

    const behavior = await call("add_smart_object_slot_behavior", { assetPath: SOD, slotIndex: 1, behaviorClass: BEHAVIOR });
    expect(behavior.slotIndex).toBe(1);
    listed = await slots();
    expect(String(rows(listed.slots)[1].raw)).toContain("SmartObjectTestBehaviorDefinition");

    await call("add_smart_object_default_behavior", { assetPath: SOD, behaviorClass: BEHAVIOR });
    listed = await slots();
    expect(listed.defaultBehaviorCount).toBe(2);

    const removed = await call("remove_smart_object_slot", { assetPath: SOD, slotIndex: 0 });
    expect(removed.deleted).toBe(true);
    listed = await slots();
    expect(listed.slotCount).toBe(1);
    expect(rows(listed.slots)[0].offset).toEqual({ x: -50, y: 0, z: 5 });
  }, 180_000);

  it("add_smart_object_component adds a SmartObjectComponent read_blueprint lists", async () => {
    await call("create_blueprint", { name: "BP_BCovSmartObjectActor", packagePath: DIR, parentClass: "Actor" });
    expect(await componentClasses(SO_BP)).not.toContain("SmartObjectComponent");
    const added = await call("add_smart_object_component", { blueprintPath: SO_BP });
    expect(added.component).toBe("SmartObjectComp");
    expect(await componentClasses(SO_BP)).toContain("SmartObjectComponent");
  }, 120_000);
});
