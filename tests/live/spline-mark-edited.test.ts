// @live-editor headless
/**
 * set_spline_points on a spline a Blueprint's construction script owns: by default it marks the spline edited, so a
 * construction script rerun keeps the points; with markEdited false the rerun puts the Blueprint's defaults back.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const BP = `${TEST_PREFIX}/SplineEdited/BP_SplineHolder`;
const LABEL = "SplineEdited_Holder";
const POINTS = [
  { x: 0, y: -12000, z: 100 },
  { x: 1500, y: -12000, z: 100 },
  { x: 3000, y: -11000, z: 100 },
];

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;
let defaultCount = 0;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const info = (): Promise<Json> => call("get_spline_info", { actorLabel: LABEL, componentName: "Path" });
const rerun = (): Promise<Json> => call("rerun_construction_scripts", { actorLabels: [LABEL] });

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  await call("add_component", { assetPath: BP, componentClass: "SplineComponent", componentName: "Path" });
  await call("place_actor", { actorClass: `${BP}.BP_SplineHolder_C`, label: LABEL, location: { x: 0, y: -12000, z: 100 } });
  defaultCount = Number((await info()).splinePointCount);
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    disconnectBridge();
  }
});

describe("set_spline_points marks a construction-script spline edited", () => {
  it("keeps the written points through a construction script rerun", async () => {
    const set = await call("set_spline_points", { actorLabel: LABEL, componentName: "Path", points: POINTS });
    expect(set.splineHasBeenEdited).toBe(true);
    expect(set.previousSplineHasBeenEdited).toBe(false);
    expect(set.creationMethod).toBe("SimpleConstructionScript");
    const before = await info();
    await rerun();
    const after = await info();
    expect(after.splinePointCount).toBe(POINTS.length);
    expect(after.splineHasBeenEdited).toBe(true);
    expect(Number(after.splineLength)).toBeCloseTo(Number(before.splineLength), 0);
  });

  it("with markEdited false, the rerun restores the Blueprint's default points", async () => {
    const set = await call("set_spline_points", { actorLabel: LABEL, componentName: "Path", points: POINTS, markEdited: false });
    expect(set.splineHasBeenEdited).toBe(false);
    expect((await info()).splinePointCount).toBe(POINTS.length);
    await rerun();
    const after = await info();
    expect(after.splineHasBeenEdited).toBe(false);
    expect(after.splinePointCount).toBe(defaultCount);
  });

  it("the rollback restores the flag it found", async () => {
    const set = await call("set_spline_points", { actorLabel: LABEL, componentName: "Path", points: POINTS });
    expect(set.previousSplineHasBeenEdited).toBe(false);
    await call("set_spline_points", (set.rollback as { payload: Json }).payload);
    expect((await info()).splineHasBeenEdited).toBe(false);
  });
});
