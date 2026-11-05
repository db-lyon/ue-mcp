// @live-editor render
/**
 * Editor-world actions read back from placed actors: object reflection and
 * function calls, undo/redo, the viewport (hit test, focus, realtime), bone
 * reads, dirty-package state, asset validation and the MapCheck message log.
 * Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Editor";
const MATERIAL = `${DIR}/M_BCovEditorDirty`;
const CUBE = "BCov_Editor_Cube";
const TARGET = "BCov_Editor_HitTarget";
const SKEL = "BCov_Editor_SkelCube";
const NO_MESH = "BCov_Editor_NoMesh";
const ACTORS = [CUBE, TARGET, SKEL, NO_MESH];
const FAR = { x: 40000, y: 40000, z: 2000 };

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const vec = (v: unknown): Vec => {
  const o = (v ?? {}) as Record<string, number>;
  return { x: Number(o.x ?? o.X), y: Number(o.y ?? o.Y), z: Number(o.z ?? o.Z) };
};
const dist = (a: Vec, b: Vec): number => Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

let bridge: EditorBridge;
let cubePath = "";
let originalCamera: { location: Vec; rotation: Json } | undefined;
let originalRealtime: boolean | undefined;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function actorLocation(label: string): Promise<Vec> {
  const details = await call("get_actor_details", { actorLabel: label });
  return vec(details.location);
}

async function cleanup(): Promise<void> {
  for (const label of ACTORS) await callBridge(bridge, "delete_actor", { actorLabel: label });
  await callBridge(bridge, "delete_asset", { assetPath: MATERIAL, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: CUBE, location: { x: 0, y: 12000, z: 200 } });
  const details = await call("get_actor_details", { actorLabel: CUBE });
  cubePath = String(details.actorPath ?? details.path);
  const info = await call("get_viewport_info", {});
  originalCamera = { location: vec(info.location), rotation: info.rotation as Json };
  const state = await callBridge(bridge, "get_viewport_state", {});
  if (succeeded(state) && typeof body(state).realtime === "boolean") originalRealtime = body(state).realtime as boolean;
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    if (originalCamera) await callBridge(bridge, "set_viewport_camera", { location: originalCamera.location, rotation: originalCamera.rotation });
    if (originalRealtime !== undefined) await callBridge(bridge, "set_realtime", { enabled: originalRealtime });
    await cleanup();
  } finally {
    disconnectBridge();
  }
}, 120_000);

describe("object reflection on a placed actor", () => {
  it("describe_object, find_object and get_object_properties resolve the actor and read bHidden", async () => {
    const described = await call("describe_object", { objectPath: cubePath, propertyNames: ["bHidden"], includeValues: true });
    expect(described.className).toBe("StaticMeshActor");
    expect(described.resolvedPath).toBe(cubePath);
    const prop = (described.properties as Json[])[0];
    expect(prop.value).toBe(false);

    const found = await call("find_object", { objectPath: cubePath });
    expect(found.found).toBe(true);
    expect((found.object as Json).objectPath).toBe(cubePath);

    const objectName = cubePath.split(".").pop()!;
    const searched = await call("find_object", { className: "StaticMeshActor", nameContains: objectName, world: "editor" });
    expect(JSON.stringify(searched.matches)).toContain(cubePath);

    const props = await call("get_object_properties", { objectPath: cubePath, propertyNames: ["bHidden"] });
    expect(props.objectClass).toBe("StaticMeshActor");
    expect(String((props.properties as Json).bHidden)).toBe("False");
  }, 120_000);

  it("invoke_object_function, invoke_function and invoke_object_functions change what get_object_properties reads", async () => {
    const hiddenText = async () =>
      String(((await call("get_object_properties", { objectPath: cubePath, propertyNames: ["bHidden"] })).properties as Json).bHidden);

    const one = await call("invoke_object_function", { objectPath: cubePath, functionName: "SetActorHiddenInGame", args: { bNewHidden: true } });
    expect(one.functionName).toBe("SetActorHiddenInGame");
    expect(await hiddenText()).toBe("True");

    const viaLabel = await call("invoke_function", { actorLabel: CUBE, functionName: "SetActorHiddenInGame", args: { bNewHidden: false } });
    expect(viaLabel.resolvedActorPath).toBe(cubePath);
    expect(await hiddenText()).toBe("False");

    const batch = await call("invoke_object_functions", {
      calls: [
        { objectPath: cubePath, functionName: "SetActorHiddenInGame", args: { bNewHidden: true } },
        { objectPath: cubePath, functionName: "SetActorHiddenInGame", args: { bNewHidden: false } },
        { objectPath: cubePath, functionName: "SetActorHiddenInGame", args: { bNewHidden: true } },
      ],
    });
    expect(batch.completedCalls).toBe(3);
    expect(await hiddenText()).toBe("True");

    await call("invoke_object_function", { objectPath: cubePath, functionName: "SetActorHiddenInGame", args: { bNewHidden: false } });
  }, 120_000);
});

describe("undo and redo", () => {
  it("undo reverses a move_actor and redo re-applies it", async () => {
    const before = await actorLocation(CUBE);
    await call("move_actor", { actorLabel: CUBE, location: { x: before.x, y: before.y, z: before.z + 777 } });
    expect((await actorLocation(CUBE)).z).toBeCloseTo(before.z + 777, 0);

    const undone = await call("undo", {});
    expect(undone.changed).toBe(true);
    expect((await actorLocation(CUBE)).z).toBeCloseTo(before.z, 0);

    const redone = await call("redo", {});
    expect(redone.changed).toBe(true);
    expect((await actorLocation(CUBE)).z).toBeCloseTo(before.z + 777, 0);
  }, 120_000);
});

describe("the level viewport", () => {
  it("hit_test_viewport_pixel finds the actor straight ahead of a posed camera", async () => {
    await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: TARGET, location: FAR, scale: { x: 4, y: 4, z: 4 } });
    await call("set_viewport_camera", { projection: "perspective", location: { x: FAR.x - 1500, y: FAR.y, z: FAR.z + 100 }, rotation: { pitch: 0, yaw: 0, roll: 0 } });
    const info = await call("get_viewport_info", {});
    expect(dist(vec(info.location), { x: FAR.x - 1500, y: FAR.y, z: FAR.z + 100 })).toBeLessThan(1);

    const hit = await call("hit_test_viewport_pixel", { x: 400, y: 300, width: 800, height: 600 });
    expect(hit.hit).toBe(true);
    expect(hit.actorLabel).toBe(TARGET);
    expect(Number(hit.distance)).toBeGreaterThan(1000);
    expect(Number(hit.distance)).toBeLessThan(1500);
  }, 120_000);

  it("focus_viewport_on_actor moves the camera to the actor, as get_viewport_info reads it", async () => {
    await call("set_viewport_camera", { location: { x: 0, y: 0, z: 5000 }, rotation: { pitch: -45, yaw: 0, roll: 0 } });
    const focused = await call("focus_viewport_on_actor", { actorLabel: TARGET });
    expect(focused.actorLabel).toBe(TARGET);
    expect(dist(vec(focused.previousLocation), { x: 0, y: 0, z: 5000 })).toBeLessThan(1);

    let at = vec((await call("get_viewport_info", {})).location);
    for (let i = 0; i < 40 && dist(at, FAR) > 3000; i++) {
      await sleep(250);
      at = vec((await call("get_viewport_info", {})).location);
    }
    expect(dist(at, FAR)).toBeLessThan(3000);
  }, 120_000);

  it("set_realtime writes the flag get_viewport_state reads and reports what it replaced", async () => {
    const off = await call("set_realtime", { enabled: false });
    expect(Number(off.viewportsChanged)).toBeGreaterThan(0);
    const offState = await call("get_viewport_state", {});
    // realtimeSetting is the stored flag; realtime is the effective state, which an editor override can hold down.
    expect(offState.realtimeSetting).toBe(false);
    if (offState.realtimeOverridden !== true) expect(offState.realtime).toBe(false);

    const on = await call("set_realtime", { enabled: true });
    expect(on.previouslyEnabled).toBe(0);
    expect(Number(on.previouslyDisabled)).toBeGreaterThan(0);
    const onState = await call("get_viewport_state", {});
    expect(onState.realtimeSetting).toBe(true);
    if (onState.realtimeOverridden !== true) expect(onState.realtime).toBe(true);
  }, 120_000);
});

describe("bones on a placed skeletal mesh", () => {
  it("read_bone_transforms samples the bones in world space at the actor's location", async () => {
    await call("place_actor", { actorClass: "SkeletalMeshActor", label: SKEL, location: { x: 600, y: 12000, z: 1234 } });
    await call("invoke_function", {
      actorLabel: SKEL,
      component: "SkeletalMeshComponent0",
      functionName: "SetSkeletalMeshAsset",
      args: { NewMesh: "/Engine/EngineMeshes/SkeletalCube.SkeletalCube" },
    });
    const bones = await call("read_bone_transforms", { actorLabel: SKEL, world: "editor", limit: 10 });
    expect(Number(bones.boneCount)).toBeGreaterThan(0);
    expect(bones.space).toBe("world");
    const samples = bones.samples as Array<{ name: string; transform: { location: Vec } }>;
    expect(samples.length).toBeGreaterThan(0);
    expect(samples[0].name).toBeTruthy();
    expect(dist(vec(samples[0].transform.location), { x: 600, y: 12000, z: 1234 })).toBeLessThan(500);
  }, 120_000);
});

describe("unsaved state and validation", () => {
  it("list_dirty_packages and get_world_state report a package left dirty by an unsaved write", async () => {
    await call("create_material", { name: "M_BCovEditorDirty", packagePath: DIR });
    await call("set_property", { objectPath: MATERIAL, propertyName: "TwoSided", value: true, save: false });

    const dirty = await call("list_dirty_packages", {});
    const content = (dirty.content as Array<{ package: string }>).map((e) => e.package);
    expect(content).toContain(MATERIAL);
    expect(dirty.contentCount).toBe(content.length);

    const world = await call("get_world_state", {});
    expect(world.dirtyPackages as string[]).toContain(MATERIAL);
    expect(world.mode).toBe("editor");
    expect(String(world.editorWorldPackage)).toMatch(/^\//);
  }, 120_000);

  it("validate_assets validates exactly the asset it is given", async () => {
    const validated = await call("validate_assets", { assetPath: MATERIAL });
    expect(validated.selectionMode).toBe("explicit");
    expect(validated.requested).toBe(1);
    expect(["valid", "warning", "notValidated", "invalid"]).toContain(validated.result);
  }, 300_000);

  it("get_message_log lists the registered listings and reads MapCheck after a map check", async () => {
    const listings = await call("get_message_log", {});
    const names = (listings.listings as Array<{ logName: string }>).map((l) => l.logName);
    expect(names).toContain("MapCheck");

    await call("place_actor", { actorClass: "StaticMeshActor", label: NO_MESH, location: { x: -600, y: 12000, z: 200 } });
    await call("execute_command", { command: "MAP CHECK" });
    const mapCheck = await call("get_message_log", { logName: "MapCheck", maxLines: 500 });
    const texts = (mapCheck.messages as Array<{ text: string }>).map((m) => m.text).join("\n");
    expect(texts.toLowerCase()).toContain("staticmesh");
    expect(Number(mapCheck.total)).toBeGreaterThan(0);
  }, 180_000);
});
