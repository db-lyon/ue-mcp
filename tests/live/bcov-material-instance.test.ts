// @live-editor headless
/**
 * Material instance authoring read back through read_material_instance and
 * list_material_static_switches, plus recompile cascades and a parameter
 * collection read. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Material/Instance`;
const PARENT = `${DIR}/M_BCov_Parent`;
const PARENT2 = `${DIR}/M_BCov_Parent2`;
const INSTANCE = `${DIR}/MI_BCov_Inst`;
const MPC = `${DIR}/MPC_BCov`;
const ALL_ASSETS = [INSTANCE, MPC, PARENT2, PARENT];

type Json = Record<string, unknown>;
type Override = { name: string; value: unknown };
type Switch = { name: string; value: boolean; override: boolean };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function switches(assetPath: string): Promise<Switch[]> {
  const listed = await call("list_material_static_switches", { assetPath });
  return listed.staticSwitches as Switch[];
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const assetPath of ALL_ASSETS) await callBridge(bridge, "delete_asset", { assetPath, force: true });
  await call("create_material", { name: "M_BCov_Parent", packagePath: DIR });
  await call("build_material_graph", {
    assetPath: PARENT,
    nodes: [
      { name: "Rough", class: "ScalarParameter", parameterName: "Rough", defaultValue: 0.5 },
      { name: "Tint", class: "VectorParameter", parameterName: "Tint", defaultValue: { r: 1, g: 1, b: 1, a: 1 } },
      { name: "Plain", class: "Constant3Vector", value: { r: 0.5, g: 0.5, b: 0.5 } },
      { name: "UseTint", class: "StaticSwitchParameter", parameterName: "UseTint" },
    ],
    connections: [
      { from: "Tint", to: "UseTint", toInput: 0 },
      { from: "Plain", to: "UseTint", toInput: 1 },
    ],
    propertyConnections: [
      { property: "BaseColor", from: "UseTint" },
      { property: "Roughness", from: "Rough" },
    ],
  });
  await call("create_material", { name: "M_BCov_Parent2", packagePath: DIR });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    for (const assetPath of ALL_ASSETS) await callBridge(bridge, "delete_asset", { assetPath, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("material instances", () => {
  it("create_material_instance makes an instance of the parent", async () => {
    await call("create_material_instance", { parentPath: PARENT, name: "MI_BCov_Inst", packagePath: DIR });
    const read = await call("read_material_instance", { assetPath: INSTANCE });
    expect(read.isInstance).toBe(true);
    expect(String(read.parentPath)).toContain("M_BCov_Parent");
    const chain = (read.parentChain as Array<{ path: string }>).map((l) => l.path);
    expect(chain).toHaveLength(2);
    expect(read.overrideCount).toBe(0);
  }, 120_000);

  it("batch_set_material_instances writes overrides read_material_instance reports", async () => {
    await call("batch_set_material_instances", {
      instances: [{
        assetPath: INSTANCE,
        parameters: [
          { name: "Rough", type: "scalar", value: 0.3 },
          { name: "Tint", type: "vector", value: { r: 0.2, g: 0.4, b: 0.6, a: 1 } },
        ],
      }],
    });
    const read = await call("read_material_instance", { assetPath: INSTANCE });
    const rough = (read.scalarOverrides as Override[]).find((p) => p.name === "Rough");
    expect(Number(rough?.value)).toBeCloseTo(0.3, 3);
    const tint = (read.vectorOverrides as Override[]).find((p) => p.name === "Tint");
    expect((tint?.value as { g: number }).g).toBeCloseTo(0.4, 3);
  }, 120_000);

  it("set_material_static_switch overrides a switch list_material_static_switches reports", async () => {
    const before = (await switches(INSTANCE)).find((s) => s.name === "UseTint");
    expect(before).toBeDefined();
    expect(before!.value).toBe(false);
    await call("set_material_static_switch", { assetPath: INSTANCE, parameterName: "UseTint", value: true });
    const after = (await switches(INSTANCE)).find((s) => s.name === "UseTint");
    expect(after?.value).toBe(true);
    expect(after?.override).toBe(true);
    const parent = (await switches(PARENT)).find((s) => s.name === "UseTint");
    expect(parent?.value).toBe(false);
  }, 120_000);

  it("clear_material_instance_parameters drops every override", async () => {
    const before = await call("read_material_instance", { assetPath: INSTANCE });
    expect(Number(before.overrideCount)).toBeGreaterThan(0);
    await call("clear_material_instance_parameters", { assetPath: INSTANCE });
    const after = await call("read_material_instance", { assetPath: INSTANCE });
    expect(after.overrideCount).toBe(0);
    expect(after.scalarOverrides as unknown[]).toHaveLength(0);
    expect(after.vectorOverrides as unknown[]).toHaveLength(0);
  }, 120_000);

  it("set_material_instance_parent reparents the instance", async () => {
    await call("set_material_instance_parent", { assetPath: INSTANCE, newParentPath: PARENT2 });
    const read = await call("read_material_instance", { assetPath: INSTANCE });
    expect(String(read.parentPath)).toContain("M_BCov_Parent2");
  }, 120_000);

  it("recompile_material with recompileChildren reaches the instance", async () => {
    const out = await call("recompile_material", { materialPath: PARENT2, recompileChildren: true });
    const children = out.recompiledChildren as string[];
    expect(children.some((p) => p.includes("MI_BCov_Inst"))).toBe(true);
    expect(out.childCount).toBe(children.length);
  }, 180_000);

  it("read_material_parameter_collection reads the collection's parameters", async () => {
    await call("create_asset_by_class", { name: "MPC_BCov", className: "MaterialParameterCollection", packagePath: DIR });
    await call("set_asset_property", {
      assetPath: MPC, propertyName: "ScalarParameters", value: "((ParameterName=\"BCovIntensity\",DefaultValue=2.5))",
    });
    const read = await call("read_material_parameter_collection", { assetPath: MPC });
    const scalar = (read.scalarParameters as Array<{ name: string; defaultValue: number }>).find((p) => p.name === "BCovIntensity");
    expect(scalar?.defaultValue).toBeCloseTo(2.5, 3);
    expect(read.vectorParameters as unknown[]).toHaveLength(0);
  }, 120_000);
});
