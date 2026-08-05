/**
 * Per-session tool graphs (#817, plan items 4.1 and 4.2).
 *
 * The defect these cover: enrichment mutates ToolDefs in place, so while every
 * editor shared one graph, a toolset present in one project's engine appeared
 * on every editor, and a call to it in a project that does not have it reached
 * that project's bridge instead of being refused.
 */
import { describe, expect, it } from "vitest";
import { z } from "zod";
import { categoryTool, cloneToolGraph, bp, type ToolDef } from "../../src/types.js";
import {
  baseGraphFor,
  unionSurface,
  unionKnowledge,
  explainMissingAction,
  type SessionSurface,
} from "../../src/session-surface.js";
import { enrichToolsWithEpicCatalog, type EpicCatalog } from "../../src/epic-enrich.js";
import { ALL_TOOLS } from "../../src/tools.js";

function graph(): ToolDef[] {
  return [
    categoryTool("alpha", "Alpha category", {
      list: bp("List things", "alpha_list"),
    }),
    categoryTool("beta", "Beta category", {
      read: bp("Read a thing", "beta_read"),
    }),
  ];
}

function surfaceOf(name: string, tools: ToolDef[], disabled: string[] = []): SessionSurface {
  return {
    session: { name } as SessionSurface["session"],
    tools,
    disabled: new Set(disabled),
    pluginRecords: [],
    knowledgeByCategory: {},
  };
}

function catalogWith(toolset: string, toolName: string): EpicCatalog {
  return {
    toolsets: [
      {
        name: toolset,
        tools: [{ name: `${toolset}.${toolName}`, description: `Does ${toolName}` }],
      },
    ],
  } as EpicCatalog;
}

describe("cloneToolGraph", () => {
  it("gives each copy its own actions, schema and description", () => {
    const original = graph();
    const copy = cloneToolGraph(original);

    copy[0].actions.injected = { description: "only on the copy" };
    copy[0].description += " (copy)";
    copy[0].schema.extra = z.string().optional();

    expect(Object.keys(original[0].actions)).toEqual(["list"]);
    expect(original[0].description).not.toContain("(copy)");
    expect("extra" in original[0].schema).toBe(false);
  });

  it("makes the copy's handler dispatch against the copy's actions", async () => {
    const original = graph();
    const copy = cloneToolGraph(original);
    const calls: string[] = [];
    copy[0].actions.only_on_copy = {
      handler: async () => {
        calls.push("copy");
        return "ok";
      },
    };

    await copy[0].handler({ bridge: {} as never, project: {} as never }, { action: "only_on_copy" });
    expect(calls).toEqual(["copy"]);

    // The original still refuses the action it never gained.
    await expect(
      original[0].handler({ bridge: {} as never, project: {} as never }, { action: "only_on_copy" }),
    ).rejects.toThrow(/Unknown action/);
  });

  it("clones the real tool graph without losing an action", () => {
    const copy = baseGraphFor(ALL_TOOLS);
    expect(copy.length).toBe(ALL_TOOLS.length);
    for (let i = 0; i < ALL_TOOLS.length; i++) {
      expect(copy[i].name).toBe(ALL_TOOLS[i].name);
      expect(Object.keys(copy[i].actions).sort()).toEqual(Object.keys(ALL_TOOLS[i].actions).sort());
    }
    expect(copy[0]).not.toBe(ALL_TOOLS[0]);
  });
});

describe("per-session Epic enrichment", () => {
  it("keeps one project's toolset off the other project's graph", () => {
    // The real graph, because routing sends a toolset to a named category and
    // falls back to the `epic` umbrella, neither of which a synthetic graph has.
    const a = baseGraphFor(ALL_TOOLS);
    const b = baseGraphFor(ALL_TOOLS);

    enrichToolsWithEpicCatalog(a, catalogWith("Niagara", "spawn"), {});
    enrichToolsWithEpicCatalog(b, catalogWith("Landscape", "sculpt"), {});

    const epicActionsIn = (g: ToolDef[]) =>
      g.flatMap((t) => Object.keys(t.actions)).filter((k) => k.startsWith("epic_"));

    const aEpic = epicActionsIn(a);
    const bEpic = epicActionsIn(b);
    expect(aEpic.length).toBeGreaterThan(0);
    expect(bEpic.length).toBeGreaterThan(0);
    expect(aEpic.some((k) => k.includes("spawn"))).toBe(true);
    expect(aEpic.some((k) => k.includes("sculpt"))).toBe(false);
    expect(bEpic.some((k) => k.includes("sculpt"))).toBe(true);
    expect(bEpic.some((k) => k.includes("spawn"))).toBe(false);
  });

  it("leaves the pristine declaration untouched", () => {
    const before = ALL_TOOLS.map((t) => Object.keys(t.actions).length);
    const session = baseGraphFor(ALL_TOOLS);
    enrichToolsWithEpicCatalog(session, catalogWith("Niagara", "spawn"), {});
    const after = ALL_TOOLS.map((t) => Object.keys(t.actions).length);
    expect(after).toEqual(before);
  });
});

describe("unionSurface", () => {
  it("advertises the union and records which editor provides each action", () => {
    const a = graph();
    const b = graph();
    b[0].actions.beta_only = { description: "only in B" };

    const union = unionSurface([surfaceOf("Alpha", a), surfaceOf("Beta", b)]);
    const alphaCategory = union.tools.find((t) => t.name === "alpha")!;

    expect(Object.keys(alphaCategory.actions).sort()).toEqual(["beta_only", "list"]);
    expect(union.providers.get("alpha.list")!.providers).toEqual(["Alpha", "Beta"]);
    expect(union.providers.get("alpha.beta_only")!.providers).toEqual(["Beta"]);
  });

  it("does not fold the union back into either session's own graph", () => {
    const a = graph();
    const b = graph();
    b[0].actions.beta_only = { description: "only in B" };

    unionSurface([surfaceOf("Alpha", a), surfaceOf("Beta", b)]);

    expect(Object.keys(a[0].actions)).toEqual(["list"]);
  });

  it("rebuilds the action enum so a merged action is accepted", () => {
    const a = graph();
    const b = graph();
    b[0].actions.beta_only = { description: "only in B" };

    const union = unionSurface([surfaceOf("Alpha", a), surfaceOf("Beta", b)]);
    const enumSchema = union.tools.find((t) => t.name === "alpha")!.schema.action;
    expect(enumSchema.safeParse("beta_only").success).toBe(true);
  });

  it("keeps a category one editor disabled in the union and records where", () => {
    const union = unionSurface([
      surfaceOf("Alpha", graph(), ["beta"]),
      surfaceOf("Beta", graph()),
    ]);
    expect(union.tools.map((t) => t.name)).toContain("beta");
    expect(union.providers.get("beta.read")!.disabledIn).toEqual(["Alpha"]);
  });
});

describe("explainMissingAction", () => {
  it("names the editors that provide an action the addressed one lacks", () => {
    const a = graph();
    const b = graph();
    b[0].actions.beta_only = { description: "only in B" };
    const union = unionSurface([surfaceOf("Alpha", a), surfaceOf("Beta", b)]);

    const msg = explainMissingAction(union, "alpha.beta_only", "Alpha", false);
    expect(msg).toContain("not available in editor 'Alpha'");
    expect(msg).toContain("Beta");
  });

  it("refuses a disabled category for the editor that disabled it, naming the config", () => {
    const union = unionSurface([
      surfaceOf("Alpha", graph(), ["beta"]),
      surfaceOf("Beta", graph()),
    ]);
    const msg = explainMissingAction(union, "beta.read", "Alpha", true);
    expect(msg).toContain("disabled for editor 'Alpha'");
    expect(msg).toContain("ue-mcp.yml");
    expect(msg).toContain("Beta");
  });

  it("says nothing when the addressed editor can serve the action", () => {
    const union = unionSurface([surfaceOf("Alpha", graph()), surfaceOf("Beta", graph())]);
    expect(explainMissingAction(union, "alpha.list", "Alpha", true)).toBeNull();
  });
});

describe("unionKnowledge", () => {
  it("merges each editor's plugin knowledge and de-duplicates shared blobs", () => {
    const a = surfaceOf("Alpha", graph());
    a.knowledgeByCategory = { alpha: ["shared", "from A"] };
    const b = surfaceOf("Beta", graph());
    b.knowledgeByCategory = { alpha: ["shared"], beta: ["from B"] };

    expect(unionKnowledge([a, b])).toEqual({
      alpha: ["shared", "from A"],
      beta: ["from B"],
    });
  });
});
