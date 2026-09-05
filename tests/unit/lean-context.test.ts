import { describe, it, expect, afterEach } from "vitest";
import { z } from "zod";
import { actionEnumValues, categoryTool, bp, type ToolDef, type ToolContext } from "../../src/types.js";
import {
  resolveContextStrategy,
  splitDescription,
  buildCatalogTool,
  applyLeanContext,
  buildMicroGateway,
} from "../../src/lean-context.js";
import { searchToolGraph } from "../../src/tool-search.js";
import { SERVER_INSTRUCTIONS, SERVER_INSTRUCTIONS_LEAN, SERVER_INSTRUCTIONS_MICRO } from "../../src/instructions.js";

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

  it("reads lean and micro from config", () => {
    delete process.env.UE_MCP_CONTEXT_STRATEGY;
    expect(resolveContextStrategy("lean")).toBe("lean");
    expect(resolveContextStrategy("micro")).toBe("micro");
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
    const enumValues = actionEnumValues(bpTool.schema.action);
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

describe("buildMicroGateway", () => {
  function microFixture(): ToolDef[] {
    return [
      categoryTool("blueprint", "Blueprint authoring.", {
        create: { description: "Create a BP", handler: async (_c, p) => ({ created: p.name }) },
        compile: bp("Compile a BP", "compile_blueprint"),
      }, undefined, {}),
      categoryTool("level", "Level actors.", {
        place_actor: bp("Place an actor", "place_actor"),
      }, undefined, {}),
    ];
  }

  const mockBridge = {
    isConnected: true,
    connect: async () => {},
    call: async (method: string, params?: Record<string, unknown>) => ({ bridgeCalled: method, params }),
  };
  const ctxB = { bridge: mockBridge } as unknown as ToolContext;
  const invoke = (gw: ToolDef, params: Record<string, unknown>) =>
    gw.actions.call.handler!(ctxB, { action: "call", ...params });

  it("exposes search alongside the three gateway actions", () => {
    const gw = buildMicroGateway(microFixture());
    expect(gw.name).toBe("tools");
    expect(Object.keys(gw.actions)).toEqual(["search", "list_categories", "describe", "call"]);
  });

  it("list_categories returns every category with a summary", async () => {
    const gw = buildMicroGateway(microFixture());
    const out = (await gw.actions.list_categories.handler!(ctxB, { action: "list_categories" })) as {
      count: number; categories: Array<{ category: string; summary: string }>;
    };
    expect(out.count).toBe(2);
    expect(out.categories.map((c) => c.category)).toEqual(["blueprint", "level"]);
  });

  it("describe lists a category's actions and rejects unknown ones", async () => {
    const gw = buildMicroGateway(microFixture());
    const ok = (await gw.actions.describe.handler!(ctxB, { action: "describe", category: "blueprint" })) as { actions: string[] };
    expect(ok.actions.some((a) => a.startsWith("create:"))).toBe(true);
    const bad = (await gw.actions.describe.handler!(ctxB, { action: "describe", category: "nope" })) as { error?: string };
    expect(bad.error).toBeDefined();
  });

  it("call routes to a handler action", async () => {
    const gw = buildMicroGateway(microFixture());
    const out = await invoke(gw, { category: "blueprint", method: "create", args: { name: "BP_X" } });
    expect(out).toEqual({ created: "BP_X" });
  });

  it("call routes a bridge action through ctx.bridge", async () => {
    const gw = buildMicroGateway(microFixture());
    const out = await invoke(gw, { category: "blueprint", method: "compile", args: { target: "BP_X" } });
    expect(out).toEqual({ bridgeCalled: "compile_blueprint", params: { target: "BP_X" } });
  });

  it("call throws on unknown category or method", async () => {
    const gw = buildMicroGateway(microFixture());
    await expect(invoke(gw, { category: "nope", method: "create" })).rejects.toThrow(/Unknown category/);
    await expect(invoke(gw, { category: "blueprint", method: "nope" })).rejects.toThrow(/Unknown action/);
  });
});

describe("compact discovery for spatial requests", () => {
  const tools = [categoryTool("level", "Spatial tools", {
    nudge_component: bp("Adjust a component. Params: componentName, axisRotation?", "nudge_component"),
    irrelevant: bp("Something else. Params: none", "irrelevant"),
  }, undefined, {
    componentName: z.string().optional(),
    axisRotation: z.object({ axis: z.enum(["forward", "right", "up"]), degrees: z.number() }).optional(),
  })];

  it("uses the same intent ranking in full, lean and micro, without losing plugins", async () => {
    const expected = searchToolGraph(tools, "clockwise").map(({ tool, action, description }) =>
      ({ category: tool, action, description }),
    );
    expect(expected[0].action).toBe("nudge_component");
    for (const discovery of [buildCatalogTool(tools), buildMicroGateway(tools)]) {
      expect(await runAction(discovery, "search", { query: "clockwise" })).toMatchObject({ results: expected });
    }
    expect(await runAction(buildMicroGateway(tools), "search", { query: "absent_word" })).toMatchObject({ count: 0, results: [] });
  });

  it("describes one action with nested arguments without dumping the category", async () => {
    for (const discovery of [buildCatalogTool(tools), buildMicroGateway(tools)]) {
      const result = await runAction(discovery, "describe", { category: "level", method: "nudge_component" }) as any;
      expect(result.action).toBe("nudge_component");
      expect(result.actions).toBeUndefined();
      expect(result.params.find((p: any) => p.name === "axisRotation").properties.axis).toMatchObject({
        required: true, enumValues: ["forward", "right", "up"],
      });
      await expect(runAction(discovery, "describe", { category: "level", method: "missing" })).rejects.toThrow("Unknown action");
    }
  });

  it("retains spatial interpretation and verification guidance in every context mode", () => {
    for (const instructions of [SERVER_INSTRUCTIONS, SERVER_INSTRUCTIONS_LEAN, SERVER_INSTRUCTIONS_MICRO]) {
      expect(instructions).toContain("dryRun=true");
      expect(instructions).toContain("viewRotation");
      expect(instructions).toContain("not visual verification");
    }
  });
});
