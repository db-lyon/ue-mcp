// @live-editor headless
/**
 * get_runtime_values with a component classFilter: an actor whose class does not match answers with one row per
 * matching component, so both instanced static mesh components on one placed actor are read.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const BP = `${TEST_PREFIX}/RuntimeValues/BP_TwoISMs`;
const LABEL = "RuntimeValues_TwoISMs";

type Json = Record<string, unknown>;
type Row = { actorLabel: string; componentName?: string; componentClass?: string; values: Record<string, unknown> };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  await call("add_component", { assetPath: BP, componentClass: "InstancedStaticMeshComponent", componentName: "TreesISM" });
  await call("add_component", { assetPath: BP, componentClass: "InstancedStaticMeshComponent", componentName: "RocksISM" });
  await call("place_actor", { actorClass: `${BP}.BP_TwoISMs_C`, label: LABEL, location: { x: 0, y: -9000, z: 100 } });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    disconnectBridge();
  }
});

describe("get_runtime_values reads every matching component", () => {
  it("returns a row for each InstancedStaticMeshComponent on the actor", async () => {
    const read = await call("get_runtime_values", {
      classFilter: "InstancedStaticMeshComponent", world: "editor", paths: ["GetInstanceCount"],
    });
    const rows = (read.rows as Row[]).filter((r) => r.actorLabel === LABEL);
    expect(rows).toHaveLength(2);
    expect(rows.map((r) => r.componentName).sort()).toEqual(
      [expect.stringContaining("RocksISM"), expect.stringContaining("TreesISM")],
    );
    for (const row of rows) {
      expect(row.componentClass).toBe("InstancedStaticMeshComponent");
      expect(row.values.GetInstanceCount).toBe(0);
    }
    expect(Number(read.matched)).toBeGreaterThanOrEqual(Number(read.matchedActors) + 1);
  });

  it("an actor class filter still answers once per actor", async () => {
    const read = await call("get_runtime_values", { classFilter: "BP_TwoISMs", world: "editor", paths: ["bHidden"] });
    const rows = (read.rows as Row[]).filter((r) => r.actorLabel === LABEL);
    expect(rows).toHaveLength(1);
    expect(rows[0].componentName).toBeUndefined();
  });
});
