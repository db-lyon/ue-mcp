// @live-editor headless
/**
 * Components on the child actor of a ChildActorComponent: query_components finds them through the
 * owning actor's label and reports their active state, and select_component selects a component on
 * the actor itself or, for one on a child actor, the ChildActorComponent the editor selects instead.
 * Refs #1656
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const OWNER = "BCov_Level_ChildActorOwner";
const CAC = "CamCAC";
const CUBE = "/Engine/BasicShapes/Cube.Cube";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function cameraRows(params: Json = {}): Promise<Json[]> {
  const found = await call("query_components", {
    actorLabelContains: OWNER, componentClass: "CameraComponent", fields: ["tick"], ...params,
  });
  return rows(found.rows);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: OWNER });
  await call("place_actor", {
    actorClass: "/Script/Engine.StaticMeshActor", label: OWNER, staticMesh: CUBE, location: { x: 0, y: 31000, z: 100 },
  });
  await call("add_component_to_actor", { actorLabel: OWNER, componentClass: "ChildActorComponent", componentName: CAC });
  const owner = rows((await call("query_components", { actorLabelContains: OWNER, componentClass: "ChildActorComponent" })).rows)[0];
  await call("invoke_object_function", {
    objectPath: `${owner.actorPath}.${CAC}`, functionName: "SetChildActorClass", args: { InClass: "/Script/Engine.CameraActor" },
  });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: OWNER });
    disconnectBridge();
  }
}, 120_000);

describe("child actor components", () => {
  it("query_components reaches a child actor's components through the owner's label", async () => {
    const found = await cameraRows({ includeChildActors: true });
    expect(found).toHaveLength(1);
    const row = found[0];
    expect(row.parentActorLabel).toBe(OWNER);
    expect(row.childActorComponent).toBe(CAC);
    const tick = row.tick as Json;
    expect(typeof tick.componentActive).toBe("boolean");
    expect(tick.autoActivate).toBe(true);
  }, 120_000);

  it("select_component selects a component on the actor itself", async () => {
    const out = await call("select_component", { actorLabel: OWNER, componentName: "StaticMeshComponent0" });
    expect(out.redirected).toBe(false);
    expect(out.selectedComponentName).toBe("StaticMeshComponent0");
    const selected = await call("get_selected_actors");
    expect((selected.actors as Json[]).map((a) => a.label)).toEqual([OWNER]);
  }, 120_000);

  it("select_component on a child actor's component selects the ChildActorComponent", async () => {
    const out = await call("select_component", { actorLabel: OWNER, componentName: "CameraComponent" });
    expect(out.redirected).toBe(true);
    expect(out.requestedComponentClass).toBe("CameraComponent");
    expect(out.selectedComponentName).toBe(CAC);
    expect(out.selectedActorLabel).toBe(OWNER);
    const selected = await call("get_selected_actors");
    expect((selected.actors as Json[]).map((a) => a.label)).toEqual([OWNER]);
  }, 120_000);

  it("select_actors on a child actor selects its owner and says so", async () => {
    const child = (await cameraRows({ includeChildActors: true }))[0];
    const out = await call("select_actors", { actorPaths: [child.actorPath] });
    expect(out.selectedPaths).toEqual([child.parentActorPath]);
    expect(rows(out.redirected)[0]?.requestedPath).toBe(child.actorPath);
  }, 120_000);
});
