// asset graph authoring (#1059) on a Mutable CustomizableObject, the graph
// type the actions were built for. Skips when Mutable is not enabled.
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

interface Pin { name: string; pinId: string; direction: string }
interface Node { nodeGuid: string; nodePath: string; nodeClass: string; pins: Pin[] }
/** A handler's own answer. A JSON-RPC result with success=false is still ok=true in this harness. */
type Answer = Record<string, unknown> & { success?: boolean; error?: string };

let bridge: EditorBridge;
let hasMutable = false;
const assetName = `CO_GraphAuthoring_${process.pid}`;
const assetPath = `${TEST_PREFIX}/${assetName}`;

async function answer(method: string, params: Record<string, unknown>): Promise<Answer> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok, r.error).toBe(true);
  return r.result as Answer;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath, force: true }).catch(() => {});
  const created = await answer("create_customizable_object", { name: assetName, packagePath: TEST_PREFIX, save: false });
  hasMutable = created.success === true;
  if (!hasMutable) expect(created.error).toMatch(/Mutable plugin not available/);
}, 60_000);

afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath, force: true }).catch(() => {});
  disconnectBridge();
});

describe("asset - graph authoring through the schema", () => {
  it("adds, connects, disconnects and removes a node, then compiles", async ({ skip }) => {
    if (!hasMutable) skip();

    const read = await answer("read_asset_graph", { assetPath });
    const graphs = (read.graphs as { nodes: (Node & { class: string; pins: Pin[] })[] }[]) ?? [];
    const base = graphs[0]?.nodes.find((n) => n.class === "CustomizableObjectNodeObject");
    expect(base, "the factory adds a Base Object node").toBeDefined();
    const components = base!.pins.find((p) => p.name === "Components")!;
    expect(components).toBeDefined();

    const add = await answer("add_graph_node", {
      assetPath, nodeClass: "CONodeComponentSkeletalMesh", posX: -400, posY: 0, save: false,
    });
    expect(add.success, add.error).toBe(true);
    expect(add.createdVia).toBe("schema_action");
    const output = (add.pins as Pin[]).find((p) => p.direction === "output")!;
    expect(output, "the new node has an output pin").toBeDefined();

    const connected = await answer("connect_graph_pins", {
      assetPath, sourceNode: add.nodeGuid, sourcePinId: output.pinId, targetPinId: components.pinId, save: false,
    });
    expect(connected.success, connected.error).toBe(true);
    expect((connected.rollback as { method?: string })?.method).toBe("disconnect_graph_pins");

    const again = await answer("connect_graph_pins", {
      assetPath, sourcePinId: output.pinId, targetPinId: components.pinId, save: false,
    });
    expect(again.success, again.error).toBe(true);
    expect(again.existed).toBe(true);

    const refused = await answer("connect_graph_pins", {
      assetPath, sourcePinId: components.pinId, targetPinId: base!.pins.find((p) => p.name === "Children")!.pinId, save: false,
    });
    expect(refused.success).toBe(false);
    expect(refused.reason).toBe("schema_disallowed");

    const disconnected = await answer("disconnect_graph_pins", {
      assetPath, sourcePinId: output.pinId, targetPinId: components.pinId, save: false,
    });
    expect(disconnected.success, disconnected.error).toBe(true);
    expect(disconnected.brokenCount).toBe(1);

    const removed = await answer("remove_graph_node", { assetPath, node: add.nodePath, save: false });
    expect(removed.success, removed.error).toBe(true);

    const rootRemoval = await answer("remove_graph_node", { assetPath, node: base!.nodeGuid, save: false });
    expect(rootRemoval.success).toBe(false);

    const compiled = await answer("compile_customizable_object", { assetPath });
    expect(compiled.success, compiled.error).toBe(true);
  });

  it("lists the schema's actions when nothing spawns the class", async ({ skip }) => {
    if (!hasMutable) skip();
    const r = await answer("add_graph_node", { assetPath, nodeClass: "Actor", save: false });
    expect(r.success).toBe(false);
    expect(Array.isArray(r.availableActions)).toBe(true);
  });

  it("compile_customizable_object refuses an asset that is not one", async () => {
    const r = await answer("compile_customizable_object", { assetPath: "/Engine/EngineResources/DefaultTexture" });
    expect(r.success).toBe(false);
    expect(r.error ?? "").toMatch(/Mutable plugin not available|not a CustomizableObject/);
  });
});
