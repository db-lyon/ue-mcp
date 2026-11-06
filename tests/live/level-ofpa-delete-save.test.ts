// @live-editor headless
/**
 * Deleting one-file-per-actor actors and saving later, with a garbage collection in between. The emptied
 * external packages used to be collected, so level(save) could not find them, reported deletedCount 0 and left
 * the files on disk, and the actors came back on reload (#1545).
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOME = "/Game/MCP_Home";
const MAP = `${TEST_PREFIX}/OfpaDeleteSave/L_OfpaDeleteSave`;
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const LABELS = ["OfpaDel_A", "OfpaDel_B"];

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let originalMap: string | undefined;

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
  expect(created.externalActors).toBe(true);
  for (const [i, label] of LABELS.entries()) {
    await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label, staticMesh: CUBE, location: { x: i * 300, y: 0, z: 100 } });
  }
  await call("save_level");
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  const back = originalMap && originalMap.startsWith("/Game/") && originalMap !== MAP ? originalMap : HOME;
  const restored = await callBridge(bridge, "load_level", { levelPath: back, discardUnsaved: true });
  if (!succeeded(restored)) await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
  disconnectBridge();
}, 300_000);

describe("level(save) after delete_actor and a garbage collection", () => {
  it("deletes the actor files and the actors stay gone after a reload", async () => {
    for (const label of LABELS) expect((await call("delete_actor", { actorLabel: label })).deleted).toBe(true);
    // The editor collects garbage on its own timer; force one so the save runs after it, as in the report.
    await call("execute_command", { command: "obj gc" });

    const saved = await call("save_level");
    expect(saved.deletedCount).toBe(LABELS.length);
    for (const row of saved.deleted as Json[]) {
      expect(String(row.package)).toContain("/__ExternalActors__/");
      expect(row.deleted).toBe(true);
    }

    await call("load_level", { levelPath: HOME });
    await call("load_level", { levelPath: MAP });
    for (const label of LABELS) expect((await call("list_actor_descs", { filter: label })).matched).toBe(0);
  }, 300_000);
});
