// @live-editor headless
/**
 * Audio import, PCM readback, sound listing, ambient sound actors and SoundCue
 * graph editing, each read back through another action. The SoundWave is
 * imported from a WAV this file writes, so the decoded samples are known.
 * Refs #1223.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/AudioWave`;
const WAVE_NAME = "SW_BCovTone";
const WAVE = `${DIR}/${WAVE_NAME}.${WAVE_NAME}`;
const EMPTY_CUE = `${DIR}/SC_BCovEmpty`;
const AUTHORED_CUE = `${DIR}/SC_BCovAuthored`;
const AMBIENT = "BCov_AudioWave_Ambient";
const SAMPLE_RATE = 44100;
const FRAMES = 11025;
const WAV_FILE = path.join(os.tmpdir(), `bcov_audiowave_${process.pid}.wav`);

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let samples: Int16Array;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** A mono 16-bit PCM sine at 440 Hz. */
function writeToneWav(file: string): Int16Array {
  const pcm = new Int16Array(FRAMES);
  for (let i = 0; i < FRAMES; i++) pcm[i] = Math.round(Math.sin((2 * Math.PI * 440 * i) / SAMPLE_RATE) * 12000);
  const data = Buffer.from(pcm.buffer, pcm.byteOffset, pcm.byteLength);
  const header = Buffer.alloc(44);
  header.write("RIFF", 0, "ascii");
  header.writeUInt32LE(36 + data.length, 4);
  header.write("WAVE", 8, "ascii");
  header.write("fmt ", 12, "ascii");
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(1, 22);
  header.writeUInt32LE(SAMPLE_RATE, 24);
  header.writeUInt32LE(SAMPLE_RATE * 2, 28);
  header.writeUInt16LE(2, 32);
  header.writeUInt16LE(16, 34);
  header.write("data", 36, "ascii");
  header.writeUInt32LE(data.length, 40);
  fs.writeFileSync(file, Buffer.concat([header, data]));
  return pcm;
}

interface CueNode { nodeId: string; type: string; children: string[] }
async function cueGraph(cuePath: string): Promise<{ root: string; count: number; nodes: CueNode[] }> {
  const g = await call("soundcue_get_graph", { cuePath });
  return { root: String(g.root ?? ""), count: Number(g.count ?? -1), nodes: (g.nodes as CueNode[]) ?? [] };
}

async function soundAssetPaths(): Promise<string[]> {
  const listed = await call("list_sound_assets", { directory: DIR, recursive: true });
  return ((listed.assets as Array<{ path: string }>) ?? []).map((a) => a.path);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: AMBIENT });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  samples = writeToneWav(WAV_FILE);
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: AMBIENT });
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
  try { fs.unlinkSync(WAV_FILE); } catch { /* already gone */ }
}, 180_000);

describe("audio import and PCM", () => {
  it("import_audio brings in a SoundWave with the file's length and channel count", async () => {
    const imported = await call("import_audio", { filePath: WAV_FILE, name: WAVE_NAME, packagePath: DIR, looping: false });
    expect(imported.importedCount).toBe(1);
    expect(imported.importedAssets).toEqual([WAVE]);
    expect(imported.numChannels).toBe(1);
    expect(Number(imported.durationSeconds)).toBeCloseTo(FRAMES / SAMPLE_RATE, 2);
    expect(await soundAssetPaths()).toContain(WAVE);
  }, 180_000);

  it("extract_sound_wave_pcm decodes the samples that were written", async () => {
    const full = await call("extract_sound_wave_pcm", { soundPath: WAVE });
    expect(full.sampleRate).toBe(SAMPLE_RATE);
    expect(full.numChannels).toBe(1);
    expect(full.numFrames).toBe(FRAMES);
    expect(full.format).toBe("pcm_s16le");
    const bytes = Buffer.from(String(full.pcmBase64), "base64");
    expect(bytes.length).toBe(FRAMES * 2);
    for (const i of [0, 1, 25, 100, 5000, FRAMES - 1]) {
      expect(Math.abs(bytes.readInt16LE(i * 2) - samples[i]), `sample ${i}`).toBeLessThanOrEqual(1);
    }

    const windowed = await call("extract_sound_wave_pcm", { soundPath: WAVE, maxSeconds: 0.1, downmixMono: true });
    expect(windowed.numFrames).toBe(Math.floor(0.1 * SAMPLE_RATE));
    expect(Buffer.from(String(windowed.pcmBase64), "base64").length).toBe(Math.floor(0.1 * SAMPLE_RATE) * 2);
  }, 120_000);
});

describe("ambient sound actor", () => {
  it("spawn_ambient_sound places an AmbientSound playing the wave", async () => {
    const spawned = await call("spawn_ambient_sound", {
      soundPath: WAVE, label: AMBIENT, location: { x: 0, y: -3000, z: 200 }, volumeMultiplier: 0.5,
    });
    expect(spawned.label).toBe(AMBIENT);

    const details = await call("get_actor_details", { actorLabel: AMBIENT });
    expect(details.class).toBe("AmbientSound");
    const loc = details.location as { x: number; y: number; z: number };
    expect(loc.y).toBeCloseTo(-3000, 1);
    expect(loc.z).toBeCloseTo(200, 1);

    const tree = await call("get_component_tree", { actorLabel: AMBIENT, includeProperties: true, componentClass: "Audio" });
    expect(JSON.stringify(tree)).toContain(WAVE_NAME);
  }, 120_000);
});

describe("SoundCue graphs", () => {
  it("create_sound_cue makes an empty cue the listing and the graph read both see", async () => {
    const created = await call("create_sound_cue", { name: "SC_BCovEmpty", packagePath: DIR });
    expect(String(created.path)).toContain("SC_BCovEmpty");
    const graph = await call("soundcue_get_graph", { cuePath: EMPTY_CUE });
    expect(graph.count).toBe(0);
    expect(graph.nodes).toEqual([]);
    expect(graph.root).toBe("");
    expect(await soundAssetPaths()).toContain(`${EMPTY_CUE}.SC_BCovEmpty`);
  }, 120_000);

  it("list_sound_assets lists only the sounds under the directory", async () => {
    const listed = await call("list_sound_assets", { directory: DIR, recursive: true });
    const assets = (listed.assets as Array<{ path: string; class: string }>) ?? [];
    expect(assets.every((a) => a.path.startsWith(`${DIR}/`))).toBe(true);
    expect(assets.find((a) => a.path === WAVE)?.class).toBe("SoundWave");
    expect(assets.find((a) => a.path === `${EMPTY_CUE}.SC_BCovEmpty`)?.class).toBe("SoundCue");
  }, 120_000);

  it("soundcue_author stamps a mixer root with two wave players under it", async () => {
    const authored = await call("soundcue_author", {
      name: "SC_BCovAuthored",
      packagePath: DIR,
      nodes: [
        { id: "mix", type: "mixer" },
        { id: "a", type: "wave_player", soundWavePath: WAVE },
        { id: "b", type: "wave_player", soundWavePath: WAVE },
      ],
      connections: [{ child: "mix" }, { child: "a", parent: "mix" }, { child: "b", parent: "mix" }],
    });
    expect(authored.errors).toBe(0);
    expect(authored.nodes).toBe(3);

    const graph = await cueGraph(AUTHORED_CUE);
    expect(graph.count).toBe(3);
    const root = graph.nodes.find((n) => n.nodeId === graph.root);
    expect(root?.type).toBe("SoundNodeMixer");
    expect(root?.children).toHaveLength(2);
    for (const child of root?.children ?? []) {
      expect(graph.nodes.find((n) => n.nodeId === child)?.type).toBe("SoundNodeWavePlayer");
    }
  }, 120_000);

  it("adds, connects, disconnects and removes a node, read back after each step", async () => {
    const before = await cueGraph(AUTHORED_CUE);
    const mixId = before.root;
    expect(mixId).toBeTruthy();

    const added = await call("soundcue_add_node", { cuePath: AUTHORED_CUE, nodeType: "delay" });
    const delayId = String(added.nodeId);
    let graph = await cueGraph(AUTHORED_CUE);
    expect(graph.count).toBe(before.count + 1);
    expect(graph.nodes.find((n) => n.nodeId === delayId)?.type).toBe("SoundNodeDelay");

    await call("soundcue_connect", { cuePath: AUTHORED_CUE, childNodeId: delayId, parentNodeId: mixId });
    graph = await cueGraph(AUTHORED_CUE);
    expect(graph.nodes.find((n) => n.nodeId === mixId)?.children).toContain(delayId);

    const detached = await call("soundcue_disconnect", { cuePath: AUTHORED_CUE, childNodeId: delayId, parentNodeId: mixId });
    expect(detached.alreadyDisconnected).toBe(false);
    expect((detached.detached as unknown[]).length).toBe(1);
    graph = await cueGraph(AUTHORED_CUE);
    expect(graph.nodes.find((n) => n.nodeId === mixId)?.children).not.toContain(delayId);

    const removed = await call("soundcue_remove_node", { cuePath: AUTHORED_CUE, nodeId: delayId });
    expect(removed.alreadyDeleted).toBe(false);
    graph = await cueGraph(AUTHORED_CUE);
    expect(graph.count).toBe(before.count);
    expect(graph.nodes.some((n) => n.nodeId === delayId)).toBe(false);
    expect(graph.root).toBe(mixId);
  }, 120_000);
});
