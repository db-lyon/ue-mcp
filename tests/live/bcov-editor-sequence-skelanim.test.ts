// @live-editor headless
/**
 * A Level Sequence that plays an AnimSequence on a placed skeletal mesh: a
 * SkeletalAnimation section's clip, offsets and play rate, read back through
 * get_sequence_info. Refs #1641.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/EditorSkelAnim";
const SEQ = `${DIR}/LS_BCovSkelAnim`;
const CLIP = `${DIR}/AS_BCovSkelAnim`;
const SKELETON = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton";
const MESH = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP";
const ACTOR = "BCov_SkelAnim_Actor";

type Json = Record<string, unknown>;
type AnimParams = { animation: string; startFrameOffset: number; endFrameOffset: number; startOffsetSeconds: number; endOffsetSeconds: number; playRate?: number; animationLengthSeconds?: number };
type Section = { class: string; startSeconds?: number; endSeconds?: number; animation?: AnimParams };
type Track = { class: string; sections?: Section[] };
type Binding = { name: string; tracks: Track[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function refused(method: string, params: Json): Promise<string> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method} should refuse: ${JSON.stringify(r.result)}`).toBe(false);
  return String(r.error ?? body(r).error ?? JSON.stringify(r.result));
}

async function info(): Promise<Json> {
  return call("get_sequence_info", { assetPath: SEQ });
}

async function animSections(): Promise<Section[]> {
  const binding = ((await info()).bindings as Binding[]).find((b) => b.name === ACTOR);
  const track = binding?.tracks.find((t) => t.class.includes("SkeletalAnimation"));
  return track?.sections ?? [];
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "delete_actor", { actorLabel: ACTOR });
  await callBridge(bridge, "delete_asset", { assetPath: SEQ, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: CLIP, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
  await call("create_sequence", { name: "AS_BCovSkelAnim", skeletonPath: SKELETON, packagePath: DIR, numFrames: 4, frameRate: 30 });
  await call("create_level_sequence", { name: "LS_BCovSkelAnim", packagePath: DIR });
  await call("spawn_skeletal_mesh_actor", { skeletalMesh: MESH, label: ACTOR, location: { x: 0, y: 43000, z: 100 } });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await cleanup();
  } finally {
    disconnectBridge();
  }
}, 120_000);

describe("SkeletalAnimation section clip, offsets and play rate", () => {
  it("add_sequence_section sets the clip and start offset, and spans the clip after the offset", async () => {
    const added = await call("add_sequence_section", {
      sequencePath: SEQ, trackType: "SkeletalAnimation", actorLabel: ACTOR,
      animation: CLIP, startSeconds: 0, startFrameOffset: 400,
    });
    const params = added.animation as AnimParams;
    expect(params.animation).toContain("AS_BCovSkelAnim");
    expect(params.startFrameOffset).toBe(400);
    expect(params.endFrameOffset).toBe(0);
    expect(params.playRate).toBeCloseTo(1, 6);
    const span = Number(params.animationLengthSeconds) - params.startOffsetSeconds;
    expect(Number((added.range as Json).endSeconds)).toBeCloseTo(span, 3);

    const [section] = await animSections();
    expect(section.animation?.animation).toContain("AS_BCovSkelAnim");
    expect(section.animation?.startFrameOffset).toBe(400);
    expect(Number(section.endSeconds)).toBeCloseTo(span, 3);
  }, 120_000);

  it("a second section keeps an explicit range and writes the end offset and play rate", async () => {
    await call("add_sequence_section", {
      sequencePath: SEQ, trackType: "SkeletalAnimation", actorLabel: ACTOR,
      animation: CLIP, startSeconds: 1, endSeconds: 2, startFrameOffset: 2000, endFrameOffset: 800, playRate: 2,
    });
    const sections = await animSections();
    expect(sections.length).toBe(2);
    const second = sections.find((s) => Math.abs(Number(s.startSeconds) - 1) < 0.001);
    expect(second, JSON.stringify(sections)).toBeDefined();
    expect(Number(second!.endSeconds)).toBeCloseTo(2, 3);
    expect(second!.animation).toMatchObject({ startFrameOffset: 2000, endFrameOffset: 800 });
    expect(Number(second!.animation!.playRate)).toBeCloseTo(2, 6);
  }, 120_000);

  it("refuses clip settings on another track type, a missing clip and a bad play rate, adding nothing", async () => {
    await refused("add_sequence_section", { sequencePath: SEQ, trackType: "Transform", actorLabel: ACTOR, animation: CLIP });
    await refused("add_sequence_section", { sequencePath: SEQ, trackType: "SkeletalAnimation", actorLabel: ACTOR, animation: `${DIR}/AS_Missing` });
    await refused("add_sequence_section", { sequencePath: SEQ, trackType: "SkeletalAnimation", actorLabel: ACTOR, animation: CLIP, playRate: 0 });
    expect((await animSections()).length).toBe(2);
    const binding = ((await info()).bindings as Binding[]).find((b) => b.name === ACTOR);
    expect(binding!.tracks.some((t) => t.class.includes("Transform"))).toBe(false);
  }, 120_000);
});
