/**
 * Everything one editor session needs to serve a call, and the set of them a
 * server holds: each session's own tool graph, plugin load, advertised list,
 * task registry and guard pipeline, built from that session's own project.
 */
import * as path from "node:path";
import type { EditorSession, SessionRegistry } from "./session.js";
import type { ProjectContext } from "../config/project.js";
import type { PluginInfo, ToolContext, ToolDef } from "../core/types.js";
import { McpError, ErrorCode } from "../core/errors.js";
import { info, warn } from "../core/log.js";
import { ALL_TOOLS } from "../tools.js";
import { applyNativeToolsConfig } from "../epic-surface.js";
import { baseGraphFor, unionSurface, type SessionSurface, type UnionSurface } from "./session-surface.js";
import { applyLeanContext, type ContextStrategy } from "../lean-context.js";
import { buildMicroGateway } from "../micro-context.js";
import { buildFlowRegistry } from "../flow/registry.js";
import { assertNoLegacyGuardTasks, buildGuards } from "../flow/guards.js";
import { createLiveGuardSource } from "../flow/guard-config.js";
import { loadFlowConfig } from "../flow/loader.js";
import type { FlowConfig, PluginEntry } from "../flow/schema.js";
import { loadPlugins, type PluginRecord } from "../extensions/loader.js";
import { readPluginsList } from "../extensions/plugins-list.js";

/**
 * Everything one editor session needs to serve a call: its own tool graph,
 * its own plugin load, and (once the context strategy is known) its own
 * advertised list and task registry.
 */
export interface SessionLoad {
  surface: SessionSurface;
  pluginLoad: Awaited<ReturnType<typeof loadPlugins>>;
  configDir: string | undefined;
  advertisedTools: ToolDef[];
  registryTools: ToolDef[];
  registry?: ReturnType<typeof buildFlowRegistry>;
}

/** The `plugins:` entries of a project's ue-mcp.yml. A bad file means none. */
function readPluginEntries(configDir: string | undefined): PluginEntry[] {
  if (!configDir) return [];
  try {
    return readPluginsList(path.join(configDir, "ue-mcp.yml"));
  } catch (e) {
    warn("plugin", `failed to parse plugins: from ue-mcp.yml - ${(e as Error).message}`);
    return [];
  }
}

/**
 * Build one session's surface: clone the pristine graph, load that project's
 * plugins into the clone, then apply that project's `nativeTools:` config.
 * Nothing here touches another session's graph or the declaration they were
 * all cloned from.
 */
export async function buildSessionLoad(
  session: EditorSession,
  packageVersion: string,
  multi: boolean,
): Promise<SessionLoad> {
  const project = session.project;
  const configDir = project.projectDir ?? undefined;
  const label = multi ? `editor '${session.name}': ` : "";

  // Plugins are resolved, validated and injected BEFORE the flow registry is
  // built so plugin tasks register cleanly.
  const pluginLoad = await loadPlugins(
    baseGraphFor(ALL_TOOLS),
    readPluginEntries(configDir),
    configDir,
    packageVersion,
    project.config.pluginConfig,
  );
  const tools = pluginLoad.tools;

  // Unreal's wrapped engine tools are declared in ALL_TOOLS, generated from a
  // recorded catalog with a reviewed effect each. The user's `nativeTools:`
  // config removes what it excludes; epic(call_tool) still reaches all of them.
  const nativeCfg = project.config.nativeTools ?? {};
  const epicSurface = applyNativeToolsConfig(tools, nativeCfg);
  if (epicSurface.removed > 0) {
    const why = nativeCfg.enabled === false
      ? "nativeTools.enabled=false"
      : `nativeTools.exclude=[${(nativeCfg.exclude ?? []).join(", ")}]`;
    info(
      "epic",
      `${label}Wrapped engine tools withheld (${why}): ${epicSurface.removed} actions; `
      + "epic(call_tool) still reaches every one of them.",
    );
  }
  for (const name of epicSurface.droppedCategories) {
    const i = tools.findIndex((t) => t.name === name);
    if (i >= 0) tools.splice(i, 1);
  }

  return {
    surface: {
      session,
      tools,
      disabled: new Set(project.config.disable ?? []),
      pluginRecords: pluginLoad.records,
      knowledgeByCategory: pluginLoad.knowledgeByCategory,
    },
    pluginLoad,
    configDir,
    advertisedTools: tools,
    registryTools: tools,
  };
}

/** The `plugins(list)` view of one load record, resolved against its project. */
export function toPluginInfo(rec: PluginRecord, project: ProjectContext): PluginInfo {
  const uePluginPresent = rec.uePluginDependency
    ? project.isUePluginEnabled(rec.uePluginDependency)
    : undefined;
  return {
    name: rec.name,
    version: rec.version,
    actionPrefix: rec.actionPrefix,
    status: rec.status,
    statusReason: rec.statusReason,
    degraded: rec.degraded,
    minServerVersion: rec.minServerVersion,
    uePluginDependency: rec.uePluginDependency,
    uePluginPresent,
    injected: rec.injected,
    provided: rec.provided,
    knowledge: rec.knowledge,
    flows: rec.flows,
    tasks: rec.tasks,
    pkgDir: rec.pkgDir,
    manifestPath: rec.manifestPath,
  };
}

/**
 * The loads of every session this server drives, each built from its own
 * project and never borrowed from another's (D1). A session without a load
 * is refused by every reader rather than answered from the first project.
 */
export class SessionLoads {
  private readonly perSession = new Map<EditorSession, SessionLoad>();
  private readonly pending = new Map<EditorSession, Promise<SessionLoad>>();
  /** Every session's surface, in the order the loads were built. */
  readonly surfaces: SessionSurface[] = [];
  /**
   * The union of every session's dispatchable graph. The untargeted-call gate
   * and asset locking read action effects from it, and explainMissingAction
   * refuses from it. Rebuilt when a session is loaded at runtime.
   */
  dispatchUnion: UnionSurface = unionSurface([]);

  constructor(
    private readonly sessions: SessionRegistry,
    /** The session a context that names none reads: the first one registered. */
    private readonly primary: EditorSession,
    private readonly contextStrategy: ContextStrategy,
    private readonly packageVersion: string,
  ) {}

  get(session: EditorSession): SessionLoad | undefined {
    return this.perSession.get(session);
  }

  all(): SessionLoad[] {
    return [...this.perSession.values()];
  }

  /** Build a session's surface during startup. Registry and guards come later. */
  async buildSurface(session: EditorSession): Promise<SessionLoad> {
    const load = await buildSessionLoad(session, this.packageVersion, this.sessions.size > 1);
    this.applyContextStrategy(load);
    this.publish(session, load);
    return load;
  }

  /** Give every loaded session its registry and guards, once all surfaces exist. */
  async finishStartup(): Promise<void> {
    for (const load of this.all()) await this.buildRegistryFor(load);
    for (const load of this.all()) await this.buildGuardsFor(load);
  }

  /**
   * The dispatch surface for one session, built on demand (D1).
   *
   * A session registered at runtime by project(add_editor) is loaded from its
   * own project here. A build already running is shared, so two callers
   * racing on a new editor wait on one build. A build that fails leaves no
   * entry, and every reader refuses instead of substituting another project's.
   */
  ensure(session: EditorSession): Promise<SessionLoad> {
    const existing = this.perSession.get(session);
    if (existing) return Promise.resolve(existing);
    const inFlight = this.pending.get(session);
    if (inFlight) return inFlight;

    const build = (async () => {
      const load = await buildSessionLoad(session, this.packageVersion, true);
      this.applyContextStrategy(load);
      await this.buildRegistryFor(load);
      await this.buildGuardsFor(load);
      this.publish(session, load);
      return load;
    })().finally(() => this.pending.delete(session));

    this.pending.set(session, build);
    return build;
  }

  /**
   * Flows declared in the session's own ue-mcp.yml, read fresh each call so
   * edits show without a restart. A session with no load has none.
   */
  getFlows(forSession: EditorSession = this.primary): Array<{ name: string; description?: string }> {
    const load = this.perSession.get(forSession);
    if (!load) return [];
    try {
      const cfg = loadFlowConfig(load.surface.tools, load.configDir, {
        tasks: load.pluginLoad.taskDefs,
        flows: load.pluginLoad.flowDefs,
      }).config;
      return Object.entries(cfg.flows).map(([name, def]) => ({
        name,
        description: (def as { description?: string }).description,
      }));
    } catch {
      return [];
    }
  }

  /** The session's own plugin records. A session with no load has none. */
  getPlugins(forSession: EditorSession = this.primary): PluginInfo[] {
    const load = this.perSession.get(forSession);
    if (!load) return [];
    return load.surface.pluginRecords.map((r) => toPluginInfo(r, forSession.project));
  }

  /**
   * The session's enabled categories. A session whose surface failed to build
   * is refused: answering from another project's graph would name actions its
   * editor does not have.
   */
  getToolGraph(forSession: EditorSession = this.primary): ToolDef[] {
    const load = this.perSession.get(forSession);
    if (!load) {
      throw new McpError(
        ErrorCode.NOT_FOUND,
        `Editor '${forSession.name}' has no tool surface built, so its actions cannot be searched or described. `
        + "Re-register it with project(add_editor); discovery does not fall back to another editor's graph.",
      );
    }
    return load.surface.tools.filter((t) => !load.surface.disabled.has(t.name));
  }

  /** A context bound to one session, with its accessors bound to it too. */
  contextFor(session: EditorSession): ToolContext {
    return {
      bridge: session.guarded,
      project: session.project,
      session,
      sessions: this.sessions,
      getFlows: (forSession) => this.getFlows(forSession ?? session),
      getPlugins: (forSession) => this.getPlugins(forSession ?? session),
      getToolGraph: (forSession) => this.getToolGraph(forSession ?? session),
    };
  }

  /**
   * The load a flow call runs against. A context naming a session with no
   * load of its own is refused, never served the first project's config.
   */
  loadFor(target: ToolContext | undefined): SessionLoad {
    const session = target?.session ?? this.primary;
    const load = this.perSession.get(session);
    if (load) return load;
    throw new McpError(
      ErrorCode.NOT_FOUND,
      `Editor '${session.name}' has no flow config or task registry of its own yet, so nothing can be run in it. ` +
        `Its surface is built when it is registered; re-register it with ` +
        `project(action='add_editor', projectPath='${session.project.projectPath ?? ""}').`,
    );
  }

  /** The addressed session's flow config, reloaded so edits apply without a restart. */
  reloadConfigFor(target?: ToolContext): FlowConfig {
    const load = this.loadFor(target);
    return loadFlowConfig(load.surface.tools, load.configDir, {
      tasks: load.pluginLoad.taskDefs,
      flows: load.pluginLoad.flowDefs,
    }).config;
  }

  /** Each session's registry dispatches only what its own project provides. */
  private applyContextStrategy(load: SessionLoad): void {
    const enabled = load.surface.tools.filter((t) => !load.surface.disabled.has(t.name));
    if (this.contextStrategy === "micro") {
      const gateway = buildMicroGateway(enabled);
      load.advertisedTools = [gateway];
      // Keep every category task in the registry so flows still resolve.
      load.registryTools = [gateway, ...load.surface.tools];
    } else if (this.contextStrategy === "lean") {
      const leaned = applyLeanContext(enabled);
      load.advertisedTools = leaned;
      // Discovery describes callable categories; internal flows retain the full registry.
      load.registryTools = [...leaned, ...load.surface.tools.filter((t) => load.surface.disabled.has(t.name))];
    } else {
      load.advertisedTools = enabled;
      load.registryTools = load.surface.tools;
    }
  }

  /** Record a finished load and make it visible to every reader. */
  private publish(session: EditorSession, load: SessionLoad): void {
    this.perSession.set(session, load);
    // The session's guard pipeline reads action effects from its own graph.
    session.toolGraph = load.registryTools;
    this.surfaces.push(load.surface);
    // The union is what explainMissingAction refuses from, so it has to know
    // about this editor before the first call is routed to it.
    this.dispatchUnion = unionSurface(this.all().map((l) => ({ ...l.surface, tools: l.registryTools })));
  }

  /** One task registry per session, built from the graph that session has (#817). */
  private async buildRegistryFor(load: SessionLoad): Promise<void> {
    const sessionRegistry = buildFlowRegistry(load.registryTools);
    for (const { name, ctor } of load.pluginLoad.taskRegistrations) {
      sessionRegistry.register(name, ctor);
    }
    for (const { classPath, ctor } of load.pluginLoad.classPathRegistrations) {
      sessionRegistry.registerClassPath(classPath, ctor);
    }
    load.registry = sessionRegistry;
  }

  /**
   * Populate one session's guard pipeline from its plugins' manifests and its
   * project's ue-mcp.yml, run against that session's own raw bridge, so a
   * guard declared by one project cannot veto another project's calls.
   */
  private async buildGuardsFor(load: SessionLoad): Promise<void> {
    const session = load.surface.session;
    const deps = {
      registry: load.registry!,
      ctx: this.contextFor(session),
      rawBridge: session.bridge,
    };

    const yamlSource = createLiveGuardSource(load.surface.tools, load.configDir, {
      tasks: load.pluginLoad.taskDefs,
      flows: load.pluginLoad.flowDefs,
    }, (message) => {
      warn("guard", `${session.name}: ${message}`);
    });
    const sources = [
      ...load.pluginLoad.guardsByPlugin.map((g) => ({ label: g.plugin, guards: g.guards })),
      yamlSource,
    ];

    // A task still named like a guard is fatal, whoever declared it: under the
    // declaration model nothing discovers it, so it would sit in the config
    // gating nothing.
    assertNoLegacyGuardTasks(Object.keys(yamlSource.config.tasks ?? {}), yamlSource);
    for (const { plugin, taskNames } of load.pluginLoad.taskNamesByPlugin) {
      assertNoLegacyGuardTasks(taskNames, { label: plugin });
    }

    const built = [];
    for (const source of sources) {
      if (Object.keys(source.guards).length === 0) continue;
      for (const guard of await buildGuards(source.guards, deps, source)) {
        built.push(guard);
      }
    }
    // Publish only after every source has resolved, so retrying a failed build
    // cannot leave a partial or duplicated guard pipeline behind.
    for (const guard of built) session.guards.register(guard);
    if (built.length > 0) {
      info("guard", `${session.name}: ${built.length} guard(s) registered`);
    }
  }
}
