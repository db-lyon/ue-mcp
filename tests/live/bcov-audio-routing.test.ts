// @live-editor headless
/**
 * Sound classes, submixes, effects, sends, attenuation, concurrency, mixes and
 * raw property writes, each read back through read_sound_routing or the asset
 * property reader on a SoundCue this file creates. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/AudioRouting`;
const CUE = `${DIR}/SC_BCovRouted`;
const at = (name: string): string => `${DIR}/${name}`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

interface Routing {
  soundClassPath?: string;
  soundClassChain: Array<{ name: string; volume: number }>;
  submixPath?: string;
  submixChain: Array<{ name: string; effectChainLength?: number; effectChain?: string[] }>;
  submixSends: Array<{ submixPath: string; sendLevel: number }>;
  attenuation?: { objectPath: string; attenuate: boolean; spatialize: boolean; falloffDistance: number };
  concurrencies: Array<{ objectPath: string; maxCount: number; limitToOwner: boolean }>;
  priority: number;
}

const routing = async (): Promise<Routing> => (await call("read_sound_routing", { soundPath: CUE })) as unknown as Routing;

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_sound_cue", { name: "SC_BCovRouted", packagePath: DIR });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 180_000);

describe("sound class routing", () => {
  it("create_sound_class and set_sound_class put the cue under a two-level class chain", async () => {
    await call("create_sound_class", { name: "SCL_BCovMaster", packagePath: DIR, properties: { Volume: 0.8 } });
    await call("create_sound_class", {
      name: "SCL_BCovChild", packagePath: DIR, properties: { Volume: 0.6 }, parentPath: at("SCL_BCovMaster"),
    });
    const set = await call("set_sound_class", { soundPath: CUE, soundClassPath: at("SCL_BCovChild") });
    expect(String(set.soundClassPath)).toContain("SCL_BCovChild");

    const r = await routing();
    expect(r.soundClassPath).toContain("SCL_BCovChild");
    expect(r.soundClassChain.map((c) => c.name)).toEqual(["SCL_BCovChild", "SCL_BCovMaster"]);
    expect(r.soundClassChain[0].volume).toBeCloseTo(0.6, 3);
    expect(r.soundClassChain[1].volume).toBeCloseTo(0.8, 3);
  }, 120_000);

  it("set_sound_class_parent moves the class under another parent and detaches it", async () => {
    await call("create_sound_class", { name: "SCL_BCovAlt", packagePath: DIR });
    const moved = await call("set_sound_class_parent", { soundClassPath: at("SCL_BCovChild"), parentPath: at("SCL_BCovAlt") });
    expect(moved.listedOnParent).toBe(true);
    expect(String(moved.previousParentPath)).toContain("SCL_BCovMaster");
    expect((await routing()).soundClassChain.map((c) => c.name)).toEqual(["SCL_BCovChild", "SCL_BCovAlt"]);

    await call("set_sound_class_parent", { soundClassPath: at("SCL_BCovChild"), parentPath: "" });
    expect((await routing()).soundClassChain.map((c) => c.name)).toEqual(["SCL_BCovChild"]);
  }, 120_000);
});

describe("submix routing", () => {
  it("create_submix and set_sound_submix route the cue through a parented submix", async () => {
    await call("create_submix", { name: "SMX_BCovRoot", packagePath: DIR });
    await call("create_submix", { name: "SMX_BCovLeaf", packagePath: DIR, parentPath: at("SMX_BCovRoot"), outputVolume: 0.5 });
    await call("set_sound_submix", { soundPath: CUE, submixPath: at("SMX_BCovLeaf") });

    const r = await routing();
    expect(r.submixPath).toContain("SMX_BCovLeaf");
    expect(r.submixChain.map((s) => s.name)).toEqual(["SMX_BCovLeaf", "SMX_BCovRoot"]);
  }, 120_000);

  it("set_submix_parent reparents the submix and detaches it to a root", async () => {
    await call("create_submix", { name: "SMX_BCovAlt", packagePath: DIR });
    const moved = await call("set_submix_parent", { submixPath: at("SMX_BCovLeaf"), parentPath: at("SMX_BCovAlt") });
    expect(String(moved.previousParentPath)).toContain("SMX_BCovRoot");
    expect((await routing()).submixChain.map((s) => s.name)).toEqual(["SMX_BCovLeaf", "SMX_BCovAlt"]);

    await call("set_submix_parent", { submixPath: at("SMX_BCovLeaf"), parentPath: "" });
    expect((await routing()).submixChain.map((s) => s.name)).toEqual(["SMX_BCovLeaf"]);
  }, 120_000);

  it("add_submix_effect appends a reverb preset to the submix's effect chain", async () => {
    const added = await call("add_submix_effect", {
      submixPath: at("SMX_BCovLeaf"), effectType: "reverb", name: "SFX_BCovReverb", packagePath: DIR,
    });
    expect(added.chainLength).toBe(1);
    expect(String(added.presetPath)).toContain("SFX_BCovReverb");

    const leaf = (await routing()).submixChain[0];
    expect(leaf.effectChainLength).toBe(1);
    expect(leaf.effectChain?.[0]).toContain("SFX_BCovReverb");
  }, 120_000);

  it("add_sound_submix_send adds a send at the requested level", async () => {
    const sent = await call("add_sound_submix_send", { soundPath: CUE, submixPath: at("SMX_BCovRoot"), sendLevel: 0.25 });
    expect(sent.sends).toBe(1);
    const sends = (await routing()).submixSends;
    expect(sends).toHaveLength(1);
    expect(sends[0].submixPath).toContain("SMX_BCovRoot");
    expect(sends[0].sendLevel).toBeCloseTo(0.25, 3);
  }, 120_000);
});

describe("attenuation, concurrency and properties", () => {
  it("create_attenuation and set_sound_attenuation attach the authored falloff, then clear it", async () => {
    await call("create_attenuation", { name: "ATT_BCov", packagePath: DIR, falloffDistance: 1234, spatialize: true });
    await call("set_sound_attenuation", { soundPath: CUE, attenuationPath: at("ATT_BCov") });

    const attached = (await routing()).attenuation;
    expect(attached?.objectPath).toContain("ATT_BCov");
    expect(attached?.falloffDistance).toBeCloseTo(1234, 1);
    expect(attached?.attenuate).toBe(true);
    expect(attached?.spatialize).toBe(true);

    const cleared = await call("set_sound_attenuation", { soundPath: CUE, attenuationPath: "" });
    expect(String(cleared.previousAttenuationPath)).toContain("ATT_BCov");
    expect((await routing()).attenuation).toBeUndefined();
  }, 120_000);

  it("create_concurrency and set_sound_concurrency attach the authored voice limit", async () => {
    await call("create_concurrency", { name: "CON_BCov", packagePath: DIR, maxCount: 3, limitToOwner: true });
    await call("set_sound_concurrency", { soundPath: CUE, concurrencyPath: at("CON_BCov") });

    const concurrencies = (await routing()).concurrencies;
    expect(concurrencies).toHaveLength(1);
    expect(concurrencies[0].objectPath).toContain("CON_BCov");
    expect(concurrencies[0].maxCount).toBe(3);
    expect(concurrencies[0].limitToOwner).toBe(true);
  }, 120_000);

  it("set_audio_property writes a sound's priority", async () => {
    const written = await call("set_audio_property", { assetPath: CUE, propertyName: "Priority", value: 3.5 });
    expect(written.propertyName).toBe("Priority");
    expect((await routing()).priority).toBeCloseTo(3.5, 3);
  }, 120_000);

  it("create_sound_mix stores its fade time and class adjusters", async () => {
    const mix = await call("create_sound_mix", {
      name: "MIX_BCov",
      packagePath: DIR,
      fadeInTime: 1.5,
      adjusters: [{ soundClassPath: at("SCL_BCovMaster"), volumeAdjuster: 0.3 }],
    });
    expect(mix.adjusters).toBe(1);

    const fade = await call("read_asset_properties", {
      assetPath: at("MIX_BCov"), propertyName: "FadeInTime", includeValues: true, valueFormat: "json",
    });
    expect(Number(fade.value)).toBeCloseTo(1.5, 3);
    const effects = await call("read_asset_properties", {
      assetPath: at("MIX_BCov"), propertyName: "SoundClassEffects", includeValues: true, valueFormat: "json",
    });
    expect(JSON.stringify(effects.value)).toContain("SCL_BCovMaster");
  }, 120_000);
});
