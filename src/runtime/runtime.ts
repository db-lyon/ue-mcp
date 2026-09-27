/**
 * The composition root: the one place a server, a CLI command or a repo
 * script builds the objects it runs on.
 *
 * It owns the editors (SessionRegistry), each editor's load (SessionLoads),
 * the dialog guards, and the settings that apply to the whole process rather
 * than to one project: the context strategy, the HTTP surface and asset
 * locking. Those are read from explicit settings when given, else from the
 * primary project's `ue-mcp:` block, which is where every existing config
 * keeps them.
 *
 * Heavy modules (the tool graph, the loader) are imported when a server or a
 * flow host is built, so a CLI command that only opens a project stays light.
 */
import { ProjectContext, type UeMcpConfig } from "../config/project.js";
import type { ElicitFn, ToolContext, ToolDef } from "../core/types.js";
import type { EditorSession, SessionRegistry } from "../sessions/session.js";
import type { SessionLoads } from "../sessions/session-load.js";
import type { ContextStrategy } from "../surface/context/lean-context.js";
import type { LockingConfig } from "../dispatch/locking.js";
import type { DialogGuard } from "../editor/dialog-guard.js";
import type { FlowConfig } from "../flow/schema.js";
import type { DescribedTaskRegistry } from "../flow/registry.js";
import type { IBridge } from "../bridge/bridge.js";

/** Server-wide settings, one value for the whole process. */
export interface RuntimeSettings {
  contextStrategy: ContextStrategy;
  http: { enabled: boolean; port?: number; host?: string };
  locking: LockingConfig;
}

/** Settings a caller pins explicitly; anything left out falls back to the primary project's block. */
export type RuntimeSettingsInput = Pick<UeMcpConfig, "context" | "http" | "locking">;

export interface RuntimeOptions {
  /** Positional project arguments; each becomes an editor. None means the project-less default. */
  projectArgs: string[];
  packageVersion: string;
  settings?: RuntimeSettingsInput;
}

/** Open one project, the way every CLI command that reads a project's config does. */
export function openProject(uprojectPath: string): ProjectContext {
  const project = new ProjectContext();
  project.setProject(uprojectPath);
  return project;
}

/** What a script needs to run a project's flows without a server around it. */
export interface FlowHost {
  project: ProjectContext;
  tools: ToolDef[];
  registry: DescribedTaskRegistry;
  /** The project's merged flow config, read now. */
  flowConfig(): FlowConfig;
  /** A context bound to `bridge` and this project. */
  contextFor(bridge: IBridge): ToolContext;
}

export class UeMcpRuntime {
  /** The client's elicitation gate, once a transport exists to ask through. */
  elicit?: ElicitFn;

  private constructor(
    readonly sessions: SessionRegistry,
    readonly loads: SessionLoads,
    readonly settings: RuntimeSettings,
    private readonly guards: {
      guardFor: (session: EditorSession, canElicit: boolean, elicit: () => ElicitFn | undefined) => DialogGuard;
    },
  ) {}

  /** The primary editor: the first registered, which a context naming none reads. */
  get primary(): EditorSession {
    return this.sessions.list()[0] ?? this.sessions.active;
  }

  /** Read the process-wide settings: explicit ones first, else the primary project's block. */
  static resolveSettings(
    fallback: UeMcpConfig,
    explicit: RuntimeSettingsInput | undefined,
    resolvers: {
      strategy: (configured?: string) => ContextStrategy;
      locking: (cfg?: UeMcpConfig["locking"]) => LockingConfig;
    },
  ): RuntimeSettings {
    const http = explicit?.http ?? fallback.http;
    return {
      contextStrategy: resolvers.strategy(explicit?.context?.strategy ?? fallback.context?.strategy),
      http: { enabled: http?.enabled === true, port: http?.port, host: http?.host },
      locking: resolvers.locking(explicit?.locking ?? fallback.locking),
    };
  }

  /**
   * Build a server's runtime: register every project, build each editor's
   * surface, registry and guards, and install the builders add_editor and
   * set_project use for editors that arrive later.
   */
  static async start(options: RuntimeOptions): Promise<UeMcpRuntime> {
    const [{ SessionRegistry }, { GuardRegistry }, { SessionLoads }, lean, locking, dialog, log, startup] = await Promise.all([
      import("../sessions/session.js"),
      import("../flow/guard.js"),
      import("../sessions/session-load.js"),
      import("../surface/context/lean-context.js"),
      import("../dispatch/locking.js"),
      import("../editor/dialog-guard.js"),
      import("../core/log.js"),
      import("./startup.js"),
    ]);

    // Every session wraps its own bridge in the guard pipeline, so it exists first.
    const sessions = new SessionRegistry(new GuardRegistry());
    startup.registerSessions(sessions, options.projectArgs);

    const primary = sessions.active;
    const settings = UeMcpRuntime.resolveSettings(primary.project.config, options.settings, {
      strategy: lean.resolveContextStrategy,
      locking: locking.resolveLockingConfig,
    });
    const loads = new SessionLoads(sessions, primary, settings.contextStrategy, options.packageVersion);

    const runtime = new UeMcpRuntime(sessions, loads, settings, {
      guardFor: (session, canElicit, elicit) =>
        dialog.guardFor(session, dialog.sessionGuardDeps(session, canElicit, elicit)),
    });

    for (const session of sessions.list()) {
      // Before the surface build, so the first call this server makes to an
      // editor already has a guard behind it.
      runtime.dialogGuardFor(session);
      await loads.buildSurface(session);
    }

    // Any other editor asking for a different strategy is named, not applied.
    const dissenting = sessions.list()
      .filter((s) => s !== primary)
      .filter((s) => lean.resolveContextStrategy(s.project.config.context?.strategy) !== settings.contextStrategy)
      .map((s) => s.name);
    if (dissenting.length > 0) {
      log.warn(
        "context",
        `Context strategy '${settings.contextStrategy}' comes from '${primary.name}' and applies to the whole server; ` +
          `${dissenting.join(", ")} ask for a different one and it is not applied.`,
      );
    }
    if (settings.locking.enabled) {
      log.info("locking", `Per-asset locking enabled (TTL ${settings.locking.ttlSeconds}s)`);
    }

    // Every route that runs actions opens its run's lock scope through this.
    const opener = locking.lockScopeOpener(settings.locking, () => loads.dispatchUnion.tools);
    runtime.baseContext().openAssetLocks = opener;

    await loads.finishStartup();
    for (const session of sessions.list()) runtime.dialogGuardFor(session);

    // add_editor awaits this, so an editor is never addressable before its own
    // surface, guard and watcher exist.
    sessions.prepareSession = async (session) => {
      await loads.ensure(session);
      runtime.dialogGuardFor(session);
    };
    sessions.reloadSession = async (session) => {
      await loads.rebuild(session);
      runtime.dialogGuardFor(session);
    };
    for (const session of sessions.list()) {
      if (session.guards.size === 0) continue;
      const label = sessions.size > 1 ? `editor '${session.name}': ` : "";
      log.info(
        "guard",
        `${label}${session.guards.size} bridge guard(s) active: ${session.guards.list().map((g) => g.name).join(", ")}`,
      );
    }
    return runtime;
  }

  /**
   * A project's flows, registry and context for a script that drives an
   * editor bridge of its own (scripts/repo-flow.ts).
   */
  static async flowHost(uprojectPath: string, tools?: ToolDef[]): Promise<FlowHost> {
    const [{ ALL_TOOLS }, { loadFlowConfig }, { buildFlowRegistry }] = await Promise.all([
      import("../tools.js"),
      import("../flow/loader.js"),
      import("../flow/registry.js"),
    ]);
    const graph = tools ?? ALL_TOOLS;
    const project = openProject(uprojectPath);
    const registry = buildFlowRegistry(graph);
    return {
      project,
      tools: graph,
      registry,
      flowConfig: () => loadFlowConfig(graph, project.projectDir ?? undefined).config,
      contextFor: (bridge) => ({ bridge, project, getToolGraph: () => graph }) as ToolContext,
    };
  }

  private base?: ToolContext;

  /** The context every route starts from: the primary editor's, with the runtime's services. */
  baseContext(): ToolContext {
    if (!this.base) {
      this.base = this.loads.contextFor(this.primary);
      const runtime = this;
      Object.defineProperty(this.base, "elicit", {
        get: () => runtime.elicit,
        set: (fn: ElicitFn | undefined) => { runtime.elicit = fn; },
        enumerable: true,
        configurable: true,
      });
    }
    return this.base;
  }

  /**
   * The dialog guard for one editor, created on first use and kept. Watching
   * starts with it, so a modal raised while the editor sits idle is known
   * before anything is called.
   */
  dialogGuardFor(session: EditorSession, canElicit = false): DialogGuard {
    const guard = this.guards.guardFor(session, canElicit, () => this.elicit);
    guard.startWatching();
    return guard;
  }

  /** At one editor that editor's list; beyond one the union, so an action one project has is addressable. */
  async advertisedTools(): Promise<ToolDef[]> {
    const { unionSurface } = await import("../sessions/session-surface.js");
    const primaryLoad = this.loads.get(this.primary)!;
    return this.sessions.size > 1
      ? unionSurface(this.loads.all().map((l) => ({ ...l.surface, tools: l.advertisedTools }))).tools
      : primaryLoad.advertisedTools;
  }

  /** The registry a flow call addressed to `target` runs through. */
  flowRegistryFor(target: ToolContext | undefined): DescribedTaskRegistry {
    return this.loads.loadFor(target).registry ?? this.loads.get(this.primary)!.registry!;
  }

  /** The flow config of the editor `target` addresses, reread when a file changed. */
  flowConfigFor(target: ToolContext | undefined): FlowConfig {
    return this.loads.reloadConfigFor(target);
  }

  /** Connect every editor's socket; one whose editor is down keeps retrying in the background. */
  async connectAll(): Promise<void> {
    const { info } = await import("../core/log.js");
    for (const session of this.sessions.list()) {
      const label = this.sessions.size > 1 ? `editor '${session.name}'` : "editor bridge";
      try {
        await session.bridge.connect();
        info("bridge", `${label} connected - live mode active`);
      } catch (e) {
        info("bridge", `${label} not reachable - will retry in background`, e);
      }
      session.bridge.startReconnecting();
    }
  }
}
