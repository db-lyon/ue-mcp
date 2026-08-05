// Regression: #726 - create_data_asset refused non-UDataAsset classes and there
// was no generic create-asset-by-class action (e.g. UPhysicalMaterial subclasses).
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  await callBridge(bridge, "delete_asset", { assetPath: `${TEST_PREFIX}/PM_SmokeByClass` }).catch(() => {});
  disconnectBridge();
});

describe("asset - create_asset_by_class (#726)", () => {
  it("creates a non-UDataAsset class (PhysicalMaterial) as an asset", async () => {
    const r = await callBridge(bridge, "create_asset_by_class", {
      name: "PM_SmokeByClass",
      className: "/Script/Engine.PhysicalMaterial",
      packagePath: TEST_PREFIX,
      onConflict: "replace",
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).not.toBe(false);
    expect(result.className).toBe("PhysicalMaterial");
    const exists = await callBridge(bridge, "execute_python", {
      code: `import unreal\nprint('EXISTS:' + str(unreal.EditorAssetLibrary.does_asset_exist('${TEST_PREFIX}/PM_SmokeByClass')))`,
    });
    expect(JSON.stringify(exists.result)).toContain("EXISTS:True");
  });

  it("rejects actor classes with a helpful error", async () => {
    const r = await callBridge(bridge, "create_asset_by_class", {
      name: "ShouldFail",
      className: "/Script/Engine.StaticMeshActor",
      packagePath: TEST_PREFIX,
    });
    const result = r.result as Record<string, unknown> | undefined;
    const failed = !r.ok || (result != null && result.success === false);
    expect(failed).toBeTruthy();
  });
});
