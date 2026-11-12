// @live-editor headless
/**
 * Leaving a never-saved map that has unsaved changes (#1851).
 *
 * demo_go_home used to answer success while the editor stayed on a dirty /Temp/Untitled map, because the
 * engine's LoadLevel refused and its answer was ignored; every later test that anchored on the home level then
 * ran on the wrong map. load_level(discardUnsaved=true) failed on the same map, because the discard was left to
 * an engine call that still refused. Each case here starts on such a map: a saved scratch map is opened and then
 * deleted, which puts the editor on a blank /Temp world, and an actor placed there makes it dirty.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOME = "/Game/MCP_Home";
const DIR = `${TEST_PREFIX}/UnsavedSwitch`;
const SCRATCH = `${DIR}/L_UnsavedSwitchScratch`;
const TARGET = `${DIR}/L_UnsavedSwitchTarget`;
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const LABEL = "UnsavedSwitch_Cube";

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

/** Put the editor on a never-saved /Temp map with an unsaved change, and return that map's package name. */
async function onDirtyUntitledMap(): Promise<string> {
  await callBridge(bridge, "delete_asset", { assetPath: SCRATCH, force: true });
  await call("create_new_level", { levelPath: SCRATCH });
  if ((await currentMap()) !== SCRATCH) await call("load_level", { levelPath: SCRATCH, discardUnsaved: true });
  expect(await currentMap()).toBe(SCRATCH);
  // Deleting the open map swaps the editor to a blank world first.
  await call("delete_asset", { assetPath: SCRATCH, force: true });
  const untitled = await currentMap();
  expect(untitled.startsWith("/Temp/"), `expected a never-saved map, got ${untitled}`).toBe(true);
  await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label: LABEL, staticMesh: CUBE, location: { x: 0, y: 0, z: 100 } });
  return untitled;
}

beforeAll(async () => {
  bridge = await getBridge();
  await call("demo_go_home", { discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: TARGET, force: true });
  await call("create_new_level", { levelPath: TARGET });
  await call("demo_go_home", { discardUnsaved: true });
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: TARGET, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: SCRATCH, force: true });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  disconnectBridge();
}, 300_000);

describe("demo_go_home on a dirty never-saved map", () => {
  it("refuses without discardUnsaved, names the map, and leaves the editor on it", async () => {
    const untitled = await onDirtyUntitledMap();
    const r = await callBridge(bridge, "demo_go_home", {});
    expect(succeeded(r), "demo_go_home must not claim success while the editor stays put").toBe(false);
    const refused = body(r);
    expect(refused.dirtyPackages as string[]).toContain(untitled);
    expect(refused.openMap).toBe(untitled);
    expect(String(refused.error)).toContain("discardUnsaved=true");
    expect(await currentMap()).toBe(untitled);
  }, 300_000);

  it("reaches the home level with discardUnsaved=true and says what it dropped", async () => {
    const untitled = await currentMap();
    expect(untitled.startsWith("/Temp/")).toBe(true);
    const home = await call("demo_go_home", { discardUnsaved: true });
    expect(home.levelPath).toBe(HOME);
    expect(home.alreadyOpen).toBe(false);
    expect(home.discardedPackages as string[]).toContain(untitled);
    expect(await currentMap()).toBe(HOME);
  }, 300_000);
});

describe("load_level(discardUnsaved=true) on a dirty never-saved map", () => {
  it("refuses without the flag and names the never-saved map", async () => {
    const untitled = await onDirtyUntitledMap();
    const r = await callBridge(bridge, "load_level", { levelPath: TARGET });
    expect(succeeded(r)).toBe(false);
    expect(body(r).dirtyPackages as string[]).toContain(untitled);
    expect(await currentMap()).toBe(untitled);
  }, 300_000);

  it("discards the map itself and opens the target", async () => {
    const untitled = await currentMap();
    expect(untitled.startsWith("/Temp/")).toBe(true);
    const loaded = await call("load_level", { levelPath: TARGET, discardUnsaved: true });
    expect(loaded.discardedPackages as string[]).toContain(untitled);
    expect(await currentMap()).toBe(TARGET);
    // The placed actor belonged to the discarded map, not to the one now open.
    expect(succeeded(await callBridge(bridge, "get_actor_details", { actorLabel: LABEL }))).toBe(false);
  }, 300_000);
});

describe("after the discards", () => {
  it("leaves nothing behind for the next caller of demo_go_home", async () => {
    // A later file calls demo_go_home without the flag; it has to find a clean editor.
    const first = await call("demo_go_home", {});
    expect(first.levelPath).toBe(HOME);
    expect(first.discardedPackages).toEqual([]);
    const again = await call("demo_go_home", {});
    expect(again.alreadyOpen).toBe(true);
    expect(await currentMap()).toBe(HOME);
  }, 300_000);
});
