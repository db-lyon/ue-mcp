// @live-editor headless
/**
 * fab(list_epic_tools) against a real editor: it reads the editor's own
 * Toolset Registry and Unreal MCP plugin, and reports exactly the Fab toolsets
 * epic_list_toolsets reports, each with the epic(call_tool) call that runs it.
 * On an engine whose launcher did not add the Fab UE Plugin (the test
 * project's UE 5.9 can come without it, #1889) the registry answers and holds
 * no Fab toolset, so the action says where Epic's Fab tools come from instead
 * of answering an empty list.
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
