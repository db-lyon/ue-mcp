// @live-editor headless
/**
 * Montage authoring on copies of the engine tutorial animations: creation,
 * appended segments, sections anchored to a segment, and the batch author,
 * each read back through read_anim_montage or list_montage_segments.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AnimA/Montage";
const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const IDLE = `${DIR}/AS_BCovA_Idle`;
const WALK = `${DIR}/AS_BCovA_Walk`;
const MONTAGE = `${DIR}/AM_BCovA`;
const BATCH = `${DIR}/AM_BCovA_Batch`;
const ASSETS = [MONTAGE, BATCH, IDLE, WALK];

type Json = Record<string, unknown>;
type Segment = { segmentIndex: number; animation: string | null; startPos: number; endPos: number; trackStartPos: number };
type Section = { name: string; startTime: number; nextSection: string };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: IDLE });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Walk_Fwd`, destinationPath: WALK });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("montage authoring", () => {
  it("create_anim_montage makes a montage whose one slot plays the source sequence", async () => {
    const created = await call("create_anim_montage", { name: "AM_BCovA", animSequencePath: IDLE, packagePath: DIR });
    expect(created.class).toBe("AnimMontage");
    const read = await call("read_anim_montage", { assetPath: MONTAGE });
    const tracks = read.slotAnimTracks as Array<{ slotName: string; segments: Array<{ animation: string }> }>;
    expect(tracks).toHaveLength(1);
    expect(tracks[0].segments).toHaveLength(1);
    expect(tracks[0].segments[0].animation).toContain("AS_BCovA_Idle");
  }, 120_000);

  it("add_montage_segment appends a trimmed segment and add_montage_section anchors a section to it", async () => {
    await call("add_montage_segment", { assetPath: MONTAGE, animSequencePath: WALK, startPos: 0, endPos: 0.5 });
    const listed = await call("list_montage_segments", { assetPath: MONTAGE });
    expect(listed.totalSegments).toBe(2);
    const segments = (listed.slots as Array<{ segments: Segment[] }>)[0].segments;
    expect(segments[1].animation).toContain("AS_BCovA_Walk");
    expect(segments[1].endPos).toBeCloseTo(0.5, 3);
    expect(segments[1].trackStartPos).toBeCloseTo(segments[0].trackStartPos + (segments[0].endPos - segments[0].startPos), 2);

    await call("add_montage_section", { assetPath: MONTAGE, sectionName: "BCovWalk", segmentIndex: 1 });
    const read = await call("read_anim_montage", { assetPath: MONTAGE });
    const section = (read.sections as Section[]).find((s) => s.name === "BCovWalk");
    expect(section?.startTime).toBeCloseTo(segments[1].trackStartPos, 2);
  }, 120_000);

  it("author_montages_batch creates a montage with its slot, blend, section and notify", async () => {
    const batch = await call("author_montages_batch", {
      items: [{
        name: "AM_BCovA_Batch", animSequencePath: WALK, packagePath: DIR, slotName: "BCovUpper", blendIn: 0.4,
        sections: [{ sectionName: "BCovLoop", startTime: 0 }],
        notifies: [{ notifyName: "BCovBatchHit", triggerTime: 0.2 }],
      }],
    });
    expect(batch.succeeded).toBe(1);
    expect((batch.items as Array<{ created: boolean }>)[0].created).toBe(true);

    const read = await call("read_anim_montage", { assetPath: BATCH });
    expect((read.slotAnimTracks as Array<{ slotName: string }>)[0].slotName).toContain("BCovUpper");
    expect(Number(read.blendIn)).toBeCloseTo(0.4, 3);
    expect((read.sections as Section[]).map((s) => s.name)).toContain("BCovLoop");
    const hit = (read.notifies as Array<{ name: string; triggerTime: number }>).find((n) => n.name === "BCovBatchHit");
    expect(hit?.triggerTime).toBeCloseTo(0.2, 2);
  }, 120_000);
});
