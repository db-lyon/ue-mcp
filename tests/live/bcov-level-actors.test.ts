// @live-editor headless
/**
 * Level actor actions read back through another action: tags, folders, mobility, materials, component
 * properties, attachment, transforms and bounds, selection and editor visibility, batch placement and
 * deletion, class queries, FBX export and spline reads. Actors are labelled BCov_Level_* in the open map
 * and deleted afterwards. Refs #1223.
 */
import * as fs from "node:fs";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const PREFIX = "BCov_Level_";
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const GRID_MATERIAL = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial";

type Json = Record<string, unknown>;
type Vec = { x: number; y: number; z: number };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function cube(label: string, location: Vec, extra: Json = {}): Promise<void> {
  await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label, staticMesh: CUBE, location, ...extra });
}

const details = (label: string) => call("get_actor_details", { actorLabel: label });

async function components(label: string): Promise<Json[]> {
  return ((await call("get_component_tree", { actorLabel: label })).components ?? []) as Json[];
}

async function meshComponent(label: string): Promise<Json> {
  const found = (await components(label)).find((c) => c.class === "StaticMeshComponent");
  expect(found, `${label} has no StaticMeshComponent`).toBeDefined();
  return found!;
}

async function componentValue(label: string, componentName: string, property: string): Promise<unknown> {
  const out = await call("get_component_details", { actorLabel: label, componentName, includeValues: true, propertyNames: [property] });
  return ((out.component as Json).values as Json)[property];
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "delete_actors", { labelPrefix: PREFIX });
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

describe("level actor tags, folders and mobility", () => {
  const A = `${PREFIX}Tagged`;

  it("add_actor_tag, remove_actor_tag, set_actor_tags and list_actor_tags agree", async () => {
    await cube(A, { x: 0, y: 20000, z: 100 });
    await call("add_actor_tag", { actorLabel: A, tag: "BCovAlpha" });
    await call("add_actor_tag", { actorLabel: A, tag: "BCovBeta" });
    expect((await call("list_actor_tags", { actorLabel: A })).tags).toEqual(["BCovAlpha", "BCovBeta"]);

    const removed = await call("remove_actor_tag", { actorLabel: A, tag: "BCovAlpha" });
    expect(removed.removed).toBe(1);
    expect((await call("list_actor_tags", { actorLabel: A })).tags).toEqual(["BCovBeta"]);

    await call("set_actor_tags", { actorLabel: A, tags: ["BCovX", "BCovY"] });
    expect((await call("list_actor_tags", { actorLabel: A })).tags).toEqual(["BCovX", "BCovY"]);
  }, 120_000);

  it("set_actor_folder_path moves the actor into a folder and back to the root", async () => {
    const moved = await call("set_actor_folder_path", { folderPath: "BCov/Level", actorLabels: [A] });
    expect(moved.changed).toBe(1);
    expect((await details(A)).folderPath).toBe("BCov/Level");
    await call("set_actor_folder_path", { folderPath: "", actorLabels: [A] });
    expect((await details(A)).folderPath).toBe("");
  }, 120_000);

  it("set_actor_mobility changes the root component mobility", async () => {
    await call("set_actor_mobility", { actorLabel: A, mobility: "movable" });
    expect((await meshComponent(A)).mobility).toBe("Movable");
    await call("set_actor_mobility", { actorLabel: A, mobility: "static" });
    expect((await meshComponent(A)).mobility).toBe("Static");
  }, 120_000);

  it("set_actor_material writes slot 0 of the mesh component", async () => {
    await call("set_actor_material", { actorLabel: A, materialPath: GRID_MATERIAL, slotIndex: 0 });
    const materials = (await meshComponent(A)).materials as string[];
    expect(materials[0]).toContain("WorldGridMaterial");
  }, 120_000);
});

describe("level component properties", () => {
  const A = `${PREFIX}Props`;
  const B1 = `${PREFIX}Bulk1`;
  const B2 = `${PREFIX}Bulk2`;

  it("set_component_property writes a value get_component_details reads back", async () => {
    await cube(A, { x: 300, y: 20000, z: 100 });
    const name = String((await meshComponent(A)).name);
    await call("set_component_property", { actorLabel: A, componentName: name, propertyName: "BoundsScale", value: 2.5 });
    const read = await call("get_component_details", { actorLabel: A, componentName: name, includeValues: true, propertyNames: ["BoundsScale"] });
    const component = read.component as Json;
    expect(component.name).toBe(name);
    expect(Number((component.values as Json).BoundsScale)).toBeCloseTo(2.5, 3);
  }, 120_000);

  it("bulk_set_component_property writes every matched actor's component", async () => {
    await cube(B1, { x: 600, y: 20000, z: 100 });
    await cube(B2, { x: 900, y: 20000, z: 100 });
    const name = String((await meshComponent(B1)).name);
    const out = await call("bulk_set_component_property", { labelPrefix: `${PREFIX}Bulk`, componentName: name, propertyName: "CastShadow", value: false });
    expect(out.matchedActors).toBe(2);
    expect(out.updatedCount).toBe(2);
    expect(out.updated).toBe(true);
    for (const label of [B1, B2]) {
      expect(String(await componentValue(label, name, "CastShadow")).toLowerCase()).toBe("false");
    }
  }, 120_000);

  it("remove_component_from_actor removes a component the tree no longer lists", async () => {
    await call("add_component_to_actor", { actorLabel: A, componentClass: "ArrowComponent", componentName: "BCovArrow" });
    expect((await components(A)).some((c) => c.name === "BCovArrow")).toBe(true);
    const removed = await call("remove_component_from_actor", { actorLabel: A, componentName: "BCovArrow" });
    expect(removed.deleted).toBe(true);
    expect((await components(A)).some((c) => c.name === "BCovArrow")).toBe(false);
  }, 120_000);
});

describe("level attachment", () => {
  const P = `${PREFIX}Parent`;
  const C = `${PREFIX}Child`;

  it("attach_component and detach_component change the child root's attach parent", async () => {
    await cube(P, { x: 0, y: 21000, z: 100 });
    await cube(C, { x: 200, y: 21000, z: 100 });
    const parentRoot = String((await meshComponent(P)).name);
    const attached = await call("attach_component", { childLabel: C, parentLabel: P });
    expect(attached.attached).toBe(true);
    expect((await meshComponent(C)).attachParent).toBe(parentRoot);

    const detached = await call("detach_component", { childLabel: C });
    expect(detached.detachmentChanged).toBe(true);
    expect((await meshComponent(C)).attachParent).toBeUndefined();
  }, 120_000);

  it("detach_actor clears an actor attachment", async () => {
    await call("attach_actor", { childLabel: C, parentLabel: P });
    expect((await details(C)).attachParent).toBe(P);
    const out = await call("detach_actor", { childLabel: C });
    expect(out.previousParentLabel).toBe(P);
    expect((await details(C)).attachParent).toBeUndefined();
  }, 120_000);
});

describe("level transforms and reads", () => {
  const AIM = `${PREFIX}Aim`;
  const REF = `${PREFIX}Ref`;
  const TGT = `${PREFIX}Target`;

  it("aim_actor_at turns the actor toward a world point", async () => {
    await cube(AIM, { x: 1000, y: 22000, z: 100 });
    await call("aim_actor_at", { actorLabel: AIM, targetPoint: { x: 1000, y: 23000, z: 100 } });
    const rotation = (await details(AIM)).rotation as { yaw: number; pitch: number };
    expect(rotation.yaw).toBeCloseTo(90, 1);
    expect(rotation.pitch).toBeCloseTo(0, 1);
  }, 120_000);

  it("get_relative_transform reports the target in the reference frame", async () => {
    await cube(REF, { x: 2000, y: 22000, z: 100 });
    await cube(TGT, { x: 2100, y: 22050, z: 100 });
    const rel = await call("get_relative_transform", { targetLabel: TGT, referenceLabel: REF });
    const loc = rel.location as Vec;
    expect(loc.x).toBeCloseTo(100, 1);
    expect(loc.y).toBeCloseTo(50, 1);
    expect(loc.z).toBeCloseTo(0, 1);
  }, 120_000);

  it("get_actor_bounds and read_actor_motion report the placed cube", async () => {
    const bounds = await call("get_actor_bounds", { actorLabel: TGT });
    expect((bounds.origin as Vec).x).toBeCloseTo(2100, 0);
    expect((bounds.extent as Vec).x).toBeCloseTo(50, 0);
    expect((bounds.extent as Vec).z).toBeCloseTo(50, 0);

    const motion = await call("read_actor_motion", { actorLabels: [TGT, REF], world: "editor" });
    const samples = motion.samples as Array<{ actorLabel: string; location: Vec }>;
    expect(samples.map((s) => s.actorLabel).sort()).toEqual([REF, TGT].sort());
    const target = samples.find((s) => s.actorLabel === TGT)!;
    expect(target.location.y).toBeCloseTo(22050, 0);
  }, 120_000);

  it("resolve_actor finds the actor by its internal name", async () => {
    const name = String((await details(TGT)).name);
    const resolved = await call("resolve_actor", { internalName: name });
    expect(resolved.actorLabel).toBe(TGT);
    expect((resolved.location as Vec).x).toBeCloseTo(2100, 0);
  }, 120_000);
});

describe("level selection and editor visibility", () => {
  const A = `${PREFIX}SelA`;
  const B = `${PREFIX}SelB`;

  it("select_actors sets the selection get_selected_actors reads", async () => {
    await cube(A, { x: 0, y: 24000, z: 100 });
    await cube(B, { x: 200, y: 24000, z: 100 });
    await call("select_actors", { actorLabels: [A, B] });
    let selected = await call("get_selected_actors");
    expect((selected.actors as Json[]).map((a) => a.label).sort()).toEqual([A, B].sort());
    await call("select_actors", { actorLabels: [A] });
    selected = await call("get_selected_actors");
    expect(selected.count).toBe(1);
    expect((selected.actors as Json[])[0].label).toBe(A);
  }, 120_000);

  it("set_editor_visibility hides and shows an actor in the editor", async () => {
    const hidden = await call("set_editor_visibility", { hidden: true, actorLabels: [B] });
    expect(hidden.changed).toBe(1);
    let listed = await call("get_world_outliner", { nameFilter: B, editorHidden: true });
    expect((listed.actors as Json[]).map((a) => a.label)).toContain(B);
    await call("set_editor_visibility", { hidden: false, actorLabels: [B] });
    listed = await call("get_world_outliner", { nameFilter: B, editorHidden: true });
    expect((listed.actors as Json[]).map((a) => a.label)).not.toContain(B);
  }, 120_000);
});

describe("level batch placement and deletion", () => {
  it("batch_translate offsets every listed actor", async () => {
    const labels = [`${PREFIX}Move1`, `${PREFIX}Move2`];
    await cube(labels[0], { x: 0, y: 25000, z: 0 });
    await cube(labels[1], { x: 200, y: 25000, z: 0 });
    const out = await call("batch_translate", { actorLabels: labels, offset: { x: 0, y: 0, z: 300 } });
    expect(out.count).toBe(2);
    for (const label of labels) expect(((await details(label)).location as Vec).z).toBeCloseTo(300, 1);
  }, 120_000);

  it("place_actors_batch spawns tagged actors and attaches the child", async () => {
    const parent = `${PREFIX}BatchParent`;
    const child = `${PREFIX}BatchChild`;
    const out = await call("place_actors_batch", {
      actors: [
        { staticMesh: CUBE, label: parent, location: { x: 0, y: 26000, z: 100 }, tags: ["BCovBatch"] },
        { staticMesh: CUBE, label: child, location: { x: 200, y: 26000, z: 100 }, parentLabel: parent },
      ],
    });
    expect(out.spawned).toBe(2);
    expect((await call("list_actor_tags", { actorLabel: parent })).tags).toContain("BCovBatch");
    expect((await details(child)).attachParent).toBe(parent);
    expect(((await details(child)).location as Vec).x).toBeCloseTo(200, 0);
  }, 120_000);

  it("spawn_grid places a grid get_actors_by_class finds, and delete_actors removes it", async () => {
    const prefix = `${PREFIX}Grid`;
    const out = await call("spawn_grid", { staticMesh: CUBE, min: { x: 0, y: 27000, z: 0 }, max: { x: 300, y: 27300, z: 0 }, countX: 2, countY: 2, labelPrefix: prefix });
    expect(out.count).toBe(4);
    const found = await call("get_actors_by_class", { className: "StaticMeshActor", labelPrefix: `${prefix}_` });
    expect(found.count).toBe(4);
    const corners = (found.actors as Array<{ location: Vec }>).map((a) => `${Math.round(a.location.x)},${Math.round(a.location.y)}`).sort();
    expect(corners).toEqual(["0,27000", "0,27300", "300,27000", "300,27300"]);

    const deleted = await call("delete_actors", { labelPrefix: `${prefix}_` });
    expect(deleted.deleted).toBe(4);
    expect((await call("get_actors_by_class", { className: "StaticMeshActor", labelPrefix: `${prefix}_` })).count).toBe(0);
  }, 120_000);
});

describe("level class queries", () => {
  it("count_actors_by_class counts the placed target points", async () => {
    const before = await call("count_actors_by_class");
    const countOf = (r: Json) => ((r.classes as Array<{ class: string; count: number }>).find((c) => c.class === "TargetPoint")?.count ?? 0);
    for (let i = 0; i < 3; i++) {
      await call("place_actor", { actorClass: "/Script/Engine.TargetPoint", label: `${PREFIX}Point${i}`, location: { x: i * 100, y: 28000, z: 0 } });
    }
    const after = await call("count_actors_by_class");
    expect(countOf(after)).toBe(countOf(before) + 3);
    expect(Number(after.totalActors)).toBe(Number(before.totalActors) + 3);
  }, 120_000);

  it("get_actors_by_component_class finds the actor carrying an added component", async () => {
    const label = `${PREFIX}Text`;
    await cube(label, { x: 0, y: 29000, z: 100 });
    await call("add_component_to_actor", { actorLabel: label, componentClass: "TextRenderComponent", componentName: "BCovText" });
    const out = await call("get_actors_by_component_class", { componentClass: "TextRenderComponent" });
    const row = (out.actors as Array<{ label: string; matchedComponents: Json[] }>).find((a) => a.label === label);
    expect(row).toBeDefined();
    expect(row!.matchedComponents.map((c) => c.name)).toContain("BCovText");
  }, 120_000);
});

describe("level export and splines", () => {
  let exported = "";

  afterAll(() => {
    for (const file of [exported, exported.replace(/\.fbx$/, ".json")]) {
      if (file && fs.existsSync(file)) fs.rmSync(file);
    }
  });

  it("export_actor_fbx writes the mesh and a metadata sidecar", async () => {
    const label = `${PREFIX}Fbx`;
    await cube(label, { x: 0, y: 30000, z: 100 });
    const out = await call("export_actor_fbx", { actorLabel: label, outputPath: "Saved/BCovLevel/BCov_Level_Fbx.fbx" });
    exported = String(out.fbx);
    expect(out.meshKind).toBe("StaticMesh");
    expect(fs.existsSync(exported)).toBe(true);
    expect(fs.statSync(exported).size).toBeGreaterThan(0);
    const meta = JSON.parse(fs.readFileSync(String(out.metadata), "utf8")) as Json;
    expect(meta.actorLabel).toBe(label);
    expect(String(meta.mesh)).toContain("/Engine/BasicShapes/Cube");
  }, 120_000);

  it("get_spline_info reads the points set_spline_points wrote", async () => {
    const label = `${PREFIX}Spline`;
    await call("place_actor", { actorClass: "/Script/Engine.Actor", label, location: { x: 0, y: 31000, z: 0 } });
    await call("add_component_to_actor", { actorLabel: label, componentClass: "SplineComponent", componentName: "BCovSpline" });
    await call("set_spline_points", {
      actorLabel: label,
      points: [
        { x: 0, y: 31000, z: 0, pointType: "Linear" },
        { x: 100, y: 31000, z: 0, pointType: "Linear" },
        { x: 200, y: 31000, z: 0, pointType: "Linear" },
      ],
    });
    const info = await call("get_spline_info", { actorLabel: label, projectPoint: { x: 150, y: 31100, z: 0 } });
    expect(info.componentName).toBe("BCovSpline");
    expect(info.splinePointCount).toBe(3);
    expect(Number(info.splineLength)).toBeCloseTo(200, 0);
    const points = info.points as Array<{ worldLocation: Vec }>;
    expect(points[2].worldLocation.x).toBeCloseTo(200, 0);
    expect(Number((info.projection as Json).distanceToSpline)).toBeCloseTo(100, 0);
  }, 120_000);
});
