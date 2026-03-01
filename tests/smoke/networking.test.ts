import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
const bpPath = `${TEST_PREFIX}/BP_NetSmokeTest`;

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "create_blueprint", { path: bpPath, parentClass: "Actor" });
});
afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: bpPath });
  disconnectBridge();
});

describe("networking — on test blueprint", () => {
  it("get_networking_info", async () => {
    const r = await callBridge(bridge, "get_networking_info", { blueprintPath: bpPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("set_replicates", async () => {
    const r = await callBridge(bridge, "set_replicates", { blueprintPath: bpPath, replicates: true });
    expect(r.ok, r.error).toBe(true);
  });

  it("configure_net_update_frequency", async () => {
    const r = await callBridge(bridge, "configure_net_update_frequency", {
      blueprintPath: bpPath, netUpdateFrequency: 10, minNetUpdateFrequency: 2,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("set_net_dormancy", async () => {
    const r = await callBridge(bridge, "set_net_dormancy", {
      blueprintPath: bpPath, dormancy: "DORM_Awake",
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("set_always_relevant", async () => {
    const r = await callBridge(bridge, "set_always_relevant", {
      blueprintPath: bpPath, alwaysRelevant: true,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("set_net_priority", async () => {
    const r = await callBridge(bridge, "set_net_priority", {
      blueprintPath: bpPath, netPriority: 3.0,
    });
    expect(r.ok, r.error).toBe(true);
  });

  it("set_replicate_movement", async () => {
    const r = await callBridge(bridge, "set_replicate_movement", {
      blueprintPath: bpPath, replicateMovement: true,
    });
    expect(r.ok, r.error).toBe(true);
  });
});
