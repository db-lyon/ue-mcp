/**
 * A stand-in for one editor's bridge, on a real loopback socket (#817, plan 7.1).
 *
 * The two-editor cases the plan calls for are about routing: a call addressed
 * to one editor has to arrive at that editor and nowhere else. Proving that
 * needs two bridges answering independently, which is exactly what CI cannot
 * provide - the runners have no Unreal engine, so there is no second editor to
 * start. What CI can provide is two sockets.
 *
 * This speaks the wire protocol the client speaks: a WebSocket carrying
 * `{id, method, params}` requests and `{id, result}` or `{id, error}` replies,
 * plus the port lockfile the client discovers a bridge through. Everything
 * about identity and routing is therefore exercised for real; only the engine
 * behind the handler is fake.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { WebSocketServer, type WebSocket } from "ws";

/** One call this bridge received. */
export interface RecordedCall {
  method: string;
  params: Record<string, unknown>;
}

export interface FakeBridgeOptions {
  /** Project root this bridge claims. A lockfile is published under it. */
  projectDir: string;
  /** Methods this bridge knows. Anything else answers as an unknown method. */
  handlers?: Record<string, (params: Record<string, unknown>) => unknown>;
  /** Answer every unlisted method with a generic success instead of an error. */
  answerAnything?: boolean;
}

export class FakeBridge {
  private server: WebSocketServer;
  private sockets = new Set<WebSocket>();
  readonly calls: RecordedCall[] = [];
  readonly projectDir: string;
  private readonly handlers: Record<string, (params: Record<string, unknown>) => unknown>;
  private readonly answerAnything: boolean;

  private constructor(server: WebSocketServer, opts: FakeBridgeOptions) {
    this.server = server;
    this.projectDir = opts.projectDir;
    this.handlers = opts.handlers ?? {};
    this.answerAnything = opts.answerAnything ?? true;

    server.on("connection", (ws) => {
      this.sockets.add(ws);
      ws.on("close", () => this.sockets.delete(ws));
      ws.on("message", (data) => {
        let msg: { id?: string; method?: string; params?: Record<string, unknown> };
        try {
          msg = JSON.parse(data.toString());
        } catch {
          return;
        }
        if (!msg.id || !msg.method) return;
        const params = msg.params ?? {};

        // The capability handshake is part of connecting, not of routing, and
        // the client sends it before any caller has asked for anything. It is
        // answered but not recorded, so `methods` reads as the calls a test
        // actually made.
        if (msg.method === "get_bridge_capabilities") {
          ws.send(JSON.stringify({
            id: msg.id,
            result: {
              protocolVersion: 2,
              projectName: path.basename(this.projectDir),
              port: this.port,
              pid: process.pid,
            },
          }));
          return;
        }

        this.calls.push({ method: msg.method, params });

        // The lock registry is the one piece of editor state this stands in
        // for rather than stubs, because per-editor lock ownership is exactly
        // what the multi-editor tier is asserting. It lives in the bridge in
        // production too, so one registry per fake bridge is faithful.
        const lockReply = this.serveLock(msg.method, params);
        if (lockReply !== undefined) {
          ws.send(JSON.stringify({ id: msg.id, result: lockReply }));
          return;
        }

        const handler = this.handlers[msg.method];
        if (handler) {
          ws.send(JSON.stringify({ id: msg.id, result: handler(params) }));
          return;
        }
        if (this.answerAnything) {
          // Echo the project so a caller can prove which editor answered.
          ws.send(JSON.stringify({
            id: msg.id,
            result: { ok: true, servedBy: this.projectDir, method: msg.method },
          }));
          return;
        }
        ws.send(JSON.stringify({
          id: msg.id,
          error: { code: -32601, message: `Unknown method: ${msg.method}` },
        }));
      });
    });
  }

  /** Bind an ephemeral loopback port and publish this project's lockfile. */
  static async start(opts: FakeBridgeOptions): Promise<FakeBridge> {
    const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
    await new Promise<void>((resolve, reject) => {
      server.once("listening", () => resolve());
      server.once("error", reject);
    });
    const bridge = new FakeBridge(server, opts);
    bridge.writeLockfile();
    return bridge;
  }

  get port(): number {
    const addr = this.server.address();
    if (addr === null || typeof addr === "string") throw new Error("fake bridge is not listening on a port");
    return addr.port;
  }

  /** Publish the port record the client discovers this bridge through. */
  writeLockfile(): void {
    const dir = path.join(this.projectDir, "Saved", "UE_MCP_Bridge");
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(
      path.join(dir, "port.json"),
      JSON.stringify({
        port: this.port,
        pid: process.pid,
        startedAt: new Date().toISOString(),
        apiVersion: 1,
      }),
      "utf-8",
    );
  }

  /** Asset path to the session id holding its lock. */
  private readonly locks = new Map<string, string>();

  /** Answer the lock methods, or undefined for anything else. */
  private serveLock(method: string, params: Record<string, unknown>): unknown | undefined {
    const owner = String(params.sessionId ?? "");
    const assetPath = String(params.path ?? params.assetPath ?? "");
    switch (method) {
      case "acquire_lock": {
        const holder = this.locks.get(assetPath);
        // Re-entrant for the same owner, refused for anybody else. That
        // asymmetry is the whole reason the owner id has to be per editor.
        if (holder !== undefined && holder !== owner) {
          return { acquired: false, holder: { sessionId: holder, ttlSecondsRemaining: 60 } };
        }
        this.locks.set(assetPath, owner);
        return { acquired: true };
      }
      case "release_lock": {
        const holder = this.locks.get(assetPath);
        if (holder === owner || params.force === true) {
          this.locks.delete(assetPath);
          return { released: true };
        }
        return { released: false, holder: { sessionId: holder } };
      }
      case "release_session_locks": {
        let released = 0;
        for (const [asset, holder] of [...this.locks]) {
          if (holder !== owner) continue;
          this.locks.delete(asset);
          released++;
        }
        return { released };
      }
      case "list_locks":
        return { locks: [...this.locks].map(([path, sessionId]) => ({ path, sessionId })) };
      default:
        return undefined;
    }
  }

  /** Who currently holds a lock on an asset here, if anyone. */
  lockHolder(assetPath: string): string | undefined {
    return this.locks.get(assetPath);
  }

  /** Methods this bridge was asked for, in order. */
  get methods(): string[] {
    return this.calls.map((c) => c.method);
  }

  async stop(): Promise<void> {
    for (const ws of this.sockets) ws.terminate();
    this.sockets.clear();
    await new Promise<void>((resolve) => this.server.close(() => resolve()));
  }
}
