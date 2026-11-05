// @live-editor headless
/**
 * Play In Editor runtime actions, each read back from the running session:
 * the player pawn and view, input mode, time dilation, PIE instances, live
 * property reads, movement mode and teleports on a placed Character, and the
 * Blueprint-error bypass start. PIE is stopped in a finally block. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const CHAR = "BCov_Editor_PieChar";
const FLOOR = "BCov_Editor_PieFloor";
const HOME = { x: 0, y: 16000, z: 0 };

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const vec = (v: unknown): Vec => {
  const o = (v ?? {}) as Record<string, number>;
  return { x: Number(o.x ?? o.X), y: Number(o.y ?? o.Y), z: Number(o.z ?? o.Z) };
};
const field = (text: unknown, name: string): number => Number(new RegExp(`${name}=(-?[\\d.]+)`).exec(String(text))?.[1]);

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

async function waitForPie(playing: boolean): Promise<boolean> {
  for (let i = 0; i < 60; i++) {
    if ((await pieRunning()) === playing) return true;
    await sleep(500);
  }
  return false;
}

async function stopPie(): Promise<void> {
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    await waitForPie(false);
  }
}

async function charRow(): Promise<Json | undefined> {
  const read = await call("get_runtime_values", { classFilter: "Character", world: "pie", paths: ["K2_GetActorLocation", "CustomTimeDilation"] });
  return (read.rows as Json[]).find((r) => r.actorLabel === CHAR);
}

beforeAll(async () => {
  bridge = await getBridge();
  await stopPie();
  for (const label of [CHAR, FLOOR]) await callBridge(bridge, "delete_actor", { actorLabel: label });
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: FLOOR, location: HOME, scale: { x: 20, y: 20, z: 0.5 } });
  await call("place_actor", { actorClass: "Character", label: CHAR, location: { x: HOME.x, y: HOME.y, z: HOME.z + 300 } });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await stopPie();
  } finally {
    for (const label of [CHAR, FLOOR]) await callBridge(bridge, "delete_actor", { actorLabel: label });
    disconnectBridge();
  }
}, 120_000);

describe("a running PIE session", () => {
  let playing = false;

  beforeAll(async () => {
    await call("pie_control", { pieAction: "start" });
    playing = await waitForPie(true);
  }, 120_000);

  afterAll(async () => {
    try {
      await callBridge(bridge, "set_pie_time_scale", { factor: 1 });
    } finally {
      await stopPie();
    }
  }, 120_000);

  it("list_pie_instances and get_pie_pawn describe the session the player pawn lives in", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const instances = await call("list_pie_instances", {});
    expect(Number(instances.count)).toBeGreaterThan(0);
    const worldPath = String((instances.instances as Json[])[0].worldPath);
    expect(worldPath).toContain("UEDPIE");

    const pawn = await call("get_pie_pawn", {});
    expect(String(pawn.path)).toContain("UEDPIE");
    const viaTarget = await call("get_object_properties", { target: "playerpawn", world: "pie", propertyNames: ["bCanBeDamaged"] });
    expect(viaTarget.objectPath).toBe(pawn.path);
  }, 60_000);

  it("pie_set_player_view writes the control rotation the player controller reads back", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const set = await call("pie_set_player_view", { pitch: -10, yaw: 37, roll: 0 });
    expect(Number((set.controlRotation as Json).yaw)).toBeCloseTo(37, 0);
    const pc = await call("get_object_properties", { target: "playercontroller", world: "pie", propertyNames: ["ControlRotation"] });
    const text = (pc.properties as Json).ControlRotation;
    expect(field(text, "Yaw")).toBeCloseTo(37, 0);
    expect(field(text, "Pitch")).toBeCloseTo(-10, 0);
  }, 60_000);

  it("stage_game_input shows the cursor the player controller reports", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const staged = await call("stage_game_input", { inputMode: "gameAndUI", showMouseCursor: true });
    expect(staged.showMouseCursor).toBe(true);
    const pc = await call("get_object_properties", { target: "playercontroller", world: "pie", propertyNames: ["bShowMouseCursor"] });
    expect(String((pc.properties as Json).bShowMouseCursor)).toBe("True");

    const back = await call("stage_game_input", { inputMode: "gameOnly", showMouseCursor: false });
    expect(back.previousShowMouseCursor).toBe(true);
  }, 60_000);

  it("set_pie_time_scale sets the global time dilation GameplayStatics reads", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const set = await call("set_pie_time_scale", { factor: 2 });
    try {
      expect(Number(set.appliedFactor)).toBeCloseTo(2, 3);
      const read = await call("invoke_static_function", { className: "GameplayStatics", functionName: "GetGlobalTimeDilation", world: "pie" });
      expect(Number((read.returnValues as Json).ReturnValue)).toBeCloseTo(2, 3);
    } finally {
      await call("set_pie_time_scale", { factor: Number(set.previousFactor) > 0 ? Number(set.previousFactor) : 1 });
    }
  }, 60_000);

  it("get_runtime_value and get_runtime_values read a live property the session wrote", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const row = await charRow();
    expect(row, "the placed Character is not in the PIE world").toBeDefined();
    const path = (await call("get_actor_details", { actorLabel: CHAR, world: "pie" })).actorPath as string;
    await call("set_object_property", { objectPath: path, propertyName: "CustomTimeDilation", value: 0.5, world: "pie" });

    const one = await call("get_runtime_value", { actorLabel: CHAR, propertyName: "CustomTimeDilation" });
    expect(Number(one.value)).toBeCloseTo(0.5, 3);
    const many = await charRow();
    expect(Number((many!.values as Json).CustomTimeDilation)).toBeCloseTo(0.5, 3);
  }, 60_000);

  it("set_movement_mode makes the Character fly and teleport_runtime_actor moves it where get_runtime_values reads it", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const mode = await call("set_movement_mode", { actorLabel: CHAR, mode: "flying" });
    expect(String(mode.mode)).toContain("Flying");
    const actorPath = String(mode.actorPath);
    const props = await call("get_object_properties", { objectPath: actorPath, world: "pie", propertyNames: ["CharacterMovement.MovementMode"] });
    expect(String((props.properties as Json)["CharacterMovement.MovementMode"])).toContain("Flying");

    const dest = { x: HOME.x + 400, y: HOME.y + 200, z: HOME.z + 900 };
    const moved = await call("teleport_runtime_actor", { actorLabel: CHAR, location: dest });
    expect(moved.teleported).toBe(true);
    let at = vec(((await charRow())!.values as Json).K2_GetActorLocation);
    for (let i = 0; i < 40 && Math.hypot(at.x - dest.x, at.y - dest.y, at.z - dest.z) >= 25; i++) {
      await sleep(50);
      at = vec(((await charRow())!.values as Json).K2_GetActorLocation);
    }
    expect(Math.hypot(at.x - dest.x, at.y - dest.y, at.z - dest.z)).toBeLessThan(25);
  }, 60_000);
});

describe("the Blueprint-error bypass start", () => {
  it("pie_start_ignoring_blueprint_errors starts a PIE session pie_control reports", async () => {
    await stopPie();
    try {
      const started = await call("pie_start_ignoring_blueprint_errors", { authorizationSource: "config" });
      expect(started.playSessionRequested).toBe(true);
      expect(started.authorizationSource).toBe("config");
      expect(await waitForPie(true)).toBe(true);
    } finally {
      await stopPie();
    }
    expect(await pieRunning()).toBe(false);
  }, 120_000);
});
