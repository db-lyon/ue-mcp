// @session per-session: One daemon per project root: it owns that project's runtime and follows each of its editors.
/**
 * The per-project daemon: one detached process per project root that owns the
 * editor bridge, survives editor restarts, and serves every MCP client (through
 * the stdio shim) and the UI over loopback HTTP.
 *
 * Every route it serves is declared in DAEMON_ROUTES (contract/daemon.ts) and
 * answered through router.ts; the handlers are the RouteHandlers built below
 * and in the *-routes.ts modules.
 */
import * as fs from "node:fs";
import * as http from "node:http";
import * as net from "node:net";
import * as path from "node:path";
import { WebSocketServer } from "ws";
import { debug, info, warn, error, stampLogs } from "../core/log.js";
import { editorStepFailure } from "./editor-step.js";
import { readEnv } from "../core/env.js";
import { packageVersion } from "../core/package-root.js";
import { compareVersions } from "../core/semver.js";
import { hostIsAllowed, tokensMatch } from "../core/loopback-host.js";
import { StatusError } from "../core/status-error.js";
import { normalizeProjectRoot } from "../bridge/wire-contract.js";
import { bridgeInstancesDir, findLiveInstanceRecord, type BridgeInstanceRecord } from "../bridge/editor-target.js";
import type { EditorSession } from "../sessions/session.js";
import { createRuntime, type Runtime } from "../server/runtime.js";
import { toToolResult } from "../server/server-dispatch.js";
import type { InProcessCallOptions } from "../server/call-options.js";
import { startVersionCheck } from "../core/version-check.js";
import { createMcpServer } from "../server/mcp-server.js";
import { startFlowHttpServer } from "../flow/http-server.js";
import {
  DAEMON_API_VERSION,
  DAEMON_FEATURES,
  DAEMON_GAP_EVENT,
  DAEMON_HEADERS,
  EXTENSION_API_VERSION,
  EXTENSION_FEATURES,
  routePath,
  describeRoute,
  REGISTRY_ACCOUNT_PERMISSION_REQUIRED_FROM,
  type CallActionOptions,
  type DaemonDiscovery,
  type DaemonEvent,
  type DaemonEventType,
  type DaemonExtensionApi,
  type DaemonHealth,
  type EditorStatus,
  type McpSessionInfo,
  type NativeDialogPressResult,
  type NativeDialogView,
  type RefusedExtension,
} from "../contract/daemon.js";
import {
  discoveryIsStale,
  newToken,
  projectKey,
  readLiveDiscovery,
  removeDiscovery,
  writeDiscovery,
} from "./discovery.js";
import { daemonRequest } from "./client.js";
import { EventLog } from "./events.js";
import { ExpectedDisconnect, settleAndClassify } from "./disconnect.js";
import { WebSocketServerTransport } from "./ws-transport.js";
import { watchDir, type DirWatch, type WatchFn } from "./dir-watch.js";
import { drainInflight, watchSupersede } from "./supersede-watch.js";
import { describeToolCall, resultError, type CallLabel } from "./activity.js";
import { openUrl } from "../core/open-url.js";
import { handleFlowRoute } from "../flow/http-server.js";
import { userDir } from "../core/user-dir.js";
import { readRegistryAuth } from "../extensions/registry-auth.js";
import { DIALOG_MODE, FEEDBACK_MODE, PLAYTEST_MODE } from "../config/modes.js";
import { resolvePlaytestMode } from "../editor/playtest-mode.js";
import { randomUUID } from "node:crypto";
import { SessionAccess, parseSessionAccessMode, SESSION_ACCESS_MODES } from "../flow/session-access.js";
import { ensureGuard, existingGuard, nativeAsBlocking, onDialogEvent } from "../editor/dialog-guard.js";
import { nativeDialogInfo, nativeDialogsForProject } from "../editor/native-dialogs.js";
import { createRouter, HANDLED, objectBody, ok, resolveEditorSession, type RouteHandlers, type RouteRequest, type RouteResult } from "./router.js";
import {
  DISRUPTION_VOTES,
  DISRUPTIVE_OPS,
  DisruptionError,
  type DisruptionCaller,
  type DisruptionVote,
  type DisruptiveOp,
  type SubagentAttestation,
} from "../dispatch/disruption.js";
import { askConsent, currentDisruptionCaller, processCaller, withDisruptionGrant } from "../dispatch/disruption-gate.js";
import { extensionsDir, loadExtensions, matchRoute, normalizeResponse, type LoadedExtension } from "./extensions.js";
import { installedCopies, recordedVersion, versionsOf } from "./extension-versions.js";
import { UiBundleStore, contentType, type UiBundleOwner } from "./ui-bundles.js";
import { ExtensionServices } from "./extension-services.js";
import { collectStatus } from "../integrations/install-status.js";
import { resolvePluginDir } from "../editor/install-marker.js";
import { editorOwnsProject, probeEditorProcessList } from "../editor/engine-observer.js";
import { applyUpdate, checkForUpdate, readPendingUpdate, repinClientConfigs, settlePendingUpdate } from "../editor/bridge-update.js";
import { subscribeFlowEvents } from "../flow/events.js";
import { registerModelProvider } from "../flow/model-provider.js";
import { registerFlowStepApprover } from "../flow/step-approval.js";
import { subscribeScriptEvents } from "../pie/script-events.js";
import { stopAllScriptWatches } from "../pie/script-watch.js";
import { pluginRoutes } from "./plugin-routes.js";
import { releaseStartLock, waitForStartTurn } from "./start-lock.js";
import { checkpointRoutes } from "./checkpoint-routes.js";
import { journalRoutes } from "./journal-routes.js";
import { hubRoutes } from "./hub-routes.js";
import { HubHolder, registerHub } from "../hub/hub-client.js";
import { createDaemonHub } from "../hub/daemon-hub.js";
import { toolsRoutes } from "./tools-routes.js";
import { checkpointsFor, subscribeCheckpointEvents } from "../checkpoints/service.js";
import { listDaemonSkills } from "../extensions/skills-list.js";
import { ContextProjectionError, ContextProjector, enabledGraph } from "../server/context-projection.js";
import { actionEffectInfo } from "../surface/action-effects.js";
import { PIE_RESYNC_TIMEOUT_MS, pieEventFromNotification, pieEventFromStatus } from "./pie-events.js";
import type { ContextStrategy } from "../config/context-strategy.js";

const DEFAULT_IDLE_MS = 30 * 60_000;
const DEFAULT_WAIT_FOR_EDITOR_SECONDS = 60;

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
  /** A same-or-newer daemon that published first; this one did not publish and should close. */
  superseded: DaemonDiscovery | null;
  /** Serve the flow HTTP surface if ue-mcp.http enables it. Called once this daemon owns the project. */
  startFlowHttp(): void;
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
  /** Replaces fs.watch for the bridge instances folder, for tests. */
  watch?: WatchFn;
}

/** Who asks for an editor operation, and on what terms (#2075). */
interface EditorOpConsent {
  /** Absent: the caller in scope (a flow's session), else the daemon. */
  caller?: DisruptionCaller;
  /** A person asked, so no subagent attestation is owed. */
  person?: boolean;
  subagents?: SubagentAttestation;
  reason?: string;
  /** What asked, for the request. */
  action?: string;
  /** A person overrides at once; the value is who. */
  force?: string;
}

/** How often the daemon checks that it can still answer, and how long a hold is worth a warning. */
const STALL_BEAT_MS = 1000;
const STALL_REPORT_MS = 3000;

/** An extension's call options, as the runtime takes them. */
function callOptions(o: CallActionOptions | undefined): InProcessCallOptions {
  return {
    ...(o?.dialogPrompt ? { dialogPrompt: o.dialogPrompt } : {}),
    ...(o?.dialogWaitMs !== undefined ? { dialogWaitMs: o.dialogWaitMs } : {}),
    ...(o?.elicit ? { elicit: o.elicit } : {}),
    ...(o?.access ? { access: o.access } : {}),
    ...(o?.signal ? { signal: o.signal } : {}),
    ...(o?.session && typeof o.session.id === "string" && o.session.id !== "" ? { session: o.session } : {}),
  };
}

/** A comma list from the query, or undefined when the parameter is absent. */
function listParam(query: URLSearchParams, name: string): string[] | undefined {
  const raw = query.get(name);
  return raw === null ? undefined : raw.split(",").map((c) => c.trim()).filter(Boolean);
}

/** The discovery record this process publishes. */
function discoveryRecord(port: number, token: string, projectDir: string, startedAt: string): DaemonDiscovery {
  return {
    pid: process.pid,
    port,
    token,
    version: packageVersion(),
    apiVersion: DAEMON_API_VERSION,
    projectRoot: normalizeProjectRoot(projectDir),
    startedAt,
  };
}

/** Whether anything accepts a connection on this loopback port; a closed port refuses at once. */
function listening(port: number, timeoutMs = 300): Promise<boolean> {
  if (!Number.isInteger(port) || port < 1 || port > 65535) return Promise.resolve(false);
  return new Promise((resolve) => {
    const socket = net.connect({ host: "127.0.0.1", port });
    const done = (up: boolean): void => {
      socket.destroy();
      resolve(up);
    };
    socket.setTimeout(timeoutMs, () => done(true));
    socket.once("connect", () => done(true));
    socket.once("error", () => done(false));
  });
}

export async function startDaemon(opts: DaemonOptions): Promise<Daemon> {
  const rt = await createRuntime(opts.projects);
  const projectDir = rt.project.projectDir ?? process.cwd();
  const events = new EventLog();
  // Every guard's dialogs on the event stream, so a UI can show a dialog it
  // was never asked about. One raised before this subscribed is replayed.
  const unsubscribeDialogs = onDialogEvent(({ type, ...data }) => {
    events.publish(`dialog.${type}`, data as unknown as Record<string, unknown>);
  });
  for (const s of rt.sessions.list()) {
    const guard = existingGuard(s);
    const open = guard?.current;
    if (guard && open) {
      events.publish("dialog.opened", {
        editor: guard.label,
        title: open.title,
        message: open.message,
        buttons: open.buttons,
        ...(open.items ? { items: open.items } : {}),
        ...(open.blocks ? { blocks: open.blocks } : {}),
        ...(open.native ? { native: open.native } : {}),
        mode: guard.mode,
      });
    }
  }
  const expected = new ExpectedDisconnect();
  const token = newToken();
  const startedAt = new Date().toISOString();
  const idleMs = opts.idleMs ?? (Number(readEnv("daemonIdleMs")) || DEFAULT_IDLE_MS);
  const waitForEditorMs = (rt.project.config.server?.waitForEditorSeconds ?? DEFAULT_WAIT_FOR_EDITOR_SECONDS) * 1000;

  // ── Editor attachment ───────────────────────────────────────────
  const states = new Map<EditorSession, EditorState>();
  const records = new Map<EditorSession, BridgeInstanceRecord | null>();
  const watchers: DirWatch[] = [];

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
    // The daemon outlives editors, so a call waits a while for one that went away.
    session.bridge.reattachWaitMs = waitForEditorMs;

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
        void resyncPie(session);
        return;
      }
      state.connected = false;
      const record = records.get(session) ?? null;
      const announced = expected.get();
      void settleAndClassify({ record, expected: announced }).then((cause) => {
        state.lastDisconnect = { cause, detail: change.detail, at: new Date().toISOString() };
        session.bridge.lastDisconnectCause = cause;
        events.publish("editor.disconnected", { editor: session.name, cause, detail: change.detail ?? null, pid: record?.pid ?? null });
        if (cause === "crashed" && rt.project.config.server?.openBrowserOnCrash) {
          openUrl(`http://127.0.0.1:${port}/ui/?token=${token}`);
        }
        // The editor is gone: renamed-aside DLLs can go, and a staged update can swap in.
        if ((cause === "closed" || cause === "crashed") && dir) settleUpdate(dir, false);
      });
    });

    // Play sessions the editor pushes, however they were started, as pie.<state>.
    session.bridge.onNotification((n) => {
      const data = pieEventFromNotification(session.name, n);
      if (!data) return;
      // An ended Play session is nobody's any more, however it was stopped.
      if (data.state === "ended") rt.sessions.disruptions.pieEnded();
      events.publish(`pie.${data.state}`, data as unknown as Record<string, unknown>);
    });
    // Attached before this subscribed: read the state now rather than wait for a reconnect.
    if (session.bridge.isConnected) void resyncPie(session);

    // Reattach as soon as an editor publishes its record, not at the next tick.
    if (dir) {
      let timer: NodeJS.Timeout | null = null;
      const watcher = watchDir(bridgeInstancesDir(dir), () => {
        if (session.bridge.isConnected) return;
        if (timer) clearTimeout(timer);
        timer = setTimeout(() => void pokeWithBackoff(session), 250);
      }, { watch: opts.watch });
      watchers.push(watcher);
    }
  };

  // A pie.* event missed while the editor was away (or before the daemon
  // started) would leave a client showing the wrong state for the rest of the
  // session, so every attach reads the state once and publishes it.
  const resyncPie = async (session: EditorSession): Promise<void> => {
    try {
      const status = await session.bridge.call("pie_control", { pieAction: "status" }, PIE_RESYNC_TIMEOUT_MS);
      if (!session.bridge.isConnected) return;
      const data = pieEventFromStatus(session.name, status);
      if (data) events.publish(`pie.${data.state}`, data as unknown as Record<string, unknown>);
    } catch (e) {
      debug("daemon", `PIE state resync for ${session.name} failed: ${e instanceof Error ? e.message : String(e)}`);
    }
  };

  const pokeWithBackoff = async (session: EditorSession): Promise<void> => {
    for (const delay of [0, 500, 1000, 2000, 4000]) {
      if (delay) await new Promise((r) => setTimeout(r, delay));
      if (await session.bridge.pokeReconnect()) return;
    }
  };

  // ── Updates ─────────────────────────────────────────────────────
  const editorProcessHolds = async (uproject: string): Promise<boolean> => {
    try {
      const probe = await probeEditorProcessList();
      return probe.failed || probe.processes.some((p) => editorOwnsProject(p, uproject));
    } catch {
      return true;
    }
  };
  // Without a pending update this still clears the spare plugin copies earlier releases left under Plugins/.
  const settleUpdate = (dir: string, editorRunning: boolean): void => {
    const pluginDir = resolvePluginDir(dir)?.dir;
    if (!pluginDir) return;
    const pending = readPendingUpdate(pluginDir);
    try {
      if (settlePendingUpdate(pluginDir, editorRunning) && pending?.state === "staged") {
        const repinned = repinClientConfigs(rt.project.projectPath ?? opts.projects[0], pending.version);
        events.publish("update.applied", { version: pending.version, state: "applied", repinned });
      }
    } catch (e) {
      if (pending) events.publish("update.failed", { version: pending.version, error: e instanceof Error ? e.message : String(e) });
    }
  };
  // A daemon restarted under a running editor (a client restarting it to update) must not swap a staged tree in beneath it.
  settleUpdate(projectDir, findLiveInstanceRecord(projectDir) !== null);

  // Look for a newer release now and then, never in the first minute (tests and short runs finish first).
  let announced: string | null = null;
  const lookForUpdate = (): void => {
    void checkForUpdate(rt.project.projectPath ?? opts.projects[0]).then((check) => {
      if (check.target && check.target !== announced) {
        announced = check.target;
        events.publish("update.available", { ...check });
      }
    });
  };
  const updateTimers: NodeJS.Timeout[] = [];
  if (readEnv("disableUpdateCheck") !== "1") {
    updateTimers.push(setTimeout(lookForUpdate, 60_000), setInterval(lookForUpdate, 6 * 60 * 60_000));
    for (const t of updateTimers) t.unref();
  }

  // Flow progress joins the same resumable stream the UI reads.
  const unsubscribeFlows = subscribeFlowEvents((e) => {
    events.publish(`flow.${e.type}`, { ...(e as unknown as Record<string, unknown>) });
  });
  // PIE test script progress, as the script watcher reads it off the bridge.
  const unsubscribeScripts = subscribeScriptEvents(({ type, ...data }) => {
    events.publish(`script.${type}`, data as unknown as Record<string, unknown>);
  });
  // Checkpoints: checkpoint.created and checkpoint.restored for one checkpoint, checkpoints.* for the history.
  const unsubscribeCheckpoints = subscribeCheckpointEvents((e) => {
    const type: DaemonEventType = e.type === "created" || e.type === "restored" ? `checkpoint.${e.type}` : `checkpoints.${e.type}`;
    events.publish(type, e.data as unknown as Record<string, unknown>);
  });

  for (const s of rt.sessions.list()) follow(s);
  rt.onSurfaceChanged(() => {
    for (const s of rt.sessions.list()) follow(s);
  });

  // ── MCP over WebSocket ──────────────────────────────────────────
  const wss = new WebSocketServer({ noServer: true });
  let clients = 0;
  const transports = new Set<WebSocketServerTransport>();
  // Every open MCP session by id, with the access mode its calls run under (#1484).
  const mcpSessions = new Map<string, { client: () => string; connectedAt: string; access: SessionAccess }>();
  const sessionIds = new WeakMap<http.IncomingMessage, string>();
  // Implicit asset leases switch on once more than one client shares the editor.
  rt.sessions.leases.liveSessions = () => mcpSessions.size;
  const listSessions = (): McpSessionInfo[] => [...mcpSessions].map(([id, m]) =>
    ({ id, client: m.client(), connectedAt: m.connectedAt, access: m.access.mode }));
  // The id goes back on the upgrade response, so a client on the socket knows which session it is.
  wss.on("headers", (headers, req) => {
    const id = randomUUID();
    sessionIds.set(req, id);
    headers.push(`${DAEMON_HEADERS.session}: ${id}`);
  });

  wss.on("connection", (ws, req) => {
    clients += 1;
    const header = String(req.headers[DAEMON_HEADERS.client] ?? "unknown");
    const transport = new WebSocketServerTransport(ws);
    transports.add(transport);
    const sessionId = sessionIds.get(req) ?? randomUUID();
    // A client can open its session read-only, so no call slips through before it sets the mode.
    const access = new SessionAccess(parseSessionAccessMode(req.headers[DAEMON_HEADERS.access]));
    const parentHeader = req.headers[DAEMON_HEADERS.parent];
    const parent = typeof parentHeader === "string" && parentHeader.trim() !== "" ? parentHeader.trim() : undefined;
    const { server, dispose } = createMcpServer(rt, { access, sessionId, ...(parent ? { parent } : {}) });
    // The MCP client's own name once it has introduced itself, else what the shim said.
    const client = (): string => server.server.getClientVersion()?.name ?? header;
    mcpSessions.set(sessionId, { client, connectedAt: new Date().toISOString(), access });
    // A participant in requests to stop the editor from the moment it connects (#2075).
    rt.sessions.disruptions.opened({ id: sessionId, label: header, ...(parent ? { parent } : {}) });

    // Every call, attributed by client, for the activity feed.
    const started = new Map<string | number, { at: number; label: CallLabel }>();
    transport.observe = (direction, message) => {
      if (direction === "in" && "method" in message && message.method === "initialize") {
        const info = (message.params as { clientInfo?: { name?: string; version?: string } } | undefined)?.clientInfo;
        events.publish("client.connected", { client: info?.name ?? header, version: info?.version ?? null, clients, session: sessionId, access: access.mode });
      } else if (direction === "in" && "method" in message && message.method === "tools/call" && "id" in message) {
        const label = describeToolCall(message.params);
        started.set(message.id as string | number, { at: Date.now(), label });
        events.publish("call.started", { client: client(), ...label });
      } else if (direction === "out" && "id" in message && started.has(message.id as string | number)) {
        const call = started.get(message.id as string | number)!;
        started.delete(message.id as string | number);
        const error = "error" in message ? message.error.message : resultError("result" in message ? message.result : undefined);
        events.publish("call.finished", {
          client: client(),
          ...call.label,
          durationMs: Date.now() - call.at,
          ok: error === undefined,
          ...(error !== undefined ? { error } : {}),
        });
      }
    };
    void server.connect(transport);
    ws.on("close", () => {
      clients -= 1;
      transports.delete(transport);
      mcpSessions.delete(sessionId);
      // A session that ends gives up its asset leases, so nobody waits out a holder that is gone.
      const released = rt.sessions.leases.releaseHolder(sessionId);
      if (released > 0) events.publish("leases.released", { session: sessionId, count: released });
      // And its hold and votes, so nobody waits on a session that is gone.
      rt.sessions.disruptions.closed(sessionId);
      dispose();
      void server.close().catch(() => {});
      events.publish("client.disconnected", { client: client(), clients, session: sessionId });
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
  // Cookies are scoped to the host, not the port, so each daemon names its own:
  // with one shared name a second project's page overwrote the first one's login.
  const cookieName = (): string => `ue_mcp_token_${port}`;
  const cookieTokens = (req: http.IncomingMessage): string[] =>
    [...(req.headers.cookie ?? "").matchAll(/(?:^|;\s*)ue_mcp_token(?:_\d+)?=([0-9a-f]+)/g)].map((m) => m[1]);
  // Every daemon listens on a fresh port, so a browser that outlives daemon
  // restarts (a client window, an editor dock) collects one login per
  // restart. Nothing expires them, and once the jar passed Node's 16 KiB
  // header limit every request came back 431. Logging in clears the logins of
  // daemons no longer listening; one still running keeps its own.
  const staleCookies = async (req: http.IncomingMessage): Promise<string[]> => {
    const names = new Set(
      [...(req.headers.cookie ?? "").matchAll(/(?:^|;\s*)(ue_mcp_token(?:_(\d+))?)=/g)]
        .filter((m) => m[2] !== String(port))
        .map((m) => m[1]),
    );
    const dead = await Promise.all([...names].map(async (name) => {
      const p = Number(name.slice("ue_mcp_token_".length));
      return name === "ue_mcp_token" || !(await listening(p)) ? name : null;
    }));
    return dead.filter((n): n is string => n !== null);
  };
  const authorized = (req: http.IncomingMessage): boolean => {
    if (!hostIsAllowed(req) || !originAllowed(req)) return false;
    const header = req.headers.authorization;
    if (typeof header === "string" && header.startsWith("Bearer ") && tokensMatch(header.slice("Bearer ".length), token)) return true;
    // The served page authenticates by cookie, set once from ?token= on /ui.
    if (cookieTokens(req).some((t) => tokensMatch(t, token))) return true;
    // EventSource cannot set headers; the stream alone also takes the token as a query.
    const url = new URL(req.url ?? "/", "http://127.0.0.1");
    return url.pathname === routePath("events") && tokensMatch(url.searchParams.get("token"), token);
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
    refusedExtensions,
    ui: ui.active()?.manifest.version ?? null,
    uiBundles: ui.extensionBundles(),
    features: DAEMON_FEATURES,
    sessions: listSessions(),
  });
  const editors = (): EditorState[] => rt.sessions.list().map((s) => states.get(s) ?? {
    name: s.name, connected: s.bridge.isConnected, port: s.bridge.port, pid: null, lastDisconnect: null,
  });

  // ── Editor operations ───────────────────────────────────────────
  // The daemon owns these, so it can say beforehand why the editor is about to
  // go away and the disconnect is classified as that rather than guessed.
  // A stop, restart or rebuild is one consent request for the whole operation,
  // and its steps run under that grant (#2075). Start takes nothing away.
  const editorOp = async (op: string, consent: EditorOpConsent = {}): Promise<Record<string, unknown>> => {
    if (op === "start" || !rt.sessions.disruptions.enforcing) return runEditorOp(op);
    const registry = rt.sessions.disruptions;
    const disruptive = op as "stop" | "restart" | "rebuild";
    const caller = consent.caller ?? currentDisruptionCaller(processCaller("ue-mcp daemon"));
    const action = consent.action ?? `editor operation ${op}`;
    if (consent.force) {
      const opened = registry.request(caller, disruptive, action, { person: true, ...(consent.reason ? { reason: consent.reason } : {}) });
      if (opened.state !== "approved") registry.override(opened.id, "approve", consent.force);
    }
    const decision = askConsent(registry, disruptive, action, {
      caller,
      ...(consent.person ? { person: true } : {}),
      ...(consent.subagents ? { subagents: consent.subagents } : {}),
      ...(consent.reason ? { reason: consent.reason } : {}),
    });
    if (!decision.proceed) {
      events.publish("editor.operation", { op, step: "consent", phase: "failed", reason: decision.message, request: decision.request.id });
      return { op, steps: [{ step: "consent", ok: false, result: decision.message, reason: decision.message }], request: decision.request };
    }
    const request = decision.request;
    if (!request) return runEditorOp(op);
    try {
      return { ...(await withDisruptionGrant(request.id, () => runEditorOp(op))), request: registry.get(request.id) ?? request };
    } finally {
      registry.complete(request.id);
    }
  };
  const runEditorOp = async (op: string): Promise<Record<string, unknown>> => {
    const step = async (category: string, action: string) => {
      const name = `${category}.${action}`;
      events.publish("editor.operation", { op, step: name, phase: "started" });
      const served = await rt.callActionOutcome(category, action);
      // `result` is the step's answer as an MCP client reads it.
      const body = toToolResult(served.outcome, rt.sessions, served.session).content.map((c) => (c.type === "text" ? c.text : "")).join("\n");
      // A handler reports failure by RETURNING success: false, which the
      // pipeline passes on as a result, not an error. Reading the error alone
      // took a failed build for a good one and launched the editor on it (#1384).
      const failure = editorStepFailure(served.outcome);
      if (failure === null) {
        events.publish("editor.operation", { op, step: name, phase: "done" });
      } else {
        warn("daemon", `${op}: ${name} failed: ${failure}`);
        events.publish("editor.operation", { op, step: name, phase: "failed", reason: failure });
      }
      // `reason` is the failure in one paragraph, so a UI can show it without parsing `result`.
      return { step: name, ok: failure === null, result: body, ...(failure === null ? {} : { reason: failure }) };
    };
    if (op === "start") return { op, steps: [await step("editor", "start_editor")] };
    if (op === "stop") return { op, steps: [await step("editor", "stop_editor")] };
    if (op === "restart") {
      expected.expect("restarting");
      return { op, steps: [await step("editor", "restart_editor")] };
    }
    expected.expect("rebuild");
    const steps = [await step("editor", "stop_editor")];
    // An editor still up holds the module DLLs, so the build would fail on them, or link over a running one.
    if (!steps[0].ok) {
      expected.clear();
      return { op, steps };
    }
    const built = await step("project", "build");
    steps.push(built);
    // A failed build leaves the editor down rather than launching it on modules that were never built.
    if (built.ok) steps.push(await step("editor", "start_editor"));
    else expected.clear();
    return { op, steps };
  };
  // Flow tasks editor.restart and editor.rebuild go through the same announced
  // operations, asked as the session whose flow runs them.
  rt.sessions.editorOperation = async (op) => (await editorOp(op)) as { steps: Array<{ step: string; ok: boolean; result: string }> };

  // Requests and holds on the event stream, for a UI or an external runtime to follow.
  const unsubscribeDisruptions = rt.sessions.disruptions.onChange((change) => {
    if (change.type === "hold") events.publish("disruption.hold", { ...change.session });
    else events.publish(`disruption.${change.type}`, { ...change.request } as unknown as Record<string, unknown>);
  });

  // ── Native dialogs ──────────────────────────────────────────────
  // Win32 boxes the editor process shows, read from outside it by the window
  // scan the dialog guard uses, so a UI can show and answer one while the
  // bridge cannot answer anything.
  const nativeDialogList = (): NativeDialogView[] =>
    rt.sessions.list().flatMap((s) =>
      nativeDialogsForProject(s.project.projectPath).map((d) => ({
        editor: s.name,
        ...nativeDialogInfo(d),
        title: d.title,
        message: d.text.join("\n"),
        text: d.text,
        buttons: d.buttons,
        hung: d.hung,
      })));
  const pressNativeDialogFor = async (hwnd: string, button: string, pid?: number): Promise<NativeDialogPressResult> => {
    for (const s of rt.sessions.list()) {
      const d = nativeDialogsForProject(s.project.projectPath).find((x) => x.hwnd === String(hwnd));
      if (!d) continue;
      if (pid !== undefined && pid !== d.pid) return { pressed: false, closed: false, error: "That dialog belongs to another process than the one named." };
      const guard = existingGuard(s) ?? (await ensureGuard(s));
      return guard.pressNativeButton(nativeAsBlocking(d), button, "person");
    }
    return { pressed: false, closed: false, error: "No native dialog with that handle is open in an editor of this project." };
  };

  // ── Context projection ──────────────────────────────────────────
  const projector = new ContextProjector(rt);
  const actionEffects = (categories?: string[]) => actionEffectInfo(enabledGraph(rt), rt.loads.dispatchUnion.tools, categories);

  // ── The Playtest Hub ────────────────────────────────────────────
  // One per project, under its data folder. Extensions bring a client's own
  // pieces through api.hub.configure; the hub is built once they have.
  const hubHolder = new HubHolder((client) => createDaemonHub({
    projectDir,
    dataDir: path.join(userDir(), "data", projectKey(projectDir)),
    events,
    callAction: async (category, action, args) => {
      const r = await rt.callAction(category, action, args);
      return { isError: !!r.isError, content: r.content };
    },
    editors: () => rt.sessions.list().map((s) => ({ connected: s.bridge.isConnected })),
    dialogs: () => rt.sessions.list().filter((s) => existingGuard(s)?.current).length,
    log: { warn: (message, detail) => warn("hub", message, detail) },
    signer: async () => (await readRegistryAuth())?.login ?? null,
    engine: () => {
      const assoc = rt.project.engineAssociation;
      return assoc && /^\d+\.\d+/.test(assoc) ? assoc.match(/^\d+\.\d+/)![0] : null;
    },
  }, client));
  const unregisterHub = registerHub(projectDir, hubHolder);

  // ── UI bundles and extensions ───────────────────────────────────
  const ui = new UiBundleStore(DAEMON_API_VERSION, opts.uiDir);
  const refusedExtensions: RefusedExtension[] = [];
  // An extension's bundles are held to its version from activation, when it installs its own UI.
  const uiOwners: UiBundleOwner[] = [];
  const services = new ExtensionServices();
  /** Extensions already told to declare "registry.account", so the log says it once. */
  const warnedAccount = new Set<string>();
  const uiOwner = (x: { name: string; version: string; uiMin?: string; uiRoot?: false }): UiBundleOwner =>
    ({ name: x.name, version: x.version, ...(x.uiMin ? { uiMin: x.uiMin } : {}), ...(x.uiRoot === false ? { root: false } : {}) });
  const extensions: LoadedExtension[] = await loadExtensions((name, routes, setBusy, pkg): DaemonExtensionApi => {
    uiOwners.push(uiOwner({ name, ...pkg }));
    ui.setOwners(uiOwners);
    return {
      apiVersion: EXTENSION_API_VERSION,
      version: packageVersion(),
      projectDir,
      dataDir: (() => {
        const dir = path.join(userDir(), "data", projectKey(projectDir), name);
        fs.mkdirSync(dir, { recursive: true });
        return dir;
      })(),
      events: {
        publish: (type, data) => events.publishFor(name, type, data),
        subscribe: (listener) => events.subscribe(listener),
        since: (lastId) => events.since(lastId),
      },
      route: (method, routePath, handler) => {
        routes.push({ method, path: routePath, handler });
      },
      hub: {
        configure: (options) => hubHolder.configure(options),
      },
      callAction: async (category, action, args, callOpts) => {
        const r = await rt.callAction(category, action, args, callOptions(callOpts));
        return { isError: !!r.isError, content: r.content };
      },
      features: EXTENSION_FEATURES,
      nativeDialogs: {
        list: () => nativeDialogList(),
        press: (hwnd, button, o) => pressNativeDialogFor(hwnd, button, o?.pid),
      },
      services: services.forOwner(name),
      versions: () => ({
        loaded: pkg.version,
        recorded: recordedVersion(projectDir, name),
        installed: versionsOf(installedCopies(opts.extensionsDir ?? extensionsDir()).get(name) ?? []),
      }),
      models: {
        register: (provider) => registerModelProvider(provider),
      },
      flows: {
        approveSteps: (approver) => registerFlowStepApprover(name, approver),
      },
      skills: {
        list: () => listDaemonSkills(projectDir),
      },
      actions: {
        effects: (categories) => actionEffects(categories),
      },
      context: {
        project: (o) => projector.project(o),
        call: async (tool, input, callOpts) => {
          const r = await projector.call(
            { strategy: callOpts?.strategy ?? rt.contextStrategy, ...(callOpts?.categories ? { categories: callOpts.categories } : {}) },
            tool,
            input ?? {},
            callOptions(callOpts),
          );
          return { isError: !!r.isError, content: r.content };
        },
      },
      editors: () => editors(),
      editorOperation: (op, o) => editorOp(op, {
        caller: o?.session && typeof o.session.id === "string" && o.session.id !== ""
          ? { id: o.session.id, label: o.session.label ?? o.session.id, ...(o.session.parent ? { parent: o.session.parent } : {}) }
          : { id: `ext:${name}`, label: name },
        ...(o?.person ? { person: true } : {}),
        ...(o?.subagents === "none" || o?.subagents === "paused" ? { subagents: o.subagents } : {}),
        ...(o?.reason ? { reason: o.reason } : {}),
        action: `${name} editorOperation ${op}`,
      }),
      setBusy,
      ui: {
        trustKey: (pem) => ui.trustKey(pem, name),
        install: (dir) => ui.install(dir),
        active: () => ui.active()?.manifest ?? null,
        own: () => ui.own(name)?.manifest ?? null,
      },
      modes: {
        get: () => {
          const feedback = FEEDBACK_MODE.scopes(projectDir);
          const dialog = DIALOG_MODE.scopes(projectDir);
          return {
            // Unset everywhere, feedback asks, as the tool does; a dialog mode left unset follows the client.
            feedback: { mode: feedback.project ?? feedback.user ?? "interactive", project: feedback.project !== undefined },
            dialog: { mode: dialog.project ?? dialog.user ?? null, project: dialog.project !== undefined },
            playtest: resolvePlaytestMode(projectDir),
          };
        },
        setFeedback: (mode) => FEEDBACK_MODE.set(mode, projectDir),
        setDialog: (mode) => DIALOG_MODE.set(mode, projectDir),
        setPlaytest: (mode) => PLAYTEST_MODE.set(mode, projectDir),
      },
      checkpoints: (() => {
        const of = (editor?: string) => checkpointsFor(resolveEditorSession(rt.sessions, editor));
        return {
          state: (o) => of(o?.editor).state(o?.limit),
          setEnabled: (enabled, o) => of(o?.editor).setEnabled(enabled),
          create: (label, o) => of(o?.editor).create(label),
          changes: (from, to, o) => of(o?.editor).changes(from, to ?? null, { assetDiffs: o?.assetDiffs }),
          assetDiff: (from, to, file, o) => of(o?.editor).assetDiff(from, to ?? null, file),
          thumbnail: (id, o) => of(o?.editor).thumbnail(id),
          openNativeDiff: (file, from, to, o) => of(o?.editor).openNativeDiff(file, from, to ?? null),
          restore: (checkpoint, o) => of(o?.editor).restore(checkpoint, { paths: o?.paths, force: o?.force }),
          clear: (o) => of(o?.editor).clear(),
        };
      })(),
      registryAccount: async () => {
        if (!pkg.permissions) {
          if (!warnedAccount.has(name)) {
            warnedAccount.add(name);
            warn("extension", `${name} uses the ue-mcp account without declaring it. Add "ueMcpExtension": { "permissions": ["registry.account"] } to its package.json; from ue-mcp ${REGISTRY_ACCOUNT_PERMISSION_REQUIRED_FROM} registryAccount() answers null without it.`);
          }
        } else if (!pkg.permissions.includes("registry.account")) {
          return null;
        }
        const auth = await readRegistryAuth();
        return auth ? { login: auth.login, token: auth.token, registry: auth.registry } : null;
      },
      log: {
        info: (message) => info(`ext:${name}`, message),
        warn: (message, detail) => warn(`ext:${name}`, message, detail),
      },
    };
  }, opts.extensionsDir, refusedExtensions, packageVersion(), projectDir);
  // One that failed to activate no longer holds its bundles back.
  ui.setOwners(extensions.map(uiOwner));
  for (const o of uiOwners) if (!extensions.some((x) => x.name === o.name)) services.revokeOwner(o.name);
  // Built now, so its queue and timers run whether or not anyone has asked it anything.
  hubHolder.get();

  // ── Routes ──────────────────────────────────────────────────────
  const errorBody = (e: unknown) => ({ error: e instanceof Error ? e.message : String(e) });
  /** The JSON body, where a body that is not JSON is the caller's mistake (400). */
  const body400 = (r: RouteRequest): Promise<unknown> => r.body().catch((e: unknown) => {
    throw new StatusError(400, errorBody(e).error);
  });
  const uiFile: RouteHandlers["ui"] = ({ url }) => {
    const file = ui.resolve(url.pathname);
    if (!file) {
      const none = !url.pathname.startsWith("/ui/ext/") && ui.active() === null;
      return {
        status: none ? 503 : 404,
        headers: { "content-type": "text/plain; charset=utf-8" },
        raw: none ? "No client UI is installed for this daemon." : "Not found.",
      };
    }
    return { status: 200, headers: { "content-type": contentType(file), "cache-control": "no-cache", "x-content-type-options": "nosniff" }, file };
  };
  // A request on these routes is a person's (a button in Studio or the
  // Launcher), so it owes no subagent attestation; it still waits for the
  // sessions using the editor, unless it names an approved request or forces.
  const httpCaller = (requester: unknown): DisruptionCaller => {
    const label = typeof requester === "string" && requester.trim() !== "" ? requester.trim().slice(0, 80) : "a person";
    return { id: `http:${label}`, label };
  };
  const editorRoute = (op: "start" | "stop" | "restart" | "rebuild"): RouteHandlers["editor.start"] => async (r) => {
    try {
      const b = objectBody(await r.body().catch(() => undefined));
      const named = typeof b.requestId === "string" ? rt.sessions.disruptions.get(b.requestId) : null;
      if (typeof b.requestId === "string" && !named) return { status: 404, body: { error: `No disruption request '${b.requestId}'. ${describeRoute("disruptions.list")} lists them.` } };
      const caller = named ? named.requester : httpCaller(b.requester);
      const out = await editorOp(op, {
        caller,
        person: true,
        action: `POST ${routePath(`editor.${op}`)}`,
        ...(typeof b.reason === "string" ? { reason: b.reason } : {}),
        ...(b.force === true ? { force: caller.label } : {}),
      });
      const steps = out.steps as Array<{ step: string }> | undefined;
      if (steps?.[0]?.step === "consent") {
        const request = out.request as { id: string };
        return {
          status: 409,
          body: {
            ...out,
            error: `${String((out.steps as Array<{ reason: string }>)[0].reason)}\nA person can run it anyway with { "force": true }, or approve it with ${describeRoute("disruptions.approve").replace("<id>", request.id)} and send { "requestId": "${request.id}" }.`,
          },
        };
      }
      return ok(out);
    } catch (e) {
      return { status: 500, body: errorBody(e) };
    }
  };
  const disruptionError = (e: unknown): RouteResult => {
    if (e instanceof DisruptionError) return { status: e.kind === "not_found" ? 404 : 409, body: { error: e.message } };
    throw e;
  };
  const participantOf = (raw: unknown): DisruptionCaller | null => {
    if (typeof raw !== "string" || raw === "") return null;
    const known = rt.sessions.disruptions.activities().find((a) => a.id === raw);
    return { id: raw, label: known?.label ?? raw, ...(known?.parent ? { parent: known.parent } : {}) };
  };
  const disruptionRoutes: Pick<RouteHandlers,
    "disruptions.list" | "disruptions.request" | "disruptions.participant" | "disruptions.hold" | "disruptions.release"
    | "disruptions.vote" | "disruptions.confirm" | "disruptions.approve" | "disruptions.deny"> = {
    "disruptions.list": () => {
      const registry = rt.sessions.disruptions;
      return ok({ enforcing: registry.enforcing, config: registry.config, requests: registry.list(), sessions: registry.activities() });
    },
    "disruptions.request": async (r) => {
      const b = objectBody(await body400(r));
      if (typeof b.op !== "string" || !DISRUPTIVE_OPS.includes(b.op as DisruptiveOp)) {
        return { status: 400, body: { error: `op is one of ${DISRUPTIVE_OPS.join(", ")}` } };
      }
      const participant = participantOf(b.participant);
      const subagents = b.subagents === "none" || b.subagents === "paused" ? b.subagents : undefined;
      const view = rt.sessions.disruptions.request(participant ?? httpCaller(b.requester), b.op as DisruptiveOp, `POST ${routePath("disruptions.request")}`, {
        ...(typeof b.reason === "string" ? { reason: b.reason } : {}),
        ...(subagents ? { subagents } : {}),
        person: participant === null,
      });
      return ok(view);
    },
    "disruptions.participant": async (r) => {
      const id = r.params.id;
      const registry = rt.sessions.disruptions;
      if (r.method === "DELETE") {
        const known = registry.has(id);
        registry.closed(id);
        return known ? ok({ id, removed: true }) : { status: 404, body: { error: `No participant '${id}'.` } };
      }
      if (r.method !== "PUT" && r.method !== "POST") return { status: 405, body: { error: "PUT registers or reports; DELETE removes" } };
      const b = objectBody(await body400(r));
      if (b.busy !== undefined && !(Array.isArray(b.busy) && b.busy.every((x) => typeof x === "string"))) {
        return { status: 400, body: { error: "busy is a list of what the participant is doing, such as [\"blueprint.add_variable\"]; [] is idle" } };
      }
      const label = typeof b.label === "string" && b.label !== "" ? b.label : participantOf(id)!.label;
      registry.report({ id, label, ...(typeof b.parent === "string" && b.parent !== "" ? { parent: b.parent } : {}) }, {
        ...(Array.isArray(b.busy) ? { busy: b.busy as string[] } : {}),
        ...(typeof b.pie === "boolean" ? { pie: b.pie } : {}),
        ...(typeof b.transactions === "number" ? { transactions: b.transactions } : {}),
      });
      return ok(registry.activities().find((a) => a.id === id));
    },
    "disruptions.hold": async (r) => {
      const b = objectBody(await body400(r));
      if (typeof b.reason !== "string" || b.reason.trim() === "") return { status: 400, body: { error: "reason says why the editor must stay up" } };
      const hold = rt.sessions.disruptions.hold(participantOf(r.params.id)!, b.reason.trim(), typeof b.ttlSeconds === "number" ? b.ttlSeconds : undefined);
      return ok({ id: r.params.id, reason: hold.reason, expiresAt: new Date(hold.expiresAt).toISOString() });
    },
    "disruptions.release": (r) => ok({ id: r.params.id, ...rt.sessions.disruptions.release(participantOf(r.params.id)!) }),
    "disruptions.vote": async (r) => {
      const b = objectBody(await body400(r));
      const participant = participantOf(b.participant);
      if (!participant) return { status: 400, body: { error: "participant is the id of the session answering" } };
      if (!DISRUPTION_VOTES.includes(b.vote as DisruptionVote)) return { status: 400, body: { error: `vote is one of ${DISRUPTION_VOTES.join(", ")}` } };
      try {
        return ok(rt.sessions.disruptions.vote(participant, r.params.id, b.vote as DisruptionVote, typeof b.reason === "string" ? b.reason : ""));
      } catch (e) {
        return disruptionError(e);
      }
    },
    "disruptions.confirm": async (r) => {
      const b = objectBody(await body400(r));
      const participant = participantOf(b.participant);
      if (!participant) return { status: 400, body: { error: "participant is the id of the session that asked" } };
      if (b.subagents !== "none" && b.subagents !== "paused") return { status: 400, body: { error: "subagents is none or paused" } };
      try {
        return ok(rt.sessions.disruptions.attest(participant, r.params.id, b.subagents));
      } catch (e) {
        return disruptionError(e);
      }
    },
    "disruptions.approve": async (r) => {
      const b = objectBody(await r.body().catch(() => undefined));
      try {
        return ok(rt.sessions.disruptions.override(r.params.id, "approve", typeof b.by === "string" && b.by !== "" ? b.by : "a person"));
      } catch (e) {
        return disruptionError(e);
      }
    },
    "disruptions.deny": async (r) => {
      const b = objectBody(await r.body().catch(() => undefined));
      try {
        return ok(rt.sessions.disruptions.override(r.params.id, "deny", typeof b.by === "string" && b.by !== "" ? b.by : "a person"));
      } catch (e) {
        return disruptionError(e);
      }
    },
  };
  const sessionAccess: RouteHandlers["sessions.access"] = async (r) => {
    const id = r.params.id;
    const session = mcpSessions.get(id);
    if (!session) return { status: 404, body: { error: `no open MCP session '${id}'` } };
    if (r.method === "GET") return ok({ id, access: session.access.mode });
    if (r.method !== "PUT" && r.method !== "POST") return { status: 405, body: { error: "GET or PUT" } };
    const mode = parseSessionAccessMode((await body400(r) as { mode?: unknown } | undefined)?.mode);
    if (!mode) return { status: 400, body: { error: `mode is one of ${SESSION_ACCESS_MODES.join(", ")}` } };
    if (session.access.set(mode)) {
      info("daemon", `session ${id} (${session.client()}) is now ${mode}`);
      events.publish("session.access", { session: id, client: session.client(), access: mode });
    }
    return ok({ id, access: session.access.mode });
  };

  const handlers: RouteHandlers = {
    "health": () => ok(health()),
    "events": ({ req, res, url }) => {
      const asked = Number(req.headers["last-event-id"] ?? url.searchParams.get("since") ?? 0) || 0;
      // An id past the newest came from an earlier daemon, which numbered its own events: start over, and say events were missed.
      const restarted = asked > events.lastId;
      const last = restarted ? 0 : asked;
      res.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-cache", connection: "keep-alive" });
      // Headers now, not with the first event: a quiet daemon would otherwise leave the client without a response.
      res.flushHeaders();
      const write = (e: DaemonEvent) =>
        res.write(`id: ${e.id}\nevent: ${e.type}\ndata: ${JSON.stringify({ at: e.at, ...e.data })}\n\n`);
      const backlog = events.since(last);
      if (backlog.gap || restarted) res.write(`event: ${DAEMON_GAP_EVENT}\ndata: {}\n\n`);
      for (const e of backlog.events) write(e);
      const unsubscribe = events.subscribe(write);
      sse.add(res);
      req.on("close", () => {
        unsubscribe();
        sse.delete(res);
        touch();
      });
      return HANDLED;
    },
    "install": () => {
      // The same read-only report as `ue-mcp status --json`.
      try {
        return ok(collectStatus(rt.project.projectPath ?? opts.projects[0]));
      } catch (e) {
        return { status: 500, body: errorBody(e) };
      }
    },
    "update.check": async ({ query }) => {
      // ?channel=beta follows that npm dist-tag instead of the one the installed version implies.
      const channel = query.get("channel");
      if (channel !== null && !/^[A-Za-z][0-9A-Za-z._-]{0,31}$/.test(channel)) return { status: 400, body: { error: "channel is an npm dist-tag such as latest or beta" } };
      return ok(await checkForUpdate(rt.project.projectPath ?? opts.projects[0], undefined, channel ?? undefined));
    },
    "update.apply": async (r) => {
      try {
        const b = ((await r.body()) ?? {}) as { version?: unknown; binaries?: unknown };
        const uproject = rt.project.projectPath ?? opts.projects[0];
        // A bridge that has not written its instance record yet (starting, or an older build) still
        // maps its DLLs, so the process table is asked too. A probe that cannot run counts as running:
        // the in-place swap is safe with the editor down, the whole-tree one is not with it up.
        const editorRunning = editors().some((e) => e.connected) || findLiveInstanceRecord(projectDir) !== null
          || (await editorProcessHolds(uproject));
        const result = await applyUpdate(uproject, {
          version: typeof b.version === "string" ? b.version : undefined,
          binaries: typeof b.binaries === "string" ? b.binaries : null,
          editorRunning,
          onEvent: (type, data) => events.publish(type, data),
        });
        return { status: result.ok ? 200 : 500, body: result };
      } catch (e) {
        return { status: 400, body: errorBody(e) };
      }
    },
    "skills": () => {
      try {
        return ok({ skills: listDaemonSkills(projectDir) });
      } catch (e) {
        return { status: 500, body: errorBody(e) };
      }
    },
    "actions": ({ query }) => {
      try {
        return ok({ actions: actionEffects(listParam(query, "categories")) });
      } catch (e) {
        return { status: e instanceof RangeError ? 400 : 500, body: errorBody(e) };
      }
    },
    "context.projection": ({ query }) => {
      const strategy = (query.get("strategy") ?? rt.contextStrategy) as ContextStrategy;
      const categories = listParam(query, "categories");
      try {
        return ok(projector.project({ strategy, ...(categories ? { categories } : {}) }));
      } catch (e) {
        return { status: e instanceof ContextProjectionError ? 400 : 500, body: errorBody(e) };
      }
    },
    "sessions.list": () => ok({ sessions: listSessions() }),
    "sessions.access": sessionAccess,
    "sessions.unknown": () => ({ status: 404, body: { error: "not found" } }),
    "dialogs.native": () => ok({ dialogs: nativeDialogList() }),
    "dialogs.native.press": async (r) => {
      const b = ((await body400(r)) ?? {}) as { hwnd?: unknown; button?: unknown; pid?: unknown };
      if ((typeof b.hwnd !== "string" && typeof b.hwnd !== "number") || typeof b.button !== "string" || b.button === "") {
        return { status: 400, body: { error: `hwnd (the window handle from ${routePath("dialogs.native")}) and button (one of its labels) are required` } };
      }
      try {
        const result = await pressNativeDialogFor(String(b.hwnd), b.button, typeof b.pid === "number" ? b.pid : undefined);
        return { status: result.pressed ? 200 : 409, body: result };
      } catch (e) {
        return { status: 400, body: errorBody(e) };
      }
    },
    // Answered before the daemon starts closing its server.
    "shutdown": ({ res }) => {
      res.writeHead(202).end();
      void shutdown("shutdown requested");
      return HANDLED;
    },
    "handoff": ({ res }) => {
      res.writeHead(202).end();
      void handoff();
      return HANDLED;
    },
    "editor.start": editorRoute("start"),
    "editor.stop": editorRoute("stop"),
    "editor.restart": editorRoute("restart"),
    "editor.rebuild": editorRoute("rebuild"),
    ...disruptionRoutes,
    ...checkpointRoutes(rt.sessions),
    ...journalRoutes(rt.sessions),
    ...hubRoutes(() => hubHolder.get(), projectDir),
    ...toolsRoutes(rt),
    // The project's ue-mcp plugins: list, registry catalog, install, update, uninstall.
    ...pluginRoutes({ projectDir, onChanged: () => events.publish("plugins.changed", {}) }),
    "flows": async ({ req, res, url, path: routed }) => {
      try {
        const handled = await handleFlowRoute(rt.flowRequest, req, res, routed.slice("/v1".length), url);
        return handled ? HANDLED : { status: 404 };
      } catch (e) {
        return { status: 500, body: errorBody(e) };
      }
    },
    "ext": async (r) => {
      const hit = matchRoute(extensions, r.method, r.url.pathname);
      if (!hit) return { status: 404 };
      // An extension's own errors are its 500, whatever they carry.
      try {
        const body = await r.body();
        const out = await hit.route.handler({
          method: r.method,
          path: r.url.pathname.slice(`/v1/ext/${hit.ext.name}`.length) || "/",
          params: hit.params,
          query: Object.fromEntries(r.query),
          headers: r.req.headers,
          body,
        });
        const n = normalizeResponse(out ?? undefined);
        return { status: n.status, headers: n.headers, raw: n.body };
      } catch (e) {
        return { status: 500, body: errorBody(e) };
      }
    },
    "ui.extension": uiFile,
    "ui": uiFile,
  };
  const route = createRouter(handlers);

  let closing: Promise<void> | null = null;
  // The header limit is raised so a browser already holding a jar past the
  // default 16 KiB still reaches the login below, which clears it.
  const httpServer = http.createServer({ maxHeaderSize: 64 * 1024 }, (req, res) => {
    const first = new URL(req.url ?? "/", "http://127.0.0.1");
    if (req.method === "GET" && (first.pathname === "/ui" || first.pathname === "/ui/") && tokensMatch(first.searchParams.get("token"), token)
      && hostIsAllowed(req)) {
      void staleCookies(req).then((stale) => {
        res.writeHead(302, {
          "set-cookie": [
            `${cookieName()}=${token}; HttpOnly; SameSite=Strict; Path=/`,
            ...stale.map((name) => `${name}=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0`),
          ],
          location: "/ui/",
        }).end();
      });
      return;
    }
    if (!authorized(req)) {
      res.writeHead(401).end();
      return;
    }
    route(req, res);
  });

  httpServer.on("upgrade", (req, socket, head) => {
    const url = new URL(req.url ?? "/", "http://127.0.0.1");
    if (url.pathname !== routePath("mcp") || !authorized(req) || closing) {
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
  // A server error after listening is logged; without a listener it would end the daemon.
  httpServer.on("error", (e) => warn("daemon", "HTTP server error", e));
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
    // Bounded: a call that never returns would leave this daemon running beside its successor.
    if (!(await drainInflight(inflight, HANDOFF_DRAIN_MS))) warn("daemon", `${inflight()} call(s) still in flight after ${HANDOFF_DRAIN_MS / 1000}s; handing off anyway`);
    await shutdown("handed off to a newer daemon");
  }

  /** The opt-in flow HTTP surface (ue-mcp.http), served by whichever process serves the project. */
  let flowHttp: { close(cb: () => void): void } | null = null;

  function shutdown(reason: string): Promise<void> {
    if (closing) return closing;
    closing = (async () => {
      info("daemon", `stopping: ${reason}`);
      events.publish("daemon.stopping", { reason });
      clearInterval(idleTimer);
      clearInterval(stallTimer);
      for (const t of updateTimers) clearTimeout(t);
      unsubscribeFlows();
      unsubscribeScripts();
      unsubscribeCheckpoints();
      unsubscribeDisruptions();
      stopAllScriptWatches();
      unregisterHub();
      hubHolder.dispose();
      unsubscribeDialogs();
      for (const x of extensions) {
        await Promise.resolve(x.dispose?.()).catch((e) => warn("extension", `${x.name} dispose failed`, e));
        services.revokeOwner(x.name);
      }
      for (const w of watchers) w.close();
      if (opts.publish !== false) removeDiscovery(projectDir);
      for (const res of sse) res.end();
      for (const ws of wss.clients) ws.close(1001, "daemon stopping");
      for (const s of rt.sessions.list()) s.bridge.disconnect();
      if (flowHttp) await new Promise<void>((r) => flowHttp!.close(() => r()));
      await new Promise<void>((r) => httpServer.close(() => r()));
    })();
    return closing;
  }

  // Bridges last, so every connect event lands on a listener.
  await rt.connectBridges();
  rt.logSummary();


  const discovery = discoveryRecord(port, token, projectDir, startedAt);
  const superseded = opts.publish !== false ? rivalDaemon(projectDir, discovery.version) : null;
  if (opts.publish !== false && !superseded) writeDiscovery(projectDir, discovery);
  events.publish("daemon.started", { pid: process.pid, port, version: discovery.version });
  info("daemon", `listening on 127.0.0.1:${port} for ${path.basename(projectDir)}`);

  // While the event loop is held nothing answers, not even /v1/health; say so, with how long.
  let lastBeat = Date.now();
  const stallTimer = setInterval(() => {
    const now = Date.now();
    const held = now - lastBeat - STALL_BEAT_MS;
    if (held >= STALL_REPORT_MS) warn("daemon", `answered nothing for ${(held / 1000).toFixed(1)}s: something held the event loop`);
    lastBeat = now;
  }, STALL_BEAT_MS);
  stallTimer.unref();

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
    superseded,
    startFlowHttp(): void {
      const http = rt.project.config.http;
      if (!http?.enabled || flowHttp) return;
      try {
        flowHttp = startFlowHttpServer(rt.flowRequest, { port: http.port, host: http.host }).server;
      } catch (e) {
        error("http", "Failed to start the flow HTTP server", e);
      }
    },
    close: () => shutdown("closed"),
  };
}

/**
 * `ue-mcp daemon run`: start in the foreground. A live daemon of an older
 * version is asked to hand off once this one is listening; a live one of the
 * same or newer version wins and this process exits. The start lock makes
 * concurrent starters wait for one daemon instead of each running their own.
 */
export async function runDaemon(projects: string[]): Promise<number> {
  stampLogs();
  // Each client's first tool response carries the notice, as in process.
  startVersionCheck(packageVersion());
  if (projects.length === 0) {
    error("daemon", "no .uproject given");
    return 2;
  }
  const projectDir = path.dirname(path.resolve(projects[0]));
  const turn = await waitForStartTurn(projectDir, (d) => compareVersions(d.version, packageVersion()) >= 0, { isStale: discoveryIsStale });
  if (turn.action === "defer") {
    info("daemon", `daemon ${turn.live.version} already running (pid ${turn.live.pid}); not starting another`);
    return 0;
  }
  const existing = turn.existing;

  let daemon: Daemon;
  try {
    daemon = await startDaemon({ projects, publish: !existing });
    if (existing && !daemon.superseded) {
      await requestHandoff(existing);
      const deadline = Date.now() + 120_000;
      while (readLiveDiscovery(projectDir)?.pid === existing.pid && Date.now() < deadline) {
        await new Promise((r) => setTimeout(r, 250));
      }
      daemon.superseded = rivalDaemon(projectDir, packageVersion(), existing.pid);
      if (!daemon.superseded) {
        writeDiscovery(projectDir, discoveryRecord(daemon.port, daemon.token, projectDir, new Date().toISOString()));
      }
    }
  } finally {
    releaseStartLock(projectDir);
  }
  if (daemon.superseded) {
    info("daemon", `daemon ${daemon.superseded.version} (pid ${daemon.superseded.pid}) published first; stopping this one`);
    await daemon.close();
    return 0;
  }

  // Only now, so a daemon taking over binds the port after the old one let it go.
  daemon.startFlowHttp();
  // The record is ours from here; once it names another live daemon, this one steps down.
  const supersede = watchSupersede({
    projectDir,
    onSuperseded: (by) => {
      info("daemon", `daemon ${by.version} (pid ${by.pid}) took over this project; stopping this one`);
      void daemon.close();
    },
  });
  const stop = (): void => void daemon.close().then(() => process.exit(0));
  process.on("SIGINT", stop);
  process.on("SIGTERM", stop);
  daemon.events.subscribe((e) => {
    if (e.type !== "daemon.stopping") return;
    supersede.stop();
    // A daemon that took over published its record here, not in startDaemon, so it withdraws it here too.
    if (existing) removeDiscovery(projectDir);
    setTimeout(() => process.exit(0), 500).unref();
  });
  return new Promise<number>(() => {});
}

/** How long a daemon asked to hand off waits for its calls; under the 120 s its successor waits for it. */
const HANDOFF_DRAIN_MS = 90_000;

/** A live same-or-newer daemon published by another process, other than `ignorePid`. */
function rivalDaemon(projectDir: string, version: string, ignorePid?: number): DaemonDiscovery | null {
  const d = readLiveDiscovery(projectDir);
  return d && d.pid !== process.pid && d.pid !== ignorePid && compareVersions(d.version, version) >= 0 ? d : null;
}

async function requestHandoff(d: DaemonDiscovery): Promise<void> {
  try {
    await daemonRequest(d, "handoff", { method: "POST" });
  } catch (e) {
    warn("daemon", `handoff request to pid ${d.pid} failed`, e);
  }
}
