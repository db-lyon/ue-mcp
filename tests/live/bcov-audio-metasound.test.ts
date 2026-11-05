// @live-editor headless
/**
 * MetaSound authoring one action at a time: source creation, graph members,
 * the four connect forms, defaults, disconnects and renames, each read back
 * through metasound_read_document or metasound_list_connections. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/AudioMetaSound`;
const MS = `${DIR}/MS_BCov.MS_BCov`;
const FREQ = "BCovFreq";
const PITCH = "BCovPitch";
const LEVEL = "BCovLevel";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

interface Member { name: string; nodeId?: string; dataType?: string; default?: unknown }
interface Edge { fromNodeId: string; fromOutput: string; toNodeId: string; toInput: string; graphInput?: string; graphOutput?: string }

async function readDoc(): Promise<{ interfaces: unknown[]; graphInputs: Member[]; graphOutputs: Member[] }> {
  const d = await call("metasound_read_document", { assetPath: MS });
  return {
    interfaces: (d.interfaces as unknown[]) ?? [],
    graphInputs: (d.graphInputs as Member[]) ?? [],
    graphOutputs: (d.graphOutputs as Member[]) ?? [],
  };
}

async function edges(): Promise<Edge[]> {
  return ((await call("metasound_list_connections", { assetPath: MS })).connections as Edge[]) ?? [];
}

/** The first vertex of a type on a node, so vertex names come from the engine rather than this file. */
async function pin(nodeId: string, direction: "inputs" | "outputs", dataType: string): Promise<string> {
  const pins = await call("metasound_list_node_pins", { assetPath: MS, nodeId, direction, dataType });
  const name = ((pins[direction] as Array<{ name: string }>) ?? [])[0]?.name;
  expect(name, `no ${dataType} ${direction} on ${nodeId}: ${JSON.stringify(pins)}`).toBeTruthy();
  return name;
}

async function addNode(nodeClassName: string, nodeVariant: string): Promise<string> {
  const added = await call("metasound_add_node", { assetPath: MS, nodeClassName, nodeNamespace: "UE", nodeVariant });
  return String(added.nodeId);
}

let sineId = "";
let audioAddId = "";
let floatAddId = "";
let audioAddIn = "";
let floatAddOut = "";

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 180_000);

describe("MetaSound authoring", () => {
  it("create_metasound_source makes a source whose document declares its interfaces and audio output", async () => {
    const created = await call("create_metasound_source", { name: "MS_BCov", packagePath: DIR, format: "mono", oneShot: true });
    expect(created.path).toBe(MS);
    expect(created.audioOutputs).toBe(1);
    const doc = await readDoc();
    expect(doc.interfaces.length).toBeGreaterThan(0);
    expect(doc.graphOutputs.some((o) => o.dataType === "Audio")).toBe(true);
  }, 120_000);

  it("metasound_add_graph_input and metasound_add_graph_output declare typed members", async () => {
    await call("metasound_add_graph_input", { assetPath: MS, name: FREQ, dataType: "Float", defaultValue: 220 });
    await call("metasound_add_graph_output", { assetPath: MS, name: LEVEL, dataType: "Float" });
    const doc = await readDoc();
    const input = doc.graphInputs.find((i) => i.name === FREQ);
    expect(input?.dataType).toBe("Float");
    expect(Number(input?.default)).toBeCloseTo(220, 3);
    expect(doc.graphOutputs.find((o) => o.name === LEVEL)?.dataType).toBe("Float");
  }, 120_000);

  it("the four connect forms each land the edge they name", async () => {
    sineId = await addNode("Sine", "Audio");
    audioAddId = await addNode("Add", "Audio");
    floatAddId = await addNode("Add", "Float");
    audioAddIn = await pin(audioAddId, "inputs", "Audio");
    const audioAddOut = await pin(audioAddId, "outputs", "Audio");
    floatAddOut = await pin(floatAddId, "outputs", "Float");

    await call("metasound_connect_graph_input", { assetPath: MS, graphInput: FREQ, toNodeId: sineId, toInput: "Frequency" });
    await call("metasound_connect", { assetPath: MS, fromNodeId: sineId, fromOutput: "Audio", toNodeId: audioAddId, toInput: audioAddIn });
    const out = await call("metasound_connect_audio_out", { assetPath: MS, fromNodeId: audioAddId, fromOutput: audioAddOut, channel: 0 });
    const audioOutName = String(out.graphOutput);
    await call("metasound_connect_graph_output", { assetPath: MS, fromNodeId: floatAddId, fromOutput: floatAddOut, graphOutput: LEVEL });

    const all = await edges();
    expect(all.some((e) => e.graphInput === FREQ && e.toNodeId === sineId && e.toInput === "Frequency")).toBe(true);
    expect(all.some((e) => e.fromNodeId === sineId && e.fromOutput === "Audio" && e.toNodeId === audioAddId && e.toInput === audioAddIn)).toBe(true);
    expect(all.some((e) => e.fromNodeId === audioAddId && e.fromOutput === audioAddOut && e.graphOutput === audioOutName)).toBe(true);
    expect(all.some((e) => e.fromNodeId === floatAddId && e.fromOutput === floatAddOut && e.graphOutput === LEVEL)).toBe(true);
  }, 180_000);

  it("metasound_set_input_default writes a graph input default and a node input default", async () => {
    await call("metasound_set_input_default", { assetPath: MS, graphInput: FREQ, value: 330, dataType: "Float" });
    const input = (await readDoc()).graphInputs.find((i) => i.name === FREQ);
    expect(Number(input?.default)).toBeCloseTo(330, 3);

    const floatIn = await pin(floatAddId, "inputs", "Float");
    await call("metasound_set_input_default", { assetPath: MS, nodeId: floatAddId, inputName: floatIn, value: 2.5, dataType: "Float" });
    const inspected = await call("metasound_inspect_node", { assetPath: MS, nodeId: floatAddId });
    const vertex = ((inspected.inputs as Array<{ name: string; default?: unknown }>) ?? []).find((i) => i.name === floatIn);
    expect(Number(vertex?.default)).toBeCloseTo(2.5, 3);
  }, 120_000);

  it("metasound_disconnect drops one edge, then clears a graph output", async () => {
    const edge = await call("metasound_disconnect", {
      assetPath: MS, fromNodeId: sineId, fromOutput: "Audio", toNodeId: audioAddId, toInput: audioAddIn,
    });
    expect(edge.form).toBe("edge");
    expect(edge.alreadyDisconnected).toBe(false);
    let all = await edges();
    expect(all.some((e) => e.fromNodeId === sineId && e.toNodeId === audioAddId)).toBe(false);
    expect(all.some((e) => e.graphInput === FREQ && e.toNodeId === sineId)).toBe(true);

    const output = await call("metasound_disconnect", { assetPath: MS, graphOutput: LEVEL });
    expect(output.form).toBe("input");
    all = await edges();
    expect(all.some((e) => e.graphOutput === LEVEL)).toBe(false);
  }, 120_000);

  it("metasound_rename_member renames a graph input and keeps its edge", async () => {
    const renamed = await call("metasound_rename_member", { assetPath: MS, memberKind: "input", name: FREQ, newName: PITCH });
    expect(renamed.unchanged).toBe(false);
    const names = (await readDoc()).graphInputs.map((i) => i.name);
    expect(names).toContain(PITCH);
    expect(names).not.toContain(FREQ);
    expect((await edges()).some((e) => e.graphInput === PITCH && e.toNodeId === sineId && e.toInput === "Frequency")).toBe(true);
  }, 120_000);
});
