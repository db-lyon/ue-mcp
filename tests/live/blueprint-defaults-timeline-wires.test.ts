// @live-editor headless
/**
 * Three Blueprint writes checked against what the editor really holds after
 * them, not what the call reported:
 * - a class default write reaches actors spawned after it and actors placed
 *   before it (#1938),
 * - delete_node on a Timeline node removes the timeline template, and its
 *   rollback brings the timeline back with its tracks and name (#1911),
 * - a connection that makes a single-link pin give up its old wire reports
 *   that wire as broken (#1905).
 * Refs #1938 #1911 #1905
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Blueprint";
const SPAWN_BP = `${DIR}/BP_BCovSpawnDefaults`;
const TIMELINE_BP = `${DIR}/BP_BCovTimelineDelete`;
const WIRE_BP = `${DIR}/BP_BCovWireReplace`;
const ALL = [SPAWN_BP, TIMELINE_BP, WIRE_BP];
const PLACED_BEFORE = "BCovDefaultsPlacedBefore";
const PLACED_AFTER = "BCovDefaultsPlacedAfter";
const TICK_AFTER = "BCovDefaultsTickAfter";
const LABELS = [PLACED_BEFORE, PLACED_AFTER, TICK_AFTER];
const GRAPH = "EventGraph";
const KSL = "/Script/Engine.KismetSystemLibrary";

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (j: Json, key: string): Row[] => (Array.isArray(j[key]) ? (j[key] as Row[]) : []);
const guid = (s: unknown): string => String(s ?? "").replace(/[^0-9a-f]/gi, "").toLowerCase();

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function addNode(path: string, nodeClass: string, nodeParams?: Json): Promise<string> {
  const added = await call("add_node", { assetPath: path, graphName: GRAPH, nodeClass, nodeParams });
  return String(added.nodeId);
}

async function actorProperty(label: string, propertyName: string): Promise<string> {
  const details = await call("get_actor_details", { actorLabel: label, includeProperties: true, propertyName });
  return JSON.stringify(rows(details, "properties"));
}

/** The timelines a Blueprint holds, read off remove_timeline's answer for a name it does not have. */
async function timelines(path: string): Promise<string[]> {
  const res = await call("remove_timeline", { assetPath: path, timelineName: "BCovNoSuchTimeline" });
  expect(res.alreadyRemoved).toBe(true);
  return (res.timelines as string[] | undefined) ?? [];
}

async function edges(path: string, kind: string): Promise<Row[]> {
  return rows(await call("get_blueprint_connections", { assetPath: path, graphName: GRAPH, kind }), "connections");
}

const hasEdge = (list: Row[], from: string, to: string): boolean =>
  list.some((e) => guid(e.fromNodeId) === guid(from) && guid(e.toNodeId) === guid(to));

async function cleanup(): Promise<void> {
  for (const label of LABELS) await callBridge(bridge, "delete_actor", { actorLabel: label });
  for (const p of ALL) await callBridge(bridge, "delete_asset", { assetPath: p, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await cleanup();
    disconnectBridge();
  }
});

describe("class default writes reach spawned and placed actors (#1938)", () => {
  it("set_class_default on an inherited native property shows on an actor spawned afterwards and on one placed before", async () => {
    await call("create_blueprint", { assetPath: SPAWN_BP, parentClass: "Actor" });
    const actorClass = `${SPAWN_BP}.BP_BCovSpawnDefaults_C`;
    await call("place_actor", { actorClass, label: PLACED_BEFORE, location: { x: 0, y: -5200, z: 100 } });

    const set = await call("set_class_default", { assetPath: SPAWN_BP, propertyName: "Tags", value: ["BCovSpawnTag"] });
    expect(Number(set.spawnDefaultsRefreshed)).toBeGreaterThanOrEqual(1);
    expect(Number(set.instancesUpdated)).toBeGreaterThanOrEqual(1);

    // Spawned after the write with no compile in between: before the fix the
    // class's post-construction list predated the write and Tags came out empty.
    await call("place_actor", { actorClass, label: PLACED_AFTER, location: { x: 200, y: -5200, z: 100 } });
    expect(await actorProperty(PLACED_AFTER, "Tags")).toContain("BCovSpawnTag");
    expect(await actorProperty(PLACED_BEFORE, "Tags")).toContain("BCovSpawnTag");
  }, 180_000);

  it("set_actor_tick_settings reaches an actor spawned afterwards", async () => {
    await call("set_actor_tick_settings", { assetPath: SPAWN_BP, bCanEverTick: true, TickInterval: 0.25 });
    await call("place_actor", { actorClass: `${SPAWN_BP}.BP_BCovSpawnDefaults_C`, label: TICK_AFTER, location: { x: 400, y: -5200, z: 100 } });
    expect(await actorProperty(TICK_AFTER, "PrimaryActorTick")).toMatch(/TickInterval=0\.25/);
  }, 180_000);
});

describe("delete_node on a Timeline node (#1911)", () => {
  it("removes the template and its rollback restores the timeline with its tracks and name", async () => {
    await call("create_blueprint", { assetPath: TIMELINE_BP, parentClass: "Actor" });
    const name = "BCovFade";
    const added = await call("add_node", { assetPath: TIMELINE_BP, graphName: GRAPH, nodeClass: "Timeline", nodeParams: { timelineName: name } });
    const nodeId = String(added.nodeId);
    expect(added.timelineName).toBe(name);
    expect(await timelines(TIMELINE_BP)).toEqual([name]);
    await call("add_timeline_track", {
      assetPath: TIMELINE_BP, timelineName: name, trackName: "Alpha", trackType: "float",
      keyframes: [{ time: 0, value: 0 }, { time: 2, value: 1 }],
    });

    const deleted = await call("delete_node", { assetPath: TIMELINE_BP, graphName: GRAPH, nodeId });
    expect(deleted.timelineTemplateRemoved).toBe(true);
    expect(await timelines(TIMELINE_BP)).not.toContain(name);

    const rollback = deleted.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("import_nodes_t3d");
    const carried = rows(rollback.payload, "timelineTemplates");
    expect(carried).toHaveLength(1);
    expect(carried[0].timelineName).toBe(name);

    const restored = await call(rollback.method, rollback.payload);
    const row = rows(restored, "restoredTimelines")[0];
    expect(row?.timelineName).toBe(name);
    expect(row?.nameRestored).toBe(true);
    expect(row?.trackCount).toBe(1);
    expect(await timelines(TIMELINE_BP)).toEqual([name]);

    // The track came back on the restored template, not as a new empty one.
    const again = await call("add_timeline_track", {
      assetPath: TIMELINE_BP, timelineName: name, trackName: "Alpha", trackType: "float",
    });
    expect(again.alreadyExists).toBe(true);
    expect(again.existingTrackType).toBe("float");
    const compiled = await call("compile_blueprint", { assetPath: TIMELINE_BP });
    expect(compiled.compiled).toBe(true);
  }, 240_000);
});

describe("a single-link pin giving up its wire is reported (#1905)", () => {
  let eventId = "";
  let firstPrint = "";
  let secondPrint = "";

  it("connect_pins reports the exec wire the schema replaced without a break flag", async () => {
    await call("create_blueprint", { assetPath: WIRE_BP, parentClass: "Actor" });
    eventId = await addNode(WIRE_BP, "CustomEvent", { eventName: "BCovFire" });
    firstPrint = await addNode(WIRE_BP, "CallFunction", { functionName: "PrintString", targetClass: KSL });
    secondPrint = await addNode(WIRE_BP, "CallFunction", { functionName: "PrintString", targetClass: KSL });

    const first = await call("connect_pins", {
      assetPath: WIRE_BP, graphName: GRAPH, sourceNodeId: eventId, sourcePin: "then", targetNodeId: firstPrint, targetPin: "execute",
    });
    expect(first.brokenSourceLinks).toBe(0);

    const second = await call("connect_pins", {
      assetPath: WIRE_BP, graphName: GRAPH, sourceNodeId: eventId, sourcePin: "then", targetNodeId: secondPrint, targetPin: "execute",
    });
    const exec = await edges(WIRE_BP, "exec");
    expect(hasEdge(exec, eventId, firstPrint)).toBe(false);
    expect(hasEdge(exec, eventId, secondPrint)).toBe(true);
    expect(second.brokenSourceLinks).toBe(1);
    expect(second.replacedLinks).toBe(1);
    const detail = rows(second, "brokenSourceLinkDetail");
    expect(guid(detail[0]?.nodeId)).toBe(guid(firstPrint));
    expect(detail[0]?.pinName).toBe("execute");
  }, 180_000);

  it("connect_pins reports the data wire a single-link input gave up", async () => {
    const nameA = await addNode(WIRE_BP, "CallFunction", { functionName: "GetObjectName", targetClass: KSL });
    const nameB = await addNode(WIRE_BP, "CallFunction", { functionName: "GetObjectName", targetClass: KSL });
    await call("connect_pins", {
      assetPath: WIRE_BP, graphName: GRAPH, sourceNodeId: nameA, sourcePin: "ReturnValue", targetNodeId: secondPrint, targetPin: "InString",
    });
    const res = await call("connect_pins", {
      assetPath: WIRE_BP, graphName: GRAPH, sourceNodeId: nameB, sourcePin: "ReturnValue", targetNodeId: secondPrint, targetPin: "InString",
    });
    const data = await edges(WIRE_BP, "data");
    expect(hasEdge(data, nameA, secondPrint)).toBe(false);
    expect(hasEdge(data, nameB, secondPrint)).toBe(true);
    expect(res.brokenTargetLinks).toBe(1);
    expect(guid(rows(res, "brokenTargetLinkDetail")[0]?.nodeId)).toBe(guid(nameA));
  }, 180_000);

  it("connect_pins_batch lists the wire each connection replaced", async () => {
    const res = await call("connect_pins_batch", {
      assetPath: WIRE_BP, graphName: GRAPH,
      connections: [{ sourceNode: eventId, sourcePin: "then", targetNode: firstPrint, targetPin: "execute" }],
    });
    expect(res.connected).toBe(1);
    expect(res.brokenLinks).toBe(1);
    const broken = rows(rows(res, "results")[0] ?? {}, "brokenLinks");
    expect(guid(broken[0]?.nodeId)).toBe(guid(secondPrint));
    const exec = await edges(WIRE_BP, "exec");
    expect(hasEdge(exec, eventId, secondPrint)).toBe(false);
    expect(hasEdge(exec, eventId, firstPrint)).toBe(true);
  }, 180_000);
});
