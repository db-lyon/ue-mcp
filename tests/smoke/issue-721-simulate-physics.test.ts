// Regression: #721 — set_simulate_physics silently no-opped because the TS schema
// named the parameter "simulate" while the handler read only "enabled".
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  await callBridge(bridge, "delete_actor", { actorLabel: "MCPTest_PhysCube" }).catch(() => {});
  disconnectBridge();
});

describe("gameplay — set_simulate_physics param (#721)", () => {
  beforeAll(async () => {
    await callBridge(bridge, "place_actor", {
      actorClass: "/Script/Engine.StaticMeshActor",
      label: "MCPTest_PhysCube",
      location: { x: 0, y: 400, z: 500 },
    });
  });

  it("honors the schema-named 'simulate' parameter", async () => {
    const r = await callBridge(bridge, "set_simulate_physics", {
      actorLabel: "MCPTest_PhysCube",
      simulate: true,
    });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as Record<string, unknown>).enabled).toBe(true);
  });

  it("still accepts the legacy 'enabled' spelling", async () => {
    const r = await callBridge(bridge, "set_simulate_physics", {
      actorLabel: "MCPTest_PhysCube",
      enabled: false,
    });
    expect(r.ok, r.error).toBe(true);
    expect((r.result as Record<string, unknown>).enabled).toBe(false);
  });

  it("errors explicitly when neither simulate nor enabled is given", async () => {
    const r = await callBridge(bridge, "set_simulate_physics", { actorLabel: "MCPTest_PhysCube" });
    const result = r.result as Record<string, unknown> | undefined;
    const failed = !r.ok || (result != null && result.success === false);
    expect(failed).toBeTruthy();
  });
});
