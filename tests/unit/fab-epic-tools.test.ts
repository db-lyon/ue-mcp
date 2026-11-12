/**
 * Epic's built-in Fab tools stay reachable through ue-mcp, and nothing ue-mcp
 * says contradicts them.
 *
 * UE 5.9's Fab plugin carries a toolset (module FabToolset) that searches the
 * Fab catalog and imports listings. Before #1915 it routed to the epic
 * category, fab(open_search) told the agent "Fab has no search API and its
 * terms forbid automated access", and an agent concluded the editor could not
 * search Fab at all.
 *
 * The names and schemas below are what Fab UE Plugin 0.0.22 registers on UE
 * 5.9, read from a live editor's epic_list_toolsets: one toolset,
 * FabToolset.FabToolset, with five tools.
 */
import { describe, expect, it } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { EPIC_TOOLSETS_BY_CATEGORY, routeEpicToolset } from "../../src/tools/epic/toolset-routes.js";
import { isFabToolName, listEpicFabTools } from "../../src/tools/fab-epic.js";
import { microGuide, SERVER_INSTRUCTIONS, SERVER_INSTRUCTIONS_LEAN } from "../../src/surface/context/instructions.js";
import type { ToolContext } from "../../src/core/types.js";

type Answer = unknown | ((params: Record<string, unknown>) => unknown);

/** The toolset and tools Fab UE Plugin 0.0.22 registers on UE 5.9, in the order the live registry lists them. */
const FAB_TOOLSET = "FabToolset.FabToolset";
const FAB_TOOLS = [
  "FabToolset.FabToolset.SearchFabMyLibrary",
  "FabToolset.FabToolset.SearchFabListings",
  "FabToolset.FabToolset.InstallFabListing",
  "FabToolset.FabToolset.GetFabListingDetails",
  "FabToolset.FabToolset.FabSignIn",
];
/** The fab epic_* action generated from the recorded 5.9 catalog for each one. */
const FAB_ACTIONS: Record<string, string> = {
  "FabToolset.FabToolset.SearchFabMyLibrary": "epic_search_fab_my_library",
  "FabToolset.FabToolset.SearchFabListings": "epic_search_fab_listings",
  "FabToolset.FabToolset.InstallFabListing": "epic_install_fab_listing",
  "FabToolset.FabToolset.GetFabListingDetails": "epic_get_fab_listing_details",
  "FabToolset.FabToolset.FabSignIn": "epic_fab_sign_in",
};
/** SearchFabListings' live input schema, abridged to its property types. */
const SEARCH_SCHEMA = {
  type: "object",
  properties: {
    keywords: { type: "string", default: "" },
    sortBy: { type: "string", default: "" },
    bFreeOnly: { type: "boolean", default: false },
    assetFormats: { type: "string", default: "unreal-engine,glb,gltf,fbx,converted-files,metahuman" },
    count: { type: "integer", minimum: 1, maximum: 24, default: 12 },
    cursor: { type: "string", default: "" },
    listingTypes: { type: "string", default: "" },
    categories: { type: "string", default: "" },
    sellers: { type: "string", default: "" },
    excludedSellers: { type: "string", default: "" },
    licenses: { type: "string", default: "" },
  },
  additionalProperties: false,
};

function ctxWith(answers: Record<string, Answer>): { ctx: ToolContext; calls: Array<[string, Record<string, unknown>]> } {
  const calls: Array<[string, Record<string, unknown>]> = [];
  const bridge = {
    call: async (method: string, params: Record<string, unknown> = {}) => {
      calls.push([method, params]);
      if (!(method in answers)) throw new Error(`Unknown method: ${method}`);
      const a = answers[method];
      if (a instanceof Error) throw a;
      return typeof a === "function" ? (a as (p: Record<string, unknown>) => unknown)(params) : a;
    },
  };
  return { ctx: { bridge } as unknown as ToolContext, calls };
}

describe("routing a Fab toolset", () => {
  it("places the Fab plugin's toolset and each of its tools in fab", () => {
    for (const name of [FAB_TOOLSET, ...FAB_TOOLS]) {
      expect(routeEpicToolset(name), name).toBe("fab");
      expect(isFabToolName(name), name).toBe(true);
    }
  });

  it("routes the generated fab actions from the recorded 5.9 catalog", () => {
    expect(EPIC_TOOLSETS_BY_CATEGORY.fab).toEqual([FAB_TOOLSET]);
    const fab = ALL_TOOLS.find((t) => t.name === "fab")!;
    for (const tool of FAB_TOOLS) {
      const spec = fab.actions[FAB_ACTIONS[tool]] as { epicTool?: { toolset: string; name: string; engines: string[] } };
      expect(spec?.epicTool, tool).toEqual({ toolset: FAB_TOOLSET, name: tool, engines: ["5.9"] });
    }
    expect(fab.actions.epic_install_fab_listing.effect).toBe("mutate");
    expect(fab.actions.epic_fab_sign_in.effect).toBe("mutate");
    expect(fab.actions.epic_search_fab_listings.effect).toBe("read");
  });

  it("would still route a toolset a later plugin version names differently", () => {
    for (const name of ["Fab.FabToolset", "FabToolset.FabAssetTools", "fab_toolset.toolsets.fab.FabTools"]) {
      expect(routeEpicToolset(name), name).toBe("fab");
      expect(isFabToolName(name), name).toBe(true);
    }
  });

  it("leaves names that only contain the letters alone", () => {
    expect(routeEpicToolset("PlayFabParty.PlayFabToolset")).not.toBe("fab");
    expect(routeEpicToolset("Fabric.FabricToolset")).not.toBe("fab");
    expect(routeEpicToolset("EditorToolset.PrefabToolset")).not.toBe("fab");
    expect(routeEpicToolset("EditorToolset.AssetToolset")).toBe("asset");
  });
});

describe("fab(list_epic_tools)", () => {
  it("is declared on fab as an in-process read", () => {
    const fab = ALL_TOOLS.find((t) => t.name === "fab")!;
    expect(fab.actions.list_epic_tools).toMatchObject({ kind: "handler", effect: "read" });
    expect(fab.schema.includeSchemas).toBeDefined();
  });

  it("lists the editor's Fab toolsets and Fab MCP tools with the call that runs each", async () => {
    const { ctx, calls } = ctxWith({
      epic_list_toolsets: {
        success: true,
        toolsets: [
          { name: "EditorToolset.AssetToolset", tools: ["EditorToolset.AssetToolset.ListAssets"] },
          { name: FAB_TOOLSET, description: "Searches Fab, reads listing details, and installs assets and code plugins", tools: FAB_TOOLS },
        ],
      },
      epic_list_mcp_tools: {
        success: true,
        available: true,
        tools: [{ name: "fab_search", description: "Search Fab", source: "plugin" }, { name: "other_tool", source: "plugin" }],
      },
    });
    const out = (await listEpicFabTools.handler(ctx, { includeSchemas: true })) as Record<string, any>;
    expect(calls).toEqual([
      ["epic_list_toolsets", { includeSchemas: true }],
      ["epic_list_mcp_tools", { includeSchemas: true }],
    ]);
    expect(out.available).toBe(true);
    expect(out.toolCount).toBe(FAB_TOOLS.length + 1);
    expect(out.toolsets).toHaveLength(1);
    expect(out.toolsets[0].name).toBe(FAB_TOOLSET);
    expect(out.toolsets[0].tools.map((t: any) => t.name)).toEqual(FAB_TOOLS);
    // epic_call_tool refuses a call without its toolset ("Missing required parameter 'toolset'"), so the call names it.
    expect(out.toolsets[0].tools[1].call).toBe(
      'epic(action="call_tool", args={toolset: "FabToolset.FabToolset", tool: "FabToolset.FabToolset.SearchFabListings", input: {...}})',
    );
    for (const tool of out.toolsets[0].tools) {
      expect(tool.action, tool.name).toBe(`fab(action="${FAB_ACTIONS[tool.name]}", args={...})`);
    }
    expect(out.mcpTools.map((t: any) => t.name)).toEqual(["fab_search"]);
    expect(out.mcpTools[0].call).toContain('epic(action="call_mcp_tool"');
    expect(out.note).toBeUndefined();
  });

  it("passes full tool objects through when the editor sends schemas", async () => {
    const tool = {
      name: "FabToolset.FabToolset.SearchFabListings",
      inputSchema: SEARCH_SCHEMA,
      outputSchema: { type: "object", properties: { returnValue: { type: "string" } }, required: ["returnValue"] },
      annotations: { accessMode: "read" },
    };
    const { ctx } = ctxWith({
      epic_list_toolsets: { success: true, toolsets: [{ name: FAB_TOOLSET, tools: [tool] }] },
      epic_list_mcp_tools: { success: true, available: true, tools: [] },
    });
    const out = (await listEpicFabTools.handler(ctx, { includeSchemas: true })) as Record<string, any>;
    expect(out.toolsets[0].tools[0]).toMatchObject({ ...tool, action: 'fab(action="epic_search_fab_listings", args={...})' });
  });

  it("names no fab action the session has withheld, and still gives the call", async () => {
    const { ctx } = ctxWith({
      epic_list_toolsets: { success: true, toolsets: [{ name: FAB_TOOLSET, tools: FAB_TOOLS }] },
      epic_list_mcp_tools: { success: true, available: true, tools: [] },
    });
    const fab = ALL_TOOLS.find((t) => t.name === "fab")!;
    const ownOnly = { ...fab, actions: Object.fromEntries(Object.entries(fab.actions).filter(([k]) => !k.startsWith("epic_"))) };
    const out = (await listEpicFabTools.handler({ ...ctx, getToolGraph: () => [ownOnly] } as ToolContext, {})) as Record<string, any>;
    for (const tool of out.toolsets[0].tools) {
      expect(tool.action, tool.name).toBeUndefined();
      expect(tool.call).toContain(`toolset: "${FAB_TOOLSET}"`);
    }
  });

  it("names no fab action for a Fab tool the recorded catalog does not have, and still gives its call", async () => {
    const { ctx } = ctxWith({
      epic_list_toolsets: { success: true, toolsets: [{ name: FAB_TOOLSET, tools: ["FabToolset.FabToolset.SomeLaterTool"] }] },
      epic_list_mcp_tools: { success: true, available: true, tools: [] },
    });
    const out = (await listEpicFabTools.handler(ctx, {})) as Record<string, any>;
    expect(out.toolsets[0].tools[0].action).toBeUndefined();
    expect(out.toolsets[0].tools[0].call).toContain('toolset: "FabToolset.FabToolset", tool: "FabToolset.FabToolset.SomeLaterTool"');
  });

  it("says where Epic's Fab tools come from when the editor has none, and why each list is missing", async () => {
    const { ctx } = ctxWith({
      epic_list_toolsets: { success: true, toolsets: [{ name: "EditorToolset.AssetToolset", tools: [] }] },
      epic_list_mcp_tools: { success: true, available: false, reason: "ModelContextProtocol plugin is not enabled" },
    });
    const out = (await listEpicFabTools.handler(ctx, {})) as Record<string, any>;
    expect(out.available).toBe(false);
    expect(out.toolCount).toBe(0);
    expect(out.mcpToolsError).toMatch(/ModelContextProtocol/);
    expect(out.registryError).toBeUndefined();
    expect(out.note).toMatch(/Fab UE Plugin/);
    expect(out.note).toMatch(/FabToolset/);
    expect(out.note).toMatch(/fab\(status\)/);
  });

  it("reports a registry refusal rather than an empty list", async () => {
    const { ctx } = ctxWith({
      epic_list_toolsets: { success: false, error: "ToolsetRegistry plugin is not loaded" },
      epic_list_mcp_tools: new Error("Unknown method: epic_list_mcp_tools"),
    });
    const out = (await listEpicFabTools.handler(ctx, {})) as Record<string, any>;
    expect(out.available).toBe(false);
    expect(out.registryError).toMatch(/ToolsetRegistry/);
    expect(out.mcpToolsError).toMatch(/Unknown method/);
  });

  it("throws when no editor answered at all", async () => {
    const down = new Error("Not connected to the editor");
    const { ctx } = ctxWith({ epic_list_toolsets: down, epic_list_mcp_tools: down });
    await expect(listEpicFabTools.handler(ctx, {})).rejects.toThrow(/Not connected/);
  });
});

describe("nothing ue-mcp says contradicts Epic's built-in Fab tools", () => {
  const fab = ALL_TOOLS.find((t) => t.name === "fab")!;
  // What ue-mcp says. The epic_* actions carry Epic's own tool descriptions, which say a seller can
  // forbid AI training on a listing; that is Epic's text about a listing, not a claim about search.
  // The category description folds every action's description in, so Epic's are cut out of it too.
  const epicTexts = Object.entries(fab.actions).filter(([key]) => key.startsWith("epic_")).map(([, a]) => a.description ?? "");
  const said = [
    epicTexts.reduce((text, epic) => (epic ? text.split(epic).join("") : text), fab.description),
    ...Object.entries(fab.actions).filter(([key]) => !key.startsWith("epic_")).map(([, a]) => a.description ?? ""),
  ].join("\n");

  it("never tells the agent Fab search is impossible or forbidden", () => {
    expect(said).not.toMatch(/no search API|forbid|does not search the public catalog|no way/i);
  });

  it("points catalog search at Epic's tools from the category and from open_search and list_owned", () => {
    expect(fab.description).toMatch(/list_epic_tools/);
    expect(fab.actions.open_search.description).toMatch(/list_epic_tools/);
    expect(fab.actions.list_owned.description).toMatch(/list_epic_tools/);
  });

  it("tells every context strategy that Unreal's own toolsets stay reachable", () => {
    for (const text of [SERVER_INSTRUCTIONS, SERVER_INSTRUCTIONS_LEAN, microGuide(ALL_TOOLS)]) {
      expect(text).toMatch(/UNREAL'S BUILT-IN TOOLS/);
      expect(text).toMatch(/epic\(call_tool\)/);
      expect(text).toMatch(/fab\(list_epic_tools\)/);
    }
  });

  it("says in the epic category that an unrecorded toolset still runs through call_tool", () => {
    const epic = ALL_TOOLS.find((t) => t.name === "epic")!;
    expect(epic.description).toMatch(/no epic_\* action but is still listed by list_toolsets and runs through call_tool/);
  });
});
