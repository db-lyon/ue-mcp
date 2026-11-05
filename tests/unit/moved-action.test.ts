import { describe, expect, it } from "vitest";
import { searchToolGraph } from "../../src/surface/context/tool-search.js";
import { ALL_TOOLS } from "../../src/tools.js";

const tool = (name: string) => ALL_TOOLS.find((t) => t.name === name)!;

describe("moved actions", () => {
  it.each(["list_loaded_modules", "is_module_loaded"])("project(%s) dispatches like reflection(%s)", (action) => {
    const alias = tool("project").actions[action];
    const canonical = tool("reflection").actions[action];
    expect(alias.movedTo).toEqual({ category: "reflection", action });
    expect(canonical.movedTo).toBeUndefined();
    expect(alias.kind).toBe(canonical.kind);
    if (alias.kind === "bridge" && canonical.kind === "bridge") expect(alias.bridge).toBe(canonical.bridge);
    expect(alias.paramSpec).toBe(canonical.paramSpec);
    expect(alias.description?.startsWith(`Deprecated address, removed in the next release: use reflection(${action}).`)).toBe(true);
  });

  it("search names only the canonical address", () => {
    const hits = searchToolGraph(ALL_TOOLS, "is module loaded", 50).map((h) => `${h.tool}.${h.action}`);
    expect(hits).toContain("reflection.is_module_loaded");
    expect(hits).not.toContain("project.is_module_loaded");
    const listHits = searchToolGraph(ALL_TOOLS, "list loaded modules", 50).map((h) => `${h.tool}.${h.action}`);
    expect(listHits).toContain("reflection.list_loaded_modules");
    expect(listHits).not.toContain("project.list_loaded_modules");
  });
});
