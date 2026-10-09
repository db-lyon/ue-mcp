/**
 * The daemon path end to end (spec 5.5): an MCP client launches the stdio shim,
 * the shim starts the project's daemon, and calls reach a stub editor bridge
 * through it. The advertised surface must be byte-identical to the in-process
 * server's, the event stream must report the editor coming and going, and a
 * daemon that dies must be replaced without the client reconnecting.
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

let wss: WebSocketServer;
let sandbox: string;
let uproject: string;
let daemonDir: string;
let env: Record<string, string>;
let viaShim: Client;
let inProcess: Client;
const seen: string[] = [];

function methods(): string[] {
  const set = new Set<string>();
  for (const tool of ALL_TOOLS) for (const spec of Object.values(tool.actions)) if (spec.kind === "bridge") set.add(spec.bridge);
  return [...set, "list_dialogs"];
}

function discovery(): { pid: number; port: number; token: string } {
  const files = fs.readdirSync(daemonDir).filter((f) => f.endsWith(".json"));
  expect(files).toHaveLength(1);
  return JSON.parse(fs.readFileSync(path.join(daemonDir, files[0]), "utf-8"));
}

async function waitFor<T>(probe: () => T | null | undefined | false, ms = 60_000): Promise<T> {
  const deadline = Date.now() + ms;
  for (;;) {
    try {
      const v = probe();
      if (v) return v;
    } catch {
      // not yet
    }
    if (Date.now() > deadline) throw new Error("timed out waiting");
    await new Promise((r) => setTimeout(r, 200));
  }
}

function connectClient(name: string, extraEnv: Record<string, string>): Promise<Client> {
  const client = new Client({ name, version: "1.0.0" }, { capabilities: {} });
  return client.connect(new StdioClientTransport({
    command: process.execPath,
    args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), uproject],
    cwd: REPO_ROOT,
    env: { ...env, ...extraEnv },
    stderr: fs.openSync(path.join(sandbox, `${name}.log`), "a"),
  })).then(() => client);
}

beforeAll(async () => {
  const actions = methods();
  wss = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  await new Promise<void>((resolve) => wss.once("listening", resolve));
  wss.on("connection", (ws: WebSocket) => {
    ws.on("message", (raw) => {
      const req = JSON.parse(String(raw)) as { id?: unknown; method?: string };
      const method = String(req.method ?? "");
      seen.push(method);
      const result = method === "get_bridge_capabilities"
        ? { protocolVersion: 2, actions, actionCount: actions.length }
        : method === "list_dialogs" ? { success: true, dialogs: [], count: 0 } : { success: true };
      ws.send(JSON.stringify({ jsonrpc: "2.0", id: req.id ?? null, result }));
    });
  });
  const addr = wss.address();
  const port = typeof addr === "object" && addr ? addr.port : 0;

  sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-daemon-"));
  daemonDir = path.join(sandbox, "daemons");
  const projectDir = path.join(sandbox, "Daemoned");
  fs.mkdirSync(path.join(projectDir, "Content"), { recursive: true });
  uproject = path.join(projectDir, "Daemoned.uproject");
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8", Modules: [] }));

  const keep = ["UE_MCP_PORT", "UE_MCP_HOST", "UE_MCP_STATE_DIR", "UE_MCP_CONFIG_DIR", "UE_MCP_CONTEXT_STRATEGY", "UE_MCP_DAEMON_DIR", "UE_MCP_DAEMON_IDLE_MS"];
  env = {
    ...(process.env as Record<string, string>),
    UE_MCP_PORT: String(port),
    UE_MCP_HOST: "127.0.0.1",
    UE_MCP_STATE_DIR: path.join(sandbox, "state"),
    UE_MCP_CONFIG_DIR: path.join(sandbox, "config"),
    UE_MCP_CONTEXT_STRATEGY: "full",
    UE_MCP_DAEMON_DIR: daemonDir,
    // A backstop: a daemon this file leaves behind exits on its own.
    UE_MCP_DAEMON_IDLE_MS: "20000",
  };
  for (const k of Object.keys(env)) if (k.startsWith("UE_MCP_") && !keep.includes(k)) delete env[k];

  [viaShim, inProcess] = await Promise.all([
    connectClient("shim", {}),
    connectClient("in-process", { UE_MCP_SERVER_MODE: "in-process" }),
  ]);
}, 240_000);

afterAll(async () => {
  // The shim first: a daemon stopped under a live shim is replaced, by design.
  await viaShim?.close().catch(() => {});
  await inProcess?.close().catch(() => {});
  try {
    const d = discovery();
    await fetch(`http://127.0.0.1:${d.port}/v1/shutdown`, { method: "POST", headers: { authorization: `Bearer ${d.token}` } });
  } catch {
    // already gone
  }
  for (const ws of wss?.clients ?? []) ws.terminate();
  await new Promise<void>((resolve) => wss?.close(() => resolve()));
  await new Promise((r) => setTimeout(r, 500));
  fs.rmSync(sandbox, { recursive: true, force: true });
});

describe("the daemon path", () => {
  it("advertises exactly what the in-process server advertises", async () => {
    // Both wait for the editor's method list, so the withheld view is settled.
    await waitFor(() => seen.filter((m) => m === "get_bridge_capabilities").length >= 2);
    const [a, b] = await Promise.all([viaShim.listTools(), inProcess.listTools()]);
    expect(a.tools.map((t) => t.name)).toEqual(b.tools.map((t) => t.name));
    expect(JSON.stringify(a.tools)).toBe(JSON.stringify(b.tools));
    expect(viaShim.getInstructions()).toBe(inProcess.getInstructions());
  }, 60_000);

  it("publishes a discovery file and refuses a request without the token", async () => {
    const d = discovery();
    expect((await fetch(`http://127.0.0.1:${d.port}/v1/health`)).status).toBe(401);
    const health = await (await fetch(`http://127.0.0.1:${d.port}/v1/health`, { headers: { authorization: `Bearer ${d.token}` } })).json();
    expect(health).toMatchObject({ ok: true, pid: d.pid, clients: 1 });
    expect(health.editors[0].connected).toBe(true);
  });

  it("refuses a request from a foreign origin or host, token or not", async () => {
    const d = discovery();
    const auth = { authorization: `Bearer ${d.token}` };
    const base = `http://127.0.0.1:${d.port}/v1/health`;
    expect((await fetch(base, { headers: { ...auth, origin: "https://evil.example" } })).status).toBe(401);
    expect((await fetch(base, { headers: { ...auth, origin: `http://127.0.0.1:${d.port}` } })).status).toBe(200);
  });

  it("serves the project's flows", async () => {
    const d = discovery();
    const res = await fetch(`http://127.0.0.1:${d.port}/v1/flows`, { headers: { authorization: `Bearer ${d.token}` } });
    expect(res.status).toBe(200);
    expect(JSON.stringify(await res.json())).toContain("niagara_fire");
  });

  it("dispatches a call through the daemon to the editor", async () => {
    const before = seen.length;
    const res = await viaShim.callTool({ name: "level", arguments: { action: "get_outliner", args: {} } });
    expect(res.isError).toBeFalsy();
    expect(seen.slice(before)).toContain("get_world_outliner");
  }, 30_000);

  it("publishes the call in the activity feed, attributed to the MCP client", async () => {
    const d = discovery();
    const auth = { authorization: `Bearer ${d.token}` };
    const start = (await (await fetch(`http://127.0.0.1:${d.port}/v1/health`, { headers: auth })).json()).lastEventId as number;
    await viaShim.callTool({ name: "level", arguments: { action: "get_outliner", args: {} } });
    const ctl = new AbortController();
    const res = await fetch(`http://127.0.0.1:${d.port}/v1/events`, { headers: { ...auth, "last-event-id": String(start) }, signal: ctl.signal });
    const reader = res.body!.getReader();
    let body = "";
    const deadline = Date.now() + 10_000;
    while (!body.includes("call.finished") && Date.now() < deadline) {
      const chunk = await reader.read();
      if (chunk.done) break;
      body += new TextDecoder().decode(chunk.value);
    }
    ctl.abort();
    expect(body).toMatch(/event: call\.started\ndata: .*"client":"shim".*"category":"level","action":"get_outliner"/);
    expect(body).toMatch(/event: call\.finished\ndata: .*"client":"shim".*"ok":true/);
  }, 30_000);

  it("streams the editor going away with its cause, resumable by id", async () => {
    const d = discovery();
    const auth = { authorization: `Bearer ${d.token}` };
    const start = (await (await fetch(`http://127.0.0.1:${d.port}/v1/health`, { headers: auth })).json()).lastEventId as number;
    for (const ws of wss.clients) ws.terminate();
    // Read the backlog after the disconnect has been classified.
    await new Promise((r) => setTimeout(r, 3000));
    const ctl = new AbortController();
    const res = await fetch(`http://127.0.0.1:${d.port}/v1/events`, { headers: { ...auth, "last-event-id": String(start) }, signal: ctl.signal });
    const reader = res.body!.getReader();
    let body = "";
    const deadline = Date.now() + 10_000;
    while (!body.includes("editor.disconnected") && Date.now() < deadline) {
      const chunk = await reader.read();
      if (chunk.done) break;
      body += new TextDecoder().decode(chunk.value);
    }
    ctl.abort();
    // The stub's socket dropped while nothing exited, so the cause is unknown, not crashed.
    expect(body).toMatch(/event: editor\.disconnected\ndata: .*"cause":"unknown"/);
  }, 30_000);

  it("replaces a daemon that dies, without the client reconnecting", async () => {
    const first = discovery();
    await fetch(`http://127.0.0.1:${first.port}/v1/shutdown`, { method: "POST", headers: { authorization: `Bearer ${first.token}` } });
    const second = await waitFor(() => {
      const d = discovery();
      return d.pid !== first.pid ? d : null;
    }, 120_000);
    expect(second.pid).not.toBe(first.pid);
    const listed = await viaShim.listTools();
    expect(listed.tools.length).toBeGreaterThan(0);
  }, 180_000);
});
