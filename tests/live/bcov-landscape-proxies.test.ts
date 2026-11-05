// @live-editor render
/**
 * World Partition landscape proxies: proxy lookup by position and the
 * physical-material collision refresh, on a level copied from the engine's
 * OpenWorld template and deleted afterwards. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Landscape`;
const MAP = `${DIR}/L_BCovLandscapeProxies`;
const TEMPLATE = "/Engine/Maps/Templates/OpenWorld";
const HOME = "/Game/MCP_Home";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

interface Proxy { label: string; minX: number; maxX: number; minY: number; maxY: number; cx: number; cy: number }

let bridge: EditorBridge;
let originalMap: string | undefined;
let proxies: Proxy[] = [];

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function readProxies(): Promise<Proxy[]> {
  const listed = await call("list_landscape_proxies", { limit: 2000 });
  return (listed.proxies as Json[]).map((p) => {
    const bounds = p.worldBounds as { origin: { x: number; y: number }; extent: { x: number; y: number } };
    return {
      label: String(p.label),
      minX: bounds.origin.x - bounds.extent.x, maxX: bounds.origin.x + bounds.extent.x,
      minY: bounds.origin.y - bounds.extent.y, maxY: bounds.origin.y + bounds.extent.y,
      cx: bounds.origin.x, cy: bounds.origin.y,
    };
  });
}

beforeAll(async () => {
  bridge = await getBridge();
  const level = body(await callBridge(bridge, "get_current_level", {}));
  originalMap = level.mapPackagePath as string | undefined;
  await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
  // Copying the template saves every external actor package, which outlives the default budget.
  const created = await bridge.call("create_new_level", { levelPath: MAP, templateLevel: TEMPLATE }, 600_000) as Json;
  expect(created.success, JSON.stringify(created)).not.toBe(false);

  proxies = await readProxies();
  if (proxies.length === 0) {
    await bridge.call("load_actor_descs", { className: "LandscapeStreamingProxy", maxActors: 1024 }, 300_000);
    proxies = await readProxies();
  }
  // The template has one parent landscape; bring its edit layers up to date before the refresh preflight.
  await callBridge(bridge, "merge_landscape_edit_layers", {});
}, 900_000);

afterAll(async () => {
  if (!bridge) return;
  const target = originalMap && originalMap.startsWith("/Game/") && originalMap !== MAP ? originalMap : HOME;
  const back = await callBridge(bridge, "load_level", { levelPath: target, discardUnsaved: true });
  if (!succeeded(back)) await callBridge(bridge, "demo_go_home", {});
  await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
  disconnectBridge();
}, 600_000);

describe("World Partition landscape proxies", () => {
  it("find_landscape_proxy_at names a loaded proxy covering the point, and nothing far outside", async () => {
    expect(proxies.length, "the OpenWorld copy loaded no LandscapeStreamingProxy").toBeGreaterThan(0);
    const target = proxies[0];
    const found = await call("find_landscape_proxy_at", { worldX: target.cx, worldY: target.cy });
    expect(found.found).toBe(true);
    expect(found.loaded).toBe(true);
    const covering = proxies
      .filter((p) => target.cx >= p.minX && target.cx <= p.maxX && target.cy >= p.minY && target.cy <= p.maxY)
      .map((p) => p.label);
    expect(covering).toContain(found.label);

    const missing = await call("find_landscape_proxy_at", { worldX: 1e8, worldY: 1e8 });
    expect(missing.found).toBe(false);
  }, 120_000);

  it("refresh_landscape_physical_material_collision rebuilds one proxy in memory and verifies its heights", async () => {
    const label = proxies[0].label;
    const refreshed = await call("refresh_landscape_physical_material_collision", { actorLabels: [label], maxActors: 1 });
    expect(refreshed.matched).toBe(1);
    expect(refreshed.refreshed).toBe(1);
    expect(refreshed.batchAccepted).toBe(true);
    expect(refreshed.saved).toBe(0);
    expect(refreshed.outdatedAfter).toBe(0);
    const actor = (refreshed.actors as Json[])[0];
    expect(actor.label).toBe(label);
    expect(actor.physicalMaterialCurrent).toBe(true);
    expect(actor.heightCollisionVerified).toBe(true);
    expect(Number(refreshed.heightCollisionComponentsVerified)).toBeGreaterThan(0);
  }, 660_000);
});
