// @live-editor headless
/**
 * Material Designer (DynamicMaterial) layer stacks: create, add a layer, write a
 * layer value, remove the layer, each read back through read_material_designer.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Material/Designer`;
const DESIGNER = `${DIR}/MD_BCov`;
const LAYER = "BCovLayer";

type Json = Record<string, unknown>;
type Layer = { index: number; name: string; bEnabled?: boolean };
type Slot = { index: number; layerCount: number; layers: Layer[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function baseSlot(): Promise<Slot> {
  const read = await call("read_material_designer", { assetPath: DESIGNER, designerSlot: "BaseColor" });
  const slots = read.slots as Slot[];
  expect(slots).toHaveLength(1);
  return slots[0];
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: DESIGNER, force: true });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: DESIGNER, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("material designer", () => {
  it("create_material_designer makes a model with a Base Color slot", async () => {
    const created = await call("create_material_designer", { name: "MD_BCov", packagePath: DIR });
    expect(Number(created.slotCount)).toBeGreaterThan(0);
    const read = await call("read_material_designer", { assetPath: DESIGNER });
    expect(Number(read.slotCount)).toBe(created.slotCount);
    const props = read.materialProperties as Array<{ property: string; designerSlot?: number }>;
    expect(props.some((p) => p.property.includes("BaseColor") && p.designerSlot !== undefined)).toBe(true);
  }, 120_000);

  it("add_material_designer_layer, set_material_designer_value and remove_material_designer_layer round-trip", async () => {
    // A slot keeps its base layer, so seed one first when the slot is empty.
    if ((await baseSlot()).layerCount === 0) {
      await call("add_material_designer_layer", { assetPath: DESIGNER, materialProperty: "BaseColor" });
    }
    const start = (await baseSlot()).layerCount;
    expect(start).toBeGreaterThan(0);

    await call("add_material_designer_layer", { assetPath: DESIGNER, materialProperty: "BaseColor", layerName: LAYER });
    const added = await baseSlot();
    expect(added.layerCount).toBe(start + 1);
    const layer = added.layers.find((l) => l.name === LAYER);
    expect(layer).toBeDefined();
    expect(layer!.bEnabled).toBe(true);

    await call("set_material_designer_value", { assetPath: DESIGNER, layerName: LAYER, propertyName: "bEnabled", value: false });
    const disabled = (await baseSlot()).layers.find((l) => l.name === LAYER);
    expect(disabled?.bEnabled).toBe(false);

    await call("remove_material_designer_layer", { assetPath: DESIGNER, layerName: LAYER });
    const removed = await baseSlot();
    expect(removed.layerCount).toBe(start);
    expect(removed.layers.some((l) => l.name === LAYER)).toBe(false);
  }, 180_000);
});
