/**
 * A native plugin handler registered with a C++ contract (#1282) is surfaced
 * from its recorded spec and every call is held to it before anything is sent.
 */
import { describe, expect, it } from "vitest";
import type { TaskConstructor } from "@db-lyon/flowkit";
import type { z } from "zod";
import { nativeHandlerSurface } from "../../src/extensions/loader.js";
import { PluginManifestSchema } from "../../src/extensions/manifest.js";
import { buildProvidedTool } from "../../src/extensions/provision.js";
import { mergeInjectionsIntoTool } from "../../src/extensions/injection.js";
import { categoryTool, bp } from "../../src/surface/category-tool.js";
import { actionSchema } from "../../src/surface/action-schema.js";
import { actionSignature } from "../../src/surface/action-signature.js";
import { contractViolation, specProblems, type HandlerSpecs } from "../../src/surface/handler-spec.js";
import { validateCategoryParams } from "../../src/surface/context/call-envelope.js";
import { deployedPlugin, recordedPluginSpecs } from "../../src/tools/project/bridge-parity.js";
import type { BridgeCapabilities } from "../../src/bridge/bridge.js";
import type { FlowContext } from "../../src/flow/context.js";
import type { IBridge } from "../../src/bridge/bridge.js";

const SPECS: HandlerSpecs = {
  stamp_set: {
    params: [
      { name: "actorPath", type: "string", required: true, description: "The stamp actor", aliases: ["path"] },
      { name: "blendMode", type: "string", required: false, description: "Height blend", enum: ["Max", "Min", "Override"] },
      { name: "priority", type: "integer", required: false, description: "Order", minimum: 0 },
      { name: "quality", type: "object", required: false, description: "Quality", fields: [
        { name: "lod", type: "integer", required: true, description: "LOD", minimum: 0, maximum: 7 },
      ] },
    ],
  },
  volume_set: {
    params: [
      { name: "actorPath", type: "string", required: true, description: "The stamp actor" },
      { name: "blendMode", type: "string", required: false, description: "Volume blend", enum: ["Additive", "Subtractive"] },
    ],
  },
};

function manifest(category: string, handlers: Record<string, unknown> = { stamp_set: { description: "Set a stamp." }, volume_set: {} }) {
  return PluginManifestSchema.parse({
    actionPrefix: "vox",
    nativeModule: {
      uePluginName: "VoxelTools", minBridgeApi: 2, source: "ue", category, specs: "handler-specs.json", handlers,
    },
  });
}

function provided() {
  const surface = nativeHandlerSurface(manifest("voxel"), "voxel-tools", new Set(), SPECS);
  if (surface?.kind !== "provide") throw new Error("expected a provided category");
  return { surface, tool: buildProvidedTool(surface.plan) };
}

function run(ctor: TaskConstructor, options: Record<string, unknown>) {
  const calls: Array<{ method: string; params: Record<string, unknown> }> = [];
  const ctx = {
    project: {},
    bridge: { call: async (method: string, params: Record<string, unknown>) => { calls.push({ method, params }); return { success: true }; } } as unknown as IBridge,
  } as FlowContext;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const task = new (ctor as any)(ctx, options);
  return { calls, done: task.execute() as Promise<unknown> };
}

describe("a native handler with a recorded contract", () => {
  it("carries its spec and strict keys, with the summary alone as its description", () => {
    const { tool } = provided();
    const action = tool.actions.stamp_set;
    expect(action.paramSpec).toEqual(SPECS.stamp_set.params);
    expect(action.strictParams).toBe(true);
    expect(action.recordedContract).toEqual({ method: "stamp_set", spec: SPECS.stamp_set });
    expect(action.description).toBe("Set a stamp.");
    expect(action.paramSpec?.map((p) => `${p.name}${p.required ? "" : "?"}`)).toEqual(
      ["actorPath", "blendMode?", "priority?", "quality?"],
    );
    expect(action.paramSpec?.[0].aliases).toEqual(["path"]);
  });

  it("advertises a key two handlers declare differently as either, and describes each action by its own", () => {
    const { tool } = provided();
    const blend = tool.schema.blendMode as z.ZodTypeAny;
    expect(blend.safeParse("Max").success).toBe(true);
    expect(blend.safeParse("Additive").success).toBe(true);
    expect(blend.safeParse("Nope").success).toBe(false);
    expect(actionSchema(tool, "stamp_set").params.find((p) => p.name === "blendMode")?.enumValues).toEqual(["Max", "Min", "Override"]);
    expect(actionSchema(tool, "volume_set").params.find((p) => p.name === "blendMode")?.enumValues).toEqual(["Additive", "Subtractive"]);
    const quality = actionSchema(tool, "stamp_set").params.find((p) => p.name === "quality");
    expect(quality?.properties?.lod).toMatchObject({ type: "integer", required: true, minimum: 0, maximum: 7 });
  });

  it("writes enums and ranges into its signature", () => {
    const { tool } = provided();
    const signature = actionSignature(tool, "stamp_set");
    expect(signature).toContain("blendMode?:{Max,Min,Override}");
    expect(signature).toContain("priority?:i(0..)");
  });

  it("sends a call that keeps the contract, untouched", async () => {
    const { surface } = provided();
    const ctor = surface.taskRegistrations.find((r) => r.name === "voxel.stamp_set")!.ctor;
    const { calls, done } = run(ctor, { path: "/Game/L.L:PersistentLevel.S", blendMode: "Min", quality: { lod: 2 } });
    await done;
    expect(calls).toEqual([{ method: "stamp_set", params: { path: "/Game/L.L:PersistentLevel.S", blendMode: "Min", quality: { lod: 2 } } }]);
  });

  it.each([
    [{ actorPath: "a", blendMode: "Additive" }, /blendMode/],
    [{ actorPath: "a", priority: -1 }, /priority/],
    [{ actorPath: "a", priority: 1.5 }, /priority/],
    [{ actorPath: "a", quality: { lod: 9 } }, /quality\.lod/],
    [{ actorPath: "a", quality: {} }, /quality\.lod/],
    [{ blendMode: "Max" }, /needs actorPath \(or path\)/],
    [{ actorPath: "a", strength: 2 }, /does not take strength/],
  ])("refuses %j before anything is sent", async (options, message) => {
    const { surface } = provided();
    const ctor = surface.taskRegistrations.find((r) => r.name === "voxel.stamp_set")!.ctor;
    const { calls, done } = run(ctor, options);
    await expect(done).rejects.toThrow(message);
    expect(calls).toEqual([]);
  });

  it("is held to the same contract when injected into a built-in category", async () => {
    const surface = nativeHandlerSurface(manifest("pcg"), "voxel-tools", new Set(["pcg"]), SPECS);
    if (surface?.kind !== "inject") throw new Error("expected an injected surface");
    const host = categoryTool("pcg", "Fake PCG.", { list_graphs: bp("read", "List.", "pcg_list_graphs") }, {});
    const { tool } = mergeInjectionsIntoTool(host, [surface.plan]);
    expect(tool.actions.vox_stamp_set.strictParams).toBe(true);
    const ctor = surface.taskRegistrations.find((r) => r.name === "pcg.vox_stamp_set")!.ctor;
    const { calls, done } = run(ctor, { actorPath: "a", blendMode: "Subtractive" });
    await expect(done).rejects.toThrow(/vox_stamp_set got "Subtractive" for blendMode/);
    expect(calls).toEqual([]);
  });

  it("leaves a handler without a spec on its manifest schema", () => {
    const surface = nativeHandlerSurface(
      manifest("voxel", { stamp_set: {}, legacy: { schema: { x: { type: "number", required: true } } } }),
      "voxel-tools", new Set(), SPECS,
    );
    if (surface?.kind !== "provide") throw new Error("expected a provided category");
    expect(surface.plan.spec.actions.legacy.contract).toBeUndefined();
    expect(surface.plan.spec.actions.legacy.schema).toEqual({ x: { type: "number", required: true } });
  });
});

describe("contractViolation", () => {
  it("checks required names and unknown keys only under strict", () => {
    const contract = { params: SPECS.stamp_set.params };
    expect(contractViolation(contract, { other: 1 })).toBeUndefined();
    expect(contractViolation({ ...contract, strict: true }, { actorPath: "a", other: 1 })).toMatch(/does not take other/);
    expect(contractViolation(contract, { blendMode: "x" })).toMatch(/blendMode/);
  });
});

describe("plugin spec drift", () => {
  it("compares only the methods a loaded plugin recorded", () => {
    const { tool } = provided();
    const recorded = recordedPluginSpecs([tool]);
    expect(Object.keys(recorded).sort()).toEqual(["stamp_set", "volume_set"]);
    const capabilities = (pluginHandlerSpecs: Record<string, unknown>) =>
      ({ legacy: false, protocolVersion: 2, handlerSpecs: {}, pluginHandlerSpecs }) as BridgeCapabilities;
    const parity = { missing: [], message: null } as never;
    const same = deployedPlugin(capabilities({ ...SPECS, other_plugin: { params: [] } }), parity, {}, recorded);
    expect(same?.handlerSpecDrift).toBeUndefined();
    const changed = deployedPlugin(capabilities({ stamp_set: SPECS.volume_set, volume_set: SPECS.volume_set }), parity, {}, recorded);
    expect(changed?.handlerSpecDrift).toEqual(["stamp_set"]);
  });
});

describe("review findings (#1282)", () => {
  const NESTED: HandlerSpecs = {
    ha: { params: [{ name: "opts", type: "object", required: false, description: "", fields: [{ name: "a", type: "number", required: false, description: "" }] }] },
    hb: { params: [{ name: "opts", type: "object", required: false, description: "", fields: [{ name: "b", type: "number", required: false, description: "" }] }] },
  };
  const nestedTool = () => {
    const surface = nativeHandlerSurface(manifest("probe", { ha: {}, hb: {} }), "probe", new Set(), NESTED);
    if (surface?.kind !== "provide") throw new Error("expected a provided category");
    return { surface, tool: buildProvidedTool(surface.plan) };
  };

  it("passes a strict action's bag through the shared shape untouched, nested fields and undeclared keys included", () => {
    const { tool } = nestedTool();
    expect(validateCategoryParams(tool, { action: "hb", opts: { b: 5 } })).toEqual({ action: "hb", opts: { b: 5 } });
    expect(validateCategoryParams(tool, { action: "hb", typo: 1 })).toEqual({ action: "hb", typo: 1 });
  });

  it("refuses an undeclared nested key under a strict contract", () => {
    expect(contractViolation({ params: NESTED.ha.params, strict: true }, { opts: { a: 1, zzz: 2 } })).toMatch(/opts/);
    expect(contractViolation({ params: NESTED.ha.params }, { opts: { a: 1, zzz: 2 } })).toBeUndefined();
  });

  it("refuses a name and its alias together under a strict contract", () => {
    expect(contractViolation({ params: SPECS.stamp_set.params, strict: true }, { actorPath: "/A", path: "/B" }))
      .toMatch(/actorPath and path, which are one parameter/);
  });

  it("refuses the envelope route's undeclared key at the contract, as micro does", async () => {
    const { tool, surface } = nestedTool();
    const bag = validateCategoryParams(tool, { action: "hb", typo: 1 });
    const { action: _action, ...options } = bag;
    const { calls, done } = run(surface.taskRegistrations.find((r) => r.name === "probe.hb")!.ctor, options);
    await expect(done).rejects.toThrow(/does not take typo/);
    expect(calls).toEqual([]);
  });

  it("refuses an enum value a signature cannot write", () => {
    const problems = specProblems({ probe: { params: [{ name: "m", type: "string", required: false, description: "", enum: ["A,B"] }] } });
    expect(problems.join("\n")).toMatch(/cannot write/);
  });
});

describe("live plugin specs (#1290)", () => {
  const withInstanceOf = {
    params: [...SPECS.volume_set.params, { name: "instanceOf", type: "string", required: false, description: "Template" }],
  };

  function runLive(options: Record<string, unknown>, pluginHandlerSpecs: Record<string, unknown> | undefined) {
    const { surface } = provided();
    const reg = surface.taskRegistrations.find((r) => r.name === "voxel.volume_set")!;
    const calls: Array<{ method: string; params: Record<string, unknown> }> = [];
    const ctx = {
      project: {},
      bridge: {
        capabilities: { pluginHandlerSpecs } as BridgeCapabilities,
        call: async (method: string, params: Record<string, unknown>) => { calls.push({ method, params }); return { success: true }; },
      } as unknown as IBridge,
    } as FlowContext;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const task = new (reg.ctor as any)(ctx, options);
    return { calls, done: task.execute() as Promise<unknown> };
  }

  it("adopts the running module's spec when it differs from the recording", async () => {
    const { calls, done } = runLive({ actorPath: "/A", instanceOf: "/T" }, { volume_set: withInstanceOf });
    await done;
    expect(calls).toEqual([{ method: "volume_set", params: { actorPath: "/A", instanceOf: "/T" } }]);
  });

  it("keeps the recording without a live spec, and names a malformed live spec in the refusal", async () => {
    await expect(runLive({ actorPath: "/A", instanceOf: "/T" }, undefined).done).rejects.toThrow(/does not take instanceOf/);
    await expect(runLive({ actorPath: "/A", instanceOf: "/T" }, { volume_set: { params: "nope" } }).done)
      .rejects.toThrow(/running module registered a different contract for volume_set/);
  });

  it("still holds the call to the live spec", async () => {
    await expect(runLive({ instanceOf: "/T" }, { volume_set: withInstanceOf }).done).rejects.toThrow(/needs actorPath/);
  });
});
