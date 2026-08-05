import { describe, it, expect, beforeAll, afterAll, afterEach } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

const TEST_LABEL = "MCPTest_Idempotent_Cube";
const TEST_LIGHT_LABEL = "MCPTest_Idempotent_Light";

beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

// Clean up any test actors between tests so we can observe created vs existed
afterEach(async () => {
  await callBridge(bridge, "delete_actor", { actorLabel: TEST_LABEL });
  await callBridge(bridge, "delete_actor", { actorLabel: TEST_LIGHT_LABEL });
});

describe("idempotency - place_actor", () => {
  it("first call returns created:true with a rollback record", async () => {
    const r = await callBridge(bridge, "place_actor", {
      actorClass: "StaticMeshActor",
      label: TEST_LABEL,
      location: { x: 0, y: 0, z: 100 },
    });
    expect(r.ok, r.error).toBe(true);
    const res = r.result as Record<string, unknown>;
    expect(res.created).toBe(true);
    expect(res.existed).toBe(false);
    expect(res.rollback).toBeDefined();
    const rb = res.rollback as { method: string; payload: { actorLabel: string } };
    expect(rb.method).toBe("delete_actor");
    expect(rb.payload.actorLabel).toBe(TEST_LABEL);
  });

  it("second call with same label returns existed:true and no rollback", async () => {
    // First create
    const first = await callBridge(bridge, "place_actor", {
      actorClass: "StaticMeshActor",
      label: TEST_LABEL,
    });
    expect(first.ok, first.error).toBe(true);

    // Second call - should short-circuit
    const second = await callBridge(bridge, "place_actor", {
      actorClass: "StaticMeshActor",
      label: TEST_LABEL,
    });
    expect(second.ok, second.error).toBe(true);
    const res = second.result as Record<string, unknown>;
    expect(res.existed).toBe(true);
    expect(res.created).toBe(false);
    expect(res.rollback).toBeUndefined();
  });

  it("onConflict:error fails the second call", async () => {
    await callBridge(bridge, "place_actor", {
      actorClass: "StaticMeshActor",
      label: TEST_LABEL,
    });

    const second = await callBridge(bridge, "place_actor", {
      actorClass: "StaticMeshActor",
      label: TEST_LABEL,
      onConflict: "error",
    });
    // Bridge error path returns ok:true but the result has success:false
    const res = second.result as Record<string, unknown>;
    expect(res.success).toBe(false);
    expect(String(res.error)).toMatch(/already exists/);
  });
});

describe("idempotency - spawn_light", () => {
  it("first call creates with rollback, second returns existed", async () => {
    const first = await callBridge(bridge, "spawn_light", {
      lightType: "point",
      label: TEST_LIGHT_LABEL,
      location: { x: 0, y: 0, z: 200 },
      intensity: 3000,
    });
    expect(first.ok, first.error).toBe(true);
    const firstRes = first.result as Record<string, unknown>;
    expect(firstRes.created).toBe(true);
    expect(firstRes.rollback).toBeDefined();

    const second = await callBridge(bridge, "spawn_light", {
      lightType: "point",
      label: TEST_LIGHT_LABEL,
    });
    expect(second.ok, second.error).toBe(true);
    const secondRes = second.result as Record<string, unknown>;
    expect(secondRes.existed).toBe(true);
  });
});

describe("idempotency - delete_actor", () => {
  it("deleting a non-existent actor returns alreadyDeleted", async () => {
    const r = await callBridge(bridge, "delete_actor", {
      actorLabel: "NonExistent_MCPTest_Nothing",
    });
    expect(r.ok, r.error).toBe(true);
    const res = r.result as Record<string, unknown>;
    expect(res.alreadyDeleted).toBe(true);
  });
});

describe("rollback payload - set_actor_material", () => {
  it("captures previous material path for rollback", async () => {
    // Create a test actor first
    await callBridge(bridge, "place_actor", {
      actorClass: "StaticMeshActor",
      label: TEST_LABEL,
      staticMesh: "/Engine/BasicShapes/Cube.Cube",
    });

    const r = await callBridge(bridge, "set_actor_material", {
      actorLabel: TEST_LABEL,
      materialPath: "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial",
      slotIndex: 0,
    });
    expect(r.ok, r.error).toBe(true);
    const res = r.result as Record<string, unknown>;
    expect(res.updated).toBe(true);
    expect(typeof res.previousMaterialPath).toBe("string");

    if (res.rollback) {
      const rb = res.rollback as { method: string; payload: Record<string, unknown> };
      expect(rb.method).toBe("set_actor_material");
      expect(rb.payload.actorLabel).toBe(TEST_LABEL);
      expect(rb.payload.materialPath).toBe(res.previousMaterialPath);
    }
  });
});
