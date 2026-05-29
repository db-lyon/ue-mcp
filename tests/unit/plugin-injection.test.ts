import { describe, it, expect } from "vitest";
import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import { mergeInjectionsIntoTool, type InjectionPlan } from "../../src/plugin/injection.js";
import { looksLikeBaseTask, nativeHandlerInjection } from "../../src/plugin/loader.js";
import { PluginManifestSchema } from "../../src/plugin/manifest.js";

function fakePcg(): ToolDef {
  return categoryTool(
    "pcg",
    "Fake PCG tool for testing.",
    {
      list_graphs: bp("pcg_list_graphs"),
      add_node: bp("Add a node", "pcg_add_node"),
    },
    undefined,
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
    const enumSchema = tool.schema.action as z.ZodEnum<[string, ...string[]]>;
    expect(enumSchema._def.values).toContain("vpp_foo");
    expect(enumSchema._def.values).toContain("list_graphs");
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
      vpp_node: bp("collision target"),
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

describe("nativeHandlerInjection", () => {
  const manifestWith = (nativeModule: Record<string, unknown>) =>
    PluginManifestSchema.parse({ actionPrefix: "pie", nativeModule });

  it("returns null when nativeModule has no category (bridge-only, back-compat)", () => {
    const manifest = manifestWith({
      uePluginName: "PIE_Studio",
      minBridgeApi: 1,
      source: "ue/Plugins/PIE_Studio",
      handlers: { record_arm: { description: "Arm" } },
    });
    expect(nativeHandlerInjection(manifest, "pie-studio")).toBeNull();
  });

  it("returns null when there is no nativeModule at all", () => {
    const manifest = PluginManifestSchema.parse({ actionPrefix: "pie" });
    expect(nativeHandlerInjection(manifest, "pie-studio")).toBeNull();
  });

  it("surfaces each handler as a prefixed action in the target category", () => {
    const manifest = manifestWith({
      uePluginName: "PIE_Studio",
      minBridgeApi: 1,
      source: "ue/Plugins/PIE_Studio",
      category: "gameplay",
      handlers: {
        record_arm: { description: "Arm the recorder" },
        apply_damage: {
          description: "Apply damage",
          schema: { amount: { type: "number", required: true } },
        },
      },
    });
    const result = nativeHandlerInjection(manifest, "pie-studio");
    expect(result).not.toBeNull();
    const { plan, taskRegistrations } = result!;

    expect(plan.category).toBe("gameplay");
    expect(plan.prefix).toBe("pie");
    // Plan actions are keyed by the BARE handler name; the prefix is applied
    // by mergeInjectionsIntoTool, matching the rest of the inject pipeline.
    expect(Object.keys(plan.actions).sort()).toEqual(["apply_damage", "record_arm"]);
    // required is forced false (see the optional-params test below).
    expect(plan.actions.apply_damage.schema).toEqual({
      amount: { type: "number", required: false },
    });

    // Dispatch tasks are registered under `<category>.<prefix>_<handler>` so
    // index.ts's `${tool}.${action}` lookup resolves them.
    const names = taskRegistrations.map((r) => r.name).sort();
    expect(names).toEqual([
      "gameplay.pie_apply_damage",
      "gameplay.pie_record_arm",
    ]);
  });

  it("forces surfaced params optional so they can't be required across the shared category", () => {
    const manifest = manifestWith({
      uePluginName: "PIE_Studio",
      minBridgeApi: 1,
      source: "ue/Plugins/PIE_Studio",
      category: "gameplay",
      handlers: {
        inject_input: {
          schema: { action_path: { type: "string", required: true } },
        },
      },
    });
    const { plan } = nativeHandlerInjection(manifest, "pie-studio")!;
    expect(plan.actions.inject_input.schema?.action_path.required).toBe(false);
  });

  it("produces actions that merge into the host category as pie_-prefixed", () => {
    const manifest = manifestWith({
      uePluginName: "PIE_Studio",
      minBridgeApi: 1,
      source: "ue/Plugins/PIE_Studio",
      category: "pcg",
      handlers: { record_arm: { description: "Arm the recorder" } },
    });
    const { plan } = nativeHandlerInjection(manifest, "pie-studio")!;
    const { tool, added } = mergeInjectionsIntoTool(fakePcg(), [plan]);
    expect(added).toEqual(["pie_record_arm"]);
    expect(tool.actions.pie_record_arm?.description).toBe("Arm the recorder");
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
