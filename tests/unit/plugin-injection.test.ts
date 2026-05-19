import { describe, it, expect } from "vitest";
import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import { mergeInjectionsIntoTool, type InjectionPlan } from "../../src/plugin/injection.js";

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
      pluginName: "ue-mcp-plugin-voxel-plugin-pro",
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
    const orig = fakePcg();
    const plan: InjectionPlan = {
      category: "pcg",
      prefix: "vpp",
      pluginName: "x",
      // attempt to inject `add_node` after prefixing - simulate by setting
      // the prefix and bare name such that prefixed equals a built-in
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
