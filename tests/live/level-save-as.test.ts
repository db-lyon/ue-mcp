// @live-editor interactive
/**
 * level(save_as): saving the open map under a new path (#2007).
 *
 * A never-saved Untitled map had no native way to a file: level(save) wrote
 * the package it already had, which for an Untitled map is under /Temp, and
 * an agent fell back to execute_python. save_as is that call, and the engine
 * save behind it opens a message box on several failures, so this runs on an
 * editor where modal dialogs come up and checks that none does. Each saved
 * map is reopened from disk to prove the actor placed in it was written.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOME = "/Game/MCP_Home";
const DIR = `${TEST_PREFIX}/SaveAs`;
const SCRATCH = `${DIR}/L_SaveAsScratch`;
const SAVED = `${DIR}/L_SaveAsFromUntitled`;
const WP_SOURCE = `${DIR}/L_SaveAsWpSource`;
const WP_SAVED = `${DIR}/L_SaveAsWpCopy`;
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const LABEL = "SaveAs_Cube";
const WP_LABEL = "SaveAs_WpCube";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function currentMap(): Promise<string> {
  return String((await call("get_current_level")).mapPackagePath ?? "");
}

async function expectNoDialogs(): Promise<void> {
  const listed = await call("list_dialogs");
  expect(Array.isArray(listed.dialogs) ? listed.dialogs : [], "save_as must not raise a modal").toEqual([]);
}

async function placeCube(label: string): Promise<void> {
  await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label, staticMesh: CUBE, location: { x: 0, y: 0, z: 100 } });
}

async function hasActor(label: string): Promise<boolean> {
  return succeeded(await callBridge(bridge, "get_actor_details", { actorLabel: label }));
}

/** Put the editor on a never-saved /Temp map holding one placed cube, and return its package name. */
async function onUntitledMapWithCube(): Promise<string> {
  await callBridge(bridge, "delete_asset", { assetPath: SCRATCH, force: true });
  await call("create_new_level", { levelPath: SCRATCH, discardUnsaved: true });
  expect(await currentMap()).toBe(SCRATCH);
  // Deleting the open map swaps the editor to a blank world first.
  await call("delete_asset", { assetPath: SCRATCH, force: true });
  const untitled = await currentMap();
  expect(untitled.startsWith("/Temp/"), `expected a never-saved map, got ${untitled}`).toBe(true);
  await placeCube(LABEL);
  return untitled;
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  for (const path of [SCRATCH, SAVED, WP_SOURCE, WP_SAVED]) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await call("demo_go_home", { discardUnsaved: true, endPIE: true });
  await cleanup();
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  await cleanup();
  disconnectBridge();
}, 300_000);

describe("save_as on a never-saved Untitled map", () => {
  it("level(save) refuses it by name and points at save_as", async () => {
    const untitled = await onUntitledMapWithCube();
    const r = await callBridge(bridge, "save_level", {});
    expect(succeeded(r)).toBe(false);
    expect(body(r).reason).toBe("untitled_map");
    expect(String(body(r).error)).toContain("save_as");
    expect(await currentMap()).toBe(untitled);
  }, 300_000);

  it("refuses a destination that exists, a /Temp path and a malformed one, writing nothing", async () => {
    const untitled = await currentMap();
    expect(untitled.startsWith("/Temp/")).toBe(true);
    for (const levelPath of [HOME, "/Temp/SaveAsNope", "not a path"]) {
      const r = await callBridge(bridge, "save_level_as", { levelPath });
      expect(succeeded(r), `save_as to ${levelPath} must be refused`).toBe(false);
      expect(await currentMap()).toBe(untitled);
    }
    expect(await hasActor(LABEL)).toBe(true);
    await expectNoDialogs();
  }, 300_000);

  it("saves the map at levelPath, opens it there, and leaves nothing unsaved", async () => {
    const untitled = await currentMap();
    const saved = await call("save_level_as", { levelPath: SAVED });
    expect(saved.levelPath).toBe(SAVED);
    expect(saved.previousLevelPath).toBe(untitled);
    expect(saved.wasUntitled).toBe(true);
    expect(saved.created).toBe(true);
    expect(await currentMap()).toBe(SAVED);
    const state = await call("get_world_state");
    expect(state.worldPackageDirty).toBe(false);
    await expectNoDialogs();
  }, 300_000);

  it("wrote the placed actor: the map reopened from disk still holds it", async () => {
    await call("load_level", { levelPath: HOME });
    expect(await hasActor(LABEL)).toBe(false);
    await call("load_level", { levelPath: SAVED });
    expect(await hasActor(LABEL)).toBe(true);
  }, 300_000);

  it("refuses the open map's own path and names level(save)", async () => {
    const r = await callBridge(bridge, "save_level_as", { levelPath: SAVED });
    expect(succeeded(r)).toBe(false);
    expect(String(body(r).error)).toContain("level(save)");
  }, 300_000);
});

describe("save_as on a saved World Partition map", () => {
  it("saves a copy with its actor packages and opens the copy", async () => {
    await call("create_new_level", { levelPath: WP_SOURCE, worldPartition: true, discardUnsaved: true });
    await placeCube(WP_LABEL);
    const saved = await call("save_level_as", { levelPath: WP_SAVED });
    expect(saved.levelPath).toBe(WP_SAVED);
    expect(saved.previousLevelPath).toBe(WP_SOURCE);
    expect(saved.wasUntitled).toBe(false);
    expect(saved.reopenedFromDisk).toBe(true);
    expect(saved.worldPartition).toBe(true);
    expect(saved.externalActors).toBe(true);
    expect(await currentMap()).toBe(WP_SAVED);
    const dirty = (await call("get_world_state")).dirtyPackages;
    const ofCopy = (Array.isArray(dirty) ? dirty : []).map(String).filter((p) => p.includes("L_SaveAsWpCopy"));
    expect(ofCopy, "every package of the copy must be on disk").toEqual([]);
    await expectNoDialogs();
  }, 300_000);

  it("wrote the actor into the copy's own actor packages", async () => {
    await call("load_level", { levelPath: HOME });
    await call("load_level", { levelPath: WP_SAVED });
    // A reopened World Partition map loads no cell by itself, so the actor is
    // looked for among the copy's actor descriptors, which come from disk.
    const descs = (await call("list_actor_descs", { filter: WP_LABEL })).actorDescs;
    const rows = Array.isArray(descs) ? (descs as Json[]) : [];
    expect(rows.length, "the copy must carry the actor placed before save_as").toBe(1);
    expect(String(rows[0].package ?? "")).toContain("L_SaveAsWpCopy");
  }, 300_000);
});
