// @live-editor headless
/**
 * Rootless actors (#1251) and missing level templates (#1252), against a real editor.
 *
 * A plain Actor spawns with no root, so it has no transform. add_component_to_actor now makes a scene component
 * the root, move_actor refuses a rootless actor instead of reporting a move that did not happen, and attach_actor
 * names why an attach fails. create_new_level and load_level refuse a template or level that does not exist
 * instead of reporting a map they left unopened.
 *
 * Runs only against the dedicated disposable test project, on whatever map is open.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const ROOTLESS = "MCPTest_Rootless";
const STATIC_CHILD = "MCPTest_StaticChild";

let bridge: EditorBridge;

/** A handler refusal arrives as a result with success:false; only a transport failure sets ok:false. */
const refusal = (r: { ok: boolean; error?: string; result?: unknown }): string | undefined => {
  const body = r.result as { success?: boolean; error?: string } | undefined;
  if (!r.ok) return r.error;
  return body?.success === false ? body.error ?? "" : undefined;
};

const deleteActors = async () => {
  for (const actorLabel of [STATIC_CHILD, ROOTLESS]) await callBridge(bridge, "delete_actor", { actorLabel });
};

beforeAll(async () => {
  bridge = await getBridge();
  await deleteActors();
});

afterAll(async () => {
  if (bridge) {
    await deleteActors();
    disconnectBridge();
  }
});

describe("rootless actors (#1251)", () => {
  it("refuses to move an actor with no root", async () => {
    const placed = await callBridge(bridge, "place_actor", { actorClass: "Actor", label: ROOTLESS });
    expect(placed.ok, placed.error).toBe(true);
    const moved = await callBridge(bridge, "move_actor", { actorLabel: ROOTLESS, location: { x: 0, y: 300, z: 0 } });
    expect(refusal(moved)).toContain("no root component");
  });

  it("makes an added scene component the root, after which the actor moves", async () => {
    const added = await callBridge(bridge, "add_component_to_actor", { actorLabel: ROOTLESS, componentClass: "SceneComponent", componentName: "DefaultSceneRoot" });
    expect(added.ok, added.error).toBe(true);
    expect((added.result as { becameRoot?: boolean }).becameRoot).toBe(true);
    const moved = await callBridge(bridge, "move_actor", { actorLabel: ROOTLESS, location: { x: 0, y: 300, z: 0 } });
    expect(moved.ok, moved.error).toBe(true);
    expect((moved.result as { location: { y: number } }).location.y).toBe(300);
  });

  it("names the mobility mismatch when a Static child meets a Movable parent", async () => {
    const placed = await callBridge(bridge, "place_actor", { actorClass: "StaticMeshActor", label: STATIC_CHILD, staticMesh: "/Engine/BasicShapes/Cube" });
    expect(placed.ok, placed.error).toBe(true);
    const attached = await callBridge(bridge, "attach_actor", { childLabel: STATIC_CHILD, parentLabel: ROOTLESS });
    expect(refusal(attached)).toContain("Static child");
  });
});

describe("level templates (#1252)", () => {
  it("refuses a template that does not exist and leaves the open map alone", async () => {
    const before = await callBridge(bridge, "get_current_level", {});
    const created = await callBridge(bridge, "create_new_level", { levelPath: `${TEST_PREFIX}/L_NoSuchTemplate`, templateLevel: "/Engine/Maps/Templates/NoSuchTemplate" });
    expect(refusal(created)).toContain("does not exist");
    const current = await callBridge(bridge, "get_current_level", {});
    expect((current.result as { mapPackagePath?: string }).mapPackagePath).toBe((before.result as { mapPackagePath?: string }).mapPackagePath);
  });

  it("refuses to load a level that does not exist", async () => {
    const loaded = await callBridge(bridge, "load_level", { levelPath: `${TEST_PREFIX}/L_DoesNotExist` });
    expect(refusal(loaded)).toContain("does not exist");
  });
});
