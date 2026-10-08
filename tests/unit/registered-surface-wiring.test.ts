/**
 * The withheld surface is WIRED (spec 6.4): the real server over stdio,
 * against a stub bridge whose handshake publishes every advertised method but
 * one. tools/list must not offer that action, and calling it must be refused
 * before anything reaches the editor.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocketServer, type WebSocket } from "ws";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { ALL_TOOLS } from "../../src/tools.js";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const MISSING = "get_world_outliner";

function advertisedMethods(): string[] {
  const methods = new Set<string>();
  for (const tool of ALL_TOOLS) {
    for (const spec of Object.values(tool.actions)) if (spec.kind === "bridge") methods.add(spec.bridge);
  }
  return [...methods];
}

let wss: WebSocketServer;
let client: Client;
let sandbox: string;
const seen: string[] = [];

beforeAll(async () => {
  const actions = [...advertisedMethods().filter((m) => m !== MISSING), "list_dialogs"];
  wss = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  await new Promise<void>((resolve) => wss.once("listening", resolve));
  wss.on("connection", (ws: WebSocket) => {
    ws.on("message", (raw) => {
      const req = JSON.parse(String(raw)) as { id?: unknown; method?: string };
      const method = String(req.method ?? "");
      seen.push(method);
      const result = method === "get_bridge_capabilities"
        ? { protocolVersion: 2, actions, actionCount: actions.length }
        : method === "list_dialogs"
          ? { success: true, dialogs: [], count: 0 }
          : { success: true };
      ws.send(JSON.stringify({ jsonrpc: "2.0", id: req.id ?? null, result }));
    });
  });
  const addr = wss.address();
  const port = typeof addr === "object" && addr ? addr.port : 0;

  sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-registered-surface-"));
  const projectDir = path.join(sandbox, "Withheld");
  fs.mkdirSync(path.join(projectDir, "Content"), { recursive: true });
  const uproject = path.join(projectDir, "Withheld.uproject");
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8", Modules: [] }));

  const keep = ["UE_MCP_PORT", "UE_MCP_HOST", "UE_MCP_STATE_DIR", "UE_MCP_CONFIG_DIR", "UE_MCP_CONTEXT_STRATEGY"];
  const env: Record<string, string> = {
    ...(process.env as Record<string, string>),
    UE_MCP_PORT: String(port),
    UE_MCP_HOST: "127.0.0.1",
    UE_MCP_STATE_DIR: path.join(sandbox, "state"),
    UE_MCP_CONFIG_DIR: path.join(sandbox, "config"),
    UE_MCP_CONTEXT_STRATEGY: "full",
  };
  for (const k of Object.keys(env)) if (k.startsWith("UE_MCP_") && !keep.includes(k)) delete env[k];

  client = new Client({ name: "ue-mcp-registered-surface", version: "1.0.0" }, { capabilities: {} });
  await client.connect(
    new StdioClientTransport({
      command: process.execPath,
      args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
      cwd: REPO_ROOT,
      env,
      stderr: fs.openSync(path.join(sandbox, "server.log"), "a"),
    }),
  );
}, 180_000);

afterAll(async () => {
  await client?.close().catch(() => {});
  await new Promise<void>((resolve) => wss?.close(() => resolve()));
  fs.rmSync(sandbox, { recursive: true, force: true });
});

describe("a method the plugin did not register", () => {
  it("is not advertised in tools/list", async () => {
    const { tools } = await client.listTools();
    const level = tools.find((t) => t.name === "level")!;
    const actionEnum = JSON.stringify((level.inputSchema.properties as Record<string, unknown>).action);
    expect(actionEnum).not.toContain('"get_outliner"');
    expect(actionEnum).toContain('"place_actor"');
    expect(level.description).not.toMatch(/^get_outliner\(/m);
  }, 60_000);

  it("leaves the flat flow tool flat when the list arrives", async () => {
    const { tools } = await client.listTools();
    const flow = tools.find((t) => t.name === "flow")!;
    const props = flow.inputSchema.properties as Record<string, unknown>;
    expect(props.flowName).toBeDefined();
    expect(props.args).toBeUndefined();
    expect(flow.description).toContain("Actions:\n- run:");
  }, 60_000);

  it("is refused by name without a round trip", async () => {
    const before = seen.length;
    const res = await client.callTool({ name: "level", arguments: { action: "get_outliner", args: { limit: 1 } } });
    const text = ((res.content as Array<{ text?: string }>)[0]?.text) ?? "";
    expect(res.isError).toBe(true);
    expect(text).toContain(`'${MISSING}'`);
    expect(text).toContain("ue-mcp update");
    expect(seen.slice(before)).not.toContain(MISSING);
  }, 60_000);

  it("leaves registered actions callable", async () => {
    const res = await client.callTool({ name: "level", arguments: { action: "place_actor", args: { actorClass: "StaticMeshActor" } } });
    expect(res.isError).toBeFalsy();
  }, 60_000);
});
