// @live-editor headless
/**
 * Play-In-Editor gameplay runtime: applying and removing a mapping context on
 * the live player, reading the action value it binds, and an impulse moving a
 * simulating body. PIE is started here and stopped in afterAll.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Gameplay/Pie";
const IA = `${DIR}/IA_BCovPie`;
const IMC = `${DIR}/IMC_BCovPie`;
const CUBE = "BCov_Gameplay_PieCube";

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Row[] => (Array.isArray(v) ? (v as Row[]) : []);
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;
let playing = false;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

/** The editor can hold PIE behind its Blueprint-error prompt; proceed through it. */
async function answerPiePrompt(): Promise<void> {
  const listed = body(await callBridge(bridge, "list_dialogs"));
  for (const dialog of rows(listed.dialogs)) {
    const proceed = ((dialog.buttons as string[] | undefined) ?? []).find((b) => /play in editor/i.test(b));
    if (proceed) await callBridge(bridge, "respond_to_dialog", { buttonLabel: proceed });
  }
}

/** Whether the first PIE player has the test context applied, and at what priority. */
async function appliedContext(): Promise<Row> {
  const read = await call("get_input_mapping_contexts", { mappingContext: "IMC_BCovPie" });
  const world = rows(read.worlds)[0];
  return rows(world?.players)[0] ?? {};
}

async function cubeLocation(): Promise<Vec> {
  const details = await call("get_component_details", { actorLabel: CUBE, world: "pie" });
  const mesh = rows(details.components).find((c) => c.class === "StaticMeshComponent");
  return mesh?.worldLocation as Vec;
}

async function wipe(): Promise<void> {
  await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    for (let i = 0; i < 20 && (await pieRunning()); i++) await sleep(500);
  }
  await wipe();
  await call("create_input_action", { name: "IA_BCovPie", packagePath: DIR });
  await call("create_input_mapping_context", { name: "IMC_BCovPie", packagePath: DIR });
  await call("add_imc_mapping", { imcPath: IMC, inputActionPath: IA, key: "SpaceBar" });

  // A movable, simulating cube with gravity off, so it holds still until pushed.
  await call("place_actor", {
    actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: CUBE,
    location: { x: 0, y: -9000, z: 2000 },
  });
  await call("set_actor_mobility", { actorLabel: CUBE, mobility: "movable" });
  await call("set_simulate_physics", { actorLabel: CUBE, simulate: true });
  await call("set_physics_properties", { actorLabel: CUBE, enableGravity: false });

  await callBridge(bridge, "pie_control", { pieAction: "start" });
  for (let i = 0; i < 40 && !playing; i++) {
    playing = await pieRunning();
    if (!playing) {
      await answerPiePrompt();
      await sleep(1000);
    }
  }
}, 240_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    for (let i = 0; i < 20 && (await pieRunning()); i++) await sleep(500);
    await wipe();
    disconnectBridge();
  }
});

describe("Enhanced Input on the live player", () => {
  it("apply_mapping_context, get_input_mapping_contexts, get_action_value and remove_mapping_context agree on the live context", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    expect((await appliedContext()).hasRequestedContext).toBe(false);

    const applied = await call("apply_mapping_context", { mappingContext: IMC, priority: 3 });
    expect(applied.alreadyApplied).toBe(false);
    const on = await appliedContext();
    expect(on.hasRequestedContext).toBe(true);
    expect(on.requestedContextPriority).toBe(3);

    // The player rebuilds its key mappings on a later tick, so the bound key shows up then.
    let keys: string[] = [];
    for (let i = 0; i < 20 && !keys.includes("SpaceBar"); i++) {
      const value = await call("get_action_value", { inputActionPath: IA });
      const entry = rows(value.actions)[0];
      expect(entry.name).toBe("IA_BCovPie");
      expect(entry.nonZero).toBe(false);
      keys = ((entry.mappedKeys as string[] | undefined) ?? []).map(String);
      if (!keys.includes("SpaceBar")) await sleep(250);
    }
    expect(keys).toContain("SpaceBar");

    const removed = await call("remove_mapping_context", { mappingContext: IMC });
    expect(removed.alreadyAbsent).toBe(false);
    expect(removed.removedPriority).toBe(3);
    expect((await appliedContext()).hasRequestedContext).toBe(false);
  }, 120_000);
});

describe("physics in PIE", () => {
  it("add_impulse moves a simulating body along the impulse", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const before = await cubeLocation();
    const pushed = await call("add_impulse", {
      actorLabel: CUBE, world: "pie", impulse: { x: 800, y: 0, z: 0 }, velChange: true,
    });
    expect(pushed.mode).toBe("impulse");
    let after = before;
    for (let i = 0; i < 20 && after.x - before.x < 100; i++) {
      await sleep(250);
      after = await cubeLocation();
    }
    expect(after.x - before.x).toBeGreaterThan(100);
    expect(Math.abs(after.y - before.y)).toBeLessThan(50);
  }, 120_000);
});
