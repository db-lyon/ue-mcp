// @live-editor headless
/**
 * Epic's Unreal MCP plugin's own tool list and resource providers, through the
 * bridge. The test project enables the plugin and a fixture plugin
 * (tests/ue_mcp/Plugins/UEMCPEpicMCPFixture) that registers tools, a legacy MCP
 * tool library and a resource provider with it.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { ErrorCode } from "../../src/core/errors.js";
import type { ProgressUpdate } from "../../src/core/types.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

interface McpTool { name: string; description: string; source: string; library?: string; inputSchema?: Json }

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function refused(method: string, params: Json): Promise<string> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok, r.error).toBe(true);
  expect(body(r).success, JSON.stringify(r.result)).toBe(false);
  return String(body(r).error);
}

interface ToolList { tools: McpTool[]; registryAdapterCount: number }

async function listTools(params: Json = {}): Promise<ToolList> {
  const r = await callBridge(bridge, "epic_list_mcp_tools", params);
  const listed = body(r);
  expect(r.ok && listed.success !== false, r.error ?? JSON.stringify(r.result)).toBe(true);
  expect(listed.available).toBe(true);
  const tools = listed.tools as McpTool[];
  expect(listed.toolCount).toBe(tools.length);
  return { tools, registryAdapterCount: Number(listed.registryAdapterCount) };
}

async function slowState(): Promise<{ cancellations: number; completions: number; pending: number }> {
  const state = await call("epic_call_mcp_tool", { tool: "ue_mcp_fixture_slow_state" });
  return state.structuredContent as { cancellations: number; completions: number; pending: number };
}

beforeAll(async () => {
  bridge = await getBridge();
}, 60_000);

afterAll(() => {
  if (bridge) disconnectBridge();
});

describe("epic_list_mcp_tools", () => {
  it("lists plugin tools and legacy library tools with their source, and counts the registry adapters it leaves out", async () => {
    const { tools, registryAdapterCount } = await listTools();
    const byName = new Map(tools.map((t) => [t.name, t]));
    for (const name of ["ue_mcp_fixture_echo", "ue_mcp_fixture_image", "ue_mcp_fixture_slow", "ue_mcp_fixture_slow_state"]) {
      expect(byName.get(name)?.source, name).toBe("plugin");
    }
    expect(byName.get("ue_mcp_fixture_echo")?.description).toContain("Echoes message back");
    const greet = byName.get("UeMcpFixtureLibraryGreet");
    expect(greet?.source).toBe("toolLibrary");
    expect(greet?.library).toBe("/Script/UEMCPEpicMCPFixture.UEMCPFixtureToolLibrary");
    // Epic's tool-search mode registers list_toolsets, describe_toolset and call_tool
    // on 5.8; 5.9 folds describe_toolset into list_toolsets and registers two.
    expect(registryAdapterCount).toBeGreaterThanOrEqual(2);
    expect(tools.some((t) => t.source === "toolSearch" || t.source === "toolsetRegistry")).toBe(false);

    const withAdapters = await listTools({ includeRegistryAdapters: true });
    expect(withAdapters.tools.length).toBe(tools.length + registryAdapterCount);
    expect(withAdapters.tools.find((t) => t.name === "call_tool")?.source).toBe("toolSearch");
  }, 60_000);

  it("narrows by name and returns schemas on request", async () => {
    const { tools } = await listTools({ nameFilter: "ue_mcp_fixture_e", includeSchemas: true });
    expect(tools.map((t) => t.name)).toEqual(["ue_mcp_fixture_echo"]);
    expect(((tools[0].inputSchema?.properties as Json)?.message as Json)?.type).toBe("string");
  }, 60_000);
});

describe("epic_call_mcp_tool", () => {
  it("runs a plugin tool with its input and returns its text and structured content", async () => {
    const r = await call("epic_call_mcp_tool", { tool: "ue_mcp_fixture_echo", input: { message: "over the bridge" } });
    expect(r.tool).toBe("ue_mcp_fixture_echo");
    expect(r.content).toEqual([{ type: "text", text: "echo: over the bridge" }]);
    expect(r.structuredContent).toEqual({ message: "over the bridge", length: 15 });

    const raw = await call("epic_call_mcp_tool", { tool: "ue_mcp_fixture_echo", inputJson: "{\"message\":\"raw\"}" });
    expect(raw.structuredContent).toEqual({ message: "raw", length: 3 });
  }, 60_000);

  it("runs a legacy MCP tool library function", async () => {
    const { tools } = await listTools({ nameFilter: "UeMcpFixtureLibraryGreet", includeSchemas: true });
    const param = Object.keys((tools[0].inputSchema?.properties as Json) ?? {})[0];
    expect(param, JSON.stringify(tools[0].inputSchema)).toBeTruthy();
    const r = await call("epic_call_mcp_tool", { tool: "UeMcpFixtureLibraryGreet", input: { [param]: "library" } });
    expect(JSON.stringify(r.content ?? r.structuredContent)).toContain("hello, library");
  }, 60_000);

  it("moves image content into media", async () => {
    const r = await call("epic_call_mcp_tool", { tool: "ue_mcp_fixture_image" });
    expect(r.content).toEqual([{ type: "text", text: "one pixel" }]);
    const media = r.media as Array<{ type: string; mimeType: string; data: string }>;
    expect(media).toHaveLength(1);
    expect(media[0].type).toBe("image");
    expect(media[0].mimeType).toBe("image/png");
    expect(Buffer.from(media[0].data, "base64").subarray(1, 4).toString("latin1")).toBe("PNG");
  }, 60_000);

  it("waits for an asynchronous tool and forwards progress while it runs", async () => {
    const before = await slowState();
    const progress: ProgressUpdate[] = [];
    const answered = await bridge.call("epic_call_mcp_tool", { tool: "ue_mcp_fixture_slow", input: { seconds: 2.5 } }, 30_000, {
      onProgress: (u) => progress.push(u),
    }) as Json;
    expect(answered.success, JSON.stringify(answered)).toBe(true);
    expect(answered.structuredContent).toEqual({ seconds: 2.5 });
    expect(progress.length).toBeGreaterThanOrEqual(1);
    expect(progress.map((p) => p.progress)).toEqual([...progress.map((p) => p.progress)].sort((a, b) => a - b));
    expect(progress[0].message).toContain("epic_call_mcp_tool");
    expect((await slowState()).completions).toBe(before.completions + 1);
  }, 60_000);

  it("forwards a cancel to the tool's CancelAsync", async () => {
    const before = await slowState();
    const abort = new AbortController();
    const pending = bridge.call("epic_call_mcp_tool", { tool: "ue_mcp_fixture_slow", input: { seconds: 60 } }, 90_000, { signal: abort.signal });
    await expect.poll(async () => (await slowState()).pending, { timeout: 10_000 }).toBe(before.pending + 1);
    abort.abort();
    await expect(pending).rejects.toMatchObject({ code: ErrorCode.CANCELLED });
    await expect.poll(async () => (await slowState()).cancellations, { timeout: 10_000 }).toBe(before.cancellations + 1);
    const after = await slowState();
    expect(after.pending).toBe(before.pending);
    expect(after.completions).toBe(before.completions);
  }, 60_000);

  it("refuses a tool no one registered", async () => {
    expect(await refused("epic_call_mcp_tool", { tool: "ue_mcp_fixture_missing" })).toContain("No MCP tool named 'ue_mcp_fixture_missing'");
  }, 60_000);
});

describe("epic MCP resources", () => {
  it("lists the fixture provider's resources and reads text and blob contents", async () => {
    const listed = await call("epic_list_mcp_resources", {});
    expect(listed.available).toBe(true);
    const resources = listed.resources as Array<{ uri: string; name?: string; mimeType?: string; description?: string }>;
    expect(listed.resourceCount).toBe(resources.length);
    const note = resources.find((r) => r.uri === "ue-mcp-fixture://notes/readme");
    expect(note).toMatchObject({ name: "readme", mimeType: "text/plain", description: "A text resource from the ue-mcp fixture provider" });
    expect(resources.some((r) => r.uri === "ue-mcp-fixture://data/blob")).toBe(true);

    const text = await call("epic_read_mcp_resource", { uri: "ue-mcp-fixture://notes/readme" });
    expect((text.contents as Json[])[0]).toMatchObject({ uri: "ue-mcp-fixture://notes/readme", mimeType: "text/plain", text: "UE-MCP fixture resource text" });

    const blob = await call("epic_read_mcp_resource", { uri: "ue-mcp-fixture://data/blob" });
    const blobContents = (blob.contents as Json[])[0];
    expect([...Buffer.from(String(blobContents.blob), "base64")]).toEqual([0, 1, 2, 3, 250]);

    expect(await refused("epic_read_mcp_resource", { uri: "ue-mcp-fixture://nowhere" })).toContain("No registered resource provider lists");
  }, 60_000);
});
