/**
 * The per-project daemon: one detached process per project root that owns the
 * editor bridge, survives editor restarts, and serves every MCP client (through
 * the stdio shim) and the UI over loopback HTTP.
 *
 * Endpoints, all on 127.0.0.1 and all requiring the bearer token from the
 * discovery file:
 *   GET  /v1/health     what the daemon is and what it is attached to
 *   GET  /v1/events     server-sent events, resumable by Last-Event-ID
 *   POST /v1/shutdown   exit now
 *   POST /v1/handoff    exit once no call is in flight (a newer daemon is taking over)
 *   WS   /v1/mcp        one MCP session per socket
 */
import * as fs from "node:fs";
import * as http from "node:http";
import * as path from "node:path";
import { WebSocketServer } from "ws";
import { info, warn, error } from "../core/log.js";
import { readEnv } from "../core/env.js";
import { packageVersion } from "../core/package-root.js";
import { normalizeProjectRoot } from "../bridge/port.js";
import { bridgeInstancesDir, findLiveInstanceRecord, type BridgeInstanceRecord } from "../bridge/editor-target.js";
import type { EditorSession } from "../sessions/session.js";
import { createRuntime, type Runtime } from "../server/runtime.js";
import { createMcpServer } from "../server/mcp-server.js";
import {
  DAEMON_API_VERSION,
  newToken,
  readLiveDiscovery,
  removeDiscovery,
  writeDiscovery,
  type DaemonDiscovery,
} from "./discovery.js";
import { EventLog } from "./events.js";
import { ExpectedDisconnect, settleAndClassify, type DisconnectCause } from "./disconnect.js";
import { WebSocketServerTransport } from "./ws-transport.js";

const DEFAULT_IDLE_MS = 30 * 60_000;

export interface EditorState {
  name: string;
  connected: boolean;
  port: number;
  pid: number | null;
  lastDisconnect: { cause: DisconnectCause; detail?: string; at: string } | null;
}

export interface Daemon {
  port: number;
  token: string;
  events: EventLog;
  runtime: Runtime;
  expected: ExpectedDisconnect;
  editors(): EditorState[];
  /** MCP sessions currently open. */
  clientCount(): number;
  /** Requests received and not yet answered, across all MCP sessions. */
  inflight(): number;
  close(): Promise<void>;
}

export interface DaemonOptions {
  /** .uproject paths; the first is the project the daemon is keyed by. */
  projects: string[];
  idleMs?: number;
  /** Fixed port, for tests. 0 picks a free one. */
  port?: number;
  /** Skip writing the discovery file, for tests that run several daemons. */
  publish?: boolean;
}

export async function startDaemon(opts: DaemonOptions): Promise<Daemon> {
  const rt = await createRuntime(opts.projects);
  const projectDir = rt.project.projectDir ?? process.cwd();
  const events = new EventLog();
  const expected = new ExpectedDisconnect();
  const token = newToken();
  const startedAt = new Date().toISOString();
  const idleMs = opts.idleMs ?? (Number(readEnv("daemonIdleMs")) || DEFAULT_IDLE_MS);

  // ── Editor attachment ───────────────────────────────────────────
  const states = new Map<EditorSession, EditorState>();
  const records = new Map<EditorSession, BridgeInstanceRecord | null>();
  const watchers: fs.FSWatcher[] = [];

  const follow = (session: EditorSession): void => {
    if (states.has(session)) return;
    const state: EditorState = {
      name: session.name,
      connected: session.bridge.isConnected,
      port: session.bridge.port,
      pid: null,
      lastDisconnect: null,
    };
    states.set(session, state);
    const dir = session.project.projectDir;

    session.bridge.onConnectionChanged((change) => {
      state.port = change.port;
      if (change.connected) {
        const record = dir ? findLiveInstanceRecord(dir) : null;
        records.set(session, record && record.port === change.port ? record : null);
        state.connected = true;
        state.pid = records.get(session)?.pid ?? null;
        expected.clear();
        events.publish("editor.connected", {
          editor: session.name,
          port: change.port,
          pid: state.pid,
          pluginVersion: records.get(session)?.pluginVersion ?? null,
        });
        return;
      }
      state.connected = false;
      const record = records.get(session) ?? null;
      const announced = expected.get();
      void settleAndClassify({ record, expected: announced }).then((cause) => {
        state.lastDisconnect = { cause, detail: change.detail, at: new Date().toISOString() };
        events.publish("editor.disconnected", { editor: session.name, cause, detail: change.detail ?? null, pid: record?.pid ?? null });
      });
    });

    // Reattach as soon as an editor publishes its record, not at the next tick.
    if (dir) {
      const instances = bridgeInstancesDir(dir);
      try {
        fs.mkdirSync(instances, { recursive: true });
        let timer: NodeJS.Timeout | null = null;
        const watcher = fs.watch(instances, () => {
          if (session.bridge.isConnected) return;
          if (timer) clearTimeout(timer);
          timer = setTimeout(() => void pokeWithBackoff(session), 250);
        });
        watchers.push(watcher);
      } catch (e) {
        warn("daemon", `cannot watch ${instances}; reattach falls back to the reconnect interval`, e);
      }
    }
  };

  const pokeWithBackoff = async (session: EditorSession): Promise<void> => {
    for (const delay of [0, 500, 1000, 2000, 4000]) {
      if (delay) await new Promise((r) => setTimeout(r, delay));
      if (await session.bridge.pokeReconnect()) return;
    }
  };

  for (const s of rt.sessions.list()) follow(s);
  rt.onSurfaceChanged(() => {
    for (const s of rt.sessions.list()) follow(s);
  });

  // ── MCP over WebSocket ──────────────────────────────────────────
  const wss = new WebSocketServer({ noServer: true });
  let clients = 0;
  const transports = new Set<WebSocketServerTransport>();

  wss.on("connection", (ws, req) => {
    clients += 1;
    const clientName = String(req.headers["x-ue-mcp-client"] ?? "unknown");
    const transport = new WebSocketServerTransport(ws);
    transports.add(transport);
    const { server, dispose } = createMcpServer(rt);
    events.publish("client.connected", { client: clientName, clients });
    void server.connect(transport);
    ws.on("close", () => {
      clients -= 1;
      transports.delete(transport);
      dispose();
      void server.close().catch(() => {});
      events.publish("client.disconnected", { client: clientName, clients });
      touch();
    });
  });

  const inflight = (): number => [...transports].reduce((n, t) => n + t.open.size, 0);

  // ── HTTP ────────────────────────────────────────────────────────
  const sse = new Set<http.ServerResponse>();
  const authorized = (req: http.IncomingMessage): boolean => {
    const header = req.headers.authorization;
    if (header === `Bearer ${token}`) return true;
    // EventSource cannot set headers; the stream alone also takes the token as a query.
    const url = new URL(req.url ?? "/", "http://127.0.0.1");
    return url.pathname === "/v1/events" && url.searchParams.get("token") === token;
  };

  const health = () => ({
    ok: true,
    pid: process.pid,
    version: packageVersion(),
    apiVersion: DAEMON_API_VERSION,
    projectRoot: normalizeProjectRoot(projectDir),
    startedAt,
    editors: editors(),
    clients,
    inflight: inflight(),
    lastEventId: events.lastId,
  });
  const editors = (): EditorState[] => rt.sessions.list().map((s) => states.get(s) ?? {
    name: s.name, connected: s.bridge.isConnected, port: s.bridge.port, pid: null, lastDisconnect: null,
  });

  let closing: Promise<void> | null = null;
  const httpServer = http.createServer((req, res) => {
    if (!authorized(req)) {
      res.writeHead(401).end();
      return;
    }
    const url = new URL(req.url ?? "/", "http://127.0.0.1");
    if (req.method === "GET" && url.pathname === "/v1/health") {
      res.writeHead(200, { "content-type": "application/json" }).end(JSON.stringify(health()));
      return;
    }
    if (req.method === "GET" && url.pathname === "/v1/events") {
      const last = Number(req.headers["last-event-id"] ?? url.searchParams.get("since") ?? 0) || 0;
      res.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-cache", connection: "keep-alive" });
      const write = (e: { id: number; type: string; at: string; data: Record<string, unknown> }) =>
        res.write(`id: ${e.id}\nevent: ${e.type}\ndata: ${JSON.stringify({ at: e.at, ...e.data })}\n\n`);
      const backlog = events.since(last);
      if (backlog.gap) res.write(`event: gap\ndata: {}\n\n`);
      for (const e of backlog.events) write(e);
      const unsubscribe = events.subscribe(write);
      sse.add(res);
      req.on("close", () => {
        unsubscribe();
        sse.delete(res);
        touch();
      });
      return;
    }
    if (req.method === "POST" && url.pathname === "/v1/shutdown") {
      res.writeHead(202).end();
      void shutdown("shutdown requested");
      return;
    }
    if (req.method === "POST" && url.pathname === "/v1/handoff") {
      res.writeHead(202).end();
      void handoff();
      return;
    }
    res.writeHead(404).end();
  });

  httpServer.on("upgrade", (req, socket, head) => {
    const url = new URL(req.url ?? "/", "http://127.0.0.1");
    if (url.pathname !== "/v1/mcp" || !authorized(req) || closing) {
      socket.write("HTTP/1.1 401 Unauthorized\r\n\r\n");
      socket.destroy();
      return;
    }
    wss.handleUpgrade(req, socket, head, (ws) => wss.emit("connection", ws, req));
  });

  await new Promise<void>((resolve, reject) => {
    httpServer.once("error", reject);
    httpServer.listen(opts.port ?? 0, "127.0.0.1", () => resolve());
  });
  const port = (httpServer.address() as { port: number }).port;

  // ── Idle exit and lifecycle ─────────────────────────────────────
  let lastActive = Date.now();
  function touch(): void {
    lastActive = Date.now();
  }
  const busy = (): boolean => clients > 0 || sse.size > 0 || editors().some((e) => e.connected);
  const idleTimer = setInterval(() => {
    if (busy()) {
      touch();
      return;
    }
    if (Date.now() - lastActive >= idleMs) void shutdown(`idle for ${Math.round(idleMs / 60_000)} min`);
  }, Math.min(60_000, Math.max(1000, Math.floor(idleMs / 4))));
  idleTimer.unref();

  async function handoff(): Promise<void> {
    info("daemon", "handoff requested; exiting once no call is in flight");
    while (inflight() > 0) await new Promise((r) => setTimeout(r, 250));
    await shutdown("handed off to a newer daemon");
  }

  function shutdown(reason: string): Promise<void> {
    if (closing) return closing;
    closing = (async () => {
      info("daemon", `stopping: ${reason}`);
      events.publish("daemon.stopping", { reason });
      clearInterval(idleTimer);
      for (const w of watchers) w.close();
      if (opts.publish !== false) removeDiscovery(projectDir);
      for (const res of sse) res.end();
      for (const ws of wss.clients) ws.close(1001, "daemon stopping");
      for (const s of rt.sessions.list()) s.bridge.disconnect();
      await new Promise<void>((r) => httpServer.close(() => r()));
    })();
    return closing;
  }

  // Bridges last, so every connect event lands on a listener.
  await rt.connectBridges();
  rt.logSummary();

  const discovery: DaemonDiscovery = {
    pid: process.pid,
    port,
    token,
    version: packageVersion(),
    apiVersion: DAEMON_API_VERSION,
    projectRoot: normalizeProjectRoot(projectDir),
    startedAt,
  };
  if (opts.publish !== false) writeDiscovery(projectDir, discovery);
  events.publish("daemon.started", { pid: process.pid, port, version: discovery.version });
  info("daemon", `listening on 127.0.0.1:${port} for ${path.basename(projectDir)}`);

  return {
    port,
    token,
    events,
    runtime: rt,
    expected,
    editors,
    clientCount: () => clients,
    inflight,
    close: () => shutdown("closed"),
  };
}

/**
 * `ue-mcp daemon run`: start in the foreground. A live daemon of an older
 * version is asked to hand off once this one is listening; a live one of the
 * same or newer version wins and this process exits.
 */
export async function runDaemon(projects: string[]): Promise<number> {
  if (projects.length === 0) {
    error("daemon", "no .uproject given");
    return 2;
  }
  const projectDir = path.dirname(path.resolve(projects[0]));
  const existing = readLiveDiscovery(projectDir);
  if (existing && !isOlder(existing.version, packageVersion())) {
    info("daemon", `daemon ${existing.version} already running (pid ${existing.pid}); not starting another`);
    return 0;
  }

  const daemon = await startDaemon({ projects, publish: !existing });
  if (existing) {
    await requestHandoff(existing);
    const deadline = Date.now() + 120_000;
    while (readLiveDiscovery(projectDir)?.pid === existing.pid && Date.now() < deadline) {
      await new Promise((r) => setTimeout(r, 250));
    }
    writeDiscovery(projectDir, {
      pid: process.pid,
      port: daemon.port,
      token: daemon.token,
      version: packageVersion(),
      apiVersion: DAEMON_API_VERSION,
      projectRoot: normalizeProjectRoot(projectDir),
      startedAt: new Date().toISOString(),
    });
  }

  const stop = (): void => void daemon.close().then(() => process.exit(0));
  process.on("SIGINT", stop);
  process.on("SIGTERM", stop);
  daemon.events.subscribe((e) => {
    if (e.type === "daemon.stopping") setTimeout(() => process.exit(0), 500).unref();
  });
  return new Promise<number>(() => {});
}

async function requestHandoff(d: DaemonDiscovery): Promise<void> {
  try {
    await fetch(`http://127.0.0.1:${d.port}/v1/handoff`, { method: "POST", headers: { authorization: `Bearer ${d.token}` } });
  } catch (e) {
    warn("daemon", `handoff request to pid ${d.pid} failed`, e);
  }
}

/** True when a is an older release than b. Prerelease ordering follows semver loosely. */
export function isOlder(a: string, b: string): boolean {
  const parse = (v: string) => {
    const [core, pre] = v.split("-", 2);
    return { nums: core.split(".").map((n) => Number(n) || 0), pre: pre ?? null };
  };
  const x = parse(a);
  const y = parse(b);
  for (let i = 0; i < 3; i++) {
    if ((x.nums[i] ?? 0) !== (y.nums[i] ?? 0)) return (x.nums[i] ?? 0) < (y.nums[i] ?? 0);
  }
  if (x.pre === y.pre) return false;
  if (x.pre === null) return false;
  if (y.pre === null) return true;
  return x.pre.localeCompare(y.pre, undefined, { numeric: true }) < 0;
}
