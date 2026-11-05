// @live-editor render
/**
 * Niagara system, emitter, renderer, module input, static switch, dynamic
 * input and user parameter actions, each read back through another action
 * (list/info reads, the sim cache, or a second write's previous value).
 * Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Niagara`;
const FOUNTAIN = "/Niagara/DefaultAssets/Templates/Emitters/Fountain";
const SYSTEM = `${DIR}/NS_BCov`;
const SPEC_SYSTEM = `${DIR}/NS_BCovSpec`;
const EMITTER_ASSET = `${DIR}/FX_BCovEmitter`;
const EMITTER = "Fountain";
const ACTOR = "BCov_Niagara_Actor";
const RANGED_FLOAT = "/Niagara/DynamicInputs/UniformRange/UniformRangedFloat";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const onEmitter = { systemPath: SYSTEM, emitterName: EMITTER };

interface Renderer { index: number; class: string; enabled: boolean }
async function renderers(): Promise<Renderer[]> {
  return (await call("list_emitter_renderers", onEmitter)).renderers as Renderer[];
}

async function emitterNames(systemPath: string): Promise<string[]> {
  const out = await call("list_emitters_in_system", { systemPath });
  return (out.emitters as Array<{ name: string }>).map((e) => e.name);
}

/** Recompile, restart the placed system and report the peak live particle count over a few frames. */
async function peakParticles(): Promise<number> {
  await call("compile_niagara_system", { systemPath: SYSTEM });
  await call("reactivate_niagara", { actorLabel: ACTOR });
  const captured = await call("capture_niagara_sim_cache", { actorLabel: ACTOR, frames: 4, advanceSimulation: true, tickDelta: 0.1 });
  const emitter = (captured.emitters as Array<{ numInstancesPerFrame: number[] }>)[0];
  return Math.max(...emitter.numInstancesPerFrame);
}

interface Override { kind: string; wiredTo?: { scriptPath?: string }; inputName?: string }
async function spawnRateOverrides(): Promise<Override[]> {
  const out = await call("list_niagara_dynamic_inputs", { ...onEmitter, stackContext: "EmitterUpdate", moduleName: "SpawnRate" });
  const mod = (out.modules as Array<{ overrides: Override[] }>)[0];
  return mod.overrides;
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: ACTOR });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_niagara_system", { name: "NS_BCov", packagePath: DIR });
  await call("add_emitter_to_system", { systemPath: SYSTEM, emitterPath: FOUNTAIN });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: ACTOR });
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
});

describe("Niagara systems and emitters", () => {
  it("create_niagara_emitter and create_niagara_system_from_spec build a system the reads list, and remove_emitter_from_system drops one", async () => {
    const emitter = await call("create_niagara_emitter", { name: "FX_BCovEmitter", packagePath: DIR });
    expect(String(emitter.path)).toContain("FX_BCovEmitter");

    const spec = await call("create_niagara_system_from_spec", {
      name: "NS_BCovSpec", packagePath: DIR, emitters: [{ path: EMITTER_ASSET }, { path: FOUNTAIN }],
    });
    expect(spec.emittersAdded).toBe(2);
    const names = await emitterNames(SPEC_SYSTEM);
    expect(names).toHaveLength(2);
    expect(names).toContain("FX_BCovEmitter");
    expect(names).toContain("Fountain");

    const info = await call("get_niagara_info", { assetPath: SPEC_SYSTEM });
    expect(info.emitterCount).toBe(2);
    expect((info.emitters as Array<{ name: string }>).map((e) => e.name)).toEqual(names);

    const removed = await call("remove_emitter_from_system", { systemPath: SPEC_SYSTEM, emitterName: "Fountain" });
    expect(removed.remainingEmitters).toBe(1);
    expect(await emitterNames(SPEC_SYSTEM)).toEqual(["FX_BCovEmitter"]);
  }, 180_000);

  it("set_emitter_property disables the emitter, which get_niagara_info and validate_niagara_system both see", async () => {
    const valid = await call("validate_niagara_system", { systemPath: SYSTEM });
    expect(valid.valid).toBe(true);
    expect((valid.emitters as Array<{ hasSpawnModule: boolean }>)[0].hasSpawnModule).toBe(true);

    const off = await call("set_emitter_property", { ...onEmitter, propertyName: "enabled", value: "false" });
    expect(off.previousEnabled).toBe(true);
    const info = await call("get_niagara_info", { assetPath: SYSTEM });
    expect((info.emitters as Array<{ enabled: boolean }>)[0].enabled).toBe(false);
    const invalid = await call("validate_niagara_system", { systemPath: SYSTEM });
    expect(invalid.valid).toBe(false);
    expect(String(invalid.reason)).toContain("emit nothing");

    await call("set_emitter_property", { ...onEmitter, propertyName: "enabled", value: "true" });
    expect((await call("validate_niagara_system", { systemPath: SYSTEM })).valid).toBe(true);
  }, 120_000);

  it("get_niagara_compiled_hlsl reports the GPU script only after set_emitter_property moves the sim target", async () => {
    const cpu = await call("get_niagara_compiled_hlsl", onEmitter);
    expect(String(cpu.note)).toContain("CPU-sim");
    expect(cpu.scriptName).toBeUndefined();

    const gpu = await call("set_emitter_property", { ...onEmitter, propertyName: "SimTarget", value: "GPUComputeSim" });
    expect(gpu.previousValue).toBe("CPUSim");
    const compiled = await call("get_niagara_compiled_hlsl", onEmitter);
    expect(compiled.scriptName).toBeTruthy();
    expect(compiled.note).toBeUndefined();

    await call("set_emitter_property", { ...onEmitter, propertyName: "SimTarget", value: "CPUSim" });
    expect(String((await call("get_niagara_compiled_hlsl", onEmitter)).note)).toContain("CPU-sim");
  }, 120_000);
});

describe("Niagara renderers", () => {
  it("add_emitter_renderer, set_renderer_property and remove_emitter_renderer show up in list_emitter_renderers", async () => {
    const before = await renderers();
    const added = await call("add_emitter_renderer", { ...onEmitter, rendererType: "ribbon" });
    const index = Number(added.rendererIndex);
    expect(index).toBe(before.length);
    const withRibbon = await renderers();
    expect(withRibbon).toHaveLength(before.length + 1);
    expect(withRibbon[index].class).toBe("NiagaraRibbonRendererProperties");
    expect(withRibbon[index].enabled).toBe(true);

    await call("set_renderer_property", { ...onEmitter, rendererIndex: index, propertyName: "bIsEnabled", value: false });
    expect((await renderers())[index].enabled).toBe(false);

    const removed = await call("remove_emitter_renderer", { ...onEmitter, rendererIndex: index });
    expect(removed.removedClass).toBe("NiagaraRibbonRendererProperties");
    const after = await renderers();
    expect(after).toHaveLength(before.length);
    expect(after.map((r) => r.class)).toEqual(before.map((r) => r.class));
  }, 120_000);
});

describe("Niagara modules and inputs", () => {
  it("list_niagara_modules and list_niagara_module_inputs find the stock SpawnRate module and its input", async () => {
    const modules = await call("list_niagara_modules", { pathFilter: "/Niagara/Modules/Emitter/SpawnRate" });
    expect((modules.modules as Array<{ path: string }>).some((m) => m.path.endsWith("SpawnRate.SpawnRate"))).toBe(true);

    const inputs = await call("list_niagara_module_inputs", { ...onEmitter, stackContext: "EmitterUpdate", moduleName: "SpawnRate" });
    const mod = (inputs.modules as Array<{ moduleName: string; inputs: Array<{ name: string; type: string }> }>)[0];
    expect(mod.moduleName).toBe("SpawnRate");
    expect(mod.inputs.some((i) => i.name === "SpawnRate")).toBe(true);
  }, 120_000);

  it("spawn_niagara_actor places a NiagaraActor and set_niagara_parameter keeps its override", async () => {
    const spawned = await call("spawn_niagara_actor", { systemPath: SYSTEM, label: ACTOR, location: { x: 0, y: 8000, z: 100 } });
    expect(spawned.actorLabel).toBe(ACTOR);
    const details = await call("get_actor_details", { actorLabel: ACTOR });
    expect(details.class).toBe("NiagaraActor");

    await call("set_niagara_parameter", { actorLabel: ACTOR, parameterName: "BCovFloat", parameterType: "float", value: 2.5 });
    const second = await call("set_niagara_parameter", { actorLabel: ACTOR, parameterName: "BCovFloat", parameterType: "float", value: 4 });
    expect(second.hadPreviousValue).toBe(true);
    expect(second.previousValue).toBe(2.5);
  }, 120_000);

  it("set_niagara_module_input on SpawnRate stops the placed system emitting", async () => {
    expect(await peakParticles()).toBeGreaterThan(0);
    const set = await call("set_niagara_module_input", { ...onEmitter, stackContext: "EmitterUpdate", moduleName: "SpawnRate", inputName: "SpawnRate", value: 0 });
    expect(set.pinsUpdated).toBe(1);
    expect(await peakParticles()).toBe(0);
  }, 240_000);

  it("set_niagara_dynamic_input wires a script into SpawnRate and remove_niagara_dynamic_input unwires it", async () => {
    const set = await call("set_niagara_dynamic_input", {
      ...onEmitter, stackContext: "EmitterUpdate", moduleName: "SpawnRate", inputName: "SpawnRate", dynamicInputScript: RANGED_FLOAT,
    });
    expect(String(set.dynamicInputScript)).toContain("UniformRangedFloat");
    const wired = await spawnRateOverrides();
    // The wired script is reported under wiredTo, beside the override kind.
    expect(wired.some((o) => o.kind === "dynamicInput" && String(o.wiredTo?.scriptPath).includes("UniformRangedFloat"))).toBe(true);

    const removed = await call("remove_niagara_dynamic_input", { ...onEmitter, stackContext: "EmitterUpdate", moduleName: "SpawnRate", inputName: "SpawnRate" });
    expect(removed.removedKind).toBe("dynamicInput");
    const after = await spawnRateOverrides();
    expect(after.some((o) => o.kind === "dynamicInput")).toBe(false);
  }, 120_000);

  it("set_niagara_static_switch flips a bool switch list_niagara_static_switches then reports", async () => {
    type Switch = { name: string; type: string; defaultValue: string; boundToPin: boolean };
    type Row = { stackContext: string; moduleName: string; staticSwitches: Switch[] };
    const listed = await call("list_niagara_static_switches", onEmitter);
    const rows = listed.modules as Row[];
    expect(rows.length).toBeGreaterThan(0);
    let target: { row: Row; sw: Switch } | undefined;
    for (const row of rows) {
      const sw = row.staticSwitches.find((s) => s.boundToPin && /bool/i.test(s.type) && /^(true|false)$/i.test(s.defaultValue));
      if (sw) { target = { row, sw }; break; }
    }
    expect(target, `no bool static switch with a pin default among ${JSON.stringify(rows)}`).toBeDefined();
    const { row, sw } = target!;
    const flipped = sw.defaultValue.toLowerCase() === "true" ? "false" : "true";
    const address = { ...onEmitter, stackContext: row.stackContext, moduleName: row.moduleName, switchName: sw.name };

    const set = await call("set_niagara_static_switch", { ...address, value: flipped });
    expect(set.previousValue).toBe(sw.defaultValue);
    const reread = await call("list_niagara_static_switches", { ...onEmitter, stackContext: row.stackContext, moduleName: row.moduleName });
    const now = (reread.modules as Row[])[0].staticSwitches.find((s) => s.name === sw.name);
    expect(now?.defaultValue).toBe(flipped);

    await call("set_niagara_static_switch", { ...address, value: sw.defaultValue });
  }, 120_000);

  it("list_niagara_system_parameters and inspect_data_interface agree on the system's user data interfaces", async () => {
    const params = await call("list_niagara_system_parameters", { systemPath: SYSTEM });
    const rows = params.parameters as Array<{ name: string; isDataInterface: boolean }>;
    expect(params.parameterCount).toBe(rows.length);
    const dis = await call("inspect_data_interface", { systemPath: SYSTEM });
    const diNames = (dis.dataInterfaces as Array<{ name: string }>).map((d) => d.name).sort();
    expect(dis.count).toBe(diNames.length);
    expect(diNames).toEqual(rows.filter((p) => p.isDataInterface).map((p) => p.name).sort());
  }, 60_000);
});

describe("Niagara HLSL module scripts", () => {
  it("create_niagara_module_from_hlsl and set_niagara_custom_hlsl write a body get_niagara_custom_hlsl reads", async () => {
    const script = `${DIR}/NM_BCovHlsl`;
    const first = "Out = In * 2;";
    const created = await call("create_niagara_module_from_hlsl", { name: "NM_BCovHlsl", packagePath: DIR, hlsl: first });
    expect(created.hlslLength).toBe(first.length);
    const read = await call("get_niagara_custom_hlsl", { scriptPath: script });
    expect((read.customHlslNodes as Array<{ hlsl: string }>)[0].hlsl).toBe(first);

    const second = "Out = In * 3;";
    const set = await call("set_niagara_custom_hlsl", { scriptPath: script, hlsl: second });
    expect(set.previousHlslLength).toBe(first.length);
    const reread = await call("get_niagara_custom_hlsl", { scriptPath: script });
    expect((reread.customHlslNodes as Array<{ hlsl: string }>)[0].hlsl).toBe(second);
  }, 120_000);

  it("create_scratch_module declares its pins in a CustomHLSL node list_niagara_modules can find", async () => {
    const created = await call("create_scratch_module", {
      name: "NM_BCovScratch", packagePath: DIR, inputs: [{ name: "BCovIn" }], outputs: [{ name: "BCovOut", type: "float" }],
    });
    expect(created.requestedInputs).toBe(1);
    const read = await call("get_niagara_custom_hlsl", { scriptPath: `${DIR}/NM_BCovScratch` });
    const hlsl = (read.customHlslNodes as Array<{ hlsl: string }>)[0].hlsl;
    expect(hlsl).toContain("float BCovIn;");
    expect(hlsl).toContain("out float BCovOut;");

    const listed = await call("list_niagara_modules", { pathFilter: DIR });
    const names = (listed.modules as Array<{ name: string }>).map((m) => m.name);
    expect(names).toContain("NM_BCovScratch");
    expect(names).toContain("NM_BCovHlsl");
  }, 120_000);
});
