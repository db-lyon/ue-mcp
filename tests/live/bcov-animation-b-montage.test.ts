// @live-editor headless
/**
 * Montage authoring read back through read_anim_montage and list_montage_segments:
 * slot, properties, sequence, segment and section removal, on a montage built
 * over copies of the engine's tutorial clips. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const DIR = `${TEST_PREFIX}/BCov/AnimB/Montage`;
const IDLE = `${DIR}/AS_BCovIdle`;
const WALK = `${DIR}/AS_BCovWalk`;
const MONTAGE = `${DIR}/AM_BCov`;
const ASSETS = [MONTAGE, IDLE, WALK];

type Json = Record<string, unknown>;
type Segment = { segmentIndex: number; animation: string | null };
type Slot = { slotName: string; segmentCount: number; segments: Segment[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function readMontage(): Promise<Json> {
  return call("read_anim_montage", { assetPath: MONTAGE });
}

async function segments(): Promise<Slot[]> {
  const listed = await call("list_montage_segments", { assetPath: MONTAGE });
  return listed.slots as Slot[];
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: IDLE });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Walk_Fwd`, destinationPath: WALK });
  await call("create_anim_montage", { name: "AM_BCov", animSequencePath: IDLE, packagePath: DIR });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
});

describe("montage authoring", () => {
  it("read_anim_montage reports the montage's sequence and default section", async () => {
    const read = await readMontage();
    const tracks = read.slotAnimTracks as Array<{ slotName: string; segments: Array<{ animation: string }> }>;
    expect(tracks.length).toBeGreaterThan(0);
    expect(tracks[0].segments[0].animation).toContain("AS_BCovIdle");
    expect(Number(read.sequenceLength)).toBeGreaterThan(0);
    expect((read.sections as unknown[]).length).toBeGreaterThan(0);
  }, 120_000);

  it("set_montage_slot renames the slot on the track", async () => {
    await call("set_montage_slot", { assetPath: MONTAGE, slotName: "BCovUpperBody", trackIndex: 0 });
    const tracks = (await readMontage()).slotAnimTracks as Array<{ slotName: string }>;
    expect(tracks[0].slotName).toBe("BCovUpperBody");
  }, 120_000);

  it("set_montage_properties writes rate scale and blend times", async () => {
    await call("set_montage_properties", { assetPath: MONTAGE, rateScale: 1.5, blendIn: 0.4, blendOut: 0.35 });
    const read = await readMontage();
    expect(Number(read.rateScale)).toBeCloseTo(1.5, 4);
    expect(Number(read.blendIn)).toBeCloseTo(0.4, 4);
    expect(Number(read.blendOut)).toBeCloseTo(0.35, 4);
  }, 120_000);

  it("set_montage_sequence swaps the clip the slot plays", async () => {
    const result = await call("set_montage_sequence", { assetPath: MONTAGE, animSequencePath: WALK, slotIndex: 0 });
    expect(Number(result.segmentsUpdated)).toBeGreaterThan(0);
    const slots = await segments();
    expect(slots[0].segments[0].animation).toContain("AS_BCovWalk");
  }, 120_000);

  it("list_montage_segments and remove_montage_segment track an appended segment", async () => {
    await call("add_montage_segment", { assetPath: MONTAGE, animSequencePath: IDLE, slotIndex: 0 });
    let slots = await segments();
    expect(slots[0].segmentCount).toBe(2);
    expect(slots[0].segments[1].animation).toContain("AS_BCovIdle");

    const removed = await call("remove_montage_segment", { assetPath: MONTAGE, segmentIndex: 1, slotIndex: 0 });
    expect(removed.segmentCount).toBe(1);
    slots = await segments();
    expect(slots[0].segmentCount).toBe(1);
    expect(slots[0].segments[0].animation).toContain("AS_BCovWalk");
  }, 120_000);

  it("remove_montage_section drops an added section", async () => {
    await call("add_montage_section", { assetPath: MONTAGE, sectionName: "BCovSection", startTime: 0.2 });
    const names = async (): Promise<string[]> =>
      ((await readMontage()).sections as Array<{ name: string }>).map((s) => s.name);
    expect(await names()).toContain("BCovSection");

    const removed = await call("remove_montage_section", { assetPath: MONTAGE, sectionName: "BCovSection" });
    expect(removed.alreadyDeleted).toBe(false);
    expect(await names()).not.toContain("BCovSection");
  }, 120_000);
});
