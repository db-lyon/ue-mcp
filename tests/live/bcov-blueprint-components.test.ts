// @live-editor headless
/**
 * Blueprint components and class defaults, each write read back: SCS add,
 * remove and reparent, component properties and materials, capsule size, tick
 * settings, class and CDO defaults, the construction script, and the two
 * component-template flushes.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Blueprint";
const BP = `${DIR}/BP_BCovComponents`;
const CHAR = `${DIR}/BP_BCovCharacter`;
const PARENT = `${DIR}/BP_BCovCompParent`;
const CHILD = `${DIR}/BP_BCovCompChild`;
const ALL = [CHILD, PARENT, CHAR, BP];
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const SHAPE_MATERIAL = "/Engine/BasicShapes/BasicShapeMaterial";

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (j: Json, key: string): Row[] => (Array.isArray(j[key]) ? (j[key] as Row[]) : []);

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function components(path: string): Promise<Row[]> {
  return rows(await call("read_blueprint", { assetPath: path }), "components");
}

async function componentValue(path: string, componentName: string, propertyName: string): Promise<Json> {
  return call("get_blueprint_component_property", { assetPath: path, componentName, propertyName });
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const p of ALL) await callBridge(bridge, "delete_asset", { assetPath: p, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  await call("create_blueprint", { assetPath: CHAR, parentClass: "Character" });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    for (const p of ALL) await callBridge(bridge, "delete_asset", { assetPath: p, force: true });
    disconnectBridge();
  }
});

describe("SCS components", () => {
  it("add_component, reparent_component and remove_component change the hierarchy read_blueprint reports", async () => {
    await call("add_component", { assetPath: BP, componentClass: "SceneComponent", componentName: "BCovPivot" });
    await call("add_component", { assetPath: BP, componentClass: "SceneComponent", componentName: "BCovArm" });
    const added = await call("add_component", {
      assetPath: BP, componentClass: "StaticMeshComponent", componentName: "BCovMesh", parentComponent: "BCovPivot",
    });
    expect(added.componentName).toBe("BCovMesh");
    let comps = await components(BP);
    expect(comps.find((c) => c.name === "BCovMesh")?.parent).toBe("BCovPivot");
    expect(comps.find((c) => c.name === "BCovMesh")?.class).toBe("StaticMeshComponent");

    const moved = await call("reparent_component", { assetPath: BP, componentName: "BCovMesh", newParent: "BCovArm" });
    expect(moved.previousParent).toBe("BCovPivot");
    comps = await components(BP);
    expect(comps.find((c) => c.name === "BCovMesh")?.parent).toBe("BCovArm");

    const removed = await call("remove_component", { assetPath: BP, componentName: "BCovPivot" });
    expect(removed.deleted).toBe(true);
    comps = await components(BP);
    expect(comps.map((c) => c.name)).not.toContain("BCovPivot");
    expect(comps.map((c) => c.name)).toContain("BCovMesh");
  }, 180_000);

  it("set_blueprint_component_property writes a mesh that the component readers return", async () => {
    await call("set_blueprint_component_property", { assetPath: BP, componentName: "BCovMesh", propertyName: "StaticMesh", value: CUBE });
    const single = await componentValue(BP, "BCovMesh", "StaticMesh");
    expect(String(single.value)).toContain("/Engine/BasicShapes/Cube");
    expect(single.inherited).toBe(false);

    const all = await call("read_component_properties", { assetPath: BP, componentName: "BCovMesh" });
    expect(all.componentClass).toBe("StaticMeshComponent");
    const mesh = rows(all, "properties").find((p) => p.name === "StaticMesh");
    expect(String(mesh?.value)).toContain("/Engine/BasicShapes/Cube");
  }, 120_000);

  it("set_component_override_materials stores the material list", async () => {
    const set = await call("set_component_override_materials", { assetPath: BP, componentName: "BCovMesh", materialPaths: [SHAPE_MATERIAL] });
    expect(set.materialCount).toBe(1);
    const read = await componentValue(BP, "BCovMesh", "OverrideMaterials");
    expect(String(read.value)).toContain("BasicShapeMaterial");
  }, 120_000);

  it("run_construction_script spawns the class and reports its components", async () => {
    const res = await call("run_construction_script", { assetPath: BP, location: { x: 0, y: 0, z: -5000 } });
    const comps = rows(res, "components");
    const mesh = comps.find((c) => c.name === "BCovMesh");
    expect(mesh, JSON.stringify(comps.map((c) => c.name))).toBeDefined();
    expect(mesh?.class).toBe("StaticMeshComponent");
    expect(Number(res.componentCount)).toBe(comps.length);
  }, 120_000);

  it("flush_blueprint_component_templates keeps a graph-free Blueprint's SCS intact", async () => {
    const flushed = await call("flush_blueprint_component_templates", { assetPath: BP });
    expect(flushed.recordsRemoved).toBe(0);
    expect(flushed.recordsAfter).toBe(flushed.recordsBefore);
    expect(flushed.removedTemplates).toEqual([]);
    expect((await components(BP)).map((c) => c.name)).toContain("BCovMesh");
  }, 120_000);
});

describe("inherited component overrides", () => {
  it("flush_inheritable_component_handler keeps a live override record", async () => {
    await call("create_blueprint", { assetPath: PARENT, parentClass: "Actor" });
    await call("add_component", { assetPath: PARENT, componentClass: "StaticMeshComponent", componentName: "BCovBaseMesh" });
    await call("create_blueprint", { assetPath: CHILD, parentClass: `${PARENT}.BP_BCovCompParent_C` });

    await call("set_blueprint_component_property", { assetPath: CHILD, componentName: "BCovBaseMesh", propertyName: "StaticMesh", value: CUBE });
    const before = await componentValue(CHILD, "BCovBaseMesh", "StaticMesh");
    expect(before.inherited).toBe(true);
    expect(String(before.value)).toContain("/Engine/BasicShapes/Cube");

    const flushed = await call("flush_inheritable_component_handler", { assetPath: CHILD });
    expect(flushed.hadInheritableComponentHandler).toBe(true);
    expect(Number(flushed.recordsBefore)).toBeGreaterThanOrEqual(1);
    expect(flushed.recordsAfter).toBe(flushed.recordsBefore);

    const after = await componentValue(CHILD, "BCovBaseMesh", "StaticMesh");
    expect(String(after.value)).toContain("/Engine/BasicShapes/Cube");
  }, 180_000);
});

describe("class defaults", () => {
  it("set_capsule_size resizes the Character capsule", async () => {
    const set = await call("set_capsule_size", { assetPath: CHAR, componentName: "CapsuleComponent", halfHeight: 77, radius: 31 });
    expect(set.halfHeight).toBe(77);
    expect(set.radius).toBe(31);
    const radius = await componentValue(CHAR, "CapsuleComponent", "CapsuleRadius");
    expect(Number(radius.value)).toBeCloseTo(31, 3);
    const half = await componentValue(CHAR, "CapsuleComponent", "CapsuleHalfHeight");
    expect(Number(half.value)).toBeCloseTo(77, 3);
  }, 120_000);

  it("set_class_default writes through a subobject path", async () => {
    const set = await call("set_class_default", { assetPath: CHAR, propertyName: "CharacterMovement.MaxWalkSpeed", value: 333 });
    expect(Number(set.value)).toBe(333);
    const read = await call("get_blueprint_variable_default", { assetPath: CHAR, name: "CharacterMovement.MaxWalkSpeed" });
    expect(Number(read.valueText)).toBe(333);
  }, 120_000);

  it("set_actor_tick_settings changes PrimaryActorTick on the CDO", async () => {
    const set = await call("set_actor_tick_settings", { assetPath: BP, bCanEverTick: true, bStartWithTickEnabled: false, TickInterval: 0.5 });
    expect(set.TickInterval).toBe(0.5);
    const interval = await call("get_blueprint_variable_default", { assetPath: BP, name: "PrimaryActorTick.TickInterval" });
    expect(Number(interval.valueText)).toBeCloseTo(0.5, 5);
    const startEnabled = await call("get_blueprint_variable_default", { assetPath: BP, name: "PrimaryActorTick.bStartWithTickEnabled" });
    expect(String(startEnabled.valueText)).toBe("False");
  }, 120_000);

  it("set_cdo_property and get_cdo_properties round-trip on the test Blueprint's class", async () => {
    const className = `${BP}.BP_BCovComponents_C`;
    await call("set_cdo_property", { className, propertyName: "InitialLifeSpan", value: 7 });
    const read = await call("get_cdo_properties", { className, propertyNames: ["InitialLifeSpan"] });
    const props = (read.properties ?? {}) as Record<string, string>;
    expect(Number(props.InitialLifeSpan)).toBe(7);
    expect(read.count).toBe(1);
  }, 120_000);
});
