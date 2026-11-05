// @live-editor headless
/**
 * StateTree property bindings: an evaluator output bound into a state task's
 * instance data, found through list_bindable_sources and read back through
 * list_bindings and read. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/StateTree";
const ST = `${DIR}/ST_BCovBindings`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
/** list_bindable_sources prints GUIDs without hyphens, the binding reads with them. */
const guid = (g: unknown): string => String(g).replace(/-/g, "").toLowerCase();

let bridge: EditorBridge;
let evalInstanceType = "";
let evalFloatProp = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
  await call("create_state_tree", { name: "ST_BCovBindings", packagePath: DIR });

  // Any loaded evaluator struct with a float instance property is a binding source.
  const types = await call("list_state_tree_node_types", { assetPath: ST, nodeType: "evaluator", schemaAllowedOnly: false });
  let evalPath = "";
  for (const t of (types.evaluator as Json).nodeTypes as Json[]) {
    if (t.instanceDataIsClass !== false) continue;
    const p = ((t.instanceProperties as Json[]) ?? []).find((ip) => ip.type === "float");
    if (p) { evalPath = String(t.structPath); evalInstanceType = String(t.instanceDataType); evalFloatProp = String(p.name); break; }
  }
  if (evalPath) await call("add_state_tree_evaluator", { assetPath: ST, structType: evalPath });

  const states = (await call("list_state_tree_states", { assetPath: ST })).states as Json[];
  await call("add_state_tree_task", { assetPath: ST, stateId: states[0].id, structType: "/Script/StateTreeModule.StateTreeDelayTask" });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: ST, force: true });
    disconnectBridge();
  }
});

describe("statetree bindings", () => {
  it("add_state_tree_binding binds an evaluator float to a task's Duration; remove_state_tree_binding drops it", async () => {
    expect(evalFloatProp, "no loaded evaluator struct with a float instance property").not.toBe("");

    const sources = (await call("list_state_tree_bindable_sources", { assetPath: ST })).sources as Json[];
    const source = sources.find((s) => s.structType === evalInstanceType);
    const target = sources.find((s) => s.structType === "StateTreeDelayTaskInstanceData");
    expect(source, JSON.stringify(sources)).toBeDefined();
    expect(target, JSON.stringify(sources)).toBeDefined();
    const sourceId = String(source!.structId);
    const targetId = String(target!.structId);

    const added = await call("add_state_tree_binding", {
      assetPath: ST, sourceStructId: sourceId, sourcePath: evalFloatProp, targetStructId: targetId, targetPath: "Duration",
    });
    expect(added.replacedExistingBinding).toBe(false);

    const listed = (await call("list_state_tree_bindings", { assetPath: ST, structId: targetId })).bindings as Json[];
    expect(listed).toHaveLength(1);
    expect(guid(listed[0].sourceStructId)).toBe(guid(sourceId));
    expect(listed[0].sourcePath).toBe(evalFloatProp);
    expect(listed[0].targetPath).toBe("Duration");

    const tree = await call("read_state_tree", { assetPath: ST });
    expect((tree.editorBindings as Json[]).some((b) => guid(b.targetStructId) === guid(targetId) && b.targetPath === "Duration")).toBe(true);

    const removed = await call("remove_state_tree_binding", { assetPath: ST, targetStructId: targetId, targetPath: "Duration" });
    expect(removed.removedBindings as Json[]).toHaveLength(1);
    const after = (await call("list_state_tree_bindings", { assetPath: ST, structId: targetId })).bindings as Json[];
    expect(after).toHaveLength(0);
  }, 120_000);
});
