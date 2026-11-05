// @live-editor render
/**
 * Level Sequence editing and transport: tracks, keyframes and the playback
 * range read back through get_sequence_info and get_sequence_channel_keys,
 * scrubbing and playing in Sequencer, closing it, and rendering frames to disk.
 * Refs #1223.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Editor";
const SEQ = `${DIR}/LS_BCovEditor`;
const CUBE = "BCov_Editor_SeqCube";
const BASE_Z = 200;

type Json = Record<string, unknown>;
type Track = { class: string; name?: string };
type Binding = { name: string; tracks: Track[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let scratch = "";

async function call(method: string, params: Json, timeoutMs?: number): Promise<Json> {
  let r: TestResult;
  if (timeoutMs) {
    try {
      r = { method, ok: true, ms: 0, result: await bridge.call(method, params, timeoutMs) };
    } catch (e) {
      r = { method, ok: false, ms: 0, error: e instanceof Error ? e.message : String(e) };
    }
  } else {
    r = await callBridge(bridge, method, params);
  }
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function info(): Promise<Json> {
  return call("get_sequence_info", { assetPath: SEQ });
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "close_sequence", {});
  await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
  await callBridge(bridge, "delete_asset", { assetPath: SEQ, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "bcov-editor-seq-"));
  await cleanup();
  await call("create_level_sequence", { name: "LS_BCovEditor", packagePath: DIR });
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: CUBE, location: { x: 1200, y: 12000, z: BASE_Z } });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await cleanup();
  } finally {
    if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("sequence authoring", () => {
  it("add_sequence_track adds a bound Transform track and a master Fade track, as get_sequence_info lists them", async () => {
    const bound = await call("add_sequence_track", { assetPath: SEQ, trackType: "Transform", actorLabel: CUBE });
    expect(bound.scope).toBe("binding");
    const master = await call("add_sequence_track", { assetPath: SEQ, trackType: "Fade" });
    expect(master.scope).toBe("master");

    const read = await info();
    const binding = (read.bindings as Binding[]).find((b) => b.name === CUBE);
    expect(binding, JSON.stringify(read.bindings)).toBeDefined();
    expect(binding!.tracks.map((t) => t.class).join(",")).toContain("Transform");
    expect((read.masterTracks as Track[]).map((t) => t.class).join(",")).toContain("Fade");
  }, 120_000);

  it("set_sequence_playback_range sets the range get_sequence_info reports", async () => {
    const set = await call("set_sequence_playback_range", { sequencePath: SEQ, startSeconds: 0, endSeconds: 3 });
    expect(set.endSeconds).toBe(3);
    const range = (await info()).playbackRange as Json;
    expect(Number(range.startSeconds)).toBeCloseTo(0, 3);
    expect(Number(range.endSeconds)).toBeCloseTo(3, 3);
  }, 120_000);

  it("set_sequence_keyframes writes keys get_sequence_channel_keys reads back", async () => {
    await call("add_sequence_section", { sequencePath: SEQ, trackType: "Transform", actorLabel: CUBE, startSeconds: 0, endSeconds: 3 });
    await call("set_sequence_keyframes", {
      sequencePath: SEQ, trackType: "Transform", actorLabel: CUBE, channel: "Location.Z",
      keyframes: [{ seconds: 0, value: BASE_Z }, { seconds: 1, value: BASE_Z + 500 }], interpolation: "linear",
    });
    const read = await call("get_sequence_channel_keys", {
      sequencePath: SEQ, trackType: "Transform", actorLabel: CUBE, channel: "Location.Z",
      sampleStartFrame: 30, sampleEndFrame: 30,
    });
    const channel = (read.channels as Array<{ keyCount: number; samples?: Array<{ value: number }> }>)[0];
    expect(channel.keyCount).toBe(2);
    expect(Number(channel.samples?.[0]?.value)).toBeCloseTo(BASE_Z + 500, 0);
  }, 120_000);
});

describe("Sequencer transport", () => {
  it("scrub_sequence moves the playhead and evaluates the keyed cube", async () => {
    const scrubbed = await call("scrub_sequence", { sequencePath: SEQ, seconds: 1 });
    expect(scrubbed.evaluated).toBe(true);
    expect(Number(scrubbed.seconds)).toBeCloseTo(1, 3);
    const details = await call("get_actor_details", { actorLabel: CUBE });
    expect(Number((details.location as Json).z)).toBeCloseTo(BASE_Z + 500, 0);

    const half = await call("scrub_sequence", { seconds: 0.5 });
    expect(Number(half.previousFrame)).toBeCloseTo(Number(scrubbed.frame), 3);
  }, 120_000);

  it("play_sequence plays and pauses the open sequence, each call seeing the previous state", async () => {
    const played = await call("play_sequence", { sequencePath: SEQ, sequenceAction: "play" });
    expect(played.sequencePath).toContain("LS_BCovEditor");
    expect(played.playing).toBe(true);

    const paused = await call("play_sequence", { sequenceAction: "pause" });
    expect(paused.wasPlaying).toBe(true);
    expect(paused.playing).toBe(false);

    const stopped = await call("play_sequence", { sequenceAction: "stop" });
    expect(stopped.playing).toBe(false);
    const after = await call("scrub_sequence", { seconds: 0 });
    expect(Number(after.previousFrame)).toBe(0);
  }, 120_000);

  it("close_sequence closes the open sequence and a second call finds nothing open", async () => {
    const closed = await call("close_sequence", {});
    expect(closed.wasOpen).toBe(true);
    expect(String(closed.closedSequence)).toContain("LS_BCovEditor");
    const again = await call("close_sequence", {});
    expect(again.wasOpen).toBe(false);
  }, 120_000);
});

describe("rendering", () => {
  it("render_sequence_frames writes one PNG per frame to the output directory", async () => {
    const outputDir = path.join(scratch, "frames");
    const rendered = await call("render_sequence_frames", {
      sequencePath: SEQ, startFrame: 0, endFrame: 1, width: 64, height: 64, outputDir,
      location: { x: 600, y: 12000, z: 400 }, rotation: { pitch: -10, yaw: 0, roll: 0 },
    }, 600_000);
    expect(rendered.renderedFrames).toBe(2);
    const files = (rendered.files as unknown[]).map((f) => (typeof f === "string" ? f : String((f as Json).path)));
    expect(files).toHaveLength(2);
    for (const f of files) {
      expect(fs.existsSync(f), f).toBe(true);
      expect(fs.statSync(f).size).toBeGreaterThan(0);
    }
    await callBridge(bridge, "close_sequence", {});
  }, 600_000);
});
