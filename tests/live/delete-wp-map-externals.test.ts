// @live-editor headless
/**
 * Deleting a World Partition map deletes the packages that belong to it (#1851).
 *
 * A World Partition map keeps each actor in its own package under __ExternalActors__/<map path> and its other
 * external objects under __ExternalObjects__/<map path>, beside the content folder rather than under the map.
 * delete_asset removed the .umap and left those folders on disk, so a map recreated at the same path inherited
 * actors it never had. The files are read from the shard project's Content folder, which is the claim.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import { liveTarget } from "./harness.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOME = "/Game/MCP_Home";
const REL = "MCPTest/DeleteWpMap";
const DIR = `${TEST_PREFIX}/DeleteWpMap`;
const MAP = `${DIR}/L_DeleteWpMap`;
const SIBLING = `${DIR}/L_DeleteWpMap/L_Nested`;
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const LABELS = ["DeleteWp_A", "DeleteWp_B", "DeleteWp_C"];

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let contentDir = "";

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** Every .uasset under a folder on disk, relative to it. */
function packagesUnder(dir: string): string[] {
  if (!fs.existsSync(dir)) return [];
  const out: string[] = [];
  const walk = (d: string) => {
    for (const entry of fs.readdirSync(d, { withFileTypes: true })) {
      const full = path.join(d, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (entry.name.endsWith(".uasset")) out.push(path.relative(dir, full));
    }
  };
  walk(dir);
  return out;
}

const externalActors = (rel: string) => path.join(contentDir, "__ExternalActors__", ...rel.split("/"));
const externalObjects = (rel: string) => path.join(contentDir, "__ExternalObjects__", ...rel.split("/"));

/** A saved World Partition map with actors placed in it, and the editor back on the home level. */
async function makeMap(levelPath: string, labels: string[]): Promise<void> {
  await callBridge(bridge, "delete_asset", { assetPath: levelPath, force: true });
  const created = await call("create_new_level", { levelPath, worldPartition: true, discardUnsaved: true });
  expect(created.externalActors).toBe(true);
  for (const [i, label] of labels.entries()) {
    await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label, staticMesh: CUBE, location: { x: i * 300, y: 0, z: 100 } });
  }
  await call("save_level");
  await call("demo_go_home", { discardUnsaved: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  contentDir = path.join((await liveTarget()).projectDir, "Content");
  await call("demo_go_home", { discardUnsaved: true });
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: SIBLING, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  disconnectBridge();
}, 300_000);

describe("delete_asset on a World Partition map", () => {
  it("refuses while the map is open, naming the way forward, and touches nothing", async () => {
    await makeMap(MAP, LABELS);
    await call("load_level", { levelPath: MAP });
    const before = packagesUnder(externalActors(`${REL}/L_DeleteWpMap`));
    expect(before.length).toBeGreaterThanOrEqual(LABELS.length);

    const single = await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
    expect(body(single).success).toBe(false);
    expect(body(single).reason).toBe("map_open");
    expect(String(body(single).error)).toContain("level(load");

    const batch = body(await callBridge(bridge, "delete_asset_batch", { assetPaths: [MAP], force: true }));
    const entry = (batch.results as Json[] | undefined)?.[0] ?? (batch.paths as Json[] | undefined)?.[0];
    expect(JSON.stringify(batch)).toContain("map_open");
    expect(entry === undefined || entry.status === "refused").toBe(true);

    expect(packagesUnder(externalActors(`${REL}/L_DeleteWpMap`))).toEqual(before);
    await call("demo_go_home", { discardUnsaved: true });
    await call("delete_asset", { assetPath: MAP, force: true });
    expect(packagesUnder(externalActors(`${REL}/L_DeleteWpMap`))).toEqual([]);
  }, 300_000);

  it("removes the map's external actor and object packages from disk", async () => {
    await makeMap(MAP, LABELS);
    const before = packagesUnder(externalActors(`${REL}/L_DeleteWpMap`));
    expect(before.length, "the saved map should have one package per placed actor").toBeGreaterThanOrEqual(LABELS.length);

    const deleted = await call("delete_asset", { assetPath: MAP, force: true });
    expect(deleted.deleted).toBe(true);
    expect(Number(deleted.externalPackagesDeleted)).toBeGreaterThanOrEqual(before.length);
    expect(deleted.externalPackagesRemaining).toBeUndefined();

    expect(packagesUnder(externalActors(`${REL}/L_DeleteWpMap`))).toEqual([]);
    expect(packagesUnder(externalObjects(`${REL}/L_DeleteWpMap`))).toEqual([]);
    expect(succeeded(await callBridge(bridge, "read_asset", { assetPath: MAP }))).toBe(false);
  }, 300_000);

  it("leaves another map's packages alone when that map sits under the deleted one's path", async () => {
    // <Map>/L_Nested keeps its actors inside <Map>'s external folder, so a folder-wide delete would take them too.
    // Only the nested map exists: the outer path has no map, which is the repeat-delete case. (Creating the outer
    // map clears its external folder, and creating the nested one under an existing outer map crashes the engine.)
    await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
    await makeMap(SIBLING, ["DeleteWp_Nested"]);
    const nestedBefore = packagesUnder(externalActors(`${REL}/L_DeleteWpMap/L_Nested`));
    expect(nestedBefore.length).toBeGreaterThanOrEqual(1);

    await call("delete_asset", { assetPath: MAP, force: true });
    expect(packagesUnder(externalActors(`${REL}/L_DeleteWpMap/L_Nested`))).toEqual(nestedBefore);

    // And the nested map still opens with its actor.
    await call("load_level", { levelPath: SIBLING });
    expect(Number((await call("list_actor_descs", { filter: "DeleteWp_Nested" })).matched)).toBe(1);
    await call("demo_go_home", { discardUnsaved: true });
  }, 300_000);
});
