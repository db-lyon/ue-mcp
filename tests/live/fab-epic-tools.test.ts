// @live-editor headless
/**
 * fab(list_epic_tools) against a real editor: it reads the editor's own
 * Toolset Registry and Unreal MCP plugin, and reports exactly the Fab toolsets
 * epic_list_toolsets reports, each with the epic(call_tool) call that runs it.
 * On an engine whose launcher did not add the Fab UE Plugin (the test
 * project's UE 5.9 can come without it, #1889) the registry answers and holds
 * no Fab toolset, so the action says where Epic's Fab tools come from instead
 * of answering an empty list.
 *
 * With the Fab UE Plugin installed and enabled (fab_status says so), the
 * registry has to hold exactly what Fab UE Plugin 0.0.22 registers on 5.9:
 * FabToolset.FabToolset with its five tools, and the call list_epic_tools names
 * has to reach the tool. A later plugin version that renames them fails here,
 * which is the signal to re-record the 5.9 catalog.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import type { ToolContext } from "../../src/core/types.js";
import { isFabToolName, listEpicFabTools } from "../../src/tools/fab-epic.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;

beforeAll(async () => {
  bridge = await getBridge();
}, 120_000);

afterAll(() => {
  if (bridge) disconnectBridge();
});

describe("fab(list_epic_tools)", () => {
  it("reports the Fab toolsets the editor's registry has, each with the call that runs its tools", async () => {
    const listed = await callBridge(bridge, "epic_list_toolsets", {});
    expect(listed.ok, listed.error).toBe(true);
    const registry = body(listed);
    expect(registry.success, JSON.stringify(registry)).not.toBe(false);
    const all = (registry.toolsets as Array<{ name: string; tools?: string[] }>) ?? [];
    expect(all.length).toBeGreaterThan(0);
    const fabInRegistry = all.filter((t) => isFabToolName(t.name));

    const out = (await listEpicFabTools.handler({ bridge } as unknown as ToolContext, {})) as Json;
    const toolsets = out.toolsets as Array<{ name: string; tools: Array<{ name: string; call: string }> }>;
    expect(out.registryError).toBeUndefined();
    expect(toolsets.map((t) => t.name).sort()).toEqual(fabInRegistry.map((t) => t.name).sort());
    for (const ts of toolsets) {
      const expected = fabInRegistry.find((t) => t.name === ts.name)!;
      expect(ts.tools.map((t) => t.name)).toEqual(expected.tools ?? []);
      for (const tool of ts.tools) expect(tool.call).toContain(`tool: "${tool.name}"`);
    }

    if (fabInRegistry.length > 0 || (out.mcpTools as unknown[]).length > 0) {
      expect(out.available).toBe(true);
      expect(out.note).toBeUndefined();
    } else {
      expect(out.available).toBe(false);
      expect(out.toolCount).toBe(0);
      expect(String(out.note)).toContain("Fab UE Plugin");
      expect(String(out.note)).toContain("FabToolset");
    }
  }, 60_000);

  it("passes each Fab tool's input schema through with includeSchemas", async () => {
    const out = (await listEpicFabTools.handler({ bridge } as unknown as ToolContext, { includeSchemas: true })) as Json;
    for (const ts of out.toolsets as Array<{ tools: Json[] }>) {
      for (const tool of ts.tools) expect(tool.inputSchema, String(tool.name)).toBeDefined();
    }
  }, 60_000);
});

describe("Epic's Fab tools with the Fab UE Plugin installed", () => {
  const FAB_TOOLSET = "FabToolset.FabToolset";
  const FAB_TOOLS = [
    "FabToolset.FabToolset.FabSignIn",
    "FabToolset.FabToolset.GetFabListingDetails",
    "FabToolset.FabToolset.InstallFabListing",
    "FabToolset.FabToolset.SearchFabListings",
    "FabToolset.FabToolset.SearchFabMyLibrary",
  ];

  async function fabPluginPresent(): Promise<boolean> {
    const status = body(await callBridge(bridge, "fab_status", {}));
    return status.pluginInstalled === true && status.pluginEnabled === true && status.pluginLoaded === true;
  }

  // The suite fails on a skipped test, so an engine without the plugin asserts the other branch instead.
  it("lists FabToolset.FabToolset with its five tools, each with its schema and its fab action", async () => {
    const out = (await listEpicFabTools.handler({ bridge } as unknown as ToolContext, { includeSchemas: true })) as Json;
    if (!(await fabPluginPresent())) {
      expect(out.toolsets).toEqual([]);
      return;
    }
    expect(out.available).toBe(true);
    const toolsets = out.toolsets as Array<{ name: string; tools: Array<Json & { name: string; call: string; action?: string }> }>;
    expect(toolsets.map((t) => t.name)).toEqual([FAB_TOOLSET]);
    const tools = toolsets[0].tools;
    // The registry promises the set of tools, not their order.
    expect(tools.map((t) => t.name).sort()).toEqual(FAB_TOOLS);
    for (const tool of tools) {
      expect(tool.call).toContain(`toolset: "${FAB_TOOLSET}", tool: "${tool.name}"`);
      expect(tool.action, tool.name).toMatch(/^fab\(action="epic_[a-z_]+", args=\{\.\.\.\}\)$/);
    }
    const search = tools.find((t) => t.name === "FabToolset.FabToolset.SearchFabListings")!;
    const props = (search.inputSchema as { properties: Record<string, { type: string }> }).properties;
    expect(props.keywords.type).toBe("string");
    expect(props.bFreeOnly.type).toBe("boolean");
    expect(props.count.type).toBe("integer");
    const install = tools.find((t) => t.name === "FabToolset.FabToolset.InstallFabListing")!;
    expect((install.inputSchema as { required: string[] }).required.sort()).toEqual(["assetFormat", "listingId"]);
    expect(install.annotations).toEqual({ accessMode: "write" });
  }, 60_000);

  it("reaches the Fab tool through the call list_epic_tools names", async () => {
    const present = await fabPluginPresent();
    // GetFabListingDetails without its required listingId: the tool's own schema check answers,
    // which proves the toolset and tool resolved, and nothing goes to Fab.
    const r = await callBridge(bridge, "epic_call_tool", {
      toolset: FAB_TOOLSET,
      tool: "FabToolset.FabToolset.GetFabListingDetails",
      input: {},
    });
    const answer = JSON.stringify(r.result ?? r.error);
    if (!present) {
      // No such toolset in this editor: refused, never run.
      expect(r.ok === false || body(r).success === false, answer).toBe(true);
      return;
    }
    expect(answer).not.toMatch(/Missing required parameter|not found/i);
    expect(answer).toContain("GetFabListingDetails");
    expect(answer).toContain("listingId");
  }, 60_000);
});
