/**
 * Everything one server process owns once, whatever number of MCP clients
 * talk to it: the editor sessions and their bridges, each session's surface,
 * guards and dialog watcher, the flow tool, and the per-call editor targeting
 * baked into the tool graph.
 *
 * In stdio mode one MCP server sits on top. In daemon mode every connected
 * client gets its own MCP server over the same runtime, so one bridge per
 * editor serves them all.
 */
import { SessionRegistry, type EditorSession } from "../sessions/session.js";
import { ueMcpConfigRejections, describeConfigRejections, type ProjectContext } from "../config/project.js";
import { attach, attachSummary } from "../editor/deployer.js";
import { resolveContextStrategy } from "../surface/context/lean-context.js";
import {
  injectEditorTarget,
  removeEditorTarget,
  injectMigrateTarget,
  removeMigrateTarget,
} from "../surface/target-params.js";
import type { ToolContext, ToolDef } from "../core/types.js";
import { dispatchCategoryCall, type DispatchDeps, type ToolResult } from "../dispatch/server-dispatch.js";
import { DialogGuard, guardFor, sessionGuardDeps } from "../editor/dialog-guard.js";
import { info, warn, error } from "../core/log.js";
import { GuardRegistry } from "../flow/guard.js";
import { loadFlowConfig } from "../flow/loader.js";
import { createFlowTool } from "../flow/flow-tool.js";
import { resolveLockingConfig, type LockingConfig } from "../dispatch/locking.js";
import { collapsingEnvWarnings } from "../config/session-env.js";
import { checkPluginFreshness } from "../editor/bridge-freshness.js";
import { unionSurface } from "../sessions/session-surface.js";
import { registeredUnion } from "../surface/registered-surface.js";
import { packageVersion } from "../core/package-root.js";
import { SessionLoads, type SessionLoad } from "../sessions/session-load.js";
import type { ContextStrategy } from "../surface/context/lean-context.js";

export interface Runtime {
  sessions: SessionRegistry;
  loads: SessionLoads;
  primary: EditorSession;
  primaryLoad: SessionLoad;
  /** The first session's project, which decides the process-level choices. */
  project: ProjectContext;
  contextStrategy: ContextStrategy;
  /** The primary session's context. Its `elicit` is the most recent caller's. */
  baseCtx: ToolContext;
  lockingCfg: LockingConfig;
  dialogGuardFor(session: EditorSession, canElicit?: boolean): DialogGuard;
  /** Category tools as advertised, before the flow tool. */
  advertisedTools: ToolDef[];
  flowTool: ToolDef;
  /** Every tool per-call targeting is injected into: the category tools and the flow tool. */
  targetable: ToolDef[];
  /** What every editor's running plugin registered, or null while any has published nothing. */
  advertisedHave(): ReadonlySet<string> | null;
  /**
   * Called after the tool graph changed in place (targeting) or what the
   * plugins registered changed. Returns an unsubscribe.
   */
  onSurfaceChanged(listener: () => void): () => void;
  /** Connect every session's bridge and keep reconnecting in the background. */
  connectBridges(): Promise<void>;
  /** One startup line naming the tool, task and plugin counts. */
  logSummary(): void;
  /**
   * Run one category action in process, through the same dispatch as an MCP
   * call: routing, guards, locking. There is no person on it, so it never elicits.
   */
  callAction(category: string, action: string, args?: Record<string, unknown>): Promise<ToolResult>;
}

/**
 * Register one session per project argument, reporting each by name. A
 * positional that fails to load is named rather than silently dropped, and
 * with none left the project-less default session attaches to 9877.
 */
function registerSessions(sessions: SessionRegistry, projectArgs: string[]): void {
  for (const arg of projectArgs) {
    try {
      const session = sessions.register({ projectPath: arg });
      info(
        "server",
        `Project loaded: ${session.project.projectName} (engine ${session.project.engineAssociation ?? "unknown"})` +
          (projectArgs.length > 1 ? ` as editor '${session.name}' on port ${session.bridge.port}` : ""),
      );

      // Non-destructive attach; deployment is reserved for `ue-mcp init` / `ue-mcp deploy`.
      const result = attach(session.project);
      info("deploy", attachSummary(result));

      // #785: a compiled plugin older than its source otherwise only shows up
      // later as "Unknown method", which reads as "not implemented yet".
      const freshness = checkPluginFreshness(session.project.projectPath);
      if (freshness.stale && freshness.message) {
        warn("deploy", freshness.message);
      }

      // D3: a malformed `ue-mcp:` key is dropped on its own, and named here.
      for (const line of describeConfigRejections(ueMcpConfigRejections(session.project.projectDir))) {
        warn("config", line);
      }
    } catch (e) {
      error("server", `Failed to initialize project '${arg}'`, e);
    }
  }

  // Which environment variables decide for every editor at once. Silent at one.
  for (const line of collapsingEnvWarnings(sessions.list().map((s) => s.name))) {
    warn("env", line);
  }

  if (sessions.size === 0) sessions.register({});
}

export async function createRuntime(projectArgs: string[]): Promise<Runtime> {
  // Every session wraps its own bridge in the guard pipeline, so it exists
  // first. The registry owns the sessions; nothing here keeps a bridge of its own.
  const sessions = new SessionRegistry(new GuardRegistry());

  // #817: every positional argument is a project with its own session.
  registerSessions(sessions, projectArgs);

  // Process-level choices (context strategy, HTTP surface, flow config source)
  // read the first session's project.
  const primary = sessions.active;
  const project = primary.project;
  // One advertised shape (full | lean | micro) for the whole process.
  const contextStrategy = resolveContextStrategy(project.config.context?.strategy);

  // Plugins and native tools are project-scoped, so each session gets a graph
  // cloned from the pristine declaration and loaded from its own project.
  const loads = new SessionLoads(sessions, primary, contextStrategy, packageVersion());
  const baseCtx = loads.contextFor(primary);

  /**
   * The dialog guard for one editor, created on first use and kept. Watching
   * starts with it, so a modal raised while the session sits idle is known
   * before anything is called.
   */
  function dialogGuardFor(forSession: EditorSession, canElicit = false): DialogGuard {
    const guard = guardFor(forSession, sessionGuardDeps(forSession, canElicit, () => baseCtx.elicit));
    guard.startWatching();
    return guard;
  }

  for (const session of sessions.list()) {
    // BEFORE the surface build, so the first call this server makes to an
    // editor already has a guard behind it.
    dialogGuardFor(session);
    await loads.buildSurface(session);
  }

  // Any other session asking for a different strategy is named, not applied.
  const dissenting = sessions.list()
    .filter((s) => s !== primary)
    .filter((s) => resolveContextStrategy(s.project.config.context?.strategy) !== contextStrategy)
    .map((s) => s.name);
  if (dissenting.length > 0) {
    warn(
      "context",
      `Context strategy '${contextStrategy}' comes from '${primary.name}' and applies to the whole server; ` +
        `${dissenting.join(", ")} ask for a different one and it is not applied.`,
    );
  }

  const primaryLoad = loads.get(primary)!;

  // At one editor, that editor's list. Beyond one, the union, so an action only
  // one project has is still addressable; dispatch to a session that lacks it
  // is refused by name.
  const advertisedTools: ToolDef[] = sessions.size > 1
    ? unionSurface(loads.all().map((l) => ({ ...l.surface, tools: l.advertisedTools }))).tools
    : primaryLoad.advertisedTools;
  if (contextStrategy !== "full") {
    info("context", `Context strategy: ${contextStrategy}`);
  }

  // Per-asset locking for concurrent agents. Opt-in; off, it is a passthrough.
  const lockingCfg = resolveLockingConfig(project.config.locking);
  if (lockingCfg.enabled) {
    info("locking", `Per-asset locking enabled (TTL ${lockingCfg.ttlSeconds}s)`);
  }

  // One task registry and guard pipeline per session, built from its own graph.
  await loads.finishStartup();
  for (const s of sessions.list()) dialogGuardFor(s);

  // project(add_editor) awaits this, so an editor is never addressable before
  // its own surface, guard and watcher exist.
  sessions.prepareSession = async (session) => {
    await loads.ensure(session);
    dialogGuardFor(session);
  };
  for (const session of sessions.list()) {
    if (session.guards.size === 0) continue;
    const label = sessions.size > 1 ? `editor '${session.name}': ` : "";
    info(
      "guard",
      `${label}${session.guards.size} bridge guard(s) active: ${session.guards.list().map((g) => g.name).join(", ")}`,
    );
  }

  // ── Flow tool: ue-mcp.yml is reloaded on every call ─────────────
  const initialLoad = loadFlowConfig(primaryLoad.surface.tools, primaryLoad.configDir, {
    tasks: primaryLoad.pluginLoad.taskDefs,
    flows: primaryLoad.pluginLoad.flowDefs,
  });
  info("flow", `ue-mcp.yml loaded - ${Object.keys(initialLoad.config.flows).length} flow(s), ${Object.keys(initialLoad.config.tasks).length} custom task(s)`);

  // Resolved from the addressed editor: a flow declared in one project's
  // ue-mcp.yml runs through that project's registry, never the first one's (D1).
  const flowTool = createFlowTool(
    (target) => loads.loadFor(target).registry ?? primaryLoad.registry!,
    (target) => loads.reloadConfigFor(target),
  );
  const targetable: ToolDef[] = [...advertisedTools, flowTool];

  // ── Surface changes ──────────────────────────────────────────────
  const listeners = new Set<() => void>();
  const notify = (): void => {
    for (const l of listeners) l();
  };

  // `editor` exists only while this process drives more than one editor, so a
  // single-editor schema is byte-for-byte what it was before sessions existed.
  let targetingSignature = "";
  const syncEditorTargeting = (): boolean => {
    const names = sessions.list().map((s) => s.name);
    const signature = names.length > 1 ? names.join(", ") : "";
    if (signature === targetingSignature) return false;
    targetingSignature = signature;
    for (const tool of targetable) {
      if (signature) {
        const outcome = injectEditorTarget(tool, names);
        if (!outcome.injected && outcome.reason) warn("targeting", outcome.reason);
        // asset(migrate) also takes a DESTINATION editor (6.5).
        const destination = injectMigrateTarget(tool, names);
        if (!destination.injected && destination.reason) warn("targeting", destination.reason);
      } else {
        removeEditorTarget(tool);
        removeMigrateTarget(tool);
      }
    }
    return true;
  };

  // What the running plugins registered (6.4).
  const advertisedHave = () => registeredUnion(sessions.list().map((s) => s.bridge));
  const watched = new WeakSet<EditorSession>();
  const watchParity = (session: EditorSession): void => {
    if (watched.has(session)) return;
    watched.add(session);
    session.bridge.onRegisteredActionsChanged(() => notify());
  };

  sessions.onCountChanged = () => {
    for (const s of sessions.list()) watchParity(s);
    syncEditorTargeting();
    notify();
  };
  syncEditorTargeting();
  for (const s of sessions.list()) watchParity(s);

  return {
    sessions,
    loads,
    primary,
    primaryLoad,
    project,
    contextStrategy,
    baseCtx,
    lockingCfg,
    dialogGuardFor,
    advertisedTools,
    flowTool,
    targetable,
    advertisedHave,
    onSurfaceChanged(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    async connectBridges() {
      // One socket per session. A session whose editor is down stays registered.
      for (const session of sessions.list()) {
        const label = sessions.size > 1 ? `editor '${session.name}'` : "editor bridge";
        try {
          await session.bridge.connect();
          info("bridge", `${label} connected - live mode active`);
        } catch (e) {
          info("bridge", `${label} not reachable - will retry in background`, e);
        }
        session.bridge.startReconnecting();
      }
    },
    callAction(category, action, args = {}) {
      // The full graph, not the advertised one: in micro mode only the gateway is advertised.
      const tool = primaryLoad.surface.tools.find((t) => t.name === category);
      if (!tool) return Promise.reject(new Error(`unknown category '${category}'`));
      const deps: DispatchDeps = {
        sessions,
        loads,
        lockingCfg,
        dialogGuardFor,
        elicit: () => undefined,
        client: () => undefined,
      };
      return dispatchCategoryCall(deps, tool, false, { ...args, action }, {});
    },
    logSummary() {
      const disabled = primaryLoad.surface.disabled;
      if (disabled.size > 0) {
        info("server", `Disabled categories: ${[...disabled].join(", ")}`);
      }
      const pluginRecords = primaryLoad.surface.pluginRecords;
      const activePluginCount = pluginRecords.filter((r) => r.status === "active").length;
      const pluginNote = pluginRecords.length > 0
        ? `, ${activePluginCount}/${pluginRecords.length} plugin(s)`
        : "";
      const taskCount = primaryLoad.registry!.listRegistered().length;
      info("server", `Registered ${advertisedTools.length + 1} tools, ${taskCount} tasks (flow engine)${pluginNote}`);
    },
  };
}
