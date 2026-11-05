// @live-editor headless
/**
 * GAS authoring and live ASC actions, each read back through another action:
 * blueprint classification, ability tags, ASC wiring, attributes, loose tags
 * and an infinite effect applied and removed. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Gas`;
const ABILITY = `${DIR}/GA_BCovGas`;
const CUE = `${DIR}/GC_BCovGas`;
const EFFECT = `${DIR}/GE_BCovGas`;
const ATTR_SET = `${DIR}/AS_BCovGas`;
const ACTOR_BP = `${DIR}/BP_BCovGasActor`;
const ACTOR = "BCov_Gas_Actor";
const LOOSE_TAG = "BCov.Gas.Loose";
const BLOCKED_TAG = "BCov.Gas.Blocked";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const live = { actorLabel: ACTOR, world: "editor" };

async function ownedTags(): Promise<string[]> {
  const state = await call("get_asc_state", live);
  return state.ownedTags as string[];
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: ACTOR });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_gameplay_tag", { tag: LOOSE_TAG, comment: "ue-mcp bcov live test" });
  await call("create_gameplay_tag", { tag: BLOCKED_TAG, comment: "ue-mcp bcov live test" });
  await call("create_attribute_set", { name: "AS_BCovGas", packagePath: DIR });
  await call("add_attribute", { attributeSetPath: ATTR_SET, attributeName: "Health" });
  await call("place_actor", { actorClass: "Actor", label: ACTOR, location: { x: 0, y: -4000, z: 100 } });
  await call("add_component_to_actor", { actorLabel: ACTOR, componentClass: "AbilitySystemComponent", componentName: "ASC" });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: ACTOR });
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
});

describe("GAS blueprint authoring", () => {
  it("create_gameplay_ability and create_gameplay_cue make blueprints get_gas_info classifies", async () => {
    const ability = await call("create_gameplay_ability", { name: "GA_BCovGas", packagePath: DIR });
    expect(String(ability.path)).toContain("GA_BCovGas");
    const abilityInfo = await call("get_gas_info", { blueprintPath: ABILITY });
    expect(abilityInfo.isGameplayAbility).toBe(true);
    expect(abilityInfo.isGameplayEffect).toBe(false);

    const cue = await call("create_gameplay_cue", { name: "GC_BCovGas", packagePath: DIR, cueType: "Static" });
    expect(cue.cueType).toBe("Static");
    const cueInfo = await call("get_gas_info", { blueprintPath: CUE });
    expect(cueInfo.parentClass).toBe("GameplayCueNotify_Static");
    expect(cueInfo.isGameplayAbility).toBe(false);

    const setInfo = await call("get_gas_info", { blueprintPath: ATTR_SET });
    expect(setInfo.isAttributeSet).toBe(true);
  }, 180_000);

  it("set_ability_tags writes the container the ability CDO then reads back", async () => {
    const set = await call("set_ability_tags", { abilityPath: ABILITY, activation_blocked_tags: [BLOCKED_TAG] });
    expect(set.unchanged).toBe(false);
    const after = await call("get_property", { objectPath: ABILITY, propertyName: "ActivationBlockedTags" });
    expect(JSON.stringify(after.value)).toContain(BLOCKED_TAG);

    await call("set_ability_tags", { abilityPath: ABILITY, activation_blocked_tags: [] });
    const cleared = await call("get_property", { objectPath: ABILITY, propertyName: "ActivationBlockedTags" });
    expect(JSON.stringify(cleared.value)).not.toContain(BLOCKED_TAG);
  }, 120_000);

  it("add_ability_system_component and set_asc_defaults wire an ASC that later calls find", async () => {
    await call("create_blueprint", { name: "BP_BCovGasActor", packagePath: DIR, parentClass: "Actor" });
    const added = await call("add_ability_system_component", { blueprintPath: ACTOR_BP, componentName: "BCovASC" });
    expect(added.created).toBe(true);
    const read = await call("read_blueprint", { assetPath: ACTOR_BP });
    expect(JSON.stringify(read.components)).toContain("BCovASC");
    const again = await call("add_ability_system_component", { blueprintPath: ACTOR_BP, componentName: "BCovASC" });
    expect(again.existed).toBe(true);

    const wired = await call("set_asc_defaults", { blueprintPath: ACTOR_BP, attributeSet: ATTR_SET });
    expect(wired.created).toBe(true);
    expect(wired.component).toBe("BCovASC");
    expect(String(wired.attributeSet)).toContain("AS_BCovGas");
    // The second call finds the entry the first one wrote on the component template.
    const rewired = await call("set_asc_defaults", { blueprintPath: ACTOR_BP, attributeSet: ATTR_SET });
    expect(rewired.existed).toBe(true);
  }, 180_000);

  it("set_effect_modifier adds a modifier the effect CDO then carries", async () => {
    // DurationPolicy lives on the generated class defaults, which create_gameplay_effect writes (#1423).
    const created = await call("create_gameplay_effect", { name: "GE_BCovGas", packagePath: DIR, durationPolicy: "Infinite" });
    expect(created.durationPolicy).toBe("Infinite");
    const mod = await call("set_effect_modifier", {
      effectPath: EFFECT, attribute: "AS_BCovGas.Health", operation: "Additive", magnitude: 5,
    });
    expect(mod.attribute).toBe("Health");
    expect(mod.replacedExisting).toBe(false);
    expect(mod.modifierCount).toBe(1);
    const modifiers = await call("get_property", { objectPath: EFFECT, propertyName: "Modifiers" });
    expect(Array.isArray(modifiers.value)).toBe(true);
    expect((modifiers.value as unknown[]).length).toBe(1);
    expect(JSON.stringify(modifiers.value)).toContain("Health");

    const replaced = await call("set_effect_modifier", {
      effectPath: EFFECT, attribute: "AS_BCovGas.Health", operation: "Additive", magnitude: 7,
    });
    expect(replaced.replacedExisting).toBe(true);
    expect(replaced.modifierCount).toBe(1);
  }, 120_000);
});

describe("GAS on a live editor-world ASC", () => {
  it("init_asc registers the attribute set and get_attribute lists it", async () => {
    const init = await call("init_asc", { ...live, attributeSet: ATTR_SET });
    expect(init.initialized).toBe(true);
    expect(String(init.attributeSet)).toContain("AS_BCovGas");
    expect(Number(init.attributeCount)).toBeGreaterThanOrEqual(1);

    const all = await call("get_attribute", live);
    const rows = all.attributes as Array<{ attribute: string; attributeSet: string }>;
    expect(rows.some((r) => r.attribute === "Health" && r.attributeSet.includes("AS_BCovGas"))).toBe(true);

    const again = await call("init_asc", { ...live, attributeSet: ATTR_SET });
    expect(again.unchanged).toBe(true);
  }, 120_000);

  it("set_attribute moves the base value get_attribute reads", async () => {
    const set = await call("set_attribute", { ...live, attribute: "Health", value: 50 });
    expect(set.baseValue).toBe(50);
    const read = await call("get_attribute", { ...live, attribute: "Health" });
    expect(read.baseValue).toBe(50);
    expect(read.currentValue).toBe(50);
  }, 60_000);

  it("set_live_attribute_value writes the registered set get_live_attribute_value reads", async () => {
    const before = await call("get_live_attribute_value", { ...live, attributeSet: ATTR_SET, attribute: "Health" });
    expect(before.registeredOnAsc).toBe(true);
    expect(before.currentValue).toBe(50);

    const set = await call("set_live_attribute_value", {
      ...live, attributeSet: ATTR_SET, attribute: "Health", value: 75, valueType: "base",
    });
    expect(set.valueChanged).toBe(true);
    expect(set.previousBaseValue).toBe(50);

    const after = await call("get_live_attribute_value", { ...live, attributeSet: ATTR_SET, attribute: "Health" });
    expect(after.aggregatorBaseValue).toBe(75);
    expect(after.attributeSetInstance).toBe(before.attributeSetInstance);
  }, 60_000);

  it("add_loose_gameplay_tag and remove_loose_gameplay_tag move the ASC's owned tags", async () => {
    expect(await ownedTags()).not.toContain(LOOSE_TAG);
    const added = await call("add_loose_gameplay_tag", { ...live, tag: LOOSE_TAG, count: 2 });
    expect(added.tagCount).toBe(2);
    expect(await ownedTags()).toContain(LOOSE_TAG);

    const removed = await call("remove_loose_gameplay_tag", { ...live, tag: LOOSE_TAG, count: 2 });
    expect(removed.tagCount).toBe(0);
    expect(await ownedTags()).not.toContain(LOOSE_TAG);
  }, 60_000);

  it("apply_effect puts an infinite effect on the ASC and remove_effect takes it off", async () => {
    const applied = await call("apply_effect", { ...live, effectClass: EFFECT });
    expect(applied.applied).toBe(true);
    const handle = String(applied.effectHandle);
    expect(handle).toMatch(/^\d+$/);

    const during = await call("get_active_effects", live);
    const effects = during.activeEffects as Array<{ handle: string; effectClass: string }>;
    expect(effects.some((e) => e.handle === handle && e.effectClass.includes("GE_BCovGas"))).toBe(true);
    // The +7 modifier sits on top of the base value set above.
    const boosted = await call("get_attribute", { ...live, attribute: "Health" });
    expect(Number(boosted.currentValue)).toBe(Number(boosted.baseValue) + 7);

    const removed = await call("remove_effect", { ...live, effectHandle: handle });
    expect(removed.removed).toBe(1);
    const after = await call("get_active_effects", live);
    expect((after.activeEffects as Array<{ handle: string }>).some((e) => e.handle === handle)).toBe(false);
    const restored = await call("get_attribute", { ...live, attribute: "Health" });
    expect(restored.currentValue).toBe(restored.baseValue);
  }, 120_000);
});
