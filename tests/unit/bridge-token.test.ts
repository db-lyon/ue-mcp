/**
 * The bridge's session token, client side, against a fake bridge that follows
 * the plugin's upgrade contract (BridgeWebSocket.cpp): refuse an Origin, then
 * require `Authorization: Bearer <token>` matching the file the bridge wrote to
 * `<UE_MCP_BRIDGE_TOKEN_DIR>/<port>.token`, else answer 401 with
 * `X-UE-MCP-Bridge-Auth: token-file` and a sentence.
 */
import { once } from "node:events";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as http from "node:http";
import * as os from "node:os";
import * as path from "node:path";
import type { AddressInfo } from "node:net";
import { WebSocketServer } from "ws";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { EditorBridge, callBridgeOnce } from "../../src/bridge/bridge.js";
import {
  BridgeTokenError,
  bridgeAuthHeaders,
  bridgeTokenDir,
  bridgeTokenPath,
  readBridgeToken,
} from "../../src/bridge/bridge-token.js";
import * as tokenMjs from "../../scripts/lib/bridge-token.mjs";
import { askOnce } from "../../scripts/bridge-target.mjs";

const MISSING_BODY =
  "This UE-MCP bridge requires a session token and the client sent none. Update ue-mcp (npx ue-mcp@latest, or run 'ue-mcp update' in the project) so it reads the token the editor publishes for port {port}.";

interface FakeBridge {
  port: number;
  token: string | null;
  /** The Authorization header of every upgrade attempt, in order (undefined when absent). */
  seen: (string | undefined)[];
  close: () => Promise<void>;
}

/**
 * `requireToken: false` is a plugin that predates the token. Otherwise the
 * bridge writes its token file, unless `publish` is false, which leaves a
 * client nothing to find.
 */
async function startFakeBridge(dir: string, opts: { requireToken: boolean; publish?: boolean }): Promise<FakeBridge> {
  const wss = new WebSocketServer({ noServer: true });
  const server = http.createServer((_req, res) => res.writeHead(426).end());
  const seen: (string | undefined)[] = [];
  const token = opts.requireToken ? crypto.randomBytes(32).toString("hex") : null;

  wss.on("connection", (ws) => {
    ws.on("message", (data) => {
      const msg = JSON.parse(data.toString()) as { id: string; method: string };
      const result = msg.method === "get_bridge_capabilities"
        ? { protocolVersion: 2, actions: ["ping"], actionCount: 1, features: token ? ["session-token"] : [] }
        : { ok: true, method: msg.method };
      ws.send(JSON.stringify({ jsonrpc: "2.0", id: msg.id, result }));
    });
  });

  server.on("upgrade", (req, socket, head) => {
    seen.push(req.headers.authorization);
    const refuse = (status: number, text: string, body: string): void => {
      const headers = token ? "X-UE-MCP-Bridge-Auth: token-file\r\nWWW-Authenticate: Bearer realm=\"ue-mcp-bridge\"\r\n" : "";
      const bytes = Buffer.from(`${body}\r\n`);
      socket.end(`HTTP/1.1 ${status} ${text}\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: ${bytes.length}\r\nConnection: close\r\n${headers}\r\n${bytes}`);
    };
    if (req.headers.origin !== undefined) return refuse(403, "Forbidden", "No web pages.");
    if (token) {
      const auth = req.headers.authorization;
      const port = (server.address() as AddressInfo).port;
      if (auth === undefined) return refuse(401, "Unauthorized", MISSING_BODY.replace("{port}", String(port)));
      if (!/^bearer\s+/i.test(auth)) return refuse(401, "Unauthorized", "The Authorization header must be 'Bearer <session token>'.");
      const presented = Buffer.from(auth.replace(/^bearer\s+/i, "").trim());
      const expected = Buffer.from(token);
      if (presented.length !== expected.length || !crypto.timingSafeEqual(presented, expected)) {
        return refuse(401, "Unauthorized", "The session token does not match this editor's.");
      }
    }
    wss.handleUpgrade(req, socket, head, (ws) => wss.emit("connection", ws, req));
  });

  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  const port = (server.address() as AddressInfo).port;
  if (token && opts.publish !== false) {
    fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
    fs.writeFileSync(path.join(dir, `${port}.token`), token, { mode: 0o600 });
  }
  return {
    port,
    token,
    seen,
    close: async () => {
      for (const c of wss.clients) c.terminate();
      await new Promise<void>((r) => server.close(() => r()));
      server.closeAllConnections?.();
    },
  };
}

let tokenDir: string;
let savedDir: string | undefined;
const open: FakeBridge[] = [];
const bridges: EditorBridge[] = [];

beforeEach(() => {
  tokenDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-token-"));
  savedDir = process.env.UE_MCP_BRIDGE_TOKEN_DIR;
  process.env.UE_MCP_BRIDGE_TOKEN_DIR = tokenDir;
});

afterEach(async () => {
  for (const b of bridges.splice(0)) b.disconnect();
  for (const f of open.splice(0)) await f.close();
  if (savedDir === undefined) delete process.env.UE_MCP_BRIDGE_TOKEN_DIR;
  else process.env.UE_MCP_BRIDGE_TOKEN_DIR = savedDir;
  fs.rmSync(tokenDir, { recursive: true, force: true });
});

async function fake(opts: { requireToken: boolean; publish?: boolean }): Promise<FakeBridge> {
  const f = await startFakeBridge(tokenDir, opts);
  open.push(f);
  return f;
}

function editorBridge(port: number): EditorBridge {
  const b = new EditorBridge("127.0.0.1", port);
  bridges.push(b);
  return b;
}

describe("EditorBridge and the session token", () => {
  it("reads the token file for its port and sends it as a bearer token", async () => {
    const f = await fake({ requireToken: true });
    const bridge = editorBridge(f.port);
    await bridge.connect(3000);
    expect(f.seen).toEqual([`Bearer ${f.token}`]);
    await expect(bridge.call("ping")).resolves.toEqual({ ok: true, method: "ping" });
  });

  it("connects without a header when no token file exists, so an older plugin still works", async () => {
    const f = await fake({ requireToken: false });
    const bridge = editorBridge(f.port);
    await bridge.connect(3000);
    expect(f.seen).toEqual([undefined]);
    await expect(bridge.call("ping")).resolves.toEqual({ ok: true, method: "ping" });
  });

  it("turns a 401 for a missing token into an actionable error, quickly", async () => {
    const f = await fake({ requireToken: true, publish: false });
    const bridge = editorBridge(f.port);
    const started = Date.now();
    const err = await bridge.connect(5000).then(() => null, (e: Error) => e);
    expect(err).not.toBeNull();
    expect(err!.message).toContain("requires a session token");
    expect(err!.message).toContain(bridgeTokenPath(f.port));
    expect(err!.message).toContain("same OS user");
    expect(err!.message).toContain("Update ue-mcp");
    expect(err!.message).not.toContain("Unexpected server response");
    expect(Date.now() - started).toBeLessThan(2000);
  });

  it("names a stale token as stale", async () => {
    const f = await fake({ requireToken: true });
    fs.writeFileSync(bridgeTokenPath(f.port), "0".repeat(64), { mode: 0o600 });
    const bridge = editorBridge(f.port);
    const err = await bridge.connect(3000).then(() => null, (e: Error) => e);
    expect(f.seen).toEqual([`Bearer ${"0".repeat(64)}`]);
    expect(err!.message).toContain("rejected the session token");
  });

  it("refuses to use a token file that does not hold a token", async () => {
    const f = await fake({ requireToken: true });
    fs.writeFileSync(bridgeTokenPath(f.port), "not a token", { mode: 0o600 });
    const bridge = editorBridge(f.port);
    const err = await bridge.connect(3000).then(() => null, (e: Error) => e);
    expect(err!.message).toContain("does not hold a token");
    expect(f.seen).toEqual([]);
  });

  it("sends the token on a one-off call too", async () => {
    const f = await fake({ requireToken: true });
    const reply = await callBridgeOnce("127.0.0.1", f.port, "ping", {}, 3000);
    expect(reply.answered).toBe(true);
    expect(f.seen).toEqual([`Bearer ${f.token}`]);
  });
});

describe("token file location and checks", () => {
  it("honours UE_MCP_BRIDGE_TOKEN_DIR", () => {
    expect(bridgeTokenDir()).toBe(tokenDir);
    expect(bridgeTokenPath(51234)).toBe(path.join(tokenDir, "51234.token"));
  });

  it("defaults to ~/.ue-mcp/bridge, the directory the plugin writes to", () => {
    expect(bridgeTokenDir({})).toBe(path.join(os.homedir(), ".ue-mcp", "bridge"));
  });

  it("returns null and no headers when there is no file", () => {
    expect(readBridgeToken(51234)).toBeNull();
    expect(bridgeAuthHeaders(51234)).toEqual({});
  });

  it("reads a token, trimming a trailing newline", () => {
    const token = "ab".repeat(32);
    fs.writeFileSync(path.join(tokenDir, "51234.token"), `${token}\n`, { mode: 0o600 });
    expect(readBridgeToken(51234)).toBe(token);
    expect(bridgeAuthHeaders(51234)).toEqual({ Authorization: `Bearer ${token}` });
  });

  it.skipIf(process.platform === "win32")("refuses a token file other users can read (POSIX modes do not exist on Windows; the plugin sets an owner-only DACL there)", () => {
    const file = path.join(tokenDir, "51234.token");
    fs.writeFileSync(file, "ab".repeat(32), { mode: 0o600 });
    fs.chmodSync(file, 0o644);
    expect(() => readBridgeToken(51234)).toThrow(BridgeTokenError);
    expect(() => readBridgeToken(51234)).toThrow(/readable by other users/);
    fs.chmodSync(file, 0o600);
    expect(readBridgeToken(51234)).toBe("ab".repeat(32));
  });

  it("refuses a token path that is not a regular file", () => {
    fs.mkdirSync(path.join(tokenDir, "51234.token"));
    expect(() => readBridgeToken(51234)).toThrow(/not a regular file/);
  });
});

describe("scripts/lib/bridge-token.mjs", () => {
  it("agrees with the TypeScript module on the path and the headers", () => {
    const token = "cd".repeat(32);
    fs.writeFileSync(path.join(tokenDir, "40000.token"), token, { mode: 0o600 });
    expect(tokenMjs.bridgeTokenDir()).toBe(bridgeTokenDir());
    expect(tokenMjs.bridgeTokenDir({})).toBe(bridgeTokenDir({}));
    expect(tokenMjs.bridgeTokenPath(40000)).toBe(bridgeTokenPath(40000));
    expect(tokenMjs.bridgeAuthHeaders(40000)).toEqual(bridgeAuthHeaders(40000));
    expect(tokenMjs.bridgeAuthHeaders(40001)).toEqual({});
  });

  it("askOnce sends the token", async () => {
    const f = await fake({ requireToken: true });
    await expect(askOnce(`ws://127.0.0.1:${f.port}`, "ping", {}, 3000)).resolves.toEqual({ ok: true, method: "ping" });
    expect(f.seen).toEqual([`Bearer ${f.token}`]);
  });

  it("askOnce explains a 401", async () => {
    const f = await fake({ requireToken: true, publish: false });
    await expect(askOnce(`ws://127.0.0.1:${f.port}`, "ping", {}, 3000)).rejects.toThrow(/requires a session token/);
  });
});
