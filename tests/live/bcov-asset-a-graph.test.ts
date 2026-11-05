// @live-editor headless
/**
 * Asset graph authoring on a Mutable CustomizableObject, every edit read back
 * through read_asset_graph: create the object, add a node, connect and
 * disconnect it, then compile. The test project enables Mutable. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AssetA/Graph";
const CO = `${DIR}/CO_BCov`;

type Json = Record<string, unknown>;
interface Link { nodeGuid: string; pinId: string }
interface Pin { name: string; pinId: string; direction: string; linkCount?: number; linkedTo?: Link[] }
interface Node { nodeGuid: string; path: string; class: string; pins: Pin[] }
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function nodes(): Promise<Node[]> {
  const read = await call("read_asset_graph", { assetPath: CO });
  return ((read.graphs as Array<{ nodes: Node[] }>) ?? []).flatMap((g) => g.nodes);
}

async function pin(nodeGuid: string, pinId: string): Promise<Pin> {
  const node = (await nodes()).find((n) => n.nodeGuid === nodeGuid);
  expect(node, `node ${nodeGuid}`).toBeDefined();
  const found = node!.pins.find((p) => p.pinId === pinId);
  expect(found, `pin ${pinId}`).toBeDefined();
  return found!;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_folder", { path: DIR });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("customizable object graph", () => {
  it("create_customizable_object makes an object whose graph holds the base node", async () => {
    const created = await call("create_customizable_object", { name: "CO_BCov", packagePath: DIR });
    expect(created.created).toBe(true);
    const all = await nodes();
    expect(all.some((n) => n.class === "CustomizableObjectNodeObject")).toBe(true);
    const health = await call("asset_health_check", { assetPath: CO });
    expect(health.onDisk).toBe(true);
  }, 120_000);

  it("add_graph_node, connect_graph_pins and disconnect_graph_pins edit the wires read_asset_graph reports", async () => {
    const base = (await nodes()).find((n) => n.class === "CustomizableObjectNodeObject")!;
    const components = base.pins.find((p) => p.name === "Components")!;
    expect(components).toBeDefined();

    const added = await call("add_graph_node", { assetPath: CO, nodeClass: "CONodeComponentSkeletalMesh", posX: -400, posY: 0, save: false });
    const guid = String(added.nodeGuid);
    const output = (added.pins as Pin[]).find((p) => p.direction === "output")!;
    expect(output).toBeDefined();
    expect((await nodes()).some((n) => n.nodeGuid === guid)).toBe(true);

    await call("connect_graph_pins", { assetPath: CO, sourceNode: guid, sourcePinId: output.pinId, targetPinId: components.pinId, save: false });
    const linked = await pin(base.nodeGuid, components.pinId);
    expect(linked.linkCount).toBe(1);
    expect(linked.linkedTo?.[0]?.nodeGuid).toBe(guid);

    const disconnected = await call("disconnect_graph_pins", { assetPath: CO, sourcePinId: output.pinId, targetPinId: components.pinId, save: false });
    expect(disconnected.brokenCount).toBe(1);
    expect((await pin(base.nodeGuid, components.pinId)).linkCount ?? 0).toBe(0);

    await call("remove_graph_node", { assetPath: CO, node: guid, save: false });
    expect((await nodes()).some((n) => n.nodeGuid === guid)).toBe(false);
  }, 120_000);

  it("compile_customizable_object compiles the object without errors", async () => {
    const compiled = await call("compile_customizable_object", { assetPath: CO });
    expect(compiled.errorCount).toBe(0);
    expect(String(compiled.compiledWith)).toMatch(/Compile/);
    if (compiled.compiled !== undefined) expect(compiled.compiled).toBe(true);
  }, 600_000);
});
