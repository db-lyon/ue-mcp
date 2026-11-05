// @live-editor headless
/**
 * query_zone_graph reads the ZoneShape actors and built ZoneGraphData of the
 * editor world: a placed shape is counted, and removing it takes it back out.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const SHAPE = "BCov_Gameplay_ZoneShape";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: SHAPE });
});

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: SHAPE });
    disconnectBridge();
  }
});

describe("ZoneGraph", () => {
  it("query_zone_graph counts a placed ZoneShape and drops it once removed", async () => {
    const before = await call("query_zone_graph", { queryMode: "summary" });
    const baseline = Number(before.zoneShapeActorCount);
    expect(before.queryMode).toBe("summary");
    expect(Number.isInteger(baseline)).toBe(true);

    await call("place_actor", { actorClass: "/Script/ZoneGraph.ZoneShape", label: SHAPE, location: { x: 0, y: -14000, z: 100 } });
    const placed = await call("query_zone_graph", { queryMode: "summary" });
    expect(Number(placed.zoneShapeActorCount)).toBe(baseline + 1);
    expect(String(placed.worldPath)).toBe(String(before.worldPath));

    await call("delete_actor", { actorLabel: SHAPE });
    const removed = await call("query_zone_graph", { queryMode: "summary" });
    expect(Number(removed.zoneShapeActorCount)).toBe(baseline);
  }, 120_000);
});
