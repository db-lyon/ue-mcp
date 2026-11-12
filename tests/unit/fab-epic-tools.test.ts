/**
 * Epic's built-in Fab tools stay reachable through ue-mcp, and nothing ue-mcp
 * says contradicts them.
 *
 * UE 5.9's Fab plugin carries a toolset (module FabToolset) that searches the
 * Fab catalog and imports listings. No recorded catalog has it, so it has no
 * epic_* action; before this, it routed to the epic category, fab(open_search)
 * told the agent "Fab has no search API and its terms forbid automated access",
 * and an agent concluded the editor could not search Fab at all.
 */
import { describe, expect, it } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { routeEpicToolset } from "../../src/tools/epic/toolset-routes.js";
import { isFabToolName, listEpicFabTools } from "../../src/tools/fab-epic.js";
import { microGuide, SERVER_INSTRUCTIONS, SERVER_INSTRUCTIONS_LEAN } from "../../src/surface/context/instructions.js";
import type { ToolContext } from "../../src/core/types.js";

type Answer = unknown | ((params: Record<string, unknown>) => unknown);

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
  it("places the Fab plugin's toolset in fab, whatever its tools are named for", () => {
    for (const name of ["FabToolset.FabToolset", "Fab.FabToolset", "FabToolset.FabAssetTools", "fab_toolset.toolsets.fab.FabTools"]) {
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
          { name: "FabToolset.FabToolset", description: "Search and import Fab content", tools: ["FabToolset.FabToolset.Search", "FabToolset.FabToolset.Import"] },
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
    expect(out.toolCount).toBe(3);
    expect(out.toolsets).toHaveLength(1);
    expect(out.toolsets[0].name).toBe("FabToolset.FabToolset");
    expect(out.toolsets[0].tools.map((t: any) => t.name)).toEqual(["FabToolset.FabToolset.Search", "FabToolset.FabToolset.Import"]);
    expect(out.toolsets[0].tools[0].call).toBe('epic(action="call_tool", args={tool: "FabToolset.FabToolset.Search", input: {...}})');
    expect(out.mcpTools.map((t: any) => t.name)).toEqual(["fab_search"]);
    expect(out.mcpTools[0].call).toContain('epic(action="call_mcp_tool"');
    expect(out.note).toBeUndefined();
  });

  it("passes full tool objects through when the editor sends schemas", async () => {
    const schema = { type: "object", properties: { query: { type: "string" } } };
    const { ctx } = ctxWith({
      epic_list_toolsets: { success: true, toolsets: [{ name: "FabToolset.FabToolset", tools: [{ name: "FabToolset.FabToolset.Search", inputSchema: schema }] }] },
      epic_list_mcp_tools: { success: true, available: true, tools: [] },
    });
    const out = (await listEpicFabTools.handler(ctx, { includeSchemas: true })) as Record<string, any>;
    expect(out.toolsets[0].tools[0]).toMatchObject({ name: "FabToolset.FabToolset.Search", inputSchema: schema });
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
  const said = [fab.description, ...Object.values(fab.actions).map((a) => a.description ?? "")].join("\n");

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
