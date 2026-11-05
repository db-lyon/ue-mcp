// @live-editor headless
/**
 * GameplayEffect duration policy, components and granted tags (#1423).
 *
 * create_gameplay_effect used to echo durationPolicy without writing it, so an "Infinite" effect stayed Instant on its
 * class defaults. Each case here reads the effect back through get_gas_info rather than trusting the write's answer.
 *
 * Runs only against the dedicated disposable test project. Everything lives under /Game/MCPTest/GasEffects.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const FOLDER = "/Game/MCPTest/GasEffects";
const INFINITE = `${FOLDER}/GE_Live1423_Infinite`;
const TIMED = `${FOLDER}/GE_Live1423_Timed`;
const TAG = "Test.Live1423.Granted";

interface EffectInfo {
  durationPolicy: string;
  durationMagnitude?: number;
  components: Array<{ class: string; addedTags?: string[] }>;
  grantedTags: string[];
}

let bridge: EditorBridge;

/** A handler refusal arrives as an answered call whose result says success: false. */
const refusal = (r: { ok: boolean; error?: string; result?: unknown }): string => {
  expect(r.ok, r.error).toBe(true);
  const body = r.result as { success?: boolean; error?: string };
  expect(body.success, JSON.stringify(body)).toBe(false);
  return body.error ?? "";
};

const succeeded = (r: { ok: boolean; error?: string; result?: unknown }): void => {
  expect(r.ok, r.error).toBe(true);
  expect((r.result as { success?: boolean }).success, JSON.stringify(r.result)).toBe(true);
};

const effectInfo = async (path: string): Promise<EffectInfo> => {
  const info = await callBridge(bridge, "get_gas_info", { blueprintPath: path });
  succeeded(info);
  return (info.result as { effect: EffectInfo }).effect;
};

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: INFINITE });
  await callBridge(bridge, "delete_asset", { assetPath: TIMED });
  const tag = await callBridge(bridge, "create_gameplay_tag", { tag: TAG });
  succeeded(tag);
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "delete_asset", { assetPath: INFINITE });
  await callBridge(bridge, "delete_asset", { assetPath: TIMED });
  disconnectBridge();
});

describe("create_gameplay_effect durationPolicy (#1423)", () => {
  it("writes Infinite onto the class defaults", async () => {
    const created = await callBridge(bridge, "create_gameplay_effect", {
      name: "GE_Live1423_Infinite",
      packagePath: FOLDER,
      durationPolicy: "Infinite",
    });
    succeeded(created);
    expect((created.result as { durationPolicy: string }).durationPolicy).toBe("Infinite");
    expect((await effectInfo(INFINITE)).durationPolicy).toBe("Infinite");
  });

  it("writes HasDuration with its magnitude", async () => {
    const created = await callBridge(bridge, "create_gameplay_effect", {
      name: "GE_Live1423_Timed",
      packagePath: FOLDER,
      durationPolicy: "HasDuration",
      durationMagnitude: 4.5,
    });
    succeeded(created);
    const info = await effectInfo(TIMED);
    expect(info.durationPolicy).toBe("HasDuration");
    expect(info.durationMagnitude).toBeCloseTo(4.5);
  });

  it("refuses an unknown policy before creating anything", async () => {
    const refused = await callBridge(bridge, "create_gameplay_effect", {
      name: "GE_Live1423_Bad",
      packagePath: FOLDER,
      durationPolicy: "Forever",
    });
    expect(refusal(refused)).toContain("Infinite");
  });
});

describe("add_effect_component (#1423)", () => {
  it("adds a component that get_gas_info then lists", async () => {
    const added = await callBridge(bridge, "add_effect_component", {
      effectPath: INFINITE,
      componentClass: "AssetTagsGameplayEffectComponent",
    });
    succeeded(added);
    expect((added.result as { created: boolean }).created).toBe(true);
    const classes = (await effectInfo(INFINITE)).components.map((c) => c.class);
    expect(classes).toContain("AssetTagsGameplayEffectComponent");
  });

  it("is idempotent on the class", async () => {
    const again = await callBridge(bridge, "add_effect_component", {
      effectPath: INFINITE,
      componentClass: "AssetTagsGameplayEffectComponent",
    });
    succeeded(again);
    expect((again.result as { existed: boolean; unchanged: boolean }).existed).toBe(true);
    const matching = (await effectInfo(INFINITE)).components.filter((c) => c.class === "AssetTagsGameplayEffectComponent");
    expect(matching).toHaveLength(1);
  });

  it("writes properties onto the component it adds", async () => {
    const added = await callBridge(bridge, "add_effect_component", {
      effectPath: INFINITE,
      componentClass: "AdditionalEffectsGameplayEffectComponent",
      properties: { bOnApplicationCopyDataFromOriginalSpec: true },
    });
    succeeded(added);
    const componentPath = (added.result as { component: { path: string } }).component.path;
    const read = await callBridge(bridge, "get_property", {
      objectPath: componentPath,
      propertyName: "bOnApplicationCopyDataFromOriginalSpec",
    });
    succeeded(read);
    expect((read.result as { value: unknown }).value).toBe(true);
  });

  it("refuses a class that is not a GameplayEffectComponent", async () => {
    const refused = await callBridge(bridge, "add_effect_component", { effectPath: INFINITE, componentClass: "StaticMeshComponent" });
    refusal(refused);
  });
});

describe("remove_effect_component (#1423)", () => {
  it("removes the component add_effect_component added, through its rollback record", async () => {
    const added = await callBridge(bridge, "add_effect_component", { effectPath: TIMED, componentClass: "AssetTagsGameplayEffectComponent" });
    succeeded(added);
    const rollback = (added.result as { rollback: { method: string; payload: Record<string, unknown> } }).rollback;
    expect(rollback.method).toBe("remove_effect_component");

    const removed = await callBridge(bridge, rollback.method, rollback.payload);
    succeeded(removed);
    expect((removed.result as { alreadyRemoved: boolean }).alreadyRemoved).toBe(false);
    expect((await effectInfo(TIMED)).components.map((c) => c.class)).not.toContain("AssetTagsGameplayEffectComponent");
  });

  it("is idempotent when the class is not there", async () => {
    const again = await callBridge(bridge, "remove_effect_component", { effectPath: TIMED, componentClass: "AssetTagsGameplayEffectComponent" });
    succeeded(again);
    expect((again.result as { alreadyRemoved: boolean }).alreadyRemoved).toBe(true);
  });
});

describe("set_effect_granted_tags (#1423)", () => {
  it("grants the tag through a TargetTags component, read back off the effect", async () => {
    const set = await callBridge(bridge, "set_effect_granted_tags", { effectPath: INFINITE, tags: [TAG] });
    succeeded(set);
    const body = set.result as { grantedTags: string[]; componentCreated: boolean; rollback?: { method: string } };
    expect(body.grantedTags).toContain(TAG);
    expect(body.componentCreated).toBe(true);
    expect(body.rollback?.method).toBe("set_effect_granted_tags");

    const info = await effectInfo(INFINITE);
    expect(info.grantedTags).toContain(TAG);
    const targetTags = info.components.find((c) => c.class === "TargetTagsGameplayEffectComponent");
    expect(targetTags?.addedTags).toEqual([TAG]);
  });

  it("refuses an unregistered tag", async () => {
    const refused = await callBridge(bridge, "set_effect_granted_tags", { effectPath: INFINITE, tags: ["Test.Live1423.NotRegistered"] });
    expect(refusal(refused)).toContain("create_tag");
  });

  it("clears the grant when written empty", async () => {
    const cleared = await callBridge(bridge, "set_effect_granted_tags", { effectPath: INFINITE, tags: [] });
    succeeded(cleared);
    expect((await effectInfo(INFINITE)).grantedTags).not.toContain(TAG);
  });
});
