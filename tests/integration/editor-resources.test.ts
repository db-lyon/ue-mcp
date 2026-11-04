/**
 * What a client sees of an editor's Epic MCP resources and media results,
 * through a real server: resources/list and resources/read served from the
 * bridge, an empty list with no editor, and an epic call whose image comes back
 * as MCP image content rather than base64 inside the JSON text.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, Sandbox } from "./harness.js";

const PIXEL = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";

let sb: Sandbox;
let bridge: FakeBridge;
let server: McpServer;

beforeAll(async () => {
  sb = new Sandbox();
  bridge = await FakeBridge.start({
    projectDir: sb.path("editor"),
    handlers: {
      epic_list_mcp_resources: () => ({
        success: true,
        available: true,
        resources: [
          { uri: "fixture://notes/readme", name: "readme", title: "Readme", mimeType: "text/plain" },
          { uri: "fixture://data/blob", mimeType: "application/octet-stream" },
        ],
      }),
      epic_read_mcp_resource: (p) => p.uri === "fixture://notes/readme"
        ? { success: true, uri: p.uri, contents: [{ uri: p.uri, mimeType: "text/plain", text: "hello" }] }
        : { success: false, error: `No registered resource provider lists '${String(p.uri)}'.` },
      epic_call_tool: () => ({
        success: true,
        toolset: "EditorToolset.EditorAppToolset",
        tool: "CaptureAssetImage",
        result: { returnValue: "urn:toolresult:EditorToolset.EditorAppToolset.CaptureAssetImage:g:/returnValue" },
        media: [{ type: "image", mimeType: "image/png", data: PIXEL, uri: "urn:toolresult:x", pointer: "/returnValue" }],
      }),
    },
  });
  server = await McpServer.start(sb, { projects: [], port: bridge.port });
}, 60_000);

afterAll(async () => {
  await server?.close();
  await bridge?.stop();
  sb.cleanup();
});

describe("resources", () => {
  it("lists and reads the editor's resources", async () => {
    covers("server.resources");
    // The list is empty until the bridge has connected, which the server does at startup.
    await expect.poll(async () => (await server.client.listResources()).resources.length, { timeout: 15_000 }).toBe(2);
    const { resources } = await server.client.listResources();
    expect(resources[0]).toMatchObject({ uri: "fixture://notes/readme", name: "readme", title: "Readme", mimeType: "text/plain" });
    // A provider that gave no name is listed under its uri, since MCP requires one.
    expect(resources[1]).toMatchObject({ uri: "fixture://data/blob", name: "fixture://data/blob" });

    const read = await server.client.readResource({ uri: "fixture://notes/readme" });
    expect(read.contents).toEqual([{ uri: "fixture://notes/readme", mimeType: "text/plain", text: "hello" }]);
    expect(bridge.calls.filter((c) => c.method === "epic_read_mcp_resource").at(-1)?.params).toEqual({ uri: "fixture://notes/readme" });

    await expect(server.client.readResource({ uri: "fixture://missing" })).rejects.toThrow(/No registered resource provider lists/);
  });

  it("lists nothing, without an error, when no editor is running", async () => {
    covers("server.resources");
    const down = await McpServer.start(sb, { projects: [sb.makeProject("Down")] });
    try {
      expect((await down.client.listResources()).resources).toEqual([]);
    } finally {
      await down.close();
    }
  }, 60_000);
});

describe("media results", () => {
  it("returns an image the editor marked as MCP image content", async () => {
    covers("server.media-content");
    const r = await server.call("tools", {
      action: "call",
      category: "epic",
      method: "call_tool",
      args: { toolset: "EditorToolset.EditorAppToolset", tool: "CaptureAssetImage" },
    });
    expect(r.isError).toBe(false);
    expect(r.media).toEqual([{ type: "image", mimeType: "image/png", data: PIXEL }]);
    expect(r.text).not.toContain(PIXEL);
    expect(json<{ media: Array<{ contentBlock: number; pointer: string }> }>(r).media[0]).toMatchObject({ contentBlock: 1, pointer: "/returnValue" });
  });
});
