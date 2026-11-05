// @live-editor headless
/**
 * Runtime virtual texture authoring: the asset, its bound volume, landscape
 * assignment and the sampler/output nodes, each read back through
 * read_runtime_virtual_texture, read_material or get_actor_details. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Material/Rvt`;
const RVT = `${DIR}/RVT_BCov`;
const M_READ = `${DIR}/M_BCov_RvtRead`;
const M_WRITE = `${DIR}/M_BCov_RvtWrite`;
const VOLUME = "BCov_Material_RvtVolume";
const LANDSCAPE = "BCov_Material_Landscape";
const ALL_ASSETS = [M_READ, M_WRITE, RVT];

type Json = Record<string, unknown>;
type Volume = { actorLabel: string; boundsAlignActorLabel?: string; transform: { location: { x: number }; scale: { x: number } } };
type Expr = { class: string; inputs?: Array<{ connectedExpressionClass?: string }> };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function landscapeDetails(): Promise<string> {
  return JSON.stringify(await call("get_actor_details", { actorLabel: LANDSCAPE, includeProperties: true }));
}

async function cleanup(): Promise<void> {
  await callBridge(bridge, "delete_actor", { actorLabel: VOLUME });
  await callBridge(bridge, "delete_actor", { actorLabel: LANDSCAPE });
  for (const assetPath of ALL_ASSETS) await callBridge(bridge, "delete_asset", { assetPath, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await cleanup();
  await call("create_landscape", {
    label: LANDSCAPE, location: { x: 20000, y: 20000, z: 0 },
    componentCountX: 1, componentCountY: 1, subsectionSizeQuads: 7, numSubsections: 1,
  });
  await call("create_material", { name: "M_BCov_RvtRead", packagePath: DIR });
  await call("create_material_simple", { name: "M_BCov_RvtWrite", packagePath: DIR, baseColor: { r: 0.2, g: 0.6, b: 0.2 } });
}, 240_000);

afterAll(async () => {
  if (bridge) {
    await cleanup();
    disconnectBridge();
  }
}, 120_000);

describe("runtime virtual textures", () => {
  let materialType = "";

  it("create_runtime_virtual_texture makes an asset read_runtime_virtual_texture describes", async () => {
    const created = await call("create_runtime_virtual_texture", { name: "RVT_BCov", packagePath: DIR });
    materialType = String(created.materialType);
    const read = await call("read_runtime_virtual_texture", { rvtPath: RVT });
    expect(read.name).toBe("RVT_BCov");
    expect(read.materialType).toBe(materialType);
    expect(read.volumeCount).toBe(0);
    expect(Number(read.tileCount)).toBeGreaterThan(0);
  }, 120_000);

  it("add_rvt_volume places a volume bound to the RVT", async () => {
    await call("add_rvt_volume", { rvtPath: RVT, actorLabel: VOLUME });
    const read = await call("read_runtime_virtual_texture", { rvtPath: RVT });
    const volumes = read.volumes as Volume[];
    expect(volumes).toHaveLength(1);
    expect(volumes[0].actorLabel).toBe(VOLUME);
  }, 120_000);

  it("set_rvt_volume_bounds fits the volume to the landscape", async () => {
    await call("set_rvt_volume_bounds", { actorLabel: VOLUME, boundsMode: "alignActor", boundsAlignActor: LANDSCAPE });
    const read = await call("read_runtime_virtual_texture", { rvtPath: RVT });
    const volume = (read.volumes as Volume[])[0];
    expect(volume.boundsAlignActorLabel).toBe(LANDSCAPE);
    expect(volume.transform.scale.x).toBeGreaterThan(10);
    expect(volume.transform.location.x).toBeGreaterThan(19000);
  }, 120_000);

  it("assign_rvt_to_landscape adds and removes the RVT on the landscape", async () => {
    expect(await landscapeDetails()).not.toContain("RVT_BCov");
    await call("assign_rvt_to_landscape", { actorLabel: LANDSCAPE, rvtPath: RVT, assignMode: "add" });
    expect(await landscapeDetails()).toContain("RVT_BCov");
    const again = await call("assign_rvt_to_landscape", { actorLabel: LANDSCAPE, rvtPath: RVT, assignMode: "add" });
    expect(again.changedProxyCount).toBe(0);
    await call("assign_rvt_to_landscape", { actorLabel: LANDSCAPE, rvtPath: RVT, assignMode: "remove" });
    expect(await landscapeDetails()).not.toContain("RVT_BCov");
  }, 120_000);

  it("add_rvt_sampler adds a sampler node the RVT read finds", async () => {
    await call("add_rvt_sampler", { materialPath: M_READ, rvtPath: RVT });
    const read = await call("read_runtime_virtual_texture", { rvtPath: RVT });
    const samplers = read.samplers as Array<{ materialPath?: string; materialTypeMatches: boolean }>;
    const ours = samplers.find((s) => (s.materialPath ?? "").includes("M_BCov_RvtRead"));
    expect(ours).toBeDefined();
    expect(ours!.materialTypeMatches).toBe(true);
    const material = await call("read_material", { assetPath: M_READ });
    expect((material.expressions as Expr[]).some((e) => e.class === "MaterialExpressionRuntimeVirtualTextureSample")).toBe(true);
  }, 120_000);

  it("add_rvt_output adds an output node fed by the material's own BaseColor", async () => {
    await call("add_rvt_output", { materialPath: M_WRITE });
    const material = await call("read_material", { assetPath: M_WRITE });
    const output = (material.expressions as Expr[]).find((e) => e.class === "MaterialExpressionRuntimeVirtualTextureOutput");
    expect(output).toBeDefined();
    expect(output!.inputs?.some((i) => i.connectedExpressionClass === "MaterialExpressionConstant3Vector")).toBe(true);
  }, 120_000);
});
