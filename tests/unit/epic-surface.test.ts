import { describe, expect, it } from "vitest";
import { z } from "zod";
import { applyNativeToolsConfig } from "../../src/surface/epic-surface.js";
import { applyLeanContext } from "../../src/surface/context/lean-context.js";
import type { ToolDef } from "../../src/core/types.js";
import { actionEnumValues, bp, categoryTool, cloneToolGraph } from "../../src/surface/category-tool.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { unionSurface, type SessionSurface } from "../../src/sessions/session-surface.js";
import { mergeInjectionsIntoTool, type InjectionPlan } from "../../src/extensions/injection.js";

function mixedTool(): ToolDef {
  return categoryTool(
    "mixed",
    "A category with local and wrapped actions.",
    {
      keep: bp(
        "read",
        "Keep the local action. Params: retained, shared",
        "keep",
        (p) => ({ retained: p.retained, shared: p.shared }),
      ),
      epic_drop: bp(
        "read",
        "Remove the wrapped action. Params: nativeOnly, shared",
        "epic_call_tool",
        (p) => ({ nativeOnly: p.nativeOnly, shared: p.shared }),
      ),
    },
    {
      retained: z.string().optional(),
      shared: z.string().optional(),
      nativeOnly: z.string().optional(),
    },
  );
}

function surface(name: string, tools: ToolDef[]): SessionSurface {
  return {
    session: { name } as SessionSurface["session"],
    tools,
    disabled: new Set(),
    pluginRecords: [],
    knowledgeByCategory: {},
  };
}

describe("native tool surface filtering", () => {
  it("rebuilds action metadata and keeps flat parameters after native actions leave", () => {
    const tool = mixedTool();

    const result = applyNativeToolsConfig([tool], { enabled: false });

    expect(result).toMatchObject({ removed: 1, byCategory: { mixed: 1 } });
    expect(Object.keys(tool.actions)).toEqual(["keep"]);
    expect(actionEnumValues(tool.schema.action)).toEqual(["keep"]);
    expect(tool.description).toContain("keep: Keep the local action");
    expect(tool.description).not.toContain("epic_drop");
    expect(Object.keys(tool.schema)).toEqual(expect.arrayContaining(["action", "retained", "shared"]));
    expect(tool.schema).toHaveProperty("nativeOnly");
    expect(tool.schema.retained.safeParse("kept").success).toBe(true);
    expect(tool.schema.shared.safeParse("shared").success).toBe(true);
  });

  it("keeps the filtered surface coherent through lean discovery", async () => {
    const tool = mixedTool();
    applyNativeToolsConfig([tool], { enabled: false });

    const lean = applyLeanContext([tool]);
    const mixed = lean.find((candidate) => candidate.name === "mixed")!;
    const catalog = lean.find((candidate) => candidate.name === "catalog")!;

    expect(actionEnumValues(mixed.schema.action)).toEqual(expect.arrayContaining(["keep", "describe"]));
    expect(actionEnumValues(mixed.schema.action)).not.toContain("epic_drop");
    await expect(catalog.actions.describe.handler!({} as never, { category: "mixed" }))
      .resolves.toMatchObject({ signatures: ["keep(retained, shared)"] });
  });

  it("rebuilds action metadata on the real full surface without guessing schema ownership", () => {
    const tools = cloneToolGraph(ALL_TOOLS);
    const animation = tools.find((tool) => tool.name === "animation")!;
    const epicAnimationActions = Object.keys(animation.actions).filter((name) => name.startsWith("epic_"));
    const beforeFields = Object.keys(animation.schema).length;

    applyNativeToolsConfig(tools, { enabled: false });

    expect(epicAnimationActions.length).toBeGreaterThan(0);
    expect(Object.keys(animation.actions)).not.toContain("epic_add_actors");
    expect(actionEnumValues(animation.schema.action)).toEqual(Object.keys(animation.actions));
    expect(animation.description).not.toContain("- epic_add_actors:");
    expect(Object.keys(animation.schema)).toHaveLength(beforeFields);
  });

  it("names no wrapped engine action anywhere once they are all withheld", () => {
    const tools = cloneToolGraph(ALL_TOOLS);

    applyNativeToolsConfig(tools, { enabled: false });

    const mentions = tools.flatMap((tool) => [
      ...[...tool.description.matchAll(/\bepic_[a-z]\w*/g)].map((m) => `${tool.name}: ${m[0]}`),
      ...Object.entries(tool.actions).flatMap(([name, spec]) =>
        [...(spec.description ?? "").matchAll(/\bepic_[a-z]\w*/g)].map((m) => `${tool.name}.${name}: ${m[0]}`)),
    ]);
    expect(mentions).toEqual([]);
  });

  it("reports categories made entirely of wrapped engine actions for removal", () => {
    const tools = cloneToolGraph(ALL_TOOLS);

    const result = applyNativeToolsConfig(tools, { enabled: false });

    expect(result.droppedCategories).toContain("dataflow");
    const dataflow = tools.find((tool) => tool.name === "dataflow")!;
    expect(Object.keys(dataflow.actions)).toEqual([]);
  });

  it("restores native action documentation from a later enabled session", async () => {
    const disabled = cloneToolGraph(ALL_TOOLS);
    const enabled = cloneToolGraph(ALL_TOOLS);
    applyNativeToolsConfig(disabled, { enabled: false });
    const union = unionSurface([surface("disabled", disabled), surface("enabled", enabled)]).tools;
    const animation = union.find((tool) => tool.name === "animation")!;
    expect(animation.actions).toHaveProperty("epic_add_actors");
    expect(actionEnumValues(animation.schema.action)).toContain("epic_add_actors");
    expect(animation.description).toContain("- epic_add_actors:");

    const catalog = applyLeanContext(union).find((tool) => tool.name === "catalog")!;
    await expect(catalog.actions.describe.handler!(
      {} as never,
      { category: "animation", method: "epic_add_actors" },
    )).resolves.toMatchObject({ action: "epic_add_actors" });
  });

  it("restores native actions into the action list, not under a plugin section", () => {
    const plan: InjectionPlan = {
      category: "animation",
      prefix: "myplug",
      pluginName: "my-plugin",
      actions: { retarget: { task: "myplug.retarget", description: "Retarget with the plugin" } },
    };
    const disabled = cloneToolGraph(ALL_TOOLS);
    const i = disabled.findIndex((tool) => tool.name === "animation");
    disabled[i] = mergeInjectionsIntoTool(disabled[i], [plan]).tool;
    applyNativeToolsConfig(disabled, { enabled: false });
    const enabled = cloneToolGraph(ALL_TOOLS);

    const union = unionSurface([surface("disabled", disabled), surface("enabled", enabled)]).tools;
    const description = union.find((tool) => tool.name === "animation")!.description;
    const pluginSection = description.indexOf("\n\nPlugin actions:\n");

    expect(pluginSection).toBeGreaterThan(0);
    expect(description.indexOf("- epic_add_actors:")).toBeGreaterThan(0);
    expect(description.indexOf("- epic_add_actors:")).toBeLessThan(pluginSection);
    expect(description.slice(pluginSection)).not.toContain("epic_");
    expect(description.slice(pluginSection)).toContain("- myplug_retarget: Retarget with the plugin");
  });
});
