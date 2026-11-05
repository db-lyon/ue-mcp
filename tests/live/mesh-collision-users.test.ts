// @live-editor headless
/**
 * asset(set_mesh_simple_collision) and level(resave_mesh_users) on a copy of the engine cube, in a scratch level
 * the test creates and deletes. The box is read back through get_mesh_collision and its BodySetupGuid is checked
 * to change; a SplineMeshComponent using the mesh is listed by a resave_mesh_users dry run, then rebuilt and saved
 * for real, its own collision copy read back from the result. A second mesh is left dirty throughout to prove the
 * resave writes only the matched actor's package.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/MeshCollisionUsers`;
const CUBE = `${DIR}/SM_MCUCube`;
const OTHER = `${DIR}/SM_MCUOther`;
const LEVEL = `${DIR}/L_MCUScratch`;
const HOME = "/Game/MCP_Home";
const ACTOR = "MCU_SplineUser";
const RAIL = "Rail";

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
type Shape = { type: string; center: Vec; extent?: Vec; radius?: number; length?: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

/** A cube-sized box, and the wider one the tests write over it. Extents are full lengths. */
const BOX_A: Shape = { type: "box", center: { x: 0, y: 0, z: 0 }, extent: { x: 100, y: 100, z: 100 } };
const BOX_B: Shape = { type: "box", center: { x: 0, y: 0, z: 0 }, extent: { x: 100, y: 300, z: 60 } };

let bridge: EditorBridge;
let originalMap: string | undefined;

async function dispatch(method: string, params: Json, timeoutMs?: number): Promise<TestResult> {
  if (!timeoutMs) return callBridge(bridge, method, params);
  try {
    return { method, ok: true, ms: 0, result: await bridge.call(method, params, timeoutMs) };
  } catch (e) {
    return { method, ok: false, ms: 0, error: e instanceof Error ? e.message : String(e) };
  }
}

async function call(method: string, params: Json, timeoutMs?: number): Promise<Json> {
  const r = await dispatch(method, params, timeoutMs);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const collision = (assetPath: string): Promise<Json> => call("get_mesh_collision", { assetPath });
const setShapes = (params: Json): Promise<Json> => call("set_mesh_simple_collision", params, 300_000);
const resave = (params: Json): Promise<Json> => call("resave_mesh_users", { meshPaths: [CUBE], componentClass: "SplineMeshComponent", ...params }, 300_000);

async function currentMap(): Promise<string | undefined> {
  return (await call("get_current_level", {})).mapPackagePath as string | undefined;
}

async function dirtyPackages(): Promise<string[]> {
  const listed = await call("list_dirty_packages", {});
  const content = (listed.content as Json[] | undefined) ?? [];
  const maps = (listed.maps as Json[] | undefined) ?? [];
  return [...content, ...maps].map((e) => String(e.package));
}

type ComponentRow = { componentName: string; before: Json; after?: Json };
type PackageRow = Json & { actors: Array<{ actorLabel: string; components: ComponentRow[] }> };

/** The one package the spline actor lives in, and its one matched component. */
function onlyRail(result: Json): { pkg: PackageRow; rail: ComponentRow } {
  const packages = result.packages as PackageRow[];
  expect(packages).toHaveLength(1);
  const pkg = packages[0];
  expect(pkg.actors.map((a) => a.actorLabel)).toEqual([ACTOR]);
  const rail = pkg.actors[0].components[0];
  expect(rail.componentName).toBe(RAIL);
  return { pkg, rail };
}

beforeAll(async () => {
  bridge = await getBridge();
  originalMap = await currentMap();
  if (originalMap === LEVEL) await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: LEVEL, force: true });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_folder", { path: DIR });
  await call("duplicate_asset", { sourcePath: "/Engine/BasicShapes/Cube", destinationPath: CUBE });
  await call("duplicate_asset", { sourcePath: "/Engine/BasicShapes/Cube", destinationPath: OTHER });
  // A known starting point: the engine cube's own collision is whatever the engine ships.
  await setShapes({ assetPath: CUBE, shapes: [BOX_A] });

  await call("create_new_level", { levelPath: LEVEL });
  if ((await currentMap()) !== LEVEL) await call("load_level", { levelPath: LEVEL, discardUnsaved: true });
  await call("place_actor", { actorClass: "/Script/Engine.Actor", label: ACTOR, location: { x: 0, y: 0, z: 200 } });
  await call("add_component_to_actor", { actorLabel: ACTOR, componentClass: "SplineMeshComponent", componentName: RAIL });
  await call("set_component_property", { actorLabel: ACTOR, componentName: RAIL, propertyName: "StaticMesh", value: CUBE });
  await call("save_level", {});
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  const restore = originalMap && originalMap.startsWith("/Game/") && originalMap !== LEVEL ? originalMap : HOME;
  await callBridge(bridge, "load_level", { levelPath: restore, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: LEVEL, force: true });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  disconnectBridge();
}, 300_000);

describe("asset(set_mesh_simple_collision)", () => {
  it("writes a box that get_mesh_collision reads back, with a new BodySetupGuid", async () => {
    const before = await collision(CUBE);
    const dry = await setShapes({ assetPath: CUBE, shapes: [BOX_B], dryRun: true });
    expect(dry.dryRun).toBe(true);
    expect(dry.wouldChange).toBe(true);
    expect((await collision(CUBE)).bodySetupGuid).toBe(before.bodySetupGuid);

    const set = await setShapes({ assetPath: CUBE, shapes: [BOX_B] });
    expect(set.updated).toBe(true);
    expect(set.previousBodySetupGuid).toBe(before.bodySetupGuid);
    expect(set.guidChanged).toBe(true);
    expect(set.saved).toBe(true);
    expect(set.persisted).toBe(true);
    expect(set.packageDirty).toBe(false);
    expect((set.previousShapes as Shape[]).map((s) => s.extent)).toEqual([BOX_A.extent]);

    const after = await collision(CUBE);
    expect(after.bodySetupGuid).toBe(set.bodySetupGuid);
    expect(after.bodySetupGuid).not.toBe(before.bodySetupGuid);
    expect(after.numBoxElems).toBe(1);
    expect(after.numConvexElems).toBe(0);
    const box = (after.shapes as Shape[])[0];
    expect(box.type).toBe("box");
    expect(box.extent!.x).toBeCloseTo(100, 3);
    expect(box.extent!.y).toBeCloseTo(300, 3);
    expect(box.extent!.z).toBeCloseTo(60, 3);
  }, 300_000);

  it("reports the same shapes again as existed, without a new GUID", async () => {
    const guid = (await collision(CUBE)).bodySetupGuid;
    const again = await setShapes({ assetPath: CUBE, shapes: [BOX_B] });
    expect(again.existed).toBe(true);
    expect(again.changed).toBe(false);
    expect(again.rollback).toBeUndefined();
    expect((await collision(CUBE)).bodySetupGuid).toBe(guid);
  }, 300_000);

  it("rolls back to the previous shapes and trace flag", async () => {
    const set = await setShapes({ assetPath: CUBE, shapes: [BOX_A, { type: "sphere", center: { x: 0, y: 0, z: 80 }, radius: 20 }] });
    const rollback = set.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("set_mesh_simple_collision");
    await setShapes(rollback.payload);
    const after = await collision(CUBE);
    expect(after.numSphereElems).toBe(0);
    expect((after.shapes as Shape[]).map((s) => s.extent)).toEqual([BOX_B.extent]);
    expect(after.collisionTraceFlag).toBe(set.previousCollisionTraceFlag);
  }, 300_000);

  it("refuses a malformed shape before touching the mesh", async () => {
    const guid = (await collision(CUBE)).bodySetupGuid;
    const r = await dispatch("set_mesh_simple_collision", {
      assetPath: CUBE,
      shapes: [{ type: "sphere", center: { x: 0, y: 0, z: 0 }, radius: 10, extent: { x: 1, y: 1, z: 1 } }],
    });
    expect(succeeded(r)).toBe(false);
    expect(JSON.stringify(body(r).error ?? r.error)).toMatch(/extent is for boxes only/);
    expect((await collision(CUBE)).bodySetupGuid).toBe(guid);
  }, 120_000);
});

describe("level(resave_mesh_users)", () => {
  it("lists the spline user and its current collision copy on a dry run, writing nothing", async () => {
    const mesh = await collision(CUBE);
    // A second package left dirty, which the real run below must not save.
    await setShapes({ assetPath: OTHER, shapes: [BOX_B], save: false });
    expect(await dirtyPackages()).toContain(OTHER);

    const dry = await resave({});
    expect(dry.dryRun).toBe(true);
    expect(dry.unchanged).toBe(true);
    expect(dry.matchedActors).toBe(1);
    expect(dry.matchedSplineMeshComponents).toBe(1);
    expect(dry.rollbackPossible).toBe(false);
    const { pkg, rail } = onlyRail(dry);
    expect(pkg.status).toBe("wouldSave");
    expect(rail.before.collisionEnabled, "the spline mesh component has collision to copy").not.toBe("NoCollision");
    // set_mesh_simple_collision changed the GUID and recreated the component's physics state, so the copy in
    // memory already follows the mesh: the GUID it cached is the mesh's, and its bounds are the wide box's.
    expect(rail.before.cachedMeshBodySetupGuid).toBe(mesh.bodySetupGuid);
    expect(rail.before.copyMatchesMeshGuid).toBe(true);
    expect((rail.before.bodySetupBounds as { size: Vec }).size.y).toBeCloseTo(300, 0);

    const again = await resave({});
    expect(onlyRail(again).pkg.modifiedUtcBefore).toBe(pkg.modifiedUtcBefore);
  }, 300_000);

  it("rebuilds the copy from the current mesh and saves only the actor's package", async () => {
    const mesh = await collision(CUBE);
    // File times are whole seconds; step past the dry run's second so a rewrite shows.
    await new Promise((resolve) => setTimeout(resolve, 1100));

    const real = await resave({ dryRun: false });
    expect(real.dryRun).toBe(false);
    expect(real.updated).toBe(true);
    expect(real.recreated).toBe(1);
    expect(real.splineCopiesRebuilt).toBe(1);
    expect(real.savedCount).toBe(1);
    expect(real.failedCount).toBe(0);
    const { pkg, rail } = onlyRail(real);
    expect(pkg.status).toBe("saved");
    expect(pkg.fileChanged).toBe(true);
    expect(pkg.dirtyNow).toBe(false);
    expect(pkg.modifiedUtcAfter).not.toBe(pkg.modifiedUtcBefore);
    const after = rail.after!;
    expect(after.cachedMeshBodySetupGuid).toBe(mesh.bodySetupGuid);
    expect(after.copyMatchesMeshGuid).toBe(true);
    const size = (after.bodySetupBounds as { size: Vec }).size;
    expect(size.y).toBeCloseTo(300, 0);
    expect(size.z).toBeCloseTo(60, 0);

    // The other dirty package was not part of the match and was not written.
    expect(await dirtyPackages()).toContain(OTHER);
  }, 300_000);

  it("matches nothing for a class the users are not, and refuses a mesh path that resolves to nothing", async () => {
    const none = await call("resave_mesh_users", { meshPaths: [CUBE], componentClass: "StaticMeshComponent" }, 300_000);
    expect(none.matchedComponents).toBe(0);
    expect(none.packages).toEqual([]);
    expect(typeof none.zeroMatchNote).toBe("string");

    const missing = await dispatch("resave_mesh_users", { meshPaths: [`${DIR}/SM_MCUMissing`], dryRun: false });
    expect(succeeded(missing)).toBe(false);
    expect(body(missing).unresolved).toHaveLength(1);
  }, 300_000);
});
