/**
 * The surface as the running plugin can serve it (spec 6.4): a bridge action
 * whose method the plugin did not register is withheld from what is advertised
 * and discovered, and nothing is withheld while no list has been published.
 */
import { describe, it, expect } from "vitest";
import {
  absentToolset,
  registeredMethodsOf,
  wrapsEpicTool,
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
import { epicToolsetNames } from "../../src/bridge/bridge.js";

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
    expect(listed.categories.map((c: { category: string }) => c.category)).toEqual(["alpha", "flow"]);

    const unfiltered = await (gateway.actions.list_categories as { handler: Function }).handler(ctxWith(null), {});
    expect(unfiltered.count).toBe(3);
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

describe("epic_* actions whose engine toolset the editor lacks (#1323, #1182)", () => {
  function epicGraph(): ToolDef[] {
    return [
      categoryTool("asset", "Asset", {
        save: bp("mutate", "Save assets to disk. Params: assetPaths?", "save_asset"),
        epic_save_assets: {
          ...bp("mutate", "[Epic Present.AssetTools] Saves assets to disk. Params: asset_paths", "epic_call_tool"),
          epicTool: { toolset: "Absent.AssetTools", name: "Absent.AssetTools.save_assets" },
        },
        epic_list: {
          ...bp("read", "[Epic Present.Tools] Lists things. Params: none", "epic_call_tool"),
          epicTool: { toolset: "Present.Tools", name: "Present.Tools.list" },
        },
      }),
    ];
  }
  const methods = ["save_asset", "epic_call_tool"];

  it("withholds nothing while the toolset list is unknown", () => {
    const view = withholdFromTool(epicGraph()[0], registeredMethodsOf({ registeredActions: methods, epicToolsets: null }))!;
    expect(Object.keys(view.actions)).toEqual(["save", "epic_save_assets", "epic_list"]);
  });

  it("withholds the action whose toolset is not registered, and only that one", () => {
    const have = registeredMethodsOf({ registeredActions: methods, epicToolsets: ["Present.Tools"] });
    expect(absentToolset(epicGraph()[0].actions.epic_save_assets, have)).toBe("Absent.AssetTools");
    const view = withholdFromTool(epicGraph()[0], have)!;
    expect(Object.keys(view.actions)).toEqual(["save", "epic_list"]);
  });

  it("refuses the call naming the toolset and the native action for the job", () => {
    const message = withheldTarget(epicGraph(), "asset.epic_save_assets", { registeredActions: methods, epicToolsets: [] });
    expect(message).toContain("'Absent.AssetTools'");
    expect(message).toContain("asset(save)");
    expect(message).toContain("epic(list_toolsets)");
    expect(withheldTarget(epicGraph(), "asset.epic_list", { registeredActions: methods, epicToolsets: ["Present.Tools"] })).toBeNull();
    expect(wrapsEpicTool(epicGraph(), "asset.epic_list")).toBe(true);
    expect(wrapsEpicTool(epicGraph(), "asset.save")).toBe(false);
  });

  it("judges a toolset absent across editors only when every editor's list is known", () => {
    const known = { registeredActions: methods, epicToolsets: ["Present.Tools"] };
    const unknown = { registeredActions: methods, epicToolsets: null };
    const spec = epicGraph()[0].actions.epic_save_assets;
    expect(absentToolset(spec, registeredUnion([known, unknown]))).toBeUndefined();
    expect(absentToolset(spec, registeredUnion([known, { ...known }]))).toBe("Absent.AssetTools");
  });

  it("reads toolset names off epic_list_toolsets, an absent registry as none", () => {
    expect(epicToolsetNames({ success: true, toolsets: [{ name: "B" }, { name: "A" }] })).toEqual(["A", "B"]);
    expect(epicToolsetNames({ success: false, error: "Epic ToolsetRegistry not available. Requires UE 5.8+" })).toEqual([]);
    expect(epicToolsetNames({ success: false, reason: "not_ready" })).toBeNull();
    expect(epicToolsetNames({ success: true })).toBeNull();
  });
});

describe("the shipped epic_* wrappers named in #1323 and #1182", () => {
  it("point at the native action that does the job", async () => {
    const { ALL_TOOLS } = await import("../../src/tools.js");
    const methods = [...new Set(ALL_TOOLS.flatMap((t) => Object.values(t.actions))
      .flatMap((s) => (s.kind === "bridge" ? [s.bridge] : [])))];
    const bridge = { registeredActions: methods, epicToolsets: [] };
    expect(withheldTarget(ALL_TOOLS, "asset.epic_save_assets", bridge)).toContain("asset(save)");
    expect(withheldTarget(ALL_TOOLS, "animation.epic_add_socket", bridge)).toContain("asset(add_socket)");
  });
});
