import { afterAll, beforeAll, describe, expect, it } from "vitest";
import {
  callBridge,
  checkFeature,
  disconnectBridge,
  getBridge,
  TEST_PREFIX,
} from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
let hasSmartObjects = false;
const assetName = `SOD_ArrayAppend_${process.pid}`;
const assetPath = `${TEST_PREFIX}/${assetName}`;

async function readSlots(): Promise<unknown[]> {
  const read = await callBridge(bridge, "read_asset_properties", {
    assetPath,
    propertyName: "Slots",
    includeValues: true,
    valueFormat: "json",
  });
  expect(read.ok, read.error).toBe(true);
  const value = (read.result as { value?: unknown[] })?.value;
  expect(Array.isArray(value)).toBe(true);
  return value ?? [];
}

beforeAll(async () => {
  bridge = await getBridge();
  hasSmartObjects = await checkFeature(bridge, "SmartObjects");
}, 60_000);

afterAll(async () => {
  if (hasSmartObjects) {
    await callBridge(bridge, "delete_asset", { assetPath, force: true }).catch(() => {});
  }
  disconnectBridge();
});

describe("asset - append reflected TArray elements", () => {
  it("appends native USTRUCT values atomically and emits a working rollback", async ({ skip }) => {
    if (!hasSmartObjects) skip();

    const created = await callBridge(bridge, "create_smart_object_definition", {
      name: assetName,
      packagePath: TEST_PREFIX,
      onConflict: "replace",
    });
    expect(created.ok, created.error).toBe(true);

    const append = await callBridge(bridge, "append_asset_array_elements", {
      assetPath,
      propertyName: "Slots",
      elements: [
        {
          Offset: { x: 11, y: 22, z: 33 },
          Rotation: { pitch: 4, yaw: 5, roll: 6 },
        },
        {
          Offset: { x: 44, y: 55, z: 66 },
          Rotation: { pitch: 7, yaw: 8, roll: 9 },
        },
      ],
    });
    expect(append.ok, append.error).toBe(true);
    const result = append.result as Record<string, unknown>;
    expect(result.previousNum).toBe(0);
    expect(result.appendedCount).toBe(2);
    expect(result.newNum).toBe(2);
    expect(result.appendedIndices).toEqual([0, 1]);
    expect(result.saved).toBe(false);

    const slots = await readSlots();
    expect(slots).toHaveLength(2);
    expect((slots[0] as { Offset?: unknown }).Offset).toEqual({ X: 11, Y: 22, Z: 33 });

    const invalid = await callBridge(bridge, "append_asset_array_elements", {
      assetPath,
      propertyName: "Slots",
      elements: [
        { Offset: { x: 77, y: 88, z: 99 } },
        { NotARealField: true },
      ],
    });
    expect(invalid.ok).toBe(true);
    const invalidResult = invalid.result as { success: boolean; error: string };
    expect(invalidResult.success).toBe(false);
    expect(invalidResult.error).toContain("elements[1]");
    expect(await readSlots()).toHaveLength(2);

    const rollback = result.rollback as {
      method: string;
      payload: Record<string, unknown>;
    };
    expect(rollback.method).toBe("set_asset_property");
    const rolledBack = await callBridge(bridge, rollback.method, rollback.payload);
    expect(rolledBack.ok, rolledBack.error).toBe(true);
    expect(await readSlots()).toHaveLength(0);
  });
});
