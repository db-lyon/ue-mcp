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
 *   GET  /v1/update     the newest release for the install's channel, and whether it has binaries for this engine
 *   POST /v1/update/apply  install it, now or when the editor exits
 *   POST /v1/shutdown   exit now
 *   POST /v1/handoff    exit once no call is in flight (a newer daemon is taking over)
 *   /v1/checkpoints     checkpoints per editor session, see checkpoint-routes.ts
 *   GET  /v1/skills     every skill a client of this project can load, see skills-list.ts
 *   GET  /v1/context/projection  what a strategy and category scope advertise, see context-projection.ts
 *   GET  /v1/actions    each action's effect and destructive flag, see action-effects.ts
 *   WS   /v1/mcp        one MCP session per socket
 */
import * as fs from "node:fs";
import * as http from "node:http";
import * as path from "node:path";
import { WebSocketServer } from "ws";
import { info, warn, error, stampLogs } from "../core/log.js";
import { editorStepFailure } from "./editor-step.js";
import { readEnv } from "../core/env.js";
import { packageVersion } from "../core/package-root.js";
import { normalizeProjectRoot } from "../bridge/port.js";
import { bridgeInstancesDir, findLiveInstanceRecord, type BridgeInstanceRecord } from "../bridge/editor-target.js";
import type { EditorSession } from "../sessions/session.js";
import { createRuntime, type Runtime } from "../server/runtime.js";
import { startVersionCheck } from "../core/version-check.js";
import { createMcpServer } from "../server/mcp-server.js";
import { startFlowHttpServer } from "../flow/http-server.js";
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
import { watchDir, type DirWatch, type WatchFn } from "./dir-watch.js";
import { describeToolCall, resultError, type CallLabel } from "./activity.js";
import { openUrl } from "../core/open-url.js";
import { handleFlowRoute, hostIsAllowed } from "../flow/http-server.js";
import { userDir } from "../core/user-dir.js";
import { readRegistryAuth } from "../extensions/registry-auth.js";
import { getDialogModeScopes, getFeedbackModeScopes, setDialogMode, setFeedbackMode, setPlaytestMode } from "../config/user-state.js";
import { resolvePlaytestMode } from "../editor/playtest-mode.js";
import { EXTENSION_API_VERSION, type DaemonExtensionApi, type DaemonHealth, type EditorStatus, type RefusedExtension } from "./extension-api.js";
import { existingGuard, onDialogEvent } from "../editor/dialog-guard.js";

/** What this daemon's extension API offers beyond its version number. */
export const EXTENSION_FEATURES: readonly string[] = ["callAction.dialogPrompt", "callAction.elicit", "checkpoints", "checkpoints.assetDiff", "events.dialog", "events.script", "models.provider", "modes", "modes.playtest", "skills", "context.projection", "actions.effects"];

/** What this daemon's HTTP API offers beyond DAEMON_API_VERSION, reported by /v1/health. */
export const DAEMON_FEATURES: readonly string[] = ["checkpoints", "checkpoints.assetDiff", "skills", "context.projection", "actions.effects", "install.pendingBuild", "editor.rebuild"];
import { loadExtensions, matchRoute, normalizeResponse, type LoadedExtension } from "./extensions.js";
import { UiBundleStore, contentType } from "./ui-bundles.js";
import { collectStatus } from "../cli/status.js";
import { resolvePluginDir } from "../editor/install-marker.js";
import { editorOwnsProject, probeEditorProcessList } from "../editor/engine-observer.js";
import { applyUpdate, checkForUpdate, readPendingUpdate, repinClientConfigs, settlePendingUpdate } from "../editor/bridge-update.js";
import { subscribeFlowEvents } from "../flow/events.js";
import { registerModelProvider } from "../flow/model-provider.js";
import { subscribeScriptEvents } from "../pie/script-events.js";
import { stopAllScriptWatches } from "../pie/script-watch.js";
import { pluginRoutes } from "./plugin-routes.js";
import { releaseStartLock, waitForStartTurn } from "./start-lock.js";
import { checkpointRoute } from "./checkpoint-routes.js";
import { checkpointsFor, subscribeCheckpointEvents } from "../checkpoints/service.js";
import { listDaemonSkills } from "../extensions/skills-list.js";
import { ContextProjectionError, ContextProjector, enabledGraph } from "../server/context-projection.js";
import { actionEffectInfo } from "../surface/action-effects.js";
import type { ContextStrategy } from "../surface/context/lean-context.js";

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

/** How often the daemon checks that it can still answer, and how long a hold is worth a warning. */
const STALL_BEAT_MS = 1000;
const STALL_REPORT_MS = 3000;

/** Pipe a file to a response; a file gone mid-read ends the response instead of the daemon. */
function streamFile(file: string, res: http.ServerResponse): void {
  fs.createReadStream(file)
    .on("error", (e) => {
      warn("daemon", `could not read ${file}`, e);
      res.destroy();
    })
    .pipe(res);
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
  // A daemon restarted under a running editor (Studio updating itself) must not swap a staged tree in beneath it.
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
    const type = e.type === "created" || e.type === "restored" ? `checkpoint.${e.type}` : `checkpoints.${e.type}`;
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

  wss.on("connection", (ws, req) => {
    clients += 1;
    const header = String(req.headers["x-ue-mcp-client"] ?? "unknown");
    const transport = new WebSocketServerTransport(ws);
    transports.add(transport);
    const { server, dispose } = createMcpServer(rt);
    // The MCP client's own name once it has introduced itself, else what the shim said.
    const client = (): string => server.server.getClientVersion()?.name ?? header;

    // Every call, attributed by client, for the activity feed.
    const started = new Map<string | number, { at: number; label: CallLabel }>();
    transport.observe = (direction, message) => {
      if (direction === "in" && "method" in message && message.method === "initialize") {
        const info = (message.params as { clientInfo?: { name?: string; version?: string } } | undefined)?.clientInfo;
        events.publish("client.connected", { client: info?.name ?? header, version: info?.version ?? null, clients });
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
      dispose();
      void server.close().catch(() => {});
      events.publish("client.disconnected", { client: client(), clients });
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
  const authorized = (req: http.IncomingMessage): boolean => {
    if (!hostIsAllowed(req) || !originAllowed(req)) return false;
    const header = req.headers.authorization;
    if (header === `Bearer ${token}`) return true;
    // The served page authenticates by cookie, set once from ?token= on /ui.
    if (cookieTokens(req).includes(token)) return true;
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
    refusedExtensions,
    ui: ui.active()?.manifest.version ?? null,
    features: DAEMON_FEATURES,
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
      const name = `${category}.${action}`;
      events.publish("editor.operation", { op, step: name, phase: "started" });
      const result = await rt.callAction(category, action);
      const body = text(result);
      // A handler reports failure by RETURNING success: false, which the dispatch
      // passes on as a result, not an error. Reading isError alone took a failed
      // build for a good one and launched the editor on it (#1384).
      const failure = editorStepFailure(result.isError === true, body);
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
  // Flow tasks editor.restart and editor.rebuild go through the same announced operations.
  rt.sessions.editorOperation = async (op) => (await editorOp(op)) as { steps: Array<{ step: string; ok: boolean; result: string }> };

  // ── Context projection ──────────────────────────────────────────
  const projector = new ContextProjector(rt);
  const actionEffects = (categories?: string[]) => actionEffectInfo(enabledGraph(rt), rt.loads.dispatchUnion.tools, categories);

  // ── UI bundles and extensions ───────────────────────────────────
  const ui = new UiBundleStore(DAEMON_API_VERSION, opts.uiDir);
  const refusedExtensions: RefusedExtension[] = [];
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
    callAction: async (category, action, args, callOpts) => {
      const r = await rt.callAction(category, action, args, {
        ...(callOpts?.dialogPrompt ? { dialogPrompt: callOpts.dialogPrompt } : {}),
        ...(callOpts?.dialogWaitMs !== undefined ? { dialogWaitMs: callOpts.dialogWaitMs } : {}),
        ...(callOpts?.elicit ? { elicit: callOpts.elicit } : {}),
      });
      return { isError: !!r.isError, content: r.content };
    },
    features: EXTENSION_FEATURES,
    models: {
      register: (provider) => registerModelProvider(provider),
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
          {
            ...(callOpts?.dialogPrompt ? { dialogPrompt: callOpts.dialogPrompt } : {}),
            ...(callOpts?.dialogWaitMs !== undefined ? { dialogWaitMs: callOpts.dialogWaitMs } : {}),
            ...(callOpts?.elicit ? { elicit: callOpts.elicit } : {}),
          },
        );
        return { isError: !!r.isError, content: r.content };
      },
    },
    editors: () => editors(),
    editorOperation: (op) => editorOp(op),
    setBusy,
    ui: {
      trustKey: (pem) => ui.trustKey(pem),
      install: (dir) => ui.install(dir),
      active: () => ui.active()?.manifest ?? null,
    },
    modes: {
      get: () => {
        const feedback = getFeedbackModeScopes(projectDir);
        const dialog = getDialogModeScopes(projectDir);
        return {
          // Unset everywhere, feedback asks, as the tool does; a dialog mode left unset follows the client.
          feedback: { mode: feedback.project ?? feedback.user ?? "interactive", project: feedback.project !== undefined },
          dialog: { mode: dialog.project ?? dialog.user ?? null, project: dialog.project !== undefined },
          playtest: resolvePlaytestMode(projectDir),
        };
      },
      setFeedback: (mode) => setFeedbackMode(mode, projectDir),
      setDialog: (mode) => setDialogMode(mode, projectDir),
      setPlaytest: (mode) => setPlaytestMode(mode, projectDir),
    },
    checkpoints: (() => {
      const of = (editor?: string) => {
        const session = editor ? rt.sessions.find(editor) : rt.sessions.active;
        if (!session) throw new Error(`No editor session '${editor}'`);
        return checkpointsFor(session);
      };
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
      const auth = await readRegistryAuth();
      return auth ? { login: auth.login, token: auth.token, registry: auth.registry } : null;
    },
    log: {
      info: (message) => info(`ext:${name}`, message),
      warn: (message, detail) => warn(`ext:${name}`, message, detail),
    },
  }), opts.extensionsDir, refusedExtensions);

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

  // The project's ue-mcp plugins: list, registry catalog, install, update, uninstall.
  const plugins = pluginRoutes({ projectDir, onChanged: () => events.publish("plugins.changed", {}) });

  const json = (res: http.ServerResponse, status: number, body: unknown): void => {
    res.writeHead(status, { "content-type": "application/json" }).end(JSON.stringify(body));
  };

  let closing: Promise<void> | null = null;
  const httpServer = http.createServer((req, res) => {
    const first = new URL(req.url ?? "/", "http://127.0.0.1");
    if (req.method === "GET" && (first.pathname === "/ui" || first.pathname === "/ui/") && first.searchParams.get("token") === token
      && hostIsAllowed(req)) {
      res.writeHead(302, {
        "set-cookie": `${cookieName()}=${token}; HttpOnly; SameSite=Strict; Path=/`,
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
    if (req.method === "GET" && url.pathname === "/v1/update") {
      // ?channel=beta follows that npm dist-tag instead of the one the installed version implies.
      const channel = url.searchParams.get("channel");
      if (channel !== null && !/^[A-Za-z][0-9A-Za-z._-]{0,31}$/.test(channel)) return json(res, 400, { error: "channel is an npm dist-tag such as latest or beta" });
      checkForUpdate(rt.project.projectPath ?? opts.projects[0], undefined, channel ?? undefined).then((check) => json(res, 200, check));
      return;
    }
    if (req.method === "POST" && url.pathname === "/v1/update/apply") {
      readBody(req)
        .then(async (body) => {
          const b = (body ?? {}) as { version?: unknown; binaries?: unknown };
          const uproject = rt.project.projectPath ?? opts.projects[0];
          // A bridge that has not written its instance record yet (starting, or an older build) still
          // maps its DLLs, so the process table is asked too. A probe that cannot run counts as running:
          // the in-place swap is safe with the editor down, the whole-tree one is not with it up.
          const editorRunning = editors().some((e) => e.connected) || findLiveInstanceRecord(projectDir) !== null
            || (await editorProcessHolds(uproject));
          return applyUpdate(uproject, {
            version: typeof b.version === "string" ? b.version : undefined,
            binaries: typeof b.binaries === "string" ? b.binaries : null,
            editorRunning,
            onEvent: (type, data) => events.publish(type, data),
          });
        })
        .then((result) => json(res, result.ok ? 200 : 500, result), (e) => json(res, 400, { error: e instanceof Error ? e.message : String(e) }));
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
    if (req.method === "GET" && url.pathname === "/v1/skills") {
      try {
        json(res, 200, { skills: listDaemonSkills(projectDir) });
      } catch (e) {
        json(res, 500, { error: e instanceof Error ? e.message : String(e) });
      }
      return;
    }
    if (req.method === "GET" && url.pathname === "/v1/actions") {
      const raw = url.searchParams.get("categories");
      try {
        json(res, 200, { actions: actionEffects(raw === null ? undefined : raw.split(",").map((c) => c.trim()).filter(Boolean)) });
      } catch (e) {
        json(res, e instanceof RangeError ? 400 : 500, { error: e instanceof Error ? e.message : String(e) });
      }
      return;
    }
    if (req.method === "GET" && url.pathname === "/v1/context/projection") {
      const strategy = (url.searchParams.get("strategy") ?? rt.contextStrategy) as ContextStrategy;
      const raw = url.searchParams.get("categories");
      const categories = raw === null ? undefined : raw.split(",").map((c) => c.trim()).filter(Boolean);
      try {
        json(res, 200, projector.project({ strategy, ...(categories ? { categories } : {}) }));
      } catch (e) {
        json(res, e instanceof ContextProjectionError ? 400 : 500, { error: e instanceof Error ? e.message : String(e) });
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
      streamFile(file, res);
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
    if (url.pathname === "/v1/plugins" || url.pathname.startsWith("/v1/plugins/")) {
      readBody(req)
        .then((body) => plugins(req.method ?? "GET", url.pathname.slice("/v1/plugins".length).replace(/\/+$/, ""), body))
        .then((r) => (r ? json(res, r.status, r.body) : void res.writeHead(404).end()), (e) => json(res, 500, { error: e instanceof Error ? e.message : String(e) }));
      return;
    }
    if (url.pathname === "/v1/checkpoints" || url.pathname.startsWith("/v1/checkpoints/")) {
      const sub = url.pathname.slice("/v1/checkpoints".length).replace(/\/+$/, "");
      readBody(req)
        .then((body) => checkpointRoute(rt.sessions, req.method ?? "GET", sub, url.searchParams, body))
        .then((r) => {
          if (!r) return void res.writeHead(404).end();
          if (!r.file) return json(res, r.status, r.body);
          // A picture is named by the blob id of the version it shows, so it never changes.
          res.writeHead(200, { "content-type": r.file.type, "cache-control": "private, max-age=31536000, immutable", "x-content-type-options": "nosniff" });
          streamFile(r.file.path, res);
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
    while (inflight() > 0) await new Promise((r) => setTimeout(r, 250));
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
      stopAllScriptWatches();
      unsubscribeDialogs();
      for (const x of extensions) await Promise.resolve(x.dispose?.()).catch((e) => warn("extension", `${x.name} dispose failed`, e));
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


  const discovery: DaemonDiscovery = {
    pid: process.pid,
    port,
    token,
    version: packageVersion(),
    apiVersion: DAEMON_API_VERSION,
    projectRoot: normalizeProjectRoot(projectDir),
    startedAt,
  };
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
        flowHttp = startFlowHttpServer(rt.flowTool, rt.baseCtx, { port: http.port, host: http.host }).server;
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
  const turn = await waitForStartTurn(projectDir, (d) => !isOlder(d.version, packageVersion()));
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
  const stop = (): void => void daemon.close().then(() => process.exit(0));
  process.on("SIGINT", stop);
  process.on("SIGTERM", stop);
  daemon.events.subscribe((e) => {
    if (e.type !== "daemon.stopping") return;
    // A daemon that took over published its record here, not in startDaemon, so it withdraws it here too.
    if (existing) removeDiscovery(projectDir);
    setTimeout(() => process.exit(0), 500).unref();
  });
  return new Promise<number>(() => {});
}

/** A live same-or-newer daemon published by another process, other than `ignorePid`. */
function rivalDaemon(projectDir: string, version: string, ignorePid?: number): DaemonDiscovery | null {
  const d = readLiveDiscovery(projectDir);
  return d && d.pid !== process.pid && d.pid !== ignorePid && !isOlder(d.version, version) ? d : null;
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
