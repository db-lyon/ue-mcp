import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("level — read", () => {
  it("get_world_outliner", async () => {
    const r = await callBridge(bridge, "get_world_outliner");
    expect(r.ok, r.error).toBe(true);
    expect(r.result).toBeDefined();
  });

  it("get_world_outliner with classFilter", async () => {
    const r = await callBridge(bridge, "get_world_outliner", { classFilter: "StaticMeshActor" });
    expect(r.ok, r.error).toBe(true);
  });

  it("get_world_outliner with nameFilter", async () => {
    const r = await callBridge(bridge, "get_world_outliner", { nameFilter: "Light" });
    expect(r.ok, r.error).toBe(true);
  });

  it("get_current_level", async () => {
    const r = await callBridge(bridge, "get_current_level");
    expect(r.ok, r.error).toBe(true);
  });

  it("list_levels", async () => {
    const r = await callBridge(bridge, "list_levels");
    expect(r.ok, r.error).toBe(true);
  });

  it("get_selected_actors", async () => {
    const r = await callBridge(bridge, "get_selected_actors");
    expect(r.ok, r.error).toBe(true);
  });

  it("list_volumes", async () => {
    const r = await callBridge(bridge, "list_volumes");
    expect(r.ok, r.error).toBe(true);
  });

  it("list_volumes with type filter", async () => {
    const r = await callBridge(bridge, "list_volumes", { volumeType: "BlockingVolume" });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("level — actor details (dynamic)", () => {
  let firstActor: string | undefined;

  beforeAll(async () => {
    const r = await callBridge(bridge, "get_world_outliner");
    if (r.ok) {
      const res = r.result as any;
      const actors = res?.actors ?? res?.outliner ?? res;
      if (Array.isArray(actors) && actors.length > 0) {
        firstActor = actors[0]?.label ?? actors[0]?.name ?? actors[0]?.actorLabel;
      }
    }
  });

  it("get_actor_details", async ({ skip }) => {
    if (!firstActor) skip();
    const r = await callBridge(bridge, "get_actor_details", { actorLabel: firstActor });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("level — write (with cleanup)", () => {
  const placed: string[] = [];

  afterAll(async () => {
    for (const label of placed) {
      await callBridge(bridge, "delete_actor", { actorLabel: label });
    }
  });

  it("place_actor (Cube)", async () => {
    const r = await callBridge(bridge, "place_actor", {
      actorClass: "/Script/Engine.StaticMeshActor",
      label: "MCPTest_Cube",
      location: { x: 0, y: 0, z: 500 },
    });
    expect(r.ok, r.error).toBe(true);
    placed.push("MCPTest_Cube");
  });

  it("move_actor", async () => {
    const r = await callBridge(bridge, "move_actor", {
      actorLabel: "MCPTest_Cube",
      location: { x: 100, y: 100, z: 500 },
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("select_actors", async () => {
    const r = await callBridge(bridge, "select_actors", { actorLabels: ["MCPTest_Cube"] });
    expect(r.ok, r.error).toBe(true);
  });

  it("spawn_light (point)", async () => {
    const r = await callBridge(bridge, "spawn_light", {
      lightType: "point",
      location: { x: 200, y: 0, z: 500 },
      intensity: 5000,
      label: "MCPTest_Light",
    });
    expect(r.ok, r.error).toBe(true);
    placed.push("MCPTest_Light");
  });

  it("set_light_properties", async () => {
    const r = await callBridge(bridge, "set_light_properties", {
      actorLabel: "MCPTest_Light",
      intensity: 8000,
      color: { r: 255, g: 128, b: 0 },
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("spawn_volume (BlockingVolume)", async () => {
    const r = await callBridge(bridge, "spawn_volume", {
      volumeType: "BlockingVolume",
      location: { x: 300, y: 0, z: 500 },
      extent: { x: 100, y: 100, z: 100 },
      label: "MCPTest_Volume",
    });
    expect(r.ok, r.error).toBe(true);
    placed.push("MCPTest_Volume");
  });

  it("add_component_to_actor", async () => {
    const r = await callBridge(bridge, "add_component_to_actor", {
      actorLabel: "MCPTest_Cube",
      componentClass: "PointLightComponent",
      componentName: "TestLight",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("delete_actor", async () => {
    const r = await callBridge(bridge, "place_actor", {
      actorClass: "/Script/Engine.StaticMeshActor",
      label: "MCPTest_DeleteMe",
      location: { x: 0, y: 0, z: 999 },
    });
    expect(r.ok, r.error).toBe(true);
    const d = await callBridge(bridge, "delete_actor", { actorLabel: "MCPTest_DeleteMe" });
    expect(d.ok, d.error).toBe(true);
  });
});
