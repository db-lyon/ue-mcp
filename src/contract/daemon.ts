// @session independent: The daemon's versioned contract: constants, the route table and public types. No state, no imports.
/**
 * The daemon contract: everything a client of the daemon or an extension
 * loaded into it can rely on, declared once.
 *
 *   DAEMON_API_VERSION     the HTTP API, the event stream and the stdio shim
 *   EXTENSION_API_VERSION  the in-process extension contract (DaemonExtensionApi)
 *
 * Within one version every change is additive; anything else bumps the number.
 * The module imports nothing, so the types below are the contract itself and
 * not a view of internal ones: an internal module that produces one of them
 * imports it from here, and one with a type of its own of the same shape is
 * checked against it where the daemon hands it over. A change here is a change
 * to what `ue-mcp/daemon` publishes, which tests/unit/daemon-contract.test.ts
 * holds against a recording.
 */

// ── Versions and the compatibility rule ───────────────────────────

/** Version of the daemon's HTTP API, event stream and shim protocol. Bump on a breaking change. */
export const DAEMON_API_VERSION = 1;

export const EXTENSION_API_VERSION = 3;

/**
 * The oldest extension API this daemon still serves. Versions up to EXTENSION_API_VERSION
 * only added to the contract, so an extension built for any of them keeps loading.
 */
export const EXTENSION_API_MIN_VERSION = 1;

/** API versions one side speaks, inclusive. A side that speaks one version is the range of that version alone. */
export interface ApiVersionRange {
  min: number;
  max: number;
}

/**
 * The one compatibility rule for both API versions: two sides work together
 * when the version ranges they speak overlap. The shim speaks this package's
 * DAEMON_API_VERSION, a UI bundle declares the range it works with, and an
 * extension declares `ueMcpExtension.api`.
 */
export function apiVersionsOverlap(a: ApiVersionRange, b: ApiVersionRange): boolean {
  return a.min <= b.max && b.min <= a.max;
}

/** The daemon API versions this package speaks. */
export const DAEMON_API_RANGE: ApiVersionRange = { min: DAEMON_API_VERSION, max: DAEMON_API_VERSION };

/** The extension API versions this package serves. */
export const EXTENSION_API_RANGE: ApiVersionRange = { min: EXTENSION_API_MIN_VERSION, max: EXTENSION_API_VERSION };

/** Whether an extension declaring api { min, max } can load here. */
export function extensionApiSupported(min: number, max: number): boolean {
  return apiVersionsOverlap({ min, max }, EXTENSION_API_RANGE);
}

// ── Features ──────────────────────────────────────────────────────

/**
 * What the HTTP API offers beyond DAEMON_API_VERSION, in the order /v1/health
 * reports them. A route added under a feature names it in DAEMON_ROUTES; a
 * feature that is not a route says what it is in DAEMON_FEATURES_BEYOND_ROUTES.
 */
export const DAEMON_FEATURES = [
  "checkpoints",
  "checkpoints.assetDiff",
  "skills",
  "context.projection",
  "actions.effects",
  "install.pendingBuild",
  "editor.rebuild",
  "ui.scopedKeys",
  "ui.extensions",
  "flows.stepApproval",
  "sessions.access",
  "journal",
  "tools.categories",
  "leases",
  "dialogs.native",
  "events.pie",
  "install.engineMismatch",
  "editor.consent",
  "hub",
] as const;

export type DaemonFeature = (typeof DAEMON_FEATURES)[number];

/** The daemon features that change what a route does rather than add one. */
export const DAEMON_FEATURES_BEYOND_ROUTES: { readonly [F in DaemonFeature]?: string } = {
  "install.pendingBuild":
    "GET /v1/install carries plugin.pendingBuild, a source bridge deployed and not built yet, which the next start through ue-mcp builds first",
  "ui.scopedKeys": "a bundle named for a loaded extension is installed and served only when signed by a key that extension trusted",
  "flows.stepApproval": "POST /v1/flows/<name>/run takes `stepApproval: { approver, token }`",
  "events.pie":
    "GET /v1/events carries pie.started, pie.ended, pie.paused and pie.resumed whenever a Play session changes, however it was started, and the current state once each time an editor attaches",
  "install.engineMismatch":
    "GET /v1/install reports the problem bridge_engine_mismatch, with action `rebuild` and engineMismatch naming the BuildIds, when the bridge or the project's UE_MCP_Studio plugin is built for another engine build, their module manifests disagree, or the install marker names another release than the descriptor",
  "editor.consent":
    "POST /v1/editor/{stop,restart,rebuild} answers 409 { error, request: EditorDisruptionRequest } while other sessions using the editor have not agreed, and takes { requestId?, force?, requester?, reason? }; GET /v1/events carries disruption.requested, disruption.updated and disruption.hold",
};

/** What the extension API offers beyond EXTENSION_API_VERSION, in the order `api.features` lists them. */
export const EXTENSION_FEATURES = [
  "callAction.dialogPrompt",
  "callAction.elicit",
  "callAction.access",
  "callAction.signal",
  "checkpoints",
  "checkpoints.assetDiff",
  "events.dialog",
  "events.script",
  "models.provider",
  "modes",
  "modes.playtest",
  "skills",
  "context.projection",
  "actions.effects",
  "extension.versions",
  "ui.scopedKeys",
  "ui.extensions",
  "extension.services",
  "flows.stepApproval",
  "dialogs.native",
  "permissions",
  "events.pie",
  "callAction.session",
  "editor.consent",
  "hub",
] as const;

export type ExtensionFeature = (typeof EXTENSION_FEATURES)[number];

/** The DaemonExtensionApi member each extension feature adds to or changes. */
export const EXTENSION_FEATURE_MEMBERS: { readonly [F in ExtensionFeature]: keyof DaemonExtensionApi } = {
  "callAction.dialogPrompt": "callAction",
  "callAction.elicit": "callAction",
  "callAction.access": "callAction",
  "callAction.signal": "callAction",
  checkpoints: "checkpoints",
  "checkpoints.assetDiff": "checkpoints",
  "events.dialog": "events",
  "events.script": "events",
  "models.provider": "models",
  modes: "modes",
  "modes.playtest": "modes",
  skills: "skills",
  "context.projection": "context",
  "actions.effects": "actions",
  "extension.versions": "versions",
  "ui.scopedKeys": "ui",
  "ui.extensions": "ui",
  "extension.services": "services",
  "flows.stepApproval": "flows",
  "dialogs.native": "nativeDialogs",
  permissions: "registryAccount",
  "events.pie": "events",
  "callAction.session": "callAction",
  "editor.consent": "editorOperation",
  hub: "hub",
};

// ── Routes ────────────────────────────────────────────────────────

/** `ANY` serves every method on the path; `WS` is a WebSocket upgrade. */
export type DaemonRouteMethod = "GET" | "POST" | "PUT" | "DELETE" | "ANY" | "WS";

/**
 * One route. `path` is matched segment by segment after trailing slashes are
 * dropped: `:name` captures one segment, and a trailing `/*` matches the path
 * itself and everything below it.
 */
export interface DaemonRoute {
  method: DaemonRouteMethod;
  path: string;
  /** The feature the route came with, when it came after DAEMON_API_VERSION 1 shipped. */
  feature?: DaemonFeature;
  /** What it does, with its query or body. */
  summary: string;
}

/**
 * Every route the daemon serves, all on 127.0.0.1 and all requiring the bearer
 * token from the discovery file (or the cookie GET /ui/?token= sets). A request
 * takes the first entry whose method and path match; one that matches none is
 * answered 404 with no body. `editor` names an editor session as /v1/health
 * lists it; omitted is the active one.
 */
export const DAEMON_ROUTES = routeTable({
  "health": { method: "GET", path: "/v1/health", summary: "what the daemon is and what it is attached to (DaemonHealth)" },
  "events": { method: "GET", path: "/v1/events", summary: "server-sent events, resumable by Last-Event-ID or ?since; ?token= also authenticates this route alone" },
  "install": { method: "GET", path: "/v1/install", summary: "the project's install state, as `ue-mcp status --json` reports it (StatusReport)" },
  "update.check": { method: "GET", path: "/v1/update", summary: "?channel: the newest release for the install's channel, and whether it has binaries for this engine" },
  "update.apply": { method: "POST", path: "/v1/update/apply", summary: "{ version?, binaries? }: install it, now or when the editor exits" },
  "skills": { method: "GET", path: "/v1/skills", feature: "skills", summary: "every skill a client of this project can load ({ skills: DaemonSkill[] })" },
  "actions": { method: "GET", path: "/v1/actions", feature: "actions.effects", summary: "?categories: each action's effect and destructive flag ({ actions: ActionEffectInfo[] })" },
  "context.projection": { method: "GET", path: "/v1/context/projection", feature: "context.projection", summary: "?strategy&categories: what a strategy and category scope advertise (ContextProjection)" },
  "sessions.list": { method: "GET", path: "/v1/sessions", feature: "sessions.access", summary: "open MCP sessions and their access mode ({ sessions: McpSessionInfo[] })" },
  "sessions.access": { method: "ANY", path: "/v1/sessions/:id/access", feature: "sessions.access", summary: "GET reads one session's access mode; PUT (or POST) { mode } sets it" },
  "sessions.unknown": { method: "ANY", path: "/v1/sessions/*", feature: "sessions.access", summary: "anything else under /v1/sessions: 404 { error }" },
  "dialogs.native": { method: "GET", path: "/v1/dialogs/native", feature: "dialogs.native", summary: "native Win32 dialogs this project's editors show ({ dialogs: NativeDialogView[] })" },
  "dialogs.native.press": { method: "POST", path: "/v1/dialogs/native/press", feature: "dialogs.native", summary: "{ hwnd, button, pid? }: press one by window handle and button label (NativeDialogPressResult)" },
  "shutdown": { method: "POST", path: "/v1/shutdown", summary: "exit now (202)" },
  "handoff": { method: "POST", path: "/v1/handoff", summary: "exit once no call is in flight, because a newer daemon is taking over (202)" },
  "editor.start": { method: "POST", path: "/v1/editor/start", summary: "start the editor" },
  "editor.stop": { method: "POST", path: "/v1/editor/stop", summary: "stop the editor" },
  "editor.restart": { method: "POST", path: "/v1/editor/restart", summary: "restart the editor, announced as restarting" },
  "editor.rebuild": { method: "POST", path: "/v1/editor/rebuild", feature: "editor.rebuild", summary: "stop the editor, build, and start it again only when the build passed; a failed step carries `reason`" },
  "disruptions.list": { method: "GET", path: "/v1/disruptions", feature: "editor.consent", summary: "requests to stop, restart, rebuild or patch the editor, and what each session is doing ({ enforcing, requests: EditorDisruptionRequest[], sessions })" },
  "disruptions.request": { method: "POST", path: "/v1/disruptions", feature: "editor.consent", summary: "{ op, requester?, reason?, participant?, subagents? }: open or re-read a request: a person's, or with participant, that participant's (EditorDisruptionRequest)" },
  "disruptions.participant": { method: "ANY", path: "/v1/disruptions/participants/:id", feature: "editor.consent", summary: "PUT { label?, parent?, busy?: string[], pie?, transactions? } registers a participant an external runtime drives and reports its work ([] is idle); DELETE removes it with its hold and votes" },
  "disruptions.hold": { method: "POST", path: "/v1/disruptions/participants/:id/hold", feature: "editor.consent", summary: "{ reason, ttlSeconds? }: the participant holds the editor" },
  "disruptions.release": { method: "POST", path: "/v1/disruptions/participants/:id/release", feature: "editor.consent", summary: "the participant releases its hold, and its wait_until_released votes become go" },
  "disruptions.vote": { method: "POST", path: "/v1/disruptions/:id/vote", feature: "editor.consent", summary: "{ participant, vote, reason? }: the participant answers go, wait_until_released or block (EditorDisruptionRequest)" },
  "disruptions.confirm": { method: "POST", path: "/v1/disruptions/:id/confirm", feature: "editor.consent", summary: "{ participant, subagents }: the requesting participant says its own subagents are none or paused (EditorDisruptionRequest)" },
  "disruptions.approve": { method: "POST", path: "/v1/disruptions/:id/approve", feature: "editor.consent", summary: "{ by? }: a person approves the request, whatever the sessions answered (EditorDisruptionRequest)" },
  "disruptions.deny": { method: "POST", path: "/v1/disruptions/:id/deny", feature: "editor.consent", summary: "{ by? }: a person denies the request (EditorDisruptionRequest)" },
  "checkpoints.state": { method: "GET", path: "/v1/checkpoints", feature: "checkpoints", summary: "?editor&limit: state and history, newest first (CheckpointState)" },
  "checkpoints.setEnabled": { method: "PUT", path: "/v1/checkpoints/enabled", feature: "checkpoints", summary: "{ editor?, enabled: true | false | null }: the per-session override, kept for the daemon's lifetime" },
  "checkpoints.create": { method: "POST", path: "/v1/checkpoints", feature: "checkpoints", summary: "{ editor?, label? } (CheckpointResult)" },
  "checkpoints.changes": { method: "GET", path: "/v1/checkpoints/changes", feature: "checkpoints", summary: "?editor&from&to&assetDiffs&blueprintDiffs (ChangesResult)" },
  "checkpoints.assetDiff": { method: "GET", path: "/v1/checkpoints/asset-diff", feature: "checkpoints.assetDiff", summary: "?editor&from&to&path: one package's asset diff (AssetDiff)" },
  "checkpoints.thumbnail": { method: "GET", path: "/v1/checkpoints/thumbnails/:file", feature: "checkpoints.assetDiff", summary: "?editor: the <id>.png an asset diff names" },
  "checkpoints.nativeDiff": { method: "POST", path: "/v1/checkpoints/native-diff", feature: "checkpoints.assetDiff", summary: "{ editor?, path, from, to? }: Unreal's own diff, in the editor (NativeDiffResult)" },
  "checkpoints.restore": { method: "POST", path: "/v1/checkpoints/restore", feature: "checkpoints", summary: "{ editor?, checkpoint, paths?, force? } (RestoreResult)" },
  "checkpoints.clear": { method: "DELETE", path: "/v1/checkpoints", feature: "checkpoints", summary: "?editor: drop the session's history" },
  "journal.state": { method: "GET", path: "/v1/journal", feature: "journal", summary: "?editor&group: state, groups, and a group's entries" },
  "journal.setGroup": { method: "PUT", path: "/v1/journal/group", feature: "journal", summary: "{ editor?, group: string | null }" },
  "journal.revert": { method: "POST", path: "/v1/journal/revert", feature: "journal", summary: "{ editor?, group }: revert one group" },
  "journal.revertAfter": { method: "POST", path: "/v1/journal/revert-after", feature: "journal", summary: "{ editor?, group }: revert every later group" },
  "hub.overview": { method: "GET", path: "/v1/hub", feature: "hub", summary: "the Playtest Hub: goals with their status, checks, attention, queue, recent runs (HubOverview)" },
  "hub.goal": { method: "GET", path: "/v1/hub/goals/:id", feature: "hub", summary: "one goal with its status and checks (GoalState)" },
  "hub.goal.create": { method: "POST", path: "/v1/hub/goals", feature: "hub", summary: "GoalDraft: create a goal or milestone ({ goal })" },
  "hub.goal.update": { method: "PUT", path: "/v1/hub/goals/:id", feature: "hub", summary: "GoalUpdate: change fields ({ goal })" },
  "hub.goal.delete": { method: "DELETE", path: "/v1/hub/goals/:id", feature: "hub", summary: "delete a goal (204)" },
  "hub.goal.addChecks": { method: "POST", path: "/v1/hub/goals/:id/checks", feature: "hub", summary: "{ checks: CheckRef[] }: add playtests to a goal ({ goal })" },
  "hub.goal.orderChecks": { method: "PUT", path: "/v1/hub/goals/:id/checks/order", feature: "hub", summary: "{ order: checkIds }: reorder a goal's playtests ({ goal })" },
  "hub.goal.removeCheck": { method: "DELETE", path: "/v1/hub/goals/:id/checks/:checkId", feature: "hub", summary: "remove a playtest from a goal ({ goal })" },
  "hub.goal.updateCheck": { method: "PUT", path: "/v1/hub/goals/:id/checks/:checkId", feature: "hub", summary: "GoalCheckUpdate: whether the playtest is required ({ goal })" },
  "hub.goal.addRequire": { method: "POST", path: "/v1/hub/goals/:id/requires", feature: "hub", summary: "{ goal, required? }: make a goal require another ({ goal })" },
  "hub.goal.orderRequires": { method: "PUT", path: "/v1/hub/goals/:id/requires", feature: "hub", summary: "{ order: goalIds }: reorder the goals a goal requires ({ goal })" },
  "hub.goal.removeRequire": { method: "DELETE", path: "/v1/hub/goals/:id/requires/:goalId", feature: "hub", summary: "stop a goal requiring another ({ goal })" },
  "hub.goal.signOff": { method: "POST", path: "/v1/hub/goals/:id/sign-off", feature: "hub", summary: "{ note? }: sign a goal off as the signed-in account ({ signOff }; 409 without one)" },
  "hub.goal.clearSignOff": { method: "DELETE", path: "/v1/hub/goals/:id/sign-off", feature: "hub", summary: "clear a goal's sign-off (204)" },
  "hub.goal.writePlaytests": { method: "POST", path: "/v1/hub/goals/:id/write-playtests", feature: "hub", summary: "{ text? }: ask a client that can write playtests to write them for the goal (202 { sessionId }; 409 when none can)" },
  "hub.check": { method: "GET", path: "/v1/hub/checks/:checkId", feature: "hub", summary: "?limit&before: one playtest with its health and runs (CheckDetail)" },
  "hub.check.update": { method: "PUT", path: "/v1/hub/checks/:checkId", feature: "hub", summary: "CheckMetaUpdate: what a playtest says about itself ({ meta })" },
  "hub.check.run": { method: "POST", path: "/v1/hub/checks/:checkId/run", feature: "hub", summary: "{ replay? }: queue a plan that runs one playtest ({ plan })" },
  "hub.rules": { method: "PUT", path: "/v1/hub/rules", feature: "hub", summary: "Partial<HealthRule>: the project's flaky and stale rule ({ rules })" },
  "hub.store.export": { method: "GET", path: "/v1/hub/store", feature: "hub", summary: "every goal, check record, sign-off and rule (HubExport)" },
  "hub.store.import": { method: "POST", path: "/v1/hub/store", feature: "hub", summary: "HubExport: merge by id (HubImportReport; 400 naming every record that does not read)" },
  "hub.runs": { method: "GET", path: "/v1/hub/runs", feature: "hub", summary: "?check&limit&before: run history, newest first ({ runs, more })" },
  "hub.run": { method: "GET", path: "/v1/hub/runs/:runId", feature: "hub", summary: "one run with its steps, series and the last pass (RunDetail)" },
  "hub.run.results": { method: "GET", path: "/v1/hub/runs/:runId/results", feature: "hub", summary: "the run's results.json as ue-mcp wrote it" },
  "hub.run.addLink": { method: "POST", path: "/v1/hub/runs/:runId/links", feature: "hub", summary: "{ link }: link a run to a tracker item ({ record })" },
  "hub.run.removeLink": { method: "DELETE", path: "/v1/hub/runs/:runId/links", feature: "hub", summary: "?url: remove a run's link ({ record })" },
  "hub.queue": { method: "GET", path: "/v1/hub/queue", feature: "hub", summary: "the plan queue (QueueState)" },
  "hub.queue.pause": { method: "PUT", path: "/v1/hub/queue/paused", feature: "hub", summary: "{ paused: boolean } (QueueState)" },
  "hub.lease": { method: "GET", path: "/v1/hub/lease", feature: "hub", summary: "the hub's hold on the editor ({ lease })" },
  "hub.release": { method: "POST", path: "/v1/hub/release", feature: "hub", summary: "Release the editor: stop what the hub holds on it and pause the queue (QueueState)" },
  "hub.plan": { method: "POST", path: "/v1/hub/plans", feature: "hub", summary: "PlanRequest: queue a plan ({ plan })" },
  "hub.plan.cancel": { method: "POST", path: "/v1/hub/plans/:id/cancel", feature: "hub", summary: "cancel a plan ({ plan })" },
  "hub.fix": { method: "POST", path: "/v1/hub/fix", feature: "hub", summary: "FixRequest: queue fix loops for failing playtests ({ queued, plan }; 409 when no client can run a fix loop)" },
  "tools.list": { method: "GET", path: "/v1/tools", feature: "tools.categories", summary: "project(list_categories)" },
  "tools.categories": { method: "POST", path: "/v1/tools/categories", feature: "tools.categories", summary: "{ category, enabled, scope?, editor? }: project(set_category_enabled)" },
  "tools.toolsets": { method: "POST", path: "/v1/tools/toolsets", feature: "tools.categories", summary: "{ toolsets, enabled, editor? }: project(set_toolset_enabled)" },
  "leases.list": { method: "GET", path: "/v1/leases", feature: "leases", summary: "project(list_leases)" },
  "tools.unsupported": { method: "ANY", path: "/v1/tools/*", feature: "tools.categories", summary: "any other method or path under /v1/tools: 405 { error }" },
  "leases.unsupported": { method: "ANY", path: "/v1/leases", feature: "leases", summary: "any other method on /v1/leases: 405 { error }" },
  "plugins.list": { method: "GET", path: "/v1/plugins", summary: "the project's plugins: version, status, categories" },
  "plugins.catalog": { method: "GET", path: "/v1/plugins/catalog", summary: "the registry's published plugins" },
  "plugins.install": { method: "POST", path: "/v1/plugins/install", summary: "{ name, version? }" },
  "plugins.uninstall": { method: "POST", path: "/v1/plugins/uninstall", summary: "{ name }" },
  "plugins.update": { method: "POST", path: "/v1/plugins/update", summary: "{ name? }" },
  "flows": { method: "ANY", path: "/v1/flows/*", summary: "list, plan and run flows; see flow/http-server.ts" },
  "ext": { method: "ANY", path: "/v1/ext/*", summary: "the routes each loaded extension serves under /v1/ext/<name>" },
  "ui.extension": { method: "GET", path: "/ui/ext/*", feature: "ui.extensions", summary: "each loaded extension's own bundle, at /ui/ext/<name>/" },
  "ui": { method: "GET", path: "/ui/*", summary: "the client UI bundle; GET /ui/?token= sets the login cookie and needs no other authentication" },
  "mcp": { method: "WS", path: "/v1/mcp", summary: "one MCP session per socket" },
});

/** Keeps each route's method and path as literals, and its prose as prose. */
function routeTable<const T extends Record<string, DaemonRoute>>(
  table: T,
): { readonly [K in keyof T]: { readonly method: T[K]["method"]; readonly path: T[K]["path"]; readonly feature?: DaemonFeature; readonly summary: string } } {
  return table;
}

export type DaemonRouteName = keyof typeof DAEMON_ROUTES;

/**
 * `METHOD /path` with each `:param` shown as `<param>`, for a message that
 * names a route. `method` names one method of an `ANY` route.
 */
export function describeRoute(name: DaemonRouteName, method?: "GET" | "POST" | "PUT" | "DELETE"): string {
  const route: DaemonRoute = DAEMON_ROUTES[name];
  return `${method ?? route.method} ${route.path.replace(/:([A-Za-z]+)/g, "<$1>")}`;
}

/** A route's path with its `:param` segments filled in, each URL-encoded. */
export function routePath(name: DaemonRouteName, params: Record<string, string> = {}): string {
  return DAEMON_ROUTES[name].path.replace(/:([A-Za-z]+)/g, (_, key: string) => encodeURIComponent(params[key] ?? ""));
}

// ── Headers ───────────────────────────────────────────────────────

/** Header names of the protocol, lower case as Node reads them. */
export const DAEMON_HEADERS = {
  /** Sent back on the WebSocket upgrade of /v1/mcp: the id of the MCP session that socket is. */
  session: "x-ue-mcp-session",
  /** Sent on the /v1/mcp upgrade: the client's name until it introduces itself. */
  client: "x-ue-mcp-client",
  /** Sent on the /v1/mcp upgrade: `read-only` opens the session read-only. */
  access: "x-ue-mcp-access",
  /** The flow HTTP server's token, accepted beside a bearer token. */
  flowToken: "x-ue-mcp-token",
  /**
   * Sent on the /v1/mcp upgrade: the session that started this one, so a
   * subagent with its own connection is attributed to its primary. The shim
   * sends UE_MCP_PARENT_SESSION here.
   */
  parent: "x-ue-mcp-parent",
  /**
   * Set by a client that relays another device's requests to the daemon: that
   * device's name, URI-encoded. The relaying client sets it itself and never
   * passes on a value the device sent. A hub sign-off records it as the
   * device it came through; absent, the request came from this computer.
   */
  via: "x-ue-mcp-via",
} as const;

// ── Events ────────────────────────────────────────────────────────

/**
 * Every event type the daemon itself publishes. An extension's events are
 * `<extension name>.<type>`, and the stream sends `gap` (no id) when the
 * reader asked to resume from an event the daemon no longer holds.
 */
export const DAEMON_EVENT_TYPES = [
  "daemon.started",
  "daemon.stopping",
  "editor.connected",
  "editor.disconnected",
  "editor.operation",
  "client.connected",
  "client.disconnected",
  "call.started",
  "call.finished",
  "session.access",
  "leases.released",
  "plugins.changed",
  "update.available",
  "update.staged",
  "update.building",
  "update.applied",
  "update.failed",
  "dialog.opened",
  "dialog.updated",
  "dialog.answered",
  "dialog.delegated",
  "dialog.closed",
  "flow.run_started",
  "flow.step_started",
  "flow.step_completed",
  "flow.step_failed",
  "flow.run_completed",
  "script.run_started",
  "script.step_started",
  "script.step_acted",
  "script.step_passed",
  "script.step_failed",
  "script.verdict_requested",
  "script.verdict",
  "script.run_finished",
  "checkpoint.created",
  "checkpoint.restored",
  "checkpoints.toggled",
  "checkpoints.cleared",
  "pie.started",
  "pie.ended",
  "pie.paused",
  "pie.resumed",
  "disruption.requested",
  "disruption.updated",
  "disruption.hold",
  "hub.changed",
  "hub.lease",
  "hub.held",
] as const;

/** The Play session states the editor pushes, each published as `pie.<state>`. */
export const PIE_STATES = ["started", "ended", "paused", "resumed"] as const;
export type PieState = (typeof PIE_STATES)[number];

/**
 * The data of a `pie.*` event. `resync` marks the one published when the
 * daemon (re)attaches to an editor, read from the editor rather than pushed by
 * it: a client that missed events while the editor was away takes it as the
 * current state. `at` is the editor's clock for a pushed event.
 */
export interface PieEventData {
  editor: string;
  state: PieState;
  simulate: boolean;
  at: string;
  resync?: boolean;
  paused?: boolean;
}

export type DaemonEventType = (typeof DAEMON_EVENT_TYPES)[number];

/** The SSE event that says events were missed. */
export const DAEMON_GAP_EVENT = "gap";

export interface DaemonEvent {
  id: number;
  type: string;
  at: string;
  data: Record<string, unknown>;
}

// ── Discovery ─────────────────────────────────────────────────────

/** The record a daemon publishes for its project root (see discoveryPath). */
export interface DaemonDiscovery {
  pid: number;
  port: number;
  token: string;
  /** ue-mcp package version the daemon runs. */
  version: string;
  apiVersion: number;
  /** Normalized project root (see normalizeProjectRoot). */
  projectRoot: string;
  startedAt: string;
}

// ── Shapes shared with the rest of ue-mcp ─────────────────────────
// Declared here in their own right. ue-mcp's internal types of the same shape
// are checked against these where the daemon hands a value over, so an
// internal change that breaks one fails to compile instead of changing the API.

/** The context strategies a client can be served. */
export type ContextStrategy = "full" | "lean" | "micro";

/** What an action does to the editor or project. */
export type ActionEffect = "read" | "mutate" | "unknown";

/** Where an action's effect came from: `declared` on its spec, `inferred` from its name. */
export type ActionEffectSource = "declared" | "inferred";

/** An MCP elicitation form. */
export interface ElicitParams {
  message: string;
  /**
   * Machine-readable context for a client that renders the form as its own
   * card, keyed by a namespaced name such as `ue-mcp/guard`. Optional; a
   * client that ignores it still has the message and the form.
   */
  _meta?: Record<string, unknown>;
  requestedSchema: {
    type: "object";
    properties: Record<string, ElicitPrimitiveSchema>;
    required?: string[];
  };
}

export type ElicitPrimitiveSchema =
  | { type: "string"; title?: string; description?: string; enum?: string[]; enumNames?: string[]; default?: string }
  | { type: "number" | "integer"; title?: string; description?: string; default?: number }
  | { type: "boolean"; title?: string; description?: string; default?: boolean }
  | (ElicitMultiSelectBase & { items: { type: "string"; enum: string[] } })
  | (ElicitMultiSelectBase & { items: { anyOf: Array<{ const: string; title: string }> } });

/** A checkbox group: the answer is the list of ticked values. */
interface ElicitMultiSelectBase {
  type: "array";
  title?: string;
  description?: string;
  minItems?: number;
  maxItems?: number;
  default?: string[];
}

/** The answer to an elicitation form. */
export interface ElicitResult {
  action: "accept" | "decline" | "cancel";
  content?: Record<string, string | number | boolean | string[]>;
}

// ── Model providers ───────────────────────────────────────────────

/** A tool invocation the model asked for, surfaced on an assistant turn. */
export interface ModelToolCall {
  /** Provider-assigned id, echoed back on the matching tool-result message. */
  id: string;
  /** Name of the tool the model wants to run. */
  name: string;
  /** Arguments the model produced, already parsed from JSON. */
  arguments: Record<string, unknown>;
}

export interface ModelMessage {
  role: "system" | "user" | "assistant" | "tool";
  /** Text content. Empty string is valid (e.g. an assistant turn that is purely tool calls). */
  content: string;
  /** Present on assistant turns that request tools. */
  toolCalls?: ModelToolCall[];
  /** On a `tool` message: the id of the tool call this result answers. */
  toolCallId?: string;
  /** On a `tool` message: the tool's name (some providers key results by name). */
  name?: string;
}

/** A tool the model is allowed to call, described to the provider. */
export interface ModelToolDefinition {
  name: string;
  description?: string;
  /** JSON Schema for the tool's arguments. */
  parameters: Record<string, unknown>;
}

/**
 * One completion request of a flow agent step. A provider may ignore any field
 * it does not support; a request that only sets `prompt` is a plain completion.
 */
export interface ModelRequest {
  /** System prompt / instructions. */
  system?: string;
  /** Convenience single user message. When `messages` is set it takes precedence and `prompt` is ignored. */
  prompt?: string;
  /** Full conversation. Overrides `prompt` when present. */
  messages?: ModelMessage[];
  /** Model identifier, provider-specific. */
  model?: string;
  /** Max output tokens. */
  maxTokens?: number;
  /** Sampling temperature, when the provider supports it. */
  temperature?: number;
  /** Stop sequences, when the provider supports them. */
  stop?: string[];
  /** Optional JSON Schema. When provided, the provider should return output that parses against it and set `parsed`. */
  schema?: Record<string, unknown>;
  /** Tools the model may call this turn. */
  tools?: ModelToolDefinition[];
  /** Constrains tool use this turn. */
  toolChoice?: "auto" | "none" | "required" | { name: string };
  /** Cancellation signal, aborted on timeout; pass it to the HTTP client so in-flight calls are cancelled. */
  signal?: AbortSignal;
}

export interface ModelResponse {
  /** Raw text returned by the model. */
  text: string;
  /** When a schema was provided and the output parsed, the structured value. */
  parsed?: unknown;
  /** Tool calls the model requested, if any. */
  toolCalls?: ModelToolCall[];
  /**
   * Why the model stopped: `stop` (natural end), `tool_use` (wants tools),
   * `length` (hit token cap), or any provider-specific string. Absent is a
   * natural stop.
   */
  finishReason?: "stop" | "tool_use" | "length" | (string & {});
  /** Token usage, if the provider reports it. */
  usage?: { inputTokens?: number; outputTokens?: number };
  /** The model that actually served the request, if the provider reports it. */
  model?: string;
}

/** The model behind flow agent steps, as an extension registers it (feature "models.provider"). */
export interface ModelProvider {
  complete(req: ModelRequest): Promise<ModelResponse>;
}

// ── Dialogs ───────────────────────────────────────────────────────

/**
 * One block of an editor dialog's body, in reading order (#1891).
 *
 * The flat `message` is every text widget of the dialog on its own line,
 * buttons and table cells included. `blocks` is the same body as it reads:
 *
 *   paragraph  text outside any table and any button; texts laid side by side
 *              in the dialog are one paragraph.
 *   table      a list or tree view: its visible column headings and one row of
 *              cells per row. `checked` (when any row has a checkbox) holds each
 *              row's state, null for a row without one, and `items` the index of
 *              that row in the dialog's `items`, which respond_to_dialog takes.
 *
 * The dialog's title and its button labels are never part of it.
 */
export type DialogBlock =
  | { type: "paragraph"; text: string }
  | {
      type: "table";
      columns: string[];
      rows: string[][];
      checked?: Array<boolean | null>;
      items?: Array<number | null>;
    };

/**
 * A blocking dialog put to a daemon extension, for a call that extension made.
 *
 * The same form an MCP client is elicited with (`message`, `requestedSchema`,
 * answered with `content.button` and, for a dialog with rows, `content.items`),
 * plus the dialog itself so the extension can draw it rather than parse it.
 */
export interface DialogPromptRequest extends ElicitParams {
  dialog: {
    /** The editor showing it. */
    editor: string;
    title: string;
    message: string;
    /** In the order the dialog lays them out. */
    buttons: string[];
    /** The rows a person can tick, for a dialog that asks per item. */
    items?: Array<{ index: number; label: string; cells: string[]; checked: boolean }>;
    /** The body as paragraphs and tables, when the plugin reports it; `message` otherwise. */
    blocks?: DialogBlock[];
    /**
     * Set for a native Win32 dialog of the editor process (a GPU device loss,
     * an assert box): its window handle, process, kind, a plain summary, and
     * any log lines that explain it.
     */
    native?: {
      hwnd: string;
      pid: number;
      kind: "gpu-crash" | "assert" | "ensure" | "missing-modules" | "crash" | "message";
      summary: string;
      details?: string[];
    };
    /** The `category.action` whose call met the dialog. */
    subject: string;
    /** An answer sooner than this many milliseconds is not taken as a person's. */
    minHumanMs: number;
  };
}

/**
 * The form's three answers, or `delegate`: the person handed this one dialog
 * to the agent, which may then answer it as it would in auto mode.
 */
export type DialogPromptResult = ElicitResult | { action: "delegate" };

export type DialogPromptFn = (request: DialogPromptRequest) => Promise<DialogPromptResult>;

/**
 * The data of a dialog.* event. `type` names the event: dialog.opened,
 * dialog.updated (the same dialog, read again with more of its content: rows,
 * blocks, or a changed tick), dialog.answered (button, and via: prompt or
 * action), dialog.delegated, dialog.closed.
 */
export interface DialogEventData {
  editor: string;
  title: string;
  message: string;
  buttons: string[];
  items?: Array<{ index: number; label: string; cells: string[]; checked: boolean }>;
  /** The body as paragraphs and tables, when the plugin reports it. */
  blocks?: DialogBlock[];
  mode?: "auto" | "interactive" | "defer";
  button?: string;
  /** `person`: pressed by a person directly, through `nativeDialogs.press` or POST /v1/dialogs/native/press. */
  via?: "prompt" | "action" | "person";
  /**
   * Set when the dialog is a native Win32 window of the editor process (feature
   * "dialogs.native"): a GPU device loss, an assert box, a startup prompt. The
   * bridge cannot answer while one is up; it is read and pressed from outside.
   */
  native?: NativeDialogInfo;
}

/** What a native dialog is, beyond its title, text and buttons (feature "dialogs.native"). */
export interface NativeDialogInfo {
  /** The window handle, as a decimal string. */
  hwnd: string;
  /** The editor process that owns it. */
  pid: number;
  kind: "gpu-crash" | "assert" | "ensure" | "missing-modules" | "crash" | "message";
  /** One plain sentence on what it means and what answering it does. */
  summary: string;
  /** Lines from the editor log that explain it, such as the GPU state after a device loss. */
  details?: string[];
}

/** One native dialog an editor of this project shows, as `nativeDialogs.list` and GET /v1/dialogs/native give it. */
export interface NativeDialogView extends NativeDialogInfo {
  /** The editor session it belongs to. */
  editor: string;
  title: string;
  message: string;
  /** Each static text control, in window order. */
  text: string[];
  /** Button labels in window order. */
  buttons: string[];
  /** The window does not answer the window manager. */
  hung: boolean;
}

/** What one press of a native dialog's button did. */
export interface NativeDialogPressResult {
  /** The press reached the window. */
  pressed: boolean;
  /** The window was gone within a few seconds of the press. */
  closed: boolean;
  button?: string;
  error?: string;
}

// ── Editors and sessions ──────────────────────────────────────────

/** Why the editor went away: announced beforehand (restarting, rebuild), or read from the evidence after. */
export type DisconnectCause = "closed" | "crashed" | "restarting" | "rebuild" | "unknown";

export interface EditorStatus {
  name: string;
  connected: boolean;
  port: number;
  pid: number | null;
  lastDisconnect: { cause: DisconnectCause; detail?: string; at: string } | null;
}

/** One open MCP session on the daemon (feature "sessions.access"). */
export interface McpSessionInfo {
  /** Stable for the life of the socket; also sent as `x-ue-mcp-session` on the WebSocket upgrade. */
  id: string;
  /** The MCP client's name once it has introduced itself, else what its shim said. */
  client: string;
  connectedAt: string;
  /** `read-only` refuses every call whose declared effect is `mutate` or `unknown`. */
  access: "read-write" | "read-only";
}

// ── Editor consent ────────────────────────────────────────────────

/** What a disruptive request would do to the editor (feature "editor.consent"). */
export type EditorDisruptionOp = "stop" | "restart" | "rebuild" | "build" | "live_coding";

/** One participant's answer to a request. `unanswered` counts as a wait. */
export interface EditorDisruptionAnswer {
  session: string;
  label: string;
  /** The participant that started this one, when known. */
  parent?: string;
  answer: "go" | "wait_until_released" | "block" | "unanswered";
  reason: string;
  /** True when read from what the participant is doing rather than said by it. */
  auto: boolean;
  /** Assets it holds leases on. */
  touched?: string[];
}

/**
 * A request to stop, restart, rebuild, build under or Live Coding patch the
 * editor (feature "editor.consent"). `confirm` waits on the requester saying
 * its own subagents are none or paused; `self.inflight` lists its other work
 * in flight, which holds the request too.
 */
export interface EditorDisruptionRequest {
  id: string;
  op: EditorDisruptionOp;
  action: string;
  requester: { id: string; label: string; parent?: string };
  reason?: string;
  state: "pending" | "confirm" | "blocked" | "approved" | "done" | "denied" | "expired";
  createdAt: string;
  expiresAt: string;
  answers: EditorDisruptionAnswer[];
  self: { attestationRequired: boolean; attested: "none" | "paused" | null; inflight: string[] };
  override?: { decision: "approve" | "deny"; by: string; at: string };
}

/** Options for api.editorOperation (feature "editor.consent"). */
export interface EditorOperationOptions {
  /** The participant asking, as registered or passed to callAction; absent is the extension itself. */
  session?: { id: string; label?: string; parent?: string };
  /** The requester's own subagents are none or paused. */
  subagents?: "none" | "paused";
  reason?: string;
  /** A person asked (a button press), so no subagent attestation is owed. */
  person?: boolean;
}

// ── UI bundles ────────────────────────────────────────────────────

export interface UiManifest {
  name: string;
  version: string;
  /** Daemon API versions this bundle works with, inclusive. */
  api: { min: number; max: number };
  /** Relative path (forward slashes) to lowercase hex SHA-256. */
  files: Record<string, string>;
  /**
   * A module another page loads from /ui/ext/<name>/: `entry` is a file in
   * `files`, `host` names the page meant to load it. Opaque to the daemon.
   */
  module?: { entry: string; host: string };
}

/** A loaded extension's own bundle, as /v1/health lists it. */
export interface UiBundleInfo {
  name: string;
  version: string;
  /** Where it is served: /ui/ext/<name>/. */
  base: string;
  module?: { entry: string; host: string };
}

// ── Health ────────────────────────────────────────────────────────

/** An extension the daemon did not load, and what it needs. */
export interface RefusedExtension {
  name: string;
  version: string;
  reason: string;
  /** The ue-mcp version it needs, when that is why. */
  needsUeMcp?: string;
}

/** GET /v1/health. */
export interface DaemonHealth {
  ok: true;
  pid: number;
  version: string;
  /** DAEMON_API_VERSION: the HTTP and shim protocol, which a UI bundle's range is checked against. */
  apiVersion: number;
  projectRoot: string;
  startedAt: string;
  editors: EditorStatus[];
  /** Open MCP sessions. */
  clients: number;
  /** Requests received from MCP sessions and not yet answered. */
  inflight: number;
  lastEventId: number;
  extensions: Array<{ name: string; version: string }>;
  /** Extensions this daemon refused to load, with why; the client offers the update that fixes it. */
  refusedExtensions?: RefusedExtension[];
  /** Version of the UI bundle being served at /ui/, or null. */
  ui: string | null;
  /**
   * Each loaded extension's own bundle, in load order: name, version, `base`
   * (/ui/ext/<name>/) and the signed `module` it declares (feature "ui.extensions").
   */
  uiBundles?: UiBundleInfo[];
  /**
   * What the HTTP API offers beyond apiVersion, for feature detection: the
   * names in DAEMON_FEATURES. A route added under a feature names it in
   * DAEMON_ROUTES; DAEMON_FEATURES_BEYOND_ROUTES says what the others change.
   * Absent on a daemon older than the field.
   */
  features?: readonly string[];
  /** Open MCP sessions with their access mode (feature "sessions.access"). */
  sessions?: McpSessionInfo[];
}

// ── Install status ────────────────────────────────────────────────

export type InstallKind = "source" | "binary";

/** The marker an install writes beside the plugin: how and for what it was installed. */
export interface InstallMarker {
  kind: InstallKind;
  /** Engine major.minor the plugin was installed for, e.g. "5.7". */
  engine: string | null;
  /** Unreal platform name: Win64, Mac or Linux. */
  platform: string | null;
  /** ue-mcp package version the plugin came from. */
  version: string | null;
}

/** A source bridge deployed by an update and not built yet. */
export interface PendingBuild {
  /** The bridge version whose source was deployed. */
  version: string;
  /** When the build was put off. */
  at: string;
  /** Why it was put off, e.g. "editor running". */
  reason: string;
  /** Set once a build of this source failed. */
  failed?: { at: string; message: string };
}

/** The bridge plugin in the project, as the install check finds it. */
export interface InstallBridgeStatus {
  deployed: boolean;
  pluginDir: string | null;
  installKind: InstallKind | null;
  enabledInUproject: boolean;
  packagedVersion: string | null;
  installedVersion: string | null;
  versionMatch: boolean | null;
  compiled: boolean;
  binaryPath?: string;
  stale: boolean;
  /** A source bridge deployed by an update and not built yet; the next start through ue-mcp builds it first (#1593). */
  pendingBuild: PendingBuild | null;
}

/** The C++ toolchain Unreal compiles a plugin with, on this machine. */
export interface ToolchainReport {
  present: boolean;
  /** What was found, named the way the installer names it. */
  name: string | null;
  detail?: string;
  /** Every location consulted, so a miss is diagnosable rather than a shrug. */
  probed: string[];
  /** The exact thing to install when nothing was found. */
  fix?: string;
}

/** The engine the project resolves to. */
export interface EngineStatus {
  resolved: boolean;
  root: string | null;
  source: string | null;
  buildTool: string | null;
}

export interface InstallProblem {
  /** Stable identifier, so a caller can branch without matching prose. */
  code:
    | "no_engine"
    | "no_toolchain"
    | "bridge_not_deployed"
    | "bridge_not_enabled"
    | "bridge_never_compiled"
    | "bridge_build_stale"
    | "bridge_version_mismatch"
    | "bridge_engine_mismatch";
  what: string;
  fix: string;
  /** The one-step remedy a UI can offer as a button, when there is one; `rebuild` is POST /v1/editor/rebuild. */
  action?: "rebuild";
  /** On bridge_engine_mismatch: the BuildIds and versions that disagree. */
  engineMismatch?: EngineMismatch;
}

/**
 * Why bridge_engine_mismatch was reported: the bridge, or the project's
 * UE_MCP_Studio plugin, is built for another engine build than the one it
 * loads in, its module manifests disagree, or its install marker names
 * another release than its descriptor.
 */
export interface EngineMismatch {
  /** The engine's BuildId, or null when it could not be read. */
  engineBuildId: string | null;
  /** The BuildId in the bridge's Binaries/<Platform>/UnrealEditor.modules. */
  bridgeBuildId: string | null;
  /** The same for the project's UE_MCP_Studio plugin, or null when it has none. */
  studioBuildId: string | null;
  /** Every distinct BuildId the bridge and Studio module manifests name, sorted. */
  manifestBuildIds: string[];
  /** The version the bridge's install marker claims. */
  markerVersion: string | null;
  /** The VersionName in the bridge's descriptor. */
  descriptorVersion: string | null;
  /** One sentence per thing found wrong. */
  reasons: string[];
}

export interface ClientStatus {
  id: string;
  name: string;
  configPath: string;
  detected: boolean;
  /** The config has a ue-mcp entry. */
  configured: boolean;
  /** The exact version the entry pins, or null for @latest or no entry. */
  pinned: string | null;
}

/** GET /v1/install, and `ue-mcp status --json`: the install and editor state of one project. */
export interface StatusReport {
  version: string;
  node: { version: string; execPath: string };
  project: { path: string; name: string; engine: string | null; engineAssociation: string | null };
  plugin: InstallBridgeStatus & { dirSource: "instance-record" | "default" | "scan" | null; marker: InstallMarker | null };
  editor: {
    running: boolean;
    instances: Array<{ pid: number; port: number; state: string | null; alive: boolean; startedAt: string | null }>;
  };
  toolchain: ToolchainReport;
  engine: EngineStatus;
  config: { path: string; exists: boolean };
  clients: ClientStatus[];
  ok: boolean;
  problems: InstallProblem[];
  nextSteps: string[];
}

// ── Machine report ────────────────────────────────────────────────

/** Version of the MachineReport shape. Bump on a breaking change; add fields freely. */
export const MACHINE_REPORT_VERSION = 1;

export interface MachineDaemon {
  key: string;
  /** The project root as the daemon recorded it (normalized: lower case, forward slashes). */
  projectRoot: string;
  /** The project's .uproject: a listed project's own, else the one in the recorded folder where the file system finds it. */
  uproject: string | null;
  pid: number;
  port: number;
  version: string;
  startedAt: string;
  discovery: string;
  log: string;
}

export interface MachineProject {
  name: string;
  uproject: string;
  dir: string;
  key: string;
  source: "recent" | "added";
  /** When Unreal last opened it, as its recent list says (YYYY.MM.DD-HH.MM.SS), else null. */
  lastOpened: string | null;
  engine: { association: string | null; version: string | null };
  /** An editor with the bridge has this project open: installs and launches wait for it to close. */
  editor: { running: boolean; pids: number[] };
  ueMcp: { setUp: boolean; bridge: { version: string; kind: "source" | "binary" | null } | null; config: boolean };
  /** Daemon extensions this project records, by name: the version it loads. */
  extensions: Record<string, string>;
  /** The plugins in the project's own Plugins folder. */
  plugins: Array<{ name: string; version: string | null }>;
  daemon: { discovery: string; log: string; running: MachineDaemon | null };
}

/**
 * The machine report: this computer's Unreal projects, the ue-mcp state of
 * each, and every daemon running, for `ue-mcp projects --json` and any app
 * that manages projects machine-wide. Tokens never appear.
 */
export interface MachineReport {
  version: typeof MACHINE_REPORT_VERSION;
  /** The ue-mcp package that wrote the report. */
  ueMcp: string;
  /** The folder of daemon discovery files and logs. */
  daemonDir: string;
  projects: MachineProject[];
  daemons: MachineDaemon[];
}

// ── Checkpoints ───────────────────────────────────────────────────

export type CheckpointTrigger = "client" | "restore-before" | "restore-after";
export type ChangeStatus = "added" | "modified" | "deleted";
export type ChangeKind = "package" | "config" | "source" | "other";

export interface CheckpointInfo {
  id: string;
  label: string;
  createdAt: string;
  trigger: CheckpointTrigger;
  files: number;
}

export interface ChangeEntry {
  path: string;
  status: ChangeStatus;
  kind: ChangeKind;
  untracked?: true;
  large?: true;
  diff?: string;
  blueprintDiff?: unknown;
  /** A package's diff from the editor, built on Unreal's own diff code. */
  assetDiff?: AssetDiff;
}

export interface RestoreRefusal {
  path: string;
  reason: "edited-outside" | "not-recorded" | "write-failed";
  detail?: string;
}

export interface CheckpointState {
  editor: string;
  enabled: boolean;
  source: "config" | "session";
  configured: boolean;
  repository: string | null;
  checkpoints: CheckpointInfo[];
}

export interface CheckpointResult extends CheckpointInfo {
  editor: string;
  changes: ChangeEntry[];
  dirtyPackages: string[];
  editorConnected: boolean;
}

export interface ChangesResult {
  editor: string;
  from: string;
  to: string | null;
  changes: ChangeEntry[];
  /**
   * Which packages carry an asset diff. Past `limit`, the rest are `deferred`:
   * their assetDiff says so, and assetDiff(from, to, path) diffs one.
   */
  assetDiffs?: { limit: number; diffed: number; deferred: string[] };
}

export interface NativeDiffResult {
  editor: string;
  path: string;
  /** window: Unreal's diff window; editor: the asset editor on the one version of an added or deleted asset; external: the text diff tool. */
  opened: "window" | "editor" | "external";
  windowTitle?: string;
  tool?: string;
  /** The label and ISO 8601 date each side of the diff window shows. */
  revisions?: { before: { label: string; date: string }; after: { label: string; date: string } };
  /** True when the versions loaded from the history are unloaded once the window or asset editor closes (#1483). */
  unloadsOnClose?: boolean;
  /** The /Temp packages those versions were loaded into. */
  tempPackages?: string[];
}

export interface RestoreResult {
  editor: string;
  checkpoint: string;
  before: string;
  after: string;
  restored: Array<{ path: string; action: "written" | "deleted" }>;
  refused: RestoreRefusal[];
  reloaded: string[];
  unloaded: string[];
  configsReloaded: string[];
  reopened: string[];
}

/** One record of Unreal's diff, as the engine produced it. */
export interface AssetDiffRecord {
  /** The engine's own message for it, such as "Base Color changed value in M_Blue" or "Added Node Constant". */
  displayString: string;
  /** EPropertyDiffType (PropertyValueChanged, PropertyAddedToA, PropertyAddedToB), EDiffType (NODE_ADDED, PIN_LINKEDTO_NODE...) or ETreeDiffType (NODE_ADDED...). */
  diffType: string;
  /** EDiffType::Category of a graph record: ADDITION, SUBTRACTION, MODIFICATION, MINOR. */
  category?: string;
  toolTip?: string;
  /** The actor, component, variable or row a record belongs to. */
  group?: string;
  /** A difference under a property the details panel does not show. */
  hidden?: boolean;
  /** FPropertySoftPath::ToDisplayName of a property record. */
  path?: string;
  /** The path as the details panel labels it: "Hide when Full" for bHideWhenFull. */
  label?: string;
  property?: string;
  /** Both values exported with FProperty::ExportText; null where the path does not exist on that side. */
  before?: string | null;
  after?: string | null;
  graph?: string;
  node1?: AssetDiffNode;
  node2?: AssetDiffNode;
  pin1?: { name: string; direction: string; nodeGuid?: string; defaultValue: string; linkedTo: string[] };
  pin2?: { name: string; direction: string; nodeGuid?: string; defaultValue: string; linkedTo: string[] };
  /** For a NODE_PROPERTY graph record: the property records behind it. */
  properties?: AssetDiffRecord[];
  /** For an added or removed actor: its class, label and transform. */
  facts?: AssetFact[];
  treeLocation?: number[];
  owningObjectPath?: string;
  propertyDiffType?: string;
}

/** A graph node a record names. */
export interface AssetDiffNode {
  guid: string;
  name: string;
  class: string;
  /** ENodeTitleType::ListView, the short title the engine's DisplayString uses. */
  title: string;
  /** ENodeTitleType::FullTitle. Set on NODE_ADDED and NODE_REMOVED records only, as are pins and properties. */
  fullTitle?: string;
  /** Visible input pins (exec pins left out) with their default values. */
  pins?: AssetDiffNodePin[];
  /** Class of the object whose properties are listed: the UMaterialExpression for a material node, the node otherwise. */
  propertiesOf?: string;
  /** The details panel's properties (CPF_Edit) of that object, exported as text, in class order (most derived first). */
  properties?: AssetDiffNodeProperty[];
  /** Properties past the per-node limit, counted. */
  propertiesDropped?: number;
}

export interface AssetDiffNodePin {
  name: string;
  displayName: string;
  defaultValue: string;
  /** Wired to another node, so the default is not what the node reads. */
  linked: boolean;
}

export interface AssetDiffNodeProperty {
  name: string;
  /** The details panel's label, such as "Value" for a Constant's R. */
  displayName: string;
  /** FProperty::ExportText of the value. */
  value: string;
  /** Still the class default. */
  isDefault: boolean;
}

/** A group of records under one of the engine's own diff categories (Graph: EventGraph, Components, Defaults, Details...). */
export interface AssetDiffSection {
  category: string;
  kind: "properties" | "graph" | "components";
  /** The engine API the records came from. */
  engineSource: string;
  entries: AssetDiffRecord[];
  /** How many of the records are under properties the details panel does not show (marked hidden). */
  hiddenChanges?: number;
  /** The engine's INFO_MESSAGE results for the section, such as what a Widget Blueprint diff does not cover. */
  notes?: string[];
}

export interface AssetFact {
  name: string;
  value: string;
}

export type AssetThumbnail =
  | {
      id: string;
      url: string;
      /** rendered or saved: a picture of the asset. graph: the changed graph of a graph-only Blueprint change. */
      source: "rendered" | "saved" | "graph";
      note?: string;
      /** The graph a graph picture shows. */
      graph?: string;
    }
  | { error: string };

/** One version of the package, as the editor loaded it. */
export interface AssetVersion {
  class: string;
  objectName?: string;
  facts: AssetFact[];
  thumbnail?: AssetThumbnail;
  /** The asset's own picture, kept here when thumbnail is the changed graph instead. */
  classThumbnail?: AssetThumbnail;
}

export interface AssetDiff {
  /** ready: diffed. deferred: over the up-front limit, diff it by path. error: see error. */
  state: "ready" | "deferred" | "error";
  error?: string;
  packageName?: string;
  assetClass?: string;
  status?: "added" | "modified" | "deleted";
  /** Which of Unreal's diffs serves this class, and what these records are. */
  engine?: {
    tool: string;
    note: string;
    native: "window" | "details" | "text";
    nativeAvailable?: boolean;
    nativeReason?: string;
    /** A Blueprint: whether its type's own diff (UBlueprint::FindDiffs, a Widget Blueprint's widget tree) ran. Absent from bridges before it did. */
    blueprintTypeDiff?: boolean;
  };
  /** null: the version did not exist. */
  before?: AssetVersion | null;
  after?: AssetVersion | null;
  classChanged?: string;
  sections?: AssetDiffSection[];
  /** The engine's own text form of the diff where its diff is a text diff (a DataTable's CSV), as a unified diff. */
  textDiff?: { format: string; diff: string };
  /** blueprint(diff)'s structural summary, for a Blueprint. */
  blueprint?: Record<string, unknown>;
  recordCount?: number;
  /** Records past maxRecords, counted. */
  recordsDropped?: number;
  /** One line to decide on. */
  summary: string;
  /** Whether Unreal's own diff view can open this, and why not. */
  nativeDiff: { available: boolean; reason?: string };
}

// ── Skills ────────────────────────────────────────────────────────

/** Who a skill belongs to. */
export type DaemonSkillOwner =
  | { kind: "ue-mcp"; version: string }
  | { kind: "plugin"; package: string; version: string | null }
  | { kind: "project" };

/** A project folder skills are read from. */
export type ProjectSkillFolder = ".claude/skills" | ".agents/skills";

/** A copy of a skill another entry with the same id hides. */
export interface ShadowedSkill {
  location: "packaged" | "project";
  folder?: ProjectSkillFolder;
  dir: string;
}

export interface DaemonSkill {
  /**
   * The skill's directory name, unique in one listing. A plugin's skills are
   * installed as `<namespace>-<skill>` (installedSkillName), so that is their id.
   */
  id: string;
  /** Frontmatter `name`, or the id when the frontmatter has none. */
  name: string;
  /** Frontmatter `description`, or "" (reported in `problems`). */
  description: string;
  /** Absolute path of the skill directory. */
  dir: string;
  /** Every other file in the directory, relative to it with `/` separators. */
  files: string[];
  /** SKILL.md without its frontmatter. */
  body: string;
  frontmatter: Record<string, unknown>;
  /** SKILL.md length in characters / 4, rounded up. */
  tokens: number;
  owner: DaemonSkillOwner;
  location: "packaged" | "project";
  /** Which project folder, for a project skill. */
  folder?: ProjectSkillFolder;
  /**
   * The copies with the same id this one hides: a project skill hides ue-mcp's
   * packaged copy of itself, and `.claude/skills` hides `.agents/skills`.
   * Absent when it hides nothing.
   */
  shadows?: ShadowedSkill[];
  /** What is wrong with SKILL.md, such as frontmatter that does not parse. Empty when nothing is. */
  problems: string[];
}

// ── Context projection and action effects ─────────────────────────

export interface ContextProjectionOptions {
  strategy: ContextStrategy;
  /** Category names. Omitted means every registered, enabled category. */
  categories?: string[];
}

export interface ProjectedTool {
  name: string;
  description: string;
  /** JSON Schema, as tools/list sends it. */
  inputSchema: Record<string, unknown>;
  /** The category this tool is, or null for one that is not a category (micro's gateway, lean's catalog, flow). */
  category: string | null;
}

export interface ContextProjection {
  strategy: ContextStrategy;
  /** The categories in scope after resolving. */
  categories: string[];
  /** The initialize instructions this strategy sends, narrowed to the scope when one was given. */
  instructions: string;
  /** What tools/list sends, in order. */
  tools: ProjectedTool[];
  /** Characters of JSON.stringify({ instructions, tools }) as the server sends them, / 4, rounded. */
  seedTokens: number;
  estimator: "chars/4";
}

/** One action's effect and whether it destroys something, for a client deciding what to confirm. */
export interface ActionEffectInfo {
  category: string;
  action: string;
  effect: ActionEffect;
  /** `declared` on the spec, `inferred` by plugin injection from the name, `undeclared` when nothing claims it. */
  effectSource: ActionEffectSource | "undeclared";
  destructive: boolean;
  /** `declared` on the spec, `lexicon` from the leading verb, `none` for a read (never destructive). */
  destructiveSource: "declared" | "lexicon" | "none";
}

// ── Context measurements ──────────────────────────────────────────

export type ContextScopeBucket = "1-3" | "4-8" | "9+";

export interface ContextMeasurementTask {
  /** The task's stable id, the same across runs so they can be compared. */
  id: string;
  /** Every token the task spent: seed, discovery and work, input and output. */
  totalTokens: number;
  /** Model requests the task made. */
  modelCalls: number;
  /** Of the tool calls, those that only discovered the surface (search, describe, list). */
  discoveryCalls: number;
  passed: boolean;
}

export interface ContextMeasurementRun {
  model: { vendor: string; model: string };
  /** How many categories the run's context was scoped to, bucketed by scopeBucket. */
  scope: ContextScopeBucket;
  strategy: ContextStrategy;
  /** The projection's seedTokens for that strategy and scope. */
  seedTokens: number;
  tasks: ContextMeasurementTask[];
}

/**
 * A context strategy measurement: which strategy costs least, per model vendor
 * and per number of categories a task needs, as parseContextMeasurement reads
 * it and schemas/context-measurement.schema.json describes it.
 */
export interface ContextMeasurement {
  version: 1;
  /** ISO 8601 date-time the measurement was taken. */
  measuredAt: string;
  /** The ue-mcp version measured. */
  ueMcpVersion: string;
  client?: { name: string; version: string };
  runs: ContextMeasurementRun[];
}

export type ContextMeasurementParse = { ok: true; value: ContextMeasurement } | { ok: false; errors: string[] };

// ── Flow step approval ────────────────────────────────────────────

export interface FlowStepApprovalRequest {
  runId: string;
  flowName: string;
  /** The task the step runs, as the registry names it: `category.action` for an action. */
  task: string;
  /** flowkit's execution phase: "task" for a step, or on_success / on_failure work. */
  phase: string;
  /** The task's options after the run resolved them. */
  options: Record<string, unknown>;
  /** The caller's token from the run request, so the approver knows whose run it is. */
  token: string;
}

export type FlowStepDecision = { allow: true } | { allow: false; reason?: string };

export type FlowStepApprover = (request: FlowStepApprovalRequest) => Promise<FlowStepDecision>;

// ── The extension contract ────────────────────────────────────────

/** The `ueMcpExtension` field of an extension's package.json. */
export interface ExtensionManifest {
  name?: string;
  api?: { min?: number; max?: number };
  /** The oldest ue-mcp the extension runs on. */
  ueMcp?: string;
  /**
   * The UI bundles it works with. The daemon serves the bundle named for the
   * extension at its own version, or else the newest from `min` up to that
   * version; never one newer. Without `min`, any older bundle qualifies.
   * `root: false` keeps its bundle off /ui/, served only at /ui/ext/<name>/.
   */
  ui?: { min?: string; root?: boolean };
  /** What the extension may reach beyond the API every extension gets. See EXTENSION_PERMISSIONS. */
  permissions?: ExtensionPermission[];
}

/**
 * The permissions an extension can declare in `ueMcpExtension.permissions`.
 * "registry.account": `registryAccount()` hands it the signed-in ue-mcp account
 * and its bearer token.
 */
export const EXTENSION_PERMISSIONS = ["registry.account"] as const;

export type ExtensionPermission = (typeof EXTENSION_PERMISSIONS)[number];

/**
 * Until this ue-mcp version an extension whose manifest has no `permissions`
 * field at all keeps `registryAccount()`, and the daemon logs once that it must
 * declare "registry.account". From it, only a declared permission grants it.
 * An extension that declares `permissions` without "registry.account" never gets it.
 */
export const REGISTRY_ACCOUNT_PERMISSION_REQUIRED_FROM = "1.4.20";

export interface ExtensionRequest {
  method: string;
  /** Path below /v1/ext/<name>, e.g. "/sessions/42". */
  path: string;
  params: Record<string, string>;
  query: Record<string, string>;
  headers: Record<string, string | string[] | undefined>;
  /** Parsed JSON body, or undefined when there was none. */
  body: unknown;
}

export interface ExtensionResponse {
  status?: number;
  headers?: Record<string, string>;
  /** An object is sent as JSON. */
  body?: unknown;
}

export type ExtensionHandler = (req: ExtensionRequest) => ExtensionResponse | undefined | Promise<ExtensionResponse | undefined>;

/** Options for one callAction. */
export interface CallActionOptions {
  /**
   * Answers a blocking dialog this call meets, in place of an MCP elicitation
   * form, with the same contract: the form (message, requestedSchema) plus the
   * dialog itself, answered accept with content.button, decline or cancel, or
   * delegate to hand that one dialog to the agent as auto mode would. An answer
   * sooner than dialog.minHumanMs is not taken as a person's.
   */
  dialogPrompt?: DialogPromptFn;
  /** How long the call waits on an open prompt before returning the dialog refusal, in ms. Default 60000. */
  dialogWaitMs?: number;
  /**
   * Answers any other form this call raises, such as feedback approval, in
   * place of an MCP elicitation form and with its contract: accept with
   * content, decline or cancel.
   */
  elicit?: (params: ElicitParams) => Promise<ElicitResult>;
  /**
   * The access mode this call runs under (feature "callAction.access").
   * `read-only` refuses an action whose declared effect is `mutate` or
   * `unknown`, and every state-changing bridge call it would make. Default
   * read-write.
   */
  access?: "read-write" | "read-only";
  /**
   * Cancels the call (feature "callAction.signal"). Already aborted, the call
   * is not dispatched and nothing reaches the editor. Aborted while the call
   * runs, it resolves at once with a CANCELLED error, the editor is asked to
   * cancel the request in flight, every later bridge call the action would
   * make is refused, and the late result is dropped. An agent passes its
   * turn's signal, so an interrupted turn dispatches nothing more.
   */
  signal?: AbortSignal;
  /**
   * The agent session the call belongs to (feature "callAction.session"): its
   * asset leases and its activity are that session's, so another session's
   * request to stop the editor waits on its calls. Absent is the daemon itself.
   */
  session?: { id: string; label?: string; parent?: string };
}

/**
 * Options for one api.context.call: the projection the call runs against, plus
 * the callAction prompts. `strategy` defaults to the one the server runs;
 * `categories` to every category.
 */
export interface ContextCallOptions extends CallActionOptions {
  strategy?: ContextStrategy;
  categories?: string[];
}

/** The text and error flag of an action's result. */
export interface ActionResult {
  isError: boolean;
  content: Array<{ type: string; text?: string }>;
}

/**
 * Objects extensions share in process (feature "extension.services"). One
 * provider holds an id at a time; `provide` throws while another does and
 * returns the revoke. `watch` calls back now if the id is provided, on each
 * provide, and with undefined on each revoke, and returns the unsubscribe.
 * Extensions activate in directory order, so a consumer may load before its
 * provider. A provider's services are revoked when it is disposed, and a
 * disposed extension's watches stop first.
 */
export interface ExtensionServicesApi {
  provide(id: string, impl: unknown): () => void;
  get<T>(id: string): T | undefined;
  watch<T>(id: string, cb: (impl: T | undefined) => void): () => void;
}

export interface DaemonExtensionApi {
  /** EXTENSION_API_VERSION of the daemon that loaded this extension. */
  apiVersion: number;
  /** ue-mcp package version. */
  version: string;
  projectDir: string;
  /** A directory for this extension's own state, per project, outside the project tree. */
  dataDir: string;

  events: {
    publish(type: string, data?: Record<string, unknown>): DaemonEvent;
    subscribe(listener: (e: DaemonEvent) => void): () => void;
    since(lastId: number): { events: DaemonEvent[]; gap: boolean };
  };

  /** Serve `method path` under /v1/ext/<name>. `:param` segments are captured. */
  route(method: "GET" | "POST" | "PUT" | "PATCH" | "DELETE", path: string, handler: ExtensionHandler): void;

  /**
   * Run one category action through the same dispatch as an MCP call. A
   * blocking dialog the call meets is put to `opts.dialogPrompt` (feature
   * "callAction.dialogPrompt"), and any other form to `opts.elicit` (feature
   * "callAction.elicit"), the way an MCP client is shown a form; without
   * them, nobody is asked.
   */
  callAction(category: string, action: string, args?: Record<string, unknown>, opts?: CallActionOptions): Promise<ActionResult>;

  /**
   * What this daemon offers beyond EXTENSION_API_VERSION, for feature detection:
   *   "callAction.dialogPrompt"  callAction takes `opts.dialogPrompt` and `opts.dialogWaitMs`
   *   "callAction.elicit"        callAction takes `opts.elicit` for every other form
   *   "callAction.access"        callAction and context.call take `opts.access`, and `read-only`
   *                              refuses every call that may change state
   *   "callAction.signal"        callAction and context.call take `opts.signal`, which cancels the call
   *   "modes"                  `modes` reads and sets this project's feedback and dialog modes
   *   "modes.playtest"           `modes.get` carries `playtest` and `modes.setPlaytest` sets it
   *   "events.dialog"            dialog.opened, dialog.updated, dialog.answered, dialog.delegated
   *                              and dialog.closed on the event stream (see DialogEventData)
   *   "models.provider"          `models.register` supplies the model flow agent steps run on
   *   "events.script"            script.run_started, script.step_started, script.step_acted,
   *                              script.step_passed, script.step_failed, script.verdict_requested,
   *                              script.verdict and script.run_finished on the event stream
   *   "checkpoints"              `checkpoints` drives each session's shadow history, and
   *                              checkpoint.created, checkpoint.restored, checkpoints.toggled and
   *                              checkpoints.cleared are on the event stream (API 3)
   *   "checkpoints.assetDiff"    `checkpoints.changes` gives each package an assetDiff built on
   *                              Unreal's diff code, and `assetDiff`, `thumbnail` and
   *                              `openNativeDiff` are on `checkpoints`
   *   "skills"                   `skills.list` lists ue-mcp's packaged skills and the project's own
   *   "context.projection"       `context.project` gives the instructions and tools a strategy and
   *                              category scope would advertise, and `context.call` runs one call
   *                              against that surface
   *   "actions.effects"          `actions.effects` lists each action's effect and whether it is destructive
   *   "extension.versions"       `versions` names this extension's loaded, recorded and installed versions
   *   "ui.scopedKeys"            a key from `ui.trustKey` verifies this extension's own bundles and
   *                              bundles no loaded extension names, never another extension's
   *   "ui.extensions"            `ui.own` gives this extension's own bundle, served at /ui/ext/<name>/
   *   "extension.services"       `services` lets extensions provide and use each other's objects in process
   *   "flows.stepApproval"       `flows.approveSteps` asks this extension before each task of a run that
   *                              names it (POST /v1/flows/<name>/run with `stepApproval`)
   *   "dialogs.native"           native Win32 dialogs the editor process shows are dialog.* events with
   *                              `native` set, block calls like any modal, are put to `dialogPrompt`, and
   *                              `nativeDialogs` lists and presses them
   *   "permissions"              `registryAccount` answers only an extension declaring "registry.account"
   *                              in `ueMcpExtension.permissions` (EXTENSION_PERMISSIONS)
   *   "events.pie"               pie.started, pie.ended, pie.paused and pie.resumed on the event stream
   *                              whenever a Play session changes, however it was started (see PieEventData)
   * EXTENSION_FEATURE_MEMBERS names the member each one adds to or changes.
   * Absent on a daemon older than the field.
   */
  features?: readonly string[];

  /**
   * Native Win32 dialogs the editors of this project show (feature
   * "dialogs.native"), read from outside the editor process: a GPU device
   * loss, an assert or ensure box, a startup prompt. `press` is a person's
   * answer, made whatever the dialog mode: it presses the named button of the
   * window with that handle, after checking the window still belongs to an
   * editor of this project, and reports dialog.answered with via "person".
   * Give an agent's answer through callAction("editor", "respond_to_dialog"),
   * which the dialog mode governs.
   */
  nativeDialogs?: {
    list(): NativeDialogView[];
    press(hwnd: string, button: string, opts?: { pid?: number }): Promise<NativeDialogPressResult>;
  };

  /**
   * Approve each flow step as it runs (feature "flows.stepApproval").
   * `approveSteps` registers `approver` under this extension's name, replacing
   * an earlier one, and returns the function that removes it. A run started
   * with `stepApproval: { approver: <this extension's name>, token }` asks it
   * before every task it creates, rollbacks excepted, with the task's
   * resolved options and the run's token; a denial fails that step with the
   * reason and the task never runs. A run naming an approver that is not
   * registered fails its first step instead of running unapproved.
   */
  flows?: {
    approveSteps(approver: FlowStepApprover): () => void;
  };

  /** Objects shared with other extensions in process (feature "extension.services"). */
  services?: ExtensionServicesApi;

  /**
   * The model behind flow agent steps (`class_path: agent` and `agent_prompt`, and the
   * built-in playtest flow), feature "models.provider", API 2. `register` makes `provider` the
   * one those steps call; the newest registration still live wins, and the function it returns
   * removes it. Core ships no provider, so without a registration an agent step fails with
   * "No model is configured for flow agent steps." and how to install one.
   */
  models?: {
    register(provider: ModelProvider): () => void;
  };

  /**
   * Checkpoints per editor session (#1482), feature "checkpoints", API 3: the
   * same operations and payloads as the /v1/checkpoints routes. `editor` names
   * a session; omitted is the active one. `setEnabled` is the per-session
   * override, kept in memory for the daemon's lifetime; null follows
   * ue-mcp.yml again.
   */
  checkpoints?: {
    state(opts?: { editor?: string; limit?: number }): Promise<CheckpointState>;
    setEnabled(enabled: boolean | null, opts?: { editor?: string }): Promise<CheckpointState>;
    create(label?: string, opts?: { editor?: string }): Promise<CheckpointResult>;
    changes(from: string, to?: string, opts?: { editor?: string; assetDiffs?: boolean }): Promise<ChangesResult>;
    /** One package's asset diff, for a path `changes` deferred (feature "checkpoints.assetDiff"). */
    assetDiff?(from: string, to: string | undefined, path: string, opts?: { editor?: string }): Promise<AssetDiff>;
    /** The PNG an asset diff's thumbnail id names, or null (feature "checkpoints.assetDiff"). */
    thumbnail?(id: string, opts?: { editor?: string }): { file: string; type: string } | null;
    /** Unreal's own diff of a package, brought to the front (feature "checkpoints.assetDiff"). */
    openNativeDiff?(path: string, from: string, to?: string, opts?: { editor?: string }): Promise<NativeDiffResult>;
    restore(checkpoint: string, opts?: { paths?: string[]; force?: boolean; editor?: string }): Promise<RestoreResult>;
    clear(opts?: { editor?: string }): Promise<{ editor: string; cleared: true }>;
  };

  /**
   * Every skill a client of this project can load (feature "skills"), the
   * same list as GET /v1/skills: ue-mcp's packaged skills and every skill
   * directory under the project's .claude/skills and .agents/skills, one
   * entry per id, with owner and location. A project copy hides the packaged
   * one of the same id and names it in `shadows`.
   */
  skills?: {
    list(): DaemonSkill[];
  };

  /**
   * The surface a client would be handed under a context strategy and a set
   * of categories (feature "context.projection"), the same answer as GET
   * /v1/context/projection. `project` throws on an unknown strategy or
   * category. `call` runs one call to a projected tool (a category, lean's
   * catalog or a category's describe, micro's tools gateway, or flow) exactly
   * as the MCP server would, through the dispatch callAction uses; a tool or a
   * gateway call outside the scope is refused with an error result.
   */
  context?: {
    project(opts: ContextProjectionOptions): ContextProjection;
    call(tool: string, input: Record<string, unknown>, opts?: ContextCallOptions): Promise<ActionResult>;
  };

  /**
   * Every enabled action's effect and destructive flag (feature
   * "actions.effects"), the same list as GET /v1/actions. The effect is read
   * from the graph the server dispatches, plugin actions included.
   * `destructive` is what the action declares, else (for mutate and unknown)
   * whether its name leads with a destructive verb; the verb list is a
   * fallback. `categories` narrows the list; an unknown name throws.
   */
  actions?: {
    effects(categories?: string[]): ActionEffectInfo[];
  };

  /**
   * This extension's versions (feature "extension.versions", #1619): the one
   * loaded, the one this project records, and every one installed on the
   * machine, oldest first. Versions install side by side and each project
   * loads its recorded one, so another project can install a newer one at any
   * time; `ue-mcp extension use <name> <version> <project>` moves a project.
   */
  versions?(): { loaded: string; recorded: string | null; installed: string[] };

  editors(): EditorStatus[];
  /**
   * Start, stop, restart or rebuild the editor, with the cause announced first.
   * A stop, restart or rebuild other sessions have not agreed to runs no step
   * and answers `{ op, steps: [{ step: "consent", ok: false, reason }], request }`
   * (feature "editor.consent").
   */
  editorOperation(op: "start" | "stop" | "restart" | "rebuild", opts?: EditorOperationOptions): Promise<Record<string, unknown>>;

  /** Report whether this extension has work in progress; the daemon does not idle-exit while it does. */
  setBusy(busy: () => boolean): void;

  /**
   * The Playtest Hub's injection point (feature "hub"). The hub itself runs in
   * the daemon; a client with concepts ue-mcp has none of, such as chat
   * sessions and run cards, supplies them here: its own way of starting and
   * following runs, a fix loop, a flow that writes playtests, the experiment
   * and Manual/Automatic settings, and the account a sign-off is from. Every
   * member is optional and has a default. Returns the function that takes the
   * client's options back out, which activate's dispose should call.
   */
  hub?: {
    configure(options: import("./hub-ports.js").HubClientOptions): () => void;
  };

  ui: {
    /**
     * Trust an Ed25519 public key (PEM) for UI bundles. A bundle named for a
     * loaded extension verifies only against that extension's keys (feature "ui.scopedKeys").
     */
    trustKey(pem: string): void;
    /** Verify a bundle directory and install it. Throws when it does not verify. */
    install(dir: string): UiManifest;
    /**
     * The bundle being served, or null. A bundle named for this extension is
     * served only at its version or older (`ueMcpExtension.ui`).
     */
    active(): UiManifest | null;
    /**
     * This extension's own bundle, newest in its version window, which the
     * daemon serves at /ui/ext/<name>/ (feature "ui.extensions"), or null.
     */
    own?(): UiManifest | null;
  };

  /**
   * The ue-mcp account this machine signed in with (`ue-mcp login`), or null.
   * The token authenticates the user to ue-mcp services; never log or display it.
   * An extension whose manifest declares `ueMcpExtension.permissions` gets it
   * only with "registry.account" in the list; one declaring none still gets it
   * until REGISTRY_ACCOUNT_PERMISSION_REQUIRED_FROM, and the daemon logs once
   * which field to add (feature "permissions").
   */
  registryAccount(): Promise<{ login: string; token: string; registry: string } | null>;

  /**
   * How feedback and editor dialogs are handled for this project, the way
   * `ue-mcp feedback mode` and `ue-mcp dialog mode` set them with --editor
   * (feature "modes"). `get` gives the effective mode and whether this project
   * sets it; `set` with undefined goes back to the per-user preference.
   */
  modes?: {
    get(): {
      feedback: { mode: "interactive" | "auto-approve" | "defer"; project: boolean };
      dialog: { mode: "interactive" | "auto" | "defer" | null; project: boolean };
      /**
       * Who starts playtest runs (feature "modes.playtest"), as `ue-mcp playtest
       * mode` sets it. null is unset: each client decides. Under manual, an MCP
       * session's pie(script_run) and pie(script_replay) are refused; calls made
       * through callAction are not, so an extension applies its own rule.
       */
      playtest: { mode: "manual" | "automatic" | null; project: boolean };
    };
    setFeedback(mode: "interactive" | "auto-approve" | "defer" | undefined): void;
    setDialog(mode: "interactive" | "auto" | "defer" | undefined): void;
    /** Set this project's playtest mode; undefined goes back to the per-user preference (feature "modes.playtest"). */
    setPlaytest(mode: "manual" | "automatic" | undefined): void;
  };

  log: {
    info(message: string): void;
    warn(message: string, detail?: unknown): void;
  };
}

export interface DaemonExtension {
  activate(api: DaemonExtensionApi): void | { dispose?: () => void | Promise<void> } | Promise<void | { dispose?: () => void | Promise<void> }>;
}
