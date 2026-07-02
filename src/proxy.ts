import { WebSocketServer, WebSocket } from "ws";
import * as fs from "node:fs";
import * as path from "node:path";
import { createHash } from "node:crypto";
import { EditorBridge } from "./bridge.js";
import type { ProjectContext } from "./project.js";
import { debug, info, warn } from "./log.js";

/**
 * ue-mcp relay daemon.
 *
 * Unlike an in-editor MCP server (which dies when the editor restarts), our
 * stdio server is already external and reconnects on its own. The daemon adds
 * three production-grade properties on top of that:
 *
 *   1. Warm connection across MCP-client restarts. The daemon holds ONE live
 *      editor WebSocket. When Claude Code / Cursor / Desktop restart and respawn
 *      the stdio server, it attaches to the already-connected daemon instead of
 *      cold-reconnecting to the editor.
 *   2. A single multiplexed editor connection. Any number of concurrent client
 *      sessions share the daemon's one socket to the editor instead of each
 *      opening its own and contending.
 *   3. Blip tolerance. Brief editor drops (hot reload, quick restart) are
 *      absorbed: a request that lands while the editor is momentarily down is
 *      held for a short grace window and replayed on reconnect, instead of
 *      failing immediately with CONNECTION_LOST.
 *
 * The daemon speaks the exact same {id, method, params} / {id, result|error}
 * wire protocol as the C++ bridge, so a stdio EditorBridge talks to it with no
 * protocol change - it just points host/port at the daemon.
 */

/** Max time a single relayed call may run once the editor is connected. The
 *  client's own per-action timeout governs user-facing latency; this is only a
 *  backstop so a wedged editor cannot pin a pending relay forever. */
const RELAY_CEILING_MS = 15 * 60 * 1000;

/** How long to hold a request that arrives while a previously-connected editor
 *  is momentarily down, before giving up. Bounded well under the client's 30s
 *  default so a hot-reload blip is absorbed without stalling cold startup. */
const RELAY_GRACE_MS = 8000;

/** Shut the daemon down after this long with zero connected clients. */
const DEFAULT_IDLE_SHUTDOWN_MS = 5 * 60 * 1000;

export interface ProxyLockfile {
  port: number;
  host: string;
  pid: number;
  startedAt: string;
  projectPath?: string;
}

/**
 * Deterministic per-project loopback port. Each .uproject gets its own stable
 * proxy port so two projects never collide and the stdio client can find the
 * daemon without a discovery handshake. An explicit config/env port wins.
 */
export function proxyPortForProject(projectPath: string, configuredPort?: number): number {
  if (configuredPort && configuredPort > 0 && configuredPort < 65536) return configuredPort;
  const digest = createHash("sha256").update(path.resolve(projectPath)).digest();
  const folded = (digest[0] << 8) | digest[1]; // 0..65535
  // Dynamic/private range 49152..65535; keep a little headroom at the top.
  return 49152 + (folded % 16000);
}

function proxyLockfilePath(projectPath: string): string {
  return path.join(path.dirname(projectPath), "Saved", "UE_MCP_Bridge", "proxy.json");
}

export function readProxyLockfile(projectPath: string | null): ProxyLockfile | null {
  if (!projectPath) return null;
  try {
    const raw = fs.readFileSync(proxyLockfilePath(projectPath), "utf8");
    const parsed = JSON.parse(raw) as ProxyLockfile;
    if (typeof parsed?.port === "number" && parsed.port > 0 && parsed.port < 65536) return parsed;
  } catch {
    // Missing or unreadable.
  }
  return null;
}

function writeProxyLockfile(projectPath: string, lock: ProxyLockfile): void {
  const file = proxyLockfilePath(projectPath);
  try {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, JSON.stringify(lock, null, 2), "utf8");
  } catch (e) {
    warn("proxy", "failed to write proxy lockfile", e);
  }
}

function removeProxyLockfile(projectPath: string): void {
  try {
    fs.rmSync(proxyLockfilePath(projectPath), { force: true });
  } catch {
    // best-effort
  }
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

interface RelayMessage {
  id?: string | number;
  method?: unknown;
  params?: unknown;
}

/** The subset of EditorBridge that relayCall needs (keeps it unit-testable). */
export interface RelayBridge {
  readonly isConnected: boolean;
  ensureConnected(timeoutMs?: number): Promise<void>;
  call(method: string, params?: Record<string, unknown>, timeoutMs?: number): Promise<unknown>;
}

export interface RelayOptions {
  /** Whether the editor has ever connected (enables blip tolerance). */
  everConnected: () => boolean;
  graceMs?: number;
  ceilingMs?: number;
  probeMs?: number;
}

/**
 * Relay one client request to the editor. Fails fast when the editor has never
 * connected (cold startup, editor not running) so callers see NOT_CONNECTED
 * just like today. Once the editor has been seen at least once, a momentary
 * drop is tolerated for graceMs before giving up.
 */
export async function relayCall(
  editor: RelayBridge,
  method: string,
  params: Record<string, unknown>,
  opts: RelayOptions,
): Promise<unknown> {
  const graceMs = opts.graceMs ?? RELAY_GRACE_MS;
  const ceilingMs = opts.ceilingMs ?? RELAY_CEILING_MS;
  const probeMs = opts.probeMs ?? 2000;

  if (!editor.isConnected) {
    // One quick attempt regardless of history.
    await editor.ensureConnected(probeMs).catch(() => {});
    if (!editor.isConnected && opts.everConnected()) {
      // Editor was up before - treat this as a blip and hold briefly.
      const deadline = Date.now() + graceMs;
      while (!editor.isConnected && Date.now() < deadline) {
        await sleep(Math.min(500, graceMs));
        await editor.ensureConnected(probeMs).catch(() => {});
      }
    }
  }
  return editor.call(method, params, ceilingMs);
}

export interface StartProxyOptions {
  host?: string;
  port?: number;
  idleShutdownMs?: number;
}

/**
 * Start the relay daemon for a loaded project. Resolves once the WS hub is
 * listening (or exits the process on EADDRINUSE, meaning a daemon for this
 * project already owns the port).
 */
export async function startProxyDaemon(project: ProjectContext, opts: StartProxyOptions = {}): Promise<void> {
  if (!project.projectPath) throw new Error("startProxyDaemon requires a loaded project");
  const projectPath = project.projectPath;

  const host = opts.host ?? project.config.proxy?.host ?? process.env.UE_MCP_PROXY_HOST ?? "127.0.0.1";
  const port = opts.port ?? proxyPortForProject(projectPath, project.config.proxy?.port);
  const idleShutdownMs = opts.idleShutdownMs ?? DEFAULT_IDLE_SHUTDOWN_MS;

  // One warm connection to the editor, reusing all of EditorBridge's reconnect,
  // pending-map and per-project lockfile logic. Do NOT pass the proxy port here:
  // the editor lives on its own port (9877 / editor lockfile).
  const editor = new EditorBridge();
  editor.projectPathForLockfile = projectPath;
  let seenEditor = false;
  const markConnected = () => { seenEditor = true; };

  editor.connect().then(markConnected, () => {});
  editor.startReconnecting();
  // Poll the connection so `seenEditor` flips the first time the editor appears
  // even if that happens via the reconnect loop rather than the initial connect.
  const seenTimer = setInterval(() => { if (editor.isConnected) markConnected(); }, 2000);

  let clientCount = 0;
  let idleTimer: ReturnType<typeof setTimeout> | null = null;

  const shutdown = (reason: string, code = 0) => {
    info("proxy", `shutting down (${reason})`);
    clearInterval(seenTimer);
    if (idleTimer) clearTimeout(idleTimer);
    removeProxyLockfile(projectPath);
    editor.disconnect();
    try { wss.close(); } catch { /* ignore */ }
    process.exit(code);
  };

  const armIdle = () => {
    if (idleTimer) clearTimeout(idleTimer);
    idleTimer = setTimeout(() => {
      if (clientCount === 0) shutdown("idle - no clients");
    }, idleShutdownMs);
  };

  const wss = new WebSocketServer({ host, port });

  wss.on("error", (err: NodeJS.ErrnoException) => {
    if (err.code === "EADDRINUSE") {
      // Another daemon for this project already owns the port. Yield to it.
      info("proxy", `port ${host}:${port} already in use - a daemon is already running; exiting`);
      clearInterval(seenTimer);
      editor.disconnect();
      process.exit(0);
    }
    warn("proxy", "websocket server error", err);
  });

  wss.on("connection", (client: WebSocket) => {
    clientCount++;
    if (idleTimer) { clearTimeout(idleTimer); idleTimer = null; }
    debug("proxy", `client connected (${clientCount} active)`);

    client.on("message", async (data) => {
      let msg: RelayMessage;
      try {
        msg = JSON.parse(data.toString()) as RelayMessage;
      } catch {
        return; // ignore malformed frames
      }
      const { id, method, params } = msg;
      if (typeof method !== "string") return;

      try {
        const result = await relayCall(
          editor,
          method,
          (params as Record<string, unknown>) ?? {},
          { everConnected: () => seenEditor },
        );
        safeSend(client, { id, result });
      } catch (e) {
        const message = e instanceof Error ? e.message : String(e);
        safeSend(client, { id, error: { code: -1, message } });
      }
    });

    client.on("close", () => {
      clientCount = Math.max(0, clientCount - 1);
      debug("proxy", `client disconnected (${clientCount} active)`);
      if (clientCount === 0) armIdle();
    });

    client.on("error", (err) => debug("proxy", "client socket error", err));
  });

  await new Promise<void>((resolve) => {
    wss.on("listening", () => {
      writeProxyLockfile(projectPath, {
        port,
        host,
        pid: process.pid,
        startedAt: new Date().toISOString(),
        projectPath,
      });
      info("proxy", `relay listening on ws://${host}:${port} for ${path.basename(projectPath)} (loopback only)`);
      armIdle();
      resolve();
    });
  });

  for (const sig of ["SIGINT", "SIGTERM"] as const) {
    process.on(sig, () => shutdown(`received ${sig}`));
  }
}

function safeSend(ws: WebSocket, payload: unknown): void {
  if (ws.readyState !== WebSocket.OPEN) return;
  try {
    ws.send(JSON.stringify(payload));
  } catch (e) {
    debug("proxy", "failed to send to client", e);
  }
}
