/**
 * Caller-controlled field selection.
 *
 * The property that matters most here is that a bad selection degrades to the
 * full result with an explanation, never to an empty one. An agent that asked
 * for `component.name` on a result keyed `components` must not conclude the
 * editor returned nothing.
 */
import { describe, it, expect } from "vitest";
import { projectResult, takeFieldSelection, attachFieldReport } from "../../src/field-select.js";
import { categoryTool, SELECT_PARAM, OMIT_PARAM, TIMEOUT_PARAM, type ToolContext } from "../../src/types.js";
import { ALL_TOOLS } from "../../src/tools.js";

const TREE = {
  actorLabel: "BP_Player",
  components: [
    { name: "Root", class: "SceneComponent", transform: { x: 1, y: 2, z: 3 } },
    { name: "Mesh", class: "SkeletalMeshComponent", transform: { x: 4, y: 5, z: 6 } },
  ],
  world: "editor",
};

describe("projectResult: select", () => {
  it("keeps a top-level field and drops the rest", () => {
    const { result, applied } = projectResult(TREE, { select: ["actorLabel"] });
    expect(result).toEqual({ actorLabel: "BP_Player" });
    expect(applied).toBe(true);
  });

  it("traverses arrays without needing bracket syntax", () => {
    const { result } = projectResult(TREE, { select: ["components.name"] });
    expect(result).toEqual({ components: [{ name: "Root" }, { name: "Mesh" }] });
  });

  it("accepts the bracket spelling as the same thing", () => {
    const bare = projectResult(TREE, { select: ["components.name"] }).result;
    const bracketed = projectResult(TREE, { select: ["components[].name"] }).result;
    expect(bracketed).toEqual(bare);
  });

  it("merges two paths into the same array into one array of objects", () => {
    // The reason the filter is a single pass rather than pick-and-merge: two
    // independently picked arrays cannot produce this.
    const { result } = projectResult(TREE, { select: ["components.name", "components.class"] });
    expect(result).toEqual({
      components: [
        { name: "Root", class: "SceneComponent" },
        { name: "Mesh", class: "SkeletalMeshComponent" },
      ],
    });
  });

  it("keeps a whole subtree when the path stops above it", () => {
    const { result } = projectResult(TREE, { select: ["components.transform"] });
    expect(result).toEqual({
      components: [{ transform: { x: 1, y: 2, z: 3 } }, { transform: { x: 4, y: 5, z: 6 } }],
    });
  });

  it("reaches into a nested object", () => {
    const { result } = projectResult(TREE, { select: ["components.transform.x"] });
    expect(result).toEqual({ components: [{ transform: { x: 1 } }, { transform: { x: 4 } }] });
  });
});

describe("projectResult: omit", () => {
  it("drops a top-level field and keeps the rest", () => {
    const { result } = projectResult(TREE, { omit: ["components"] });
    expect(result).toEqual({ actorLabel: "BP_Player", world: "editor" });
  });

  it("drops a field from every element of an array", () => {
    const { result } = projectResult(TREE, { omit: ["components.transform"] }) as { result: typeof TREE };
    expect(result.components).toEqual([
      { name: "Root", class: "SceneComponent" },
      { name: "Mesh", class: "SkeletalMeshComponent" },
    ]);
    expect(result.actorLabel).toBe("BP_Player");
  });

  it("runs after select, so keeping a subtree and dropping one field inside it works", () => {
    const { result } = projectResult(TREE, {
      select: ["components"],
      omit: ["components.transform"],
    });
    expect(result).toEqual({
      components: [
        { name: "Root", class: "SceneComponent" },
        { name: "Mesh", class: "SkeletalMeshComponent" },
      ],
    });
  });
});

describe("projectResult: paths that do not match", () => {
  it("reports a misspelled path rather than ignoring it", () => {
    const { notFound } = projectResult(TREE, { select: ["actorLabel", "compnents.name"] });
    expect(notFound).toEqual(["compnents.name"]);
  });

  it("returns the FULL result when nothing matched at all", () => {
    // An empty object here would read as an empty answer from the editor
    // rather than as a filter that did not fit.
    const { result, applied, notFound } = projectResult(TREE, { select: ["nope.nothing"] });
    expect(result).toBe(TREE);
    expect(applied).toBe(false);
    expect(notFound).toEqual(["nope.nothing"]);
  });

  it("leaves a result alone when no selection was asked for", () => {
    const { result, applied } = projectResult(TREE, {});
    expect(result).toBe(TREE);
    expect(applied).toBe(false);
  });

  it("does not try to filter a scalar result", () => {
    const { result, applied } = projectResult("done", { select: ["anything"] });
    expect(result).toBe("done");
    expect(applied).toBe(false);
  });
});

describe("takeFieldSelection", () => {
  it("accepts a bare string as a single path", () => {
    const { selection, rest } = takeFieldSelection({ select: "a.b", action: "go" });
    expect(selection.select).toEqual(["a.b"]);
    expect(rest).toEqual({ action: "go" });
  });

  it("strips both parameters so neither can reach a bridge call", () => {
    const { rest } = takeFieldSelection({ select: ["a"], omit: ["b"], assetPath: "/Game/X" });
    expect(rest).toEqual({ assetPath: "/Game/X" });
  });

  it("ignores an empty or non-string selection", () => {
    const { selection } = takeFieldSelection({ select: [], omit: [42] });
    expect(selection.select).toBeUndefined();
    expect(selection.omit).toBeUndefined();
  });
});

describe("attachFieldReport", () => {
  it("says nothing when every path matched", () => {
    const result = { a: 1 };
    expect(attachFieldReport(result, { result, notFound: [], applied: true })).toBe(result);
  });

  it("names the paths that matched nothing", () => {
    const out = attachFieldReport({ a: 1 }, { result: { a: 1 }, notFound: ["b"], applied: true }) as Record<string, unknown>;
    expect((out.fieldsNotFound as { paths: string[] }).paths).toEqual(["b"]);
  });
});

describe("dispatch integration", () => {
  const ctx = {} as ToolContext;
  const tool = categoryTool("probe", "test", {
    go: { kind: "handler", effect: "read", description: "Params: none", handler: async () => TREE },
    echo: { kind: "handler", effect: "read", description: "Params: none", handler: async (_c, p) => ({ seen: Object.keys(p).sort() }) },
  });

  it("applies a selection to a handler result", async () => {
    const out = await tool.handler(ctx, { action: "go", select: ["actorLabel"] });
    expect(out).toEqual({ actorLabel: "BP_Player" });
  });

  it("never forwards the routing parameters to the handler", async () => {
    // `action` does reach a custom handler, which is long-standing behaviour;
    // the three the dispatcher consumes must not.
    const out = await tool.handler(ctx, { action: "echo", select: ["seen"], omit: ["nothing"], timeoutMs: 5 }) as { seen: string[] };
    expect(out.seen).toEqual(["action"]);
  });

  it("keeps the path repair report visible through a narrow selection", async () => {
    // A select that does not name pathsRepaired must not filter away the
    // account of a parameter the server rewrote.
    const pathTool = categoryTool("probe", "test", {
      go: { kind: "handler", effect: "read", description: "Params: assetPath", handler: async (_c, p) => ({ used: p.assetPath, extra: 1 }) },
    });
    const out = await pathTool.handler(ctx, {
      action: "go",
      assetPath: "\\Game\\A",
      select: ["used"],
    }) as Record<string, unknown>;
    expect(out.used).toBe("/Game/A");
    expect(out.extra).toBeUndefined();
    expect(out.pathsRepaired).toBeDefined();
  });
});

describe("routing parameters are not shadowed", () => {
  it("no category declares a parameter name that dispatch consumes", () => {
    // A category's extraSchema is spread AFTER the routing parameters, so a
    // category declaring one of these would replace its published schema while
    // dispatch went on stripping the value: the handler would never receive it,
    // and the manifest would promise that it does.
    //
    // Compared by identity against the exported declarations rather than by
    // their prose, so rewording a description cannot fail this.
    const routing: Array<[string, unknown]> = [
      ["timeoutMs", TIMEOUT_PARAM],
      ["select", SELECT_PARAM],
      ["omit", OMIT_PARAM],
    ];
    const clashes: string[] = [];
    for (const tool of ALL_TOOLS) {
      for (const [param, declaration] of routing) {
        if (tool.schema[param] !== declaration) clashes.push(`${tool.name}.${param}`);
      }
    }
    expect(clashes).toEqual([]);
  });
});
