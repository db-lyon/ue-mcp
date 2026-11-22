/**
 * `ue-mcp surface`, spawned: it measures a real stdio server (ue-mcp itself,
 * editor down) and a streamable HTTP server started here, and its numbers
 * match what an MCP client is handed.
 */
import * as http from "node:http";
import type { AddressInfo } from "node:net";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { ListToolsRequestSchema } from "@modelcontextprotocol/sdk/types.js";
import { measureSurface } from "../../src/cli/surface-measure.js";
import { covers } from "../helpers/covers.js";
import { ENTRY, McpServer, runCli, Sandbox, TSX } from "./harness.js";

let sb: Sandbox;
beforeAll(() => {
  sb = new Sandbox();
});
afterAll(() => sb.cleanup());

/** The env a measured ue-mcp gets: editor down, full strategy, in process. */
const MEASURED_ENV = ["--env", "UE_MCP_PORT=1", "--env", "UE_MCP_CONTEXT_STRATEGY=full", "--env", "UE_MCP_SERVER_MODE=in-process"];

describe("ue-mcp surface over stdio", () => {
  it("measures ue-mcp with no editor and agrees with a client's own tools/list", async () => {
    covers("cli.surface.stdio");
    const r = await runCli(sb, ["surface", "--json", ...MEASURED_ENV, "--", process.execPath, "--import", TSX, ENTRY], { timeoutMs: 120_000 });
    expect(r.code, r.stderr).toBe(0);
    const report = JSON.parse(r.stdout) as { ok: boolean; server: { name: string }; tools: number; actions: number; schemaBytes: number; perTool: unknown[] };
    expect(report.ok).toBe(true);
    expect(report.server.name).toBe("ue-mcp");

    const direct = await McpServer.start(sb, { projects: [], strategy: "full" });
    try {
      const expected = measureSurface(await direct.listTools());
      expect(report.tools).toBe(expected.tools);
      expect(report.actions).toBe(expected.actions);
      expect(report.schemaBytes).toBe(expected.schemaBytes);
      expect(report.actions).toBeGreaterThan(report.tools);
    } finally {
      await direct.close();
    }

    const text = await runCli(sb, ["surface", ...MEASURED_ENV, "--", process.execPath, "--import", TSX, ENTRY], { timeoutMs: 120_000 });
    expect(text.code, text.stderr).toBe(0);
    expect(text.stdout).toMatch(new RegExp(`^tools\\s+${report.tools}$`, "m"));
    expect(text.stdout).toMatch(new RegExp(`^actions\\s+${report.actions.toLocaleString("en-US")}$`, "m"));
  });

  it("exits 1 with the server's stderr when the command is not an MCP server", async () => {
    covers("cli.surface.stdio");
    const r = await runCli(sb, ["surface", "--timeout", "20", "--", process.execPath, "-e", "console.error('not a server'); process.exit(3)"]);
    expect(r.code).toBe(1);
    expect(r.stderr).toContain("exited with code 3");
    expect(r.stderr).toContain("not a server");
  });

  it("refuses a command line with nothing to measure with exit 2", async () => {
    covers("cli.surface.stdio");
    const r = await runCli(sb, ["surface", "--json"]);
    expect(r.code).toBe(2);
    expect(JSON.parse(r.stdout)).toMatchObject({ ok: false });
  });
});

describe("ue-mcp surface --url", () => {
  let server: http.Server;
  let url: string;
  beforeAll(async () => {
    const tools = [
      { name: "scene", description: "Scene edits", inputSchema: { type: "object", properties: { operation: { type: "string", enum: ["spawn", "move", "delete"] } } } },
      { name: "screenshot", description: "Capture the viewport", inputSchema: { type: "object", properties: {} } },
    ];
    server = http.createServer(async (req, res) => {
      // Stateless: a fresh server and transport per request, as the SDK's stateless example does.
      const mcp = new Server({ name: "fixture-http", version: "9.9.9" }, { capabilities: { tools: {} } });
      mcp.setRequestHandler(ListToolsRequestSchema, async () => ({ tools }));
      const transport = new StreamableHTTPServerTransport({ sessionIdGenerator: undefined });
      res.on("close", () => {
        void transport.close();
        void mcp.close();
      });
      let body = "";
      for await (const chunk of req) body += chunk;
      await mcp.connect(transport);
      await transport.handleRequest(req, res, body ? JSON.parse(body) : undefined);
    });
    await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
    url = `http://127.0.0.1:${(server.address() as AddressInfo).port}/mcp`;
  });
  afterAll(() => new Promise<void>((resolve) => server.close(() => resolve())));

  it("measures a streamable HTTP server", async () => {
    covers("cli.surface.url");
    const r = await runCli(sb, ["surface", "--json", "--url", url]);
    expect(r.code, r.stderr).toBe(0);
    const report = JSON.parse(r.stdout) as { server: { name: string; version: string }; tools: number; actions: number; perTool: Array<{ name: string; actionParam: string | null; actions: number }> };
    expect(report.server).toEqual({ name: "fixture-http", version: "9.9.9" });
    expect(report.tools).toBe(2);
    expect(report.actions).toBe(4);
    expect(report.perTool).toEqual([
      expect.objectContaining({ name: "scene", actionParam: "operation", actions: 3 }),
      expect.objectContaining({ name: "screenshot", actionParam: null, actions: 1 }),
    ]);
  });

  it("exits 1 when nothing answers at the URL", async () => {
    covers("cli.surface.url");
    const r = await runCli(sb, ["surface", "--timeout", "20", "--url", "http://127.0.0.1:1/mcp"]);
    expect(r.code).toBe(1);
    expect(r.stderr).toMatch(/^surface: /m);
  });
});
