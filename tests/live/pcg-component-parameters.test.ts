// @live-editor headless
/**
 * A PCGComponent inside a Blueprint actor: assigning its graph through UPCGComponent::SetGraph and overriding its
 * graph parameters on the Blueprint's component template, so a construction script rerun keeps them; reading each
 * parameter's value and override state back; and a write to the placed instance reporting what the rerun kept.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/PcgComponentParams`;
const GRAPH = `${DIR}/PCG_CompParams`;
const BP = `${DIR}/BP_PCGHolder`;
const HOLDER = "PcgComponentParams_Holder";
const VOLUME = "PcgComponentParams_Volume";

type Json = Record<string, unknown>;
type Param = { name: string; value: unknown; overridden: boolean };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const param = (result: Json, name: string): Param | undefined =>
  ((result.parameters as Param[] | undefined) ?? []).find((p) => p.name === name);

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const read = (selector: Json): Promise<Json> => call("get_pcg_component_parameters", selector);

beforeAll(async () => {
  bridge = await getBridge();
  for (const label of [HOLDER, VOLUME]) await callBridge(bridge, "delete_actor", { actorLabel: label });
  for (const asset of [BP, GRAPH]) await callBridge(bridge, "delete_asset", { assetPath: asset, force: true });
  await call("create_pcg_graph", { name: "PCG_CompParams", packagePath: DIR });
  await call("set_pcg_graph_parameters", {
    assetPath: GRAPH,
    graphParameters: [{ name: "Spacing", type: "double", default: 100 }, { name: "Enabled", type: "bool", default: true }],
  });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  await call("add_component", { assetPath: BP, componentClass: "PCGComponent", componentName: "PCG" });
  await call("place_actor", { actorClass: `${BP}.BP_PCGHolder_C`, label: HOLDER, location: { x: 0, y: -14000, z: 100 } });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    for (const label of [HOLDER, VOLUME]) await callBridge(bridge, "delete_actor", { actorLabel: label });
    for (const asset of [BP, GRAPH]) await callBridge(bridge, "delete_asset", { assetPath: asset, force: true });
    disconnectBridge();
  }
});

describe("PCG component graph and parameter overrides on a Blueprint", () => {
  it("set_pcg_component_graph assigns the graph on the Blueprint template, and the placed actor rebuilds with it", async () => {
    const set = await call("set_pcg_component_graph", { blueprintPath: BP, graphPath: GRAPH });
    expect(set.target).toBe("blueprint");
    expect(String(set.graph)).toContain("PCG_CompParams");
    expect(set.previousGraph).toBeNull();
    expect(param(set, "Spacing")).toMatchObject({ value: 100, overridden: false });

    const placed = await read({ actorLabel: HOLDER });
    expect(String(placed.graph)).toContain("PCG_CompParams");
    expect(placed.blueprintOwned).toBe(true);
    expect((placed.warnings as string[])[0]).toContain("blueprintPath");
  });

  it("set_pcg_component_parameters on the Blueprint persists through a construction script rerun", async () => {
    const set = await call("set_pcg_component_parameters", { blueprintPath: BP, overrides: { Spacing: 250 } });
    expect(param(set, "Spacing")).toMatchObject({ value: 250, overridden: true });
    expect((set.previous as Json).Spacing).toBeNull();

    await call("rerun_construction_scripts", { actorLabels: [HOLDER] });
    const placed = await read({ actorLabel: HOLDER });
    expect(param(placed, "Spacing")).toMatchObject({ value: 250, overridden: true });
    expect(param(placed, "Enabled")).toMatchObject({ overridden: false });
    const template = await read({ blueprintPath: BP });
    expect(param(template, "Spacing")).toMatchObject({ value: 250, overridden: true });

    // The rollback clears the override again.
    await call("set_pcg_component_parameters", (set.rollback as { payload: Json }).payload);
    expect(param(await read({ blueprintPath: BP }), "Spacing")).toMatchObject({ value: 100, overridden: false });
  });

  it("a write to the placed instance reports whether the construction script rerun kept it", async () => {
    const r = await callBridge(bridge, "set_pcg_component_parameters", { actorLabel: HOLDER, overrides: { Spacing: 400 } });
    const result = body(r);
    const after = param(await read({ actorLabel: HOLDER }), "Spacing");
    if (result.success === false) {
      expect(result.reason).toBe("discarded_by_reconstruction");
      expect(result.discarded).toContain("Spacing");
      expect(String(result.error)).toContain(BP.split("/").pop() as string);
      expect(after?.value === 400 && after.overridden).toBe(false);
    } else {
      expect(result.persistedThroughReconstruction).toBe(true);
      expect(after).toMatchObject({ value: 400, overridden: true });
    }
  });

  it("refuses a parameter the graph does not declare, changing nothing", async () => {
    const before = param(await read({ blueprintPath: BP }), "Spacing");
    const r = await callBridge(bridge, "set_pcg_component_parameters", { blueprintPath: BP, overrides: { Spacing: 7, NoSuchParam: 1 } });
    expect(body(r).success).toBe(false);
    expect(String(body(r).error)).toContain("NoSuchParam");
    expect(param(await read({ blueprintPath: BP }), "Spacing")).toEqual(before);
  });
});

describe("PCG component graph on a placed volume", () => {
  it("assigns and clears the graph on a native component, which a rerun does not rebuild", async () => {
    await call("add_pcg_volume", { label: VOLUME, location: { x: 2000, y: -14000, z: 100 } });
    const set = await call("set_pcg_component_graph", { actorLabel: VOLUME, graphPath: GRAPH });
    expect(set.target).toBe("actor");
    expect(set.blueprintOwned).toBe(false);
    expect(String((await read({ actorLabel: VOLUME })).graph)).toContain("PCG_CompParams");

    const overridden = await call("set_pcg_component_parameters", { actorLabel: VOLUME, overrides: { Enabled: false } });
    expect(param(overridden, "Enabled")).toMatchObject({ value: false, overridden: true });

    const cleared = await call("set_pcg_component_graph", { actorLabel: VOLUME, graphPath: "" });
    expect(cleared.graph).toBeNull();
    expect(cleared.rollbackLossy).toBe(true);
    await call("set_pcg_component_graph", (cleared.rollback as { payload: Json }).payload);
    expect(String((await read({ actorLabel: VOLUME })).graph)).toContain("PCG_CompParams");
  });
});
