// @live-editor headless
/**
 * get_sequence_binding_components reads the component property values of a
 * possessable's level actor and of a spawnable's object template, neither of
 * which get_component_details reaches. Refs #1640.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/SeqBindingComponents`;
const SEQ = `${DIR}/LS_BCovBindingComponents`;
const POSSESSED = "BCov_SeqBindComp_Possessed";
const SPAWN_SOURCE = "BCov_SeqBindComp_SpawnSource";

type Json = Record<string, unknown>;
type Component = { name: string; class: string; path: string; source: string; properties: Json; missingProperties?: string[] };
type Binding = { name: string; guid: string; type: string; resolved: boolean; source?: string; components: Component[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function meshComponentName(label: string): Promise<string> {
  const out = await call("get_component_tree", { actorLabel: label });
  const components = (out.components ?? []) as Json[];
  const mesh = components.find((c) => c.class === "StaticMeshComponent");
  expect(mesh, JSON.stringify(out)).toBeDefined();
  return String(mesh!.name);
}

async function cube(label: string, x: number): Promise<string> {
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label, location: { x, y: 23000, z: 100 } });
  const name = await meshComponentName(label);
  await call("set_component_property", { actorLabel: label, componentName: name, propertyName: "CastShadow", value: false });
  await call("set_component_property", { actorLabel: label, componentName: name, propertyName: "CustomDepthStencilValue", value: 7 });
  return name;
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "delete_actor", { actorLabel: POSSESSED });
  await callBridge(bridge, "delete_actor", { actorLabel: SPAWN_SOURCE });
  await callBridge(bridge, "delete_asset", { assetPath: SEQ, force: true });
}

let possessedComponent = "";
let spawnComponent = "";

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
  await call("create_level_sequence", { name: "LS_BCovBindingComponents", packagePath: DIR });
  possessedComponent = await cube(POSSESSED, 1500);
  spawnComponent = await cube(SPAWN_SOURCE, 1800);
  await call("add_sequence_track", { assetPath: SEQ, trackType: "Transform", actorLabel: POSSESSED });
  // No bridge action makes a spawnable yet, so the fixture uses the scripting library.
  const code = [
    "import unreal",
    `seq = unreal.load_asset('${SEQ}')`,
    "eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)",
    `actor = [a for a in eas.get_all_level_actors() if a.get_actor_label() == '${SPAWN_SOURCE}'][0]`,
    "unreal.MovieSceneSequenceExtensions.add_spawnable_from_instance(seq, actor)",
  ].join("\n");
  await call("execute_python", { code });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await cleanup();
  } finally {
    disconnectBridge();
  }
}, 120_000);

describe("get_sequence_binding_components", () => {
  it("reads the default rendering flags off a possessable's level actor", async () => {
    const out = await call("get_sequence_binding_components", { sequencePath: SEQ, bindingName: POSSESSED });
    expect(out.defaultProperties).toBe(true);
    const bindings = out.bindings as Binding[];
    expect(bindings).toHaveLength(1);
    expect(bindings[0].type).toBe("possessable");
    expect(bindings[0].resolved).toBe(true);
    expect(bindings[0].source).toBe("editorWorld");
    const mesh = bindings[0].components.find((c) => c.name === possessedComponent);
    expect(mesh, JSON.stringify(bindings[0])).toBeDefined();
    expect(mesh!.path).toContain("PersistentLevel");
    expect(mesh!.properties.CastShadow).toBe(false);
    expect(mesh!.properties.CustomDepthStencilValue).toBe(7);
    expect(typeof mesh!.properties.bVisibleInRayTracing).toBe("boolean");
    expect(typeof mesh!.properties.bHoldout).toBe("boolean");
    expect(typeof mesh!.properties.LightingChannels).toBe("string");
  }, 60_000);

  it("reads a spawnable's object template, with Python spelling, dotted paths and a class filter", async () => {
    const out = await call("get_sequence_binding_components", {
      sequencePath: SEQ,
      bindingName: SPAWN_SOURCE,
      componentClass: "PrimitiveComponent",
      propertyNames: ["cast_shadow", "b_holdout", "LightingChannels.bChannel0", "custom_depth_stencil_value", "NotAProperty"],
    });
    const binding = (out.bindings as Binding[])[0];
    expect(binding.type).toBe("spawnable");
    expect(binding.resolved).toBe(true);
    expect(["spawnableTemplate", "customBindingTemplate"]).toContain(binding.source);
    expect(binding.components.length).toBeGreaterThan(0);
    for (const c of binding.components) expect(c.path).not.toContain("PersistentLevel");
    const mesh = binding.components.find((c) => c.name === spawnComponent);
    expect(mesh, JSON.stringify(binding)).toBeDefined();
    expect(mesh!.properties.CastShadow).toBe(false);
    expect(mesh!.properties.CustomDepthStencilValue).toBe(7);
    expect(typeof mesh!.properties.bHoldout).toBe("boolean");
    expect(mesh!.properties["LightingChannels.bChannel0"]).toBe(true);
    expect(mesh!.missingProperties).toEqual(["NotAProperty"]);
  }, 60_000);

  it("a class filter that matches nothing returns the binding with no components", async () => {
    const out = await call("get_sequence_binding_components", { sequencePath: SEQ, componentClass: "CineCameraComponent" });
    expect(out.bindingCount).toBe(2);
    expect(out.componentCount).toBe(0);
  }, 60_000);
});
