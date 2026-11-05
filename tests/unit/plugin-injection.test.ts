import { describe, it, expect } from "vitest";
import { z } from "zod";
import type { ToolDef } from "../../src/core/types.js";
import { actionEnumValues, categoryTool, bp } from "../../src/surface/category-tool.js";
import { mergeInjectionsIntoTool, type InjectionPlan } from "../../src/extensions/injection.js";
import { looksLikeBaseTask, nativeHandlerSurface } from "../../src/extensions/loader.js";
import { PluginManifestSchema } from "../../src/extensions/manifest.js";
import { buildProvidedTool } from "../../src/extensions/provision.js";
import { requiringParams } from "../../src/extensions/manifest-params.js";

function fakePcg(): ToolDef {
  return categoryTool(
    "pcg",
    "Fake PCG tool for testing.",
    {
      list_graphs: bp("read", "pcg_list_graphs"),
      add_node: bp("read", "Add a node", "pcg_add_node"),
    },
    { graphPath: z.string().optional() },
  );
}

describe("mergeInjectionsIntoTool", () => {
  it("returns the original tool when no plans apply", () => {
    const orig = fakePcg();
    const { tool, added, skipped } = mergeInjectionsIntoTool(orig, []);
    expect(tool).toBe(orig);
    expect(added).toEqual([]);
    expect(skipped).toEqual([]);
  });

  it("adds prefixed actions from a plan", () => {
    const orig = fakePcg();
    const plan: InjectionPlan = {
      category: "pcg",
      prefix: "vpp",
      pluginName: "ue-mcp-voxel-pro",
      actions: {
        scatter_on_terrain: {
          task: "vpp.scatter_on_terrain",
          description: "Scatter meshes on a voxel terrain",
          schema: { graphPath: { type: "string", required: true } },
        },
      },
    };
    const { tool, added } = mergeInjectionsIntoTool(orig, [plan]);
    expect(added).toEqual(["vpp_scatter_on_terrain"]);
    expect(tool.actions.vpp_scatter_on_terrain).toBeTruthy();
    expect(tool.actions.list_graphs).toBeTruthy();
    expect(tool.actions.add_node).toBeTruthy();
    expect(tool.description).toContain("Plugin actions:");
    expect(tool.description).toContain("vpp_scatter_on_terrain");
  });

  it("rebuilds the action enum to include the injected name", () => {
    const orig = fakePcg();
    const plan: InjectionPlan = {
      category: "pcg",
      prefix: "vpp",
      pluginName: "x",
      actions: { foo: { task: "vpp.foo", description: "" } },
    };
    const { tool } = mergeInjectionsIntoTool(orig, [plan]);
    const advertised = actionEnumValues(tool.schema.action);
    expect(advertised).toContain("vpp_foo");
    expect(advertised).toContain("list_graphs");
  });

  it("skips built-in collisions and never overrides", () => {
    const plan: InjectionPlan = {
      category: "pcg",
      prefix: "vpp",
      pluginName: "x",
      actions: { node: { task: "vpp.node", description: "" } },
    };
    // Hand-craft a built-in named exactly `vpp_node` to provoke a collision.
    const builtin = categoryTool("pcg", "x", {
      vpp_node: bp("read", "collision target"),
    });
    const { added, skipped } = mergeInjectionsIntoTool(builtin, [plan]);
    expect(added).toEqual([]);
    expect(skipped).toHaveLength(1);
    expect(skipped[0].action).toBe("vpp_node");
  });

  it("first plan wins on inter-plugin collisions", () => {
    const orig = fakePcg();
    const planA: InjectionPlan = {
      category: "pcg",
      prefix: "vpp",
      pluginName: "a",
      actions: { foo: { task: "vpp.foo", description: "from a" } },
    };
    const planB: InjectionPlan = {
      category: "pcg",
      prefix: "vpp",
      pluginName: "b",
      actions: { foo: { task: "vpp.foo", description: "from b" } },
    };
    const { added, skipped } = mergeInjectionsIntoTool(orig, [planA, planB]);
    expect(added).toEqual(["vpp_foo"]);
    expect(skipped).toHaveLength(1);
  });
});

describe("nativeHandlerSurface", () => {
  const BUILTINS = new Set(["gameplay", "pcg", "editor"]);
  const manifestWith = (nativeModule: Record<string, unknown>) =>
    PluginManifestSchema.parse({ actionPrefix: "recorder", nativeModule });

  it("returns null when nativeModule has no category (bridge-only, back-compat)", () => {
    const manifest = manifestWith({
      uePluginName: "Sample_Recorder",
      minBridgeApi: 1,
      source: "ue/Plugins/Sample_Recorder",
      handlers: { record_arm: { description: "Arm" } },
    });
    expect(nativeHandlerSurface(manifest, "sample-recorder", BUILTINS)).toBeNull();
  });

  it("returns null when there is no nativeModule at all", () => {
    const manifest = PluginManifestSchema.parse({ actionPrefix: "recorder" });
    expect(nativeHandlerSurface(manifest, "sample-recorder", BUILTINS)).toBeNull();
  });

  it("injects into a built-in category with prefixed actions", () => {
    const manifest = manifestWith({
      uePluginName: "Sample_Recorder",
      minBridgeApi: 1,
      source: "ue/Plugins/Sample_Recorder",
      category: "gameplay",
      handlers: {
        record_arm: { description: "Arm the recorder" },
        apply_damage: {
          description: "Apply damage",
          schema: { amount: { type: "number", required: true } },
        },
      },
    });
    const result = nativeHandlerSurface(manifest, "sample-recorder", BUILTINS);
    expect(result?.kind).toBe("inject");
    if (result?.kind !== "inject") throw new Error("expected inject");
    const { plan, taskRegistrations } = result;

    expect(plan.category).toBe("gameplay");
    expect(plan.prefix).toBe("recorder");
    // Plan actions are keyed by the BARE handler name; the prefix is applied
    // by mergeInjectionsIntoTool, matching the rest of the inject pipeline.
    expect(Object.keys(plan.actions).sort()).toEqual(["apply_damage", "record_arm"]);
    // The plan keeps the author's required flag; the merge lifts the key optional.
    expect(plan.actions.apply_damage.schema).toEqual({
      amount: { type: "number", required: true },
    });
    // Dispatch tasks register under `<category>.<prefix>_<handler>`.
    expect(taskRegistrations.map((r) => r.name).sort()).toEqual([
      "gameplay.recorder_apply_damage",
      "gameplay.recorder_record_arm",
    ]);
  });

  it("provisions a NEW category with unprefixed actions when category isn't built-in", () => {
    const manifest = manifestWith({
      uePluginName: "Sample_Recorder",
      minBridgeApi: 1,
      source: "ue/Plugins/Sample_Recorder",
      category: "recorder",
      categoryDescription: "PIE test automation",
      handlers: {
        record_arm: { description: "Arm the recorder" },
        replay_arm: { description: "Arm replay" },
      },
    });
    const result = nativeHandlerSurface(manifest, "sample-recorder", BUILTINS);
    expect(result?.kind).toBe("provide");
    if (result?.kind !== "provide") throw new Error("expected provide");
    const { plan, taskRegistrations } = result;

    expect(plan.category).toBe("recorder");
    expect(plan.description).toBe("PIE test automation");
    // Provided-category actions are NOT prefixed - the category is the namespace.
    expect(Object.keys(plan.spec.actions).sort()).toEqual(["record_arm", "replay_arm"]);
    // Dispatch registers under `<category>.<handler>` (no prefix).
    expect(taskRegistrations.map((r) => r.name).sort()).toEqual([
      "recorder.record_arm",
      "recorder.replay_arm",
    ]);
  });

  it("lifts surfaced params optional in the shared category and keeps required on the action (#1270)", () => {
    const manifest = manifestWith({
      uePluginName: "Sample_Recorder",
      minBridgeApi: 1,
      source: "ue/Plugins/Sample_Recorder",
      category: "gameplay",
      handlers: {
        inject_input: {
          schema: { action_path: { type: "string", required: true } },
        },
      },
    });
    const result = nativeHandlerSurface(manifest, "sample-recorder", BUILTINS);
    if (result?.kind !== "inject") throw new Error("expected inject");
    const { tool } = mergeInjectionsIntoTool(fakePcg(), [result.plan]);
    expect(tool.schema.action_path.isOptional()).toBe(true);
    expect(tool.actions.recorder_inject_input.paramSpec).toEqual([
      { name: "action_path", type: "string", required: true, description: "" },
    ]);
  });

  it("inject plan merges into the host built-in category as recorder_-prefixed", () => {
    const manifest = manifestWith({
      uePluginName: "Sample_Recorder",
      minBridgeApi: 1,
      source: "ue/Plugins/Sample_Recorder",
      category: "pcg",
      handlers: { record_arm: { description: "Arm the recorder" } },
    });
    const result = nativeHandlerSurface(manifest, "sample-recorder", BUILTINS);
    if (result?.kind !== "inject") throw new Error("expected inject");
    const { tool, added } = mergeInjectionsIntoTool(fakePcg(), [result.plan]);
    expect(added).toEqual(["recorder_record_arm"]);
    expect(tool.actions.recorder_record_arm?.description).toBe("Arm the recorder");
  });
});

describe("looksLikeBaseTask", () => {
  // Regression: npm 7+ installs peerDependencies into the consumer's
  // node_modules, giving the plugin its own copy of @db-lyon/flowkit with a
  // distinct BaseTask class identity. `instanceof BaseTask` then fails even
  // when the plugin is behaviourally correct. We rely on duck-typing.
  it("accepts a class with run() and execute() on its own prototype", () => {
    class GoodTask {
      get taskName() { return "fake"; }
      async execute() { return { success: true }; }
      async run() { return this.execute(); }
    }
    expect(looksLikeBaseTask(GoodTask)).toBe(true);
  });

  it("accepts a class that inherits run() / execute() from a parent", () => {
    class FakeBase {
      async run() { return { success: true }; }
      async execute() { return { success: true }; }
    }
    class Sub extends FakeBase {
      get taskName() { return "fake"; }
    }
    expect(looksLikeBaseTask(Sub)).toBe(true);
  });

  it("rejects a class missing execute()", () => {
    class NoExecute {
      async run() { return { success: true }; }
    }
    expect(looksLikeBaseTask(NoExecute)).toBe(false);
  });

  it("rejects a class missing run()", () => {
    class NoRun {
      async execute() { return { success: true }; }
    }
    expect(looksLikeBaseTask(NoRun)).toBe(false);
  });

  it("rejects non-functions", () => {
    expect(looksLikeBaseTask({})).toBe(false);
    expect(looksLikeBaseTask(null)).toBe(false);
    expect(looksLikeBaseTask(undefined)).toBe(false);
    expect(looksLikeBaseTask("class")).toBe(false);
  });
});

describe("required plugin params (#1270)", () => {
  const plan: InjectionPlan = {
    category: "pcg",
    prefix: "vpp",
    pluginName: "x",
    actions: {
      scatter: {
        task: "vpp.scatter",
        description: "Scatter meshes",
        schema: { meshPath: { type: "string", required: true }, density: { type: "number" } },
      },
    },
  };

  it("lifts a required field optional, so other actions in the category do not need it", () => {
    const { tool } = mergeInjectionsIntoTool(fakePcg(), [plan]);
    expect(tool.schema.meshPath.isOptional()).toBe(true);
    expect(z.object(tool.schema).safeParse({ action: "list_graphs" }).success).toBe(true);
  });

  it("keeps the field required on its own action's spec and Params clause", () => {
    const { tool } = mergeInjectionsIntoTool(fakePcg(), [plan]);
    const spec = tool.actions.vpp_scatter;
    expect(spec.description).toBe("Scatter meshes Params: meshPath, density?");
    expect(spec.paramSpec?.map((p) => [p.name, p.required])).toEqual([["meshPath", true], ["density", false]]);
  });

  it("does the same for a provided category", () => {
    const tool = buildProvidedTool({
      category: "vox",
      pluginName: "x",
      spec: {
        actions: {
          stamp: { task: "t", schema: { shape: { type: "string", required: true } } },
          clear: { task: "t" },
        },
      },
    });
    expect(tool.schema.shape.isOptional()).toBe(true);
    expect(tool.actions.stamp.paramSpec?.[0]).toMatchObject({ name: "shape", required: true });
    expect(tool.actions.clear.paramSpec).toBeUndefined();
  });

  it("refuses a dispatch missing the required field and runs one that has it", async () => {
    class Task {
      constructor(_ctx: unknown, readonly options: Record<string, unknown>) {}
      async run() {
        return { success: true, data: this.options };
      }
    }
    const Wrapped = requiringParams(Task as never, "pcg.vpp_scatter", ["meshPath"]) as unknown as typeof Task;
    await expect(new Wrapped({}, { density: 1 }).run()).rejects.toThrow(/pcg.vpp_scatter needs meshPath/);
    await expect(new Wrapped({}, { meshPath: "/Game/M" }).run()).resolves.toMatchObject({ success: true });
    expect(requiringParams(Task as never, "x", [])).toBe(Task);
  });
});
