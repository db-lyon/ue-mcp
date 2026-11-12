// @live-editor render
/**
 * Landscape splines on a World Partition map, where each network lives on its
 * own LandscapeSplineActor rather than on the landscape: the first point makes
 * one, later points join it through ownerPath, and a segment joins them there.
 * The map is created for the file and deleted afterwards.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { TEST_PROJECT_DIR } from "../../scripts/bridge-target.mjs";

const HOME = "/Game/MCP_Home";
const MAP = `${TEST_PREFIX}/LandscapeSplinesWP/L_LandscapeSplinesWP`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let originalMap: string | undefined;
let actorPath = "";

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  originalMap = body(await callBridge(bridge, "get_current_level", {})).mapPackagePath as string | undefined;
  await call("demo_go_home");
  await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
  const created = await call("create_new_level", { levelPath: MAP, worldPartition: true });
  expect(created.worldPartition).toBe(true);
  const landscape = await call("create_landscape", {
    label: "LspWP_Landscape", location: { x: 0, y: 0, z: 0 },
    componentCountX: 2, componentCountY: 2, subsectionSizeQuads: 15, numSubsections: 1,
  });
  actorPath = String(landscape.actorPath);
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  const back = originalMap && originalMap.startsWith("/Game/") && originalMap !== MAP ? originalMap : HOME;
  const restored = await callBridge(bridge, "load_level", { levelPath: back, discardUnsaved: true });
  if (!succeeded(restored)) await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
  disconnectBridge();
  // Deleting the map leaves its one-file-per-actor packages behind on disk.
  const content = path.join(TEST_PROJECT_DIR, "Content");
  const rel = MAP.slice("/Game/".length);
  for (const kind of ["__ExternalActors__", "__ExternalObjects__"]) {
    const dir = path.join(content, kind, rel);
    fs.rmSync(dir, { recursive: true, force: true });
    for (let up = path.dirname(dir); up.length > content.length; up = path.dirname(up)) {
      try { fs.rmdirSync(up); } catch { break; }
    }
  }
}, 300_000);

describe("landscape splines on a World Partition map", () => {
  it("create_landscape_spline_point puts the first point in a new LandscapeSplineActor and ownerPath keeps the next in it", async () => {
    const first = await call("create_landscape_spline_point", { actorPath, name: "WpA", location: { x: 300, y: 1500, z: 200 } });
    expect(first.created).toBe(true);
    expect(first.createdOwner).toBe(true);
    const owner = String(first.ownerPath);
    expect(owner).not.toBe(actorPath);
    expect((first.controlPoint as Json).ownerClass).toBe("LandscapeSplineActor");
    expect((first.controlPoint as Json).landscapePath).toBe(actorPath);

    const second = await call("create_landscape_spline_point", { actorPath, ownerPath: owner, name: "WpB", location: { x: 2700, y: 1500, z: 200 } });
    expect(second.createdOwner).toBe(false);
    expect(second.ownerPath).toBe(owner);

    const joined = await call("connect_landscape_spline_points", { startPoint: "WpA", endPoint: "WpB" });
    expect(joined.created).toBe(true);
    expect((joined.segment as Json).owner).toBe(owner);

    const listed = await call("list_landscape_splines");
    const points = (listed.controlPoints as Json[]).filter((p) => p.owner === owner);
    expect(points.map((p) => p.name).sort()).toEqual(["WpA", "WpB"]);
    expect((listed.segments as Json[]).filter((s) => s.owner === owner)).toHaveLength(1);

    // A point made without ownerPath starts a network of its own.
    const third = await call("create_landscape_spline_point", { actorPath, name: "WpC", location: { x: 1500, y: 300, z: 200 } });
    expect(third.createdOwner).toBe(true);
    expect(third.ownerPath).not.toBe(owner);
    const cross = await callBridge(bridge, "connect_landscape_spline_points", { startPoint: "WpA", endPoint: "WpC" });
    expect(succeeded(cross)).toBe(false);
    expect(String(cross.error ?? JSON.stringify(cross.result))).toContain("different spline owners");
  }, 180_000);
});
