/**
 * The per-project daemon: one detached process per project root that owns the
 * editor bridge, survives editor restarts, and serves every MCP client (through
 * the stdio shim) and the UI over loopback HTTP.
 *
 * Endpoints, all on 127.0.0.1 and all requiring the bearer token from the
 * discovery file:
 *   GET  /v1/health     what the daemon is and what it is attached to
 *   GET  /v1/events     server-sent events, resumable by Last-Event-ID
 *   GET  /v1/install    the project's install state, as `ue-mcp status --json` reports it
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
  projectKey,
  readLiveDiscovery,
  removeDiscovery,
  writeDiscovery,
  type DaemonDiscovery,
} from "./discovery.js";
import { EventLog } from "./events.js";
import { ExpectedDisconnect, settleAndClassify } from "./disconnect.js";
import { WebSocketServerTransport } from "./ws-transport.js";
import { handleFlowRoute, hostIsAllowed } from "../flow/http-server.js";
import { userDir } from "../core/user-dir.js";
import { readRegistryAuth } from "../extensions/registry-auth.js";
import { EXTENSION_API_VERSION, type DaemonExtensionApi, type DaemonHealth, type EditorStatus } from "./extension-api.js";
import { loadExtensions, matchRoute, normalizeResponse, type LoadedExtension } from "./extensions.js";
import { UiBundleStore, contentType } from "./ui-bundles.js";
import { collectStatus } from "../cli/status.js";
import { subscribeFlowEvents } from "../flow/events.js";

const DEFAULT_IDLE_MS = 30 * 60_000;

export type EditorState = EditorStatus;

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
  extensions: LoadedExtension[];
  ui: UiBundleStore;
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
  /** Where extensions are loaded from. Defaults to extensionsDir(). */
  extensionsDir?: string;
  /** Where UI bundles are kept. Defaults to uiStoreDir(). */
  uiDir?: string;
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

  // Flow progress joins the same resumable stream the UI reads.
  const unsubscribeFlows = subscribeFlowEvents((e) => {
    events.publish(`flow.${e.type}`, { ...(e as unknown as Record<string, unknown>) });
  });

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
  // A page served from somewhere else must not drive the editor, even holding a token.
  const originAllowed = (req: http.IncomingMessage): boolean => {
    const origin = req.headers.origin;
    if (origin === undefined) return true;
    return origin === `http://127.0.0.1:${port}` || origin === `http://localhost:${port}`;
  };
  const cookieToken = (req: http.IncomingMessage): string | null => {
    const m = /(?:^|;\s*)ue_mcp_token=([0-9a-f]+)/.exec(req.headers.cookie ?? "");
    return m ? m[1] : null;
  };
  const authorized = (req: http.IncomingMessage): boolean => {
    if (!hostIsAllowed(req) || !originAllowed(req)) return false;
    const header = req.headers.authorization;
    if (header === `Bearer ${token}`) return true;
    // The served page authenticates by cookie, set once from ?token= on /ui.
    if (cookieToken(req) === token) return true;
    // EventSource cannot set headers; the stream alone also takes the token as a query.
    const url = new URL(req.url ?? "/", "http://127.0.0.1");
    return url.pathname === "/v1/events" && url.searchParams.get("token") === token;
  };

  const health = (): DaemonHealth => ({
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
    extensions: extensions.map((x) => ({ name: x.name, version: x.version })),
    ui: ui.active()?.manifest.version ?? null,
  });
  const editors = (): EditorState[] => rt.sessions.list().map((s) => states.get(s) ?? {
    name: s.name, connected: s.bridge.isConnected, port: s.bridge.port, pid: null, lastDisconnect: null,
  });

  // ── Editor operations ───────────────────────────────────────────
  // The daemon owns these, so it can say beforehand why the editor is about to
  // go away and the disconnect is classified as that rather than guessed.
  const editorOp = async (op: string): Promise<Record<string, unknown>> => {
    const text = (r: { content?: Array<{ text?: string }> }) => r.content?.map((c) => c.text ?? "").join("\n") ?? "";
    const step = async (category: string, action: string) => {
      events.publish("editor.operation", { op, step: `${category}.${action}`, phase: "started" });
      const result = await rt.callAction(category, action);
      events.publish("editor.operation", { op, step: `${category}.${action}`, phase: result.isError ? "failed" : "done" });
      return { step: `${category}.${action}`, ok: !result.isError, result: text(result) };
    };
    if (op === "start") return { op, steps: [await step("editor", "start_editor")] };
    if (op === "stop") return { op, steps: [await step("editor", "stop_editor")] };
    if (op === "restart") {
      expected.expect("restarting");
      return { op, steps: [await step("editor", "restart_editor")] };
    }
    expected.expect("rebuild");
    const steps = [await step("editor", "stop_editor")];
    const built = await step("project", "build");
    steps.push(built);
    // A failed build leaves the editor down rather than relaunching the old binary.
    if (built.ok) steps.push(await step("editor", "start_editor"));
    else expected.clear();
    return { op, steps };
  };

  // ── UI bundles and extensions ───────────────────────────────────
  const ui = new UiBundleStore(DAEMON_API_VERSION, opts.uiDir);
  const extensions: LoadedExtension[] = await loadExtensions((name, routes, setBusy): DaemonExtensionApi => ({
    apiVersion: EXTENSION_API_VERSION,
    version: packageVersion(),
    projectDir,
    dataDir: (() => {
      const dir = path.join(userDir(), "data", projectKey(projectDir), name);
      fs.mkdirSync(dir, { recursive: true });
      return dir;
    })(),
    events: {
      publish: (type, data) => events.publish(`${name}.${type}`, data),
      subscribe: (listener) => events.subscribe(listener),
      since: (lastId) => events.since(lastId),
    },
    route: (method, routePath, handler) => {
      routes.push({ method, path: routePath, handler });
    },
    callAction: async (category, action, args) => {
      const r = await rt.callAction(category, action, args);
      return { isError: !!r.isError, content: r.content };
    },
    editors: () => editors(),
    editorOperation: (op) => editorOp(op),
    setBusy,
    ui: {
      trustKey: (pem) => ui.trustKey(pem),
      install: (dir) => ui.install(dir),
      active: () => ui.active()?.manifest ?? null,
    },
    registryAccount: async () => {
      const auth = await readRegistryAuth();
      return auth ? { login: auth.login, token: auth.token, registry: auth.registry } : null;
    },
    log: {
      info: (message) => info(`ext:${name}`, message),
      warn: (message, detail) => warn(`ext:${name}`, message, detail),
    },
  }), opts.extensionsDir);

  const readBody = (req: http.IncomingMessage): Promise<unknown> => new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    req.on("data", (c: Buffer) => chunks.push(c));
    req.on("end", () => {
      const raw = Buffer.concat(chunks).toString("utf-8").trim();
      if (!raw) return resolve(undefined);
      try {
        resolve(JSON.parse(raw));
      } catch (e) {
        reject(e);
      }
    });
    req.on("error", reject);
  });

  const json = (res: http.ServerResponse, status: number, body: unknown): void => {
    res.writeHead(status, { "content-type": "application/json" }).end(JSON.stringify(body));
  };

  let closing: Promise<void> | null = null;
  const httpServer = http.createServer((req, res) => {
    const first = new URL(req.url ?? "/", "http://127.0.0.1");
    if (req.method === "GET" && (first.pathname === "/ui" || first.pathname === "/ui/") && first.searchParams.get("token") === token
      && hostIsAllowed(req)) {
      res.writeHead(302, {
        "set-cookie": `ue_mcp_token=${token}; HttpOnly; SameSite=Strict; Path=/`,
        location: "/ui/",
      }).end();
      return;
    }
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
    if (req.method === "GET" && url.pathname === "/v1/install") {
      // The same read-only report as `ue-mcp status --json`.
      try {
        json(res, 200, collectStatus(rt.project.projectPath ?? opts.projects[0]));
      } catch (e) {
        json(res, 500, { error: e instanceof Error ? e.message : String(e) });
      }
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
    const op = /^\/v1\/editor\/(start|stop|restart|rebuild)$/.exec(url.pathname);
    if (req.method === "POST" && op) {
      editorOp(op[1]).then(
        (result) => json(res, 200, result),
        (e) => json(res, 500, { error: e instanceof Error ? e.message : String(e) }),
      );
      return;
    }
    if (req.method === "GET" && (url.pathname === "/ui" || url.pathname.startsWith("/ui/"))) {
      const file = ui.resolve(url.pathname);
      if (!file) {
        const none = ui.active() === null;
        res.writeHead(none ? 503 : 404, { "content-type": "text/plain; charset=utf-8" })
          .end(none ? "No client UI is installed for this daemon." : "Not found.");
        return;
      }
      res.writeHead(200, {
        "content-type": contentType(file),
        "cache-control": "no-cache",
        "x-content-type-options": "nosniff",
      });
      fs.createReadStream(file).pipe(res);
      return;
    }
    if (url.pathname.startsWith("/v1/ext/")) {
      const hit = matchRoute(extensions, req.method ?? "GET", url.pathname);
      if (!hit) {
        res.writeHead(404).end();
        return;
      }
      readBody(req)
        .then((body) => hit.route.handler({
          method: req.method ?? "GET",
          path: url.pathname.slice(`/v1/ext/${hit.ext.name}`.length) || "/",
          params: hit.params,
          query: Object.fromEntries(url.searchParams),
          headers: req.headers,
          body,
        }))
        .then((out) => {
          const n = normalizeResponse(out ?? undefined);
          res.writeHead(n.status, n.headers).end(n.body);
        }, (e) => json(res, 500, { error: e instanceof Error ? e.message : String(e) }));
      return;
    }
    if (url.pathname === "/v1/flows" || url.pathname.startsWith("/v1/flows/")) {
      const sub = url.pathname.slice("/v1".length).replace(/\/+$/, "");
      handleFlowRoute(rt.flowTool, rt.baseCtx, req, res, sub, url).then(
        (handled) => { if (!handled) res.writeHead(404).end(); },
        (e) => json(res, 500, { error: e instanceof Error ? e.message : String(e) }),
      );
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
  const busy = (): boolean => clients > 0 || sse.size > 0 || editors().some((e) => e.connected) || extensions.some((x) => x.busy());
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
      unsubscribeFlows();
      for (const x of extensions) await Promise.resolve(x.dispose?.()).catch((e) => warn("extension", `${x.name} dispose failed`, e));
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
    extensions,
    ui,
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
