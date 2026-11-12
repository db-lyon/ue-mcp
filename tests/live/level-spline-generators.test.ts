// @live-editor headless
/**
 * Spline generators: a spline actor made from points, a chain of spline mesh
 * pieces along it, meshes scattered along it as instances and as actors, and
 * the regeneration rules: a rerun replaces what the last run made, rebuild
 * reuses the stored parameters, and clear removes both.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const LABEL = "SplineGen_Road";
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder";
const Y = -16000;
// A straight 2000 cm run along +X.
const POINTS = [
  { x: 0, y: Y, z: 100 },
  { x: 1000, y: Y, z: 100 },
  { x: 2000, y: Y, z: 100 },
];

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let actorPath = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function components(): Promise<Json[]> {
  const tree = await call("get_component_tree", { actorPath });
  return (tree.components as Json[]) ?? [];
}

/** Labels of the actors carrying tag, through delete_actors' preview. */
async function taggedActors(tag: string): Promise<string[]> {
  const listed = await call("delete_actors", { tag, dryRun: true });
  return ((listed.labels as string[]) ?? []).map(String);
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "delete_actors", { labelPrefix: "SplineGenScatter_", dryRun: false });
  for (let i = 0; i < 3; i++) {
    if (!succeeded(await callBridge(bridge, "delete_actor", { actorLabel: LABEL }))) break;
  }
}

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await cleanup();
    disconnectBridge();
  }
}, 120_000);

describe("create_spline_actor", () => {
  it("spawns an actor whose spline runs through the points, and a repeat of the label reports existed", async () => {
    const created = await call("create_spline_actor", { label: LABEL, splinePoints: POINTS, pointType: "Linear", tags: ["SplineGenTest"] });
    expect(created.created).toBe(true);
    expect(created.componentName).toBe("Spline");
    expect(created.pointCount).toBe(3);
    expect(Number(created.splineLength)).toBeCloseTo(2000, 0);
    expect((created.rollback as Json).method).toBe("delete_actor");
    actorPath = String(created.actorPath);

    const info = await call("get_spline_info", { actorPath, componentName: "Spline" });
    expect(info.splinePointCount).toBe(3);
    expect(Number(info.splineLength)).toBeCloseTo(2000, 0);
    expect((await call("list_actor_tags", { actorPath })).tags).toEqual(expect.arrayContaining(["SplineGenTest"]));

    const again = await call("create_spline_actor", { label: LABEL, splinePoints: POINTS });
    expect(again.existed).toBe(true);
    expect(again.actorPath).toBe(actorPath);
  }, 60_000);

  it("onConflict update rewrites the points and its rollback restores them", async () => {
    const longer = [...POINTS, { x: 3000, y: Y, z: 100 }];
    const updated = await call("create_spline_actor", { label: LABEL, splinePoints: longer, pointType: "Linear", onConflict: "update" });
    expect(updated.updated).toBe(true);
    expect(Number((await call("get_spline_info", { actorPath })).splineLength)).toBeCloseTo(3000, 0);
    await call("create_spline_actor", { ...(updated.rollback as { payload: Json }).payload, pointType: "Linear" });
    expect(Number((await call("get_spline_info", { actorPath })).splineLength)).toBeCloseTo(2000, 0);
  }, 60_000);
});

describe("build_spline_meshes", () => {
  it("fills the spline with pieces of the requested length, and a rerun replaces them", async () => {
    const built = await call("build_spline_meshes", { actorPath, staticMesh: CUBE, spacing: 400 });
    expect(built.pieceCount).toBe(5);
    expect(Number(built.pieceLength)).toBeCloseTo(400, 1);
    expect(built.created).toBe(true);
    expect((built.rollback as Json).method).toBe("clear_spline_generated");
    const pieces = (await components()).filter((c) => String(c.class).includes("SplineMeshComponent"));
    expect(pieces).toHaveLength(5);
    expect(pieces.every((c) => String(c.staticMesh ?? "").includes("Cube"))).toBe(true);

    const rerun = await call("build_spline_meshes", { actorPath, staticMesh: CUBE, spacing: 500 });
    expect(rerun.pieceCount).toBe(4);
    expect(rerun.replacedPrevious).toBe(5);
    expect((await components()).filter((c) => String(c.class).includes("SplineMeshComponent"))).toHaveLength(4);
    // The rollback reruns the previous parameters.
    const rollback = rerun.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("build_spline_meshes");
    expect(rollback.payload.spacing).toBe(400);
  }, 60_000);

  it("rebuild reuses the stored parameters with a passed one on top", async () => {
    const rebuilt = await call("build_spline_meshes", { actorPath, rebuild: true, startOffset: 1000 });
    expect((rebuilt.params as Json).staticMesh).toBe(CUBE);
    expect((rebuilt.params as Json).spacing).toBe(500);
    expect(rebuilt.pieceCount).toBe(2);
    expect((await components()).filter((c) => String(c.class).includes("SplineMeshComponent"))).toHaveLength(2);
  }, 60_000);
});

describe("scatter_along_spline", () => {
  it("places instances at the interval in one HISM per mesh, the same every time for one seed", async () => {
    const scattered = await call("scatter_along_spline", {
      actorPath, generatorName: "Posts", meshes: [CUBE, CYLINDER], interval: 250, seed: 7, offset: { x: 0, y: 300, z: 0 },
    });
    expect(scattered.placed).toBe(9);
    const perMesh = scattered.perMesh as Json[];
    expect(perMesh.reduce((n, r) => n + Number(r.count), 0)).toBe(9);
    let total = 0;
    for (const row of perMesh.filter((r) => Number(r.count) > 0)) {
      const instances = await call("get_instance_transforms", { actorPath, componentName: row.componentName });
      const list = instances.instances as Json[];
      expect(list).toHaveLength(Number(row.count));
      for (const inst of list) expect(Number((inst.location as Json).y)).toBeCloseTo(Y + 300, 0);
      total += list.length;
    }
    expect(total).toBe(9);

    const again = await call("scatter_along_spline", { actorPath, generatorName: "Posts", rebuild: true });
    expect(again.placed).toBe(9);
    expect(again.replacedPrevious).toBe(perMesh.filter((r) => Number(r.count) > 0).length);
    expect((again.perMesh as Json[]).map((r) => r.count)).toEqual(perMesh.map((r) => r.count));
  }, 60_000);

  it("count spreads placements evenly and mode actors spawns tagged StaticMeshActors", async () => {
    const scattered = await call("scatter_along_spline", {
      actorPath, generatorName: "SplineGenScatter", meshes: [CUBE], count: 3, mode: "actors", alignToSpline: false,
    });
    expect(scattered.placed).toBe(3);
    const actors = scattered.actors as string[];
    expect(actors).toHaveLength(3);
    const labels = await taggedActors(String(scattered.outputTag));
    expect(labels.sort()).toEqual(["SplineGenScatter_0", "SplineGenScatter_1", "SplineGenScatter_2"]);
    const middle = await call("get_actor_details", { actorPath: actors[1] });
    expect(Number((middle.location as Json).x)).toBeCloseTo(1000, 0);
  }, 60_000);
});

describe("clear_spline_generated", () => {
  it("removes a generator's output and stored parameters, rolls back by rerunning, and reports alreadyDeleted after", async () => {
    const cleared = await call("clear_spline_generated", { actorPath, generatorName: "SplineGenScatter" });
    expect(cleared.removedActors as string[]).toHaveLength(3);
    expect(cleared.removedParams).toBe(true);
    expect(await taggedActors(`SplineGen:${actorPath.split(".").pop()}:SplineGenScatter`)).toHaveLength(0);

    const rollback = cleared.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("scatter_along_spline");
    const replayed = await call(rollback.method, rollback.payload);
    expect(replayed.placed).toBe(3);

    await call("clear_spline_generated", { actorPath, generatorName: "SplineGenScatter" });
    const empty = await call("clear_spline_generated", { actorPath, generatorName: "SplineGenScatter" });
    expect(empty.alreadyDeleted).toBe(true);

    const meshes = await call("clear_spline_generated", { actorPath, generatorName: "SplineMeshes" });
    expect(meshes.removedComponents as string[]).toHaveLength(2);
    expect((await components()).filter((c) => String(c.class).includes("SplineMeshComponent"))).toHaveLength(0);
    const rebuild = await callBridge(bridge, "build_spline_meshes", { actorPath, rebuild: true });
    expect(succeeded(rebuild)).toBe(false);
  }, 60_000);
});
