// @live-editor headless
/**
 * The demo category: the step list, building the first three steps, cleanup and
 * the home level, read back through the level, actor and asset readers. The
 * level that was open before the file runs is reopened afterwards. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Demo`;
const SCRATCH_MAP = `${DIR}/L_BCovDemoScratch`;
const HOME = "/Game/MCP_Home";
const DEMO_LEVEL = "/Game/Demo/DemoLevel";
const DEMO_MATERIALS = ["/Game/Demo/M_Demo_Floor", "/Game/Demo/M_Demo_Glow", "/Game/Demo/M_Demo_Pillar"];

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let originalMap: string | undefined;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function currentMap(): Promise<string | undefined> {
  return (await call("get_current_level", {})).mapPackagePath as string | undefined;
}

async function exists(assetPath: string): Promise<boolean> {
  return succeeded(await callBridge(bridge, "read_asset", { assetPath }));
}

beforeAll(async () => {
  bridge = await getBridge();
  originalMap = await currentMap();
  // Leftovers from an interrupted run; this also leaves the editor on the home level.
  await callBridge(bridge, "demo_cleanup", {});
  await callBridge(bridge, "delete_asset", { assetPath: SCRATCH_MAP, force: true });
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "demo_cleanup", {});
  const restore = originalMap && originalMap.startsWith("/Game/") && !originalMap.startsWith("/Game/Demo/") && originalMap !== SCRATCH_MAP
    ? originalMap
    : HOME;
  await callBridge(bridge, "load_level", { levelPath: restore });
  await callBridge(bridge, "delete_asset", { assetPath: SCRATCH_MAP, force: true });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  disconnectBridge();
}, 300_000);

describe("demo", () => {
  it("demo_get_steps lists the nineteen steps in order", async () => {
    const listed = await call("demo_get_steps", {});
    const steps = listed.steps as Array<{ index: number; id: string; description: string }>;
    expect(listed.count).toBe(19);
    expect(steps.map((s) => s.index)).toEqual(Array.from({ length: 19 }, (_, i) => i + 1));
    expect(steps[0].id).toBe("create_level");
    expect(steps[2].id).toBe("floor");
    expect(steps[18].id).toBe("save");
    for (const s of steps) expect(s.description.length).toBeGreaterThan(0);
  }, 60_000);

  it("demo_step builds the demo level, its materials and the floor", async () => {
    const level = await call("demo_step", { step: 1 });
    expect(level.stepId).toBe("create_level");
    expect(await currentMap()).toBe(DEMO_LEVEL);

    const materials = await call("demo_step", { step: 2 });
    expect(materials.count).toBe(3);
    for (const m of DEMO_MATERIALS) expect(await exists(m), m).toBe(true);

    const floor = await call("demo_step", { step: 3 });
    expect(floor.actorLabel).toBe("Demo_Floor");
    const details = await call("get_actor_details", { actorLabel: "Demo_Floor" });
    expect(details.class).toBe("StaticMeshActor");
    expect((details.scale as { x: number }).x).toBeCloseTo(60, 3);
  }, 300_000);

  it("demo_cleanup deletes the demo level and materials and anchors on the home level", async () => {
    const cleaned = await call("demo_cleanup", {});
    expect(cleaned.previousLevelPath).toBe(DEMO_LEVEL);
    expect(cleaned.levelSwitched).toBe(true);
    expect(Number(cleaned.assetsDeleted)).toBeGreaterThanOrEqual(4);
    expect(await currentMap()).toBe(HOME);
    expect(await exists(DEMO_LEVEL)).toBe(false);
    for (const m of DEMO_MATERIALS) expect(await exists(m), m).toBe(false);
  }, 300_000);

  it("demo_go_home leaves another map for the home level", async () => {
    await call("create_new_level", { levelPath: SCRATCH_MAP });
    if ((await currentMap()) !== SCRATCH_MAP) await call("load_level", { levelPath: SCRATCH_MAP });
    expect(await currentMap()).toBe(SCRATCH_MAP);

    const home = await call("demo_go_home", {});
    expect(home.levelPath).toBe(HOME);
    expect(home.previousLevelPath).toBe(SCRATCH_MAP);
    expect(home.alreadyOpen).toBe(false);
    expect(await currentMap()).toBe(HOME);
  }, 300_000);
});
