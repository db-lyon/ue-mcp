import { describe, it, expect, afterEach } from "vitest";
import { z } from "zod";
import { categoryTool, bp, type ToolDef, type ToolContext } from "../../src/types.js";
import {
  resolveContextStrategy,
  splitDescription,
  buildCatalogTool,
  applyLeanContext,
} from "../../src/lean-context.js";

function fixtureTools(): ToolDef[] {
  return [
    categoryTool("blueprint", "Blueprint authoring.", {
      create: bp("Create a new Blueprint asset", "create_blueprint"),
      add_node: bp("Add a node to a graph", "add_node"),
    }, undefined, {}),
    categoryTool("level", "Level actors and volumes.", {
      place_actor: bp("Spawn an actor into the level", "place_actor"),
      delete_actor: bp("Remove an actor from the level", "delete_actor"),
    }, undefined, {}),
  ];
}

// Minimal context; the discovery handlers ignore it.
const ctx = {} as ToolContext;

async function runAction(tool: ToolDef, action: string, params: Record<string, unknown> = {}) {
  return tool.actions[action].handler!(ctx, { action, ...params });
}

describe("resolveContextStrategy", () => {
  const saved = process.env.UE_MCP_CONTEXT_STRATEGY;
  afterEach(() => {
    if (saved === undefined) delete process.env.UE_MCP_CONTEXT_STRATEGY;
    else process.env.UE_MCP_CONTEXT_STRATEGY = saved;
  });

  it("defaults to full", () => {
    delete process.env.UE_MCP_CONTEXT_STRATEGY;
    expect(resolveContextStrategy()).toBe("full");
    expect(resolveContextStrategy(undefined)).toBe("full");
  });

  it("reads lean from config", () => {
    delete process.env.UE_MCP_CONTEXT_STRATEGY;
    expect(resolveContextStrategy("lean")).toBe("lean");
  });

  it("env overrides config, case-insensitively", () => {
    process.env.UE_MCP_CONTEXT_STRATEGY = "LEAN";
    expect(resolveContextStrategy("full")).toBe("lean");
    process.env.UE_MCP_CONTEXT_STRATEGY = "full";
    expect(resolveContextStrategy("lean")).toBe("full");
  });

  it("treats unknown values as full", () => {
    delete process.env.UE_MCP_CONTEXT_STRATEGY;
    expect(resolveContextStrategy("verbose")).toBe("full");
  });
});

describe("splitDescription", () => {
  it("splits a categoryTool description into summary and catalog", () => {
    const tool = fixtureTools()[0];
    const { summary, catalog } = splitDescription(tool.description);
    expect(summary).toBe("Blueprint authoring.");
    expect(catalog).toContain("- create: Create a new Blueprint asset");
    expect(catalog).toContain("- add_node:");
  });

  it("handles a description with no catalog", () => {
    expect(splitDescription("Just a summary")).toEqual({ summary: "Just a summary", catalog: "" });
  });
});

describe("applyLeanContext", () => {
  it("does not mutate the input tools", () => {
    const tools = fixtureTools();
    const before = tools[0].description;
    applyLeanContext(tools);
    expect(tools[0].description).toBe(before);
    expect(tools[0].actions.describe).toBeUndefined();
  });

  it("prepends a catalog tool and trims category descriptions", () => {
    const leaned = applyLeanContext(fixtureTools());
    expect(leaned[0].name).toBe("catalog");
    const bpTool = leaned.find((t) => t.name === "blueprint")!;
    expect(bpTool.description).toContain("Blueprint authoring.");
    expect(bpTool.description).not.toContain("- create:");
    expect(bpTool.description).toContain('blueprint(action="describe")');
  });

  it("adds a describe action to each category, preserving originals", () => {
    const leaned = applyLeanContext(fixtureTools());
    const bpTool = leaned.find((t) => t.name === "blueprint")!;
    expect(Object.keys(bpTool.actions)).toEqual(["create", "add_node", "describe"]);
    // The action enum must include the injected describe so it validates.
    const enumValues = (bpTool.schema.action as z.ZodEnum<[string, ...string[]]>).options;
    expect(enumValues).toContain("describe");
  });

  it("per-category describe returns that category's action list", async () => {
    const leaned = applyLeanContext(fixtureTools());
    const bpTool = leaned.find((t) => t.name === "blueprint")!;
    const out = (await runAction(bpTool, "describe")) as { category: string; count: number; actions: string[] };
    expect(out.category).toBe("blueprint");
    expect(out.count).toBe(2);
    expect(out.actions).toContain("- create: Create a new Blueprint asset");
  });
});

describe("catalog discovery tool", () => {
  it("search ranks matching actions across categories", async () => {
    const catalog = buildCatalogTool(fixtureTools());
    const out = (await runAction(catalog, "search", { query: "actor" })) as {
      count: number;
      results: Array<{ category: string; action: string }>;
    };
    expect(out.count).toBeGreaterThan(0);
    const keys = out.results.map((r) => `${r.category}.${r.action}`);
    expect(keys).toContain("level.place_actor");
    expect(keys).toContain("level.delete_actor");
    // Blueprint actions should not match "actor".
    expect(keys.some((k) => k.startsWith("blueprint."))).toBe(false);
  });

  it("search errors on an empty query", async () => {
    const catalog = buildCatalogTool(fixtureTools());
    const out = (await runAction(catalog, "search", { query: "  " })) as { error?: string };
    expect(out.error).toBeDefined();
  });

  it("describe lists a category, and rejects unknown categories", async () => {
    const catalog = buildCatalogTool(fixtureTools());
    const ok = (await runAction(catalog, "describe", { category: "level" })) as { count: number };
    expect(ok.count).toBe(2);
    const bad = (await runAction(catalog, "describe", { category: "nope" })) as { error?: string; categories?: string[] };
    expect(bad.error).toBeDefined();
    expect(bad.categories).toContain("level");
  });

  it("list_categories returns every category with its summary", async () => {
    const catalog = buildCatalogTool(fixtureTools());
    const out = (await runAction(catalog, "list_categories")) as {
      count: number;
      categories: Array<{ category: string; summary: string }>;
    };
    expect(out.count).toBe(2);
    expect(out.categories.find((c) => c.category === "blueprint")?.summary).toBe("Blueprint authoring.");
  });
});
