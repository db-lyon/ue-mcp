/**
 * The surface as the running plugin can serve it (spec 6.4): a bridge action
 * whose method the plugin did not register is withheld from what is advertised
 * and discovered, and nothing is withheld while no list has been published.
 */
import { describe, it, expect } from "vitest";
import {
  registeredSet,
  registeredUnion,
  withheldTarget,
  withholdFromTool,
  withholdUnregistered,
} from "../../src/surface/registered-surface.js";
import { actionEnumValues, bp, categoryTool } from "../../src/surface/category-tool.js";
import { buildMicroGateway } from "../../src/surface/context/micro-context.js";
import { applyLeanContext } from "../../src/surface/context/lean-context.js";
import type { ToolContext, ToolDef } from "../../src/core/types.js";

function graph(): ToolDef[] {
  return [
    categoryTool("alpha", "Alpha", {
      list: bp("read", "List things. Params: none", "alpha_list"),
      save: bp("mutate", "Save a thing. Params: none", "alpha_save"),
      local: { kind: "handler", effect: "read", description: "Params: none", handler: async () => ({}) },
    }),
    categoryTool("beta", "Beta", {
      read: bp("read", "Read a thing. Params: none", "beta_read"),
    }),
  ];
}

const ctxWith = (registeredActions: string[] | null): ToolContext =>
  ({ bridge: { registeredActions } } as unknown as ToolContext);

describe("withholding unregistered bridge actions", () => {
  it("withholds nothing while no list has been published", () => {
    const tools = graph();
    expect(registeredSet(null)).toBeNull();
    expect(registeredSet([])).toBeNull();
    expect(withholdFromTool(tools[0], null)).toBe(tools[0]);
    expect(withholdUnregistered(tools, null)).toEqual(tools);
  });

  it("returns the same tool when the plugin has every method", () => {
    const tools = graph();
    expect(withholdFromTool(tools[0], new Set(["alpha_list", "alpha_save"]))).toBe(tools[0]);
  });

  it("drops the action, its enum value and its catalog line", () => {
    const view = withholdFromTool(graph()[0], new Set(["alpha_list"]))!;
    expect(Object.keys(view.actions)).toEqual(["list", "local"]);
    expect(actionEnumValues(view.schema.action)).toEqual(["list", "local"]);
    expect(view.description).toContain("- list:");
    expect(view.description).not.toContain("- save:");
  });

  it("keeps handler actions, which have no bridge method to check", () => {
    const view = withholdFromTool(graph()[0], new Set(["unrelated"]))!;
    expect(Object.keys(view.actions)).toEqual(["local"]);
  });

  it("drops a category with nothing left", () => {
    const visible = withholdUnregistered(graph(), new Set(["alpha_list"]));
    expect(visible.map((t) => t.name)).toEqual(["alpha"]);
  });

  it("does not filter across editors while any one has published no list", () => {
    expect(registeredUnion([{ registeredActions: ["a"] }, { registeredActions: null }])).toBeNull();
    expect([...registeredUnion([{ registeredActions: ["a"] }, { registeredActions: ["b"] }])!].sort()).toEqual(["a", "b"]);
  });
});

describe("refusing a withheld call", () => {
  it("names the method and the remedy", () => {
    const message = withheldTarget(graph(), "alpha.save", { registeredActions: ["alpha_list"] });
    expect(message).toContain("'alpha_save'");
    expect(message).toContain("ue-mcp update");
  });

  it("lets registered, handler and unfiltered calls through", () => {
    const bridge = { registeredActions: ["alpha_list"] };
    expect(withheldTarget(graph(), "alpha.list", bridge)).toBeNull();
    expect(withheldTarget(graph(), "alpha.local", bridge)).toBeNull();
    expect(withheldTarget(graph(), "alpha.save", { registeredActions: null })).toBeNull();
  });
});

describe("discovery reads the registered surface in every strategy", () => {
  it("micro: search, describe and list_categories skip withheld actions", async () => {
    const gateway = buildMicroGateway(graph());
    const ctx = ctxWith(["alpha_list"]);
    const search = await (gateway.actions.search as { handler: Function }).handler(ctx, { query: "thing" });
    expect(JSON.stringify(search)).not.toContain("save");
    expect(JSON.stringify(search)).toContain("alpha.list");

    const describeAlpha = await (gateway.actions.describe as { handler: Function }).handler(ctx, { category: "alpha" });
    expect(describeAlpha.count).toBe(2);

    const listed = await (gateway.actions.list_categories as { handler: Function }).handler(ctx, {});
    expect(listed.categories.map((c: { category: string }) => c.category)).toEqual(["alpha"]);

    const unfiltered = await (gateway.actions.list_categories as { handler: Function }).handler(ctxWith(null), {});
    expect(unfiltered.count).toBe(2);
  });

  it("lean: the catalog and a category's describe skip withheld actions", async () => {
    const [catalog, alpha] = applyLeanContext(graph());
    const ctx = ctxWith(["alpha_list"]);
    const described = await (catalog.actions.describe as { handler: Function }).handler(ctx, { category: "alpha" });
    expect(described.count).toBe(2);
    const own = await (alpha.actions.describe as { handler: Function }).handler(ctx, {});
    expect(own.signatures.join("\n")).not.toContain("save");
  });
});
