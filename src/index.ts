#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { SessionRegistry, type EditorSession } from "./session.js";
import type { ProjectContext } from "./project.js";
import { attach, attachSummary } from "./deployer.js";
import { SERVER_INSTRUCTIONS, SERVER_INSTRUCTIONS_LEAN, SERVER_INSTRUCTIONS_MICRO, multiEditorInstructions } from "./instructions.js";
import { resolveContextStrategy, applyLeanContext, buildMicroGateway } from "./lean-context.js";
import {
  isDirectiveResponse,
  injectEditorTarget,
  removeEditorTarget,
  stripEditorTarget,
  sessionContext,
  EDITOR_TARGET_PARAM,
  type ToolDef,
  type ToolContext,
  type PluginInfo,
  type ElicitFn,
  type ProgressFn,
  type ProgressUpdate,
} from "./types.js";
import { McpError, ErrorCode } from "./errors.js";
import { info, warn, debug } from "./log.js";
import { startVersionCheck, consumeUpgradeNotice } from "./version-check.js";
import { buildFlowRegistry } from "./flow/registry.js";
import { GuardRegistry } from "./flow/guard.js";
import { discoverTaskGuards } from "./flow/task-guards.js";
import { loadFlowConfig } from "./flow/loader.js";
import { createFlowTool } from "./flow/flow-tool.js";
import { startFlowHttpServer } from "./flow/http-server.js";
import type { FlowContext } from "./flow/context.js";
import type { FlowConfig, PluginEntry } from "./flow/schema.js";
import { loadPlugins, type PluginRecord } from "./plugin/loader.js";
import { withAssetLocks, resolveLockingConfig } from "./locking.js";
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";

import { ALL_TOOLS, setLiveToolGraph } from "./tools.js";
import { enrichToolsWithEpicCatalog, type EpicCatalog } from "./epic-enrich.js";
import { checkPluginFreshness } from "./plugin-freshness.js";
import { saveCatalogCache, loadCatalogCache, loadBakedCatalog } from "./epic-cache.js";
import {
  baseGraphFor,
  unionSurface,
  unionKnowledge,
  explainMissingAction,
  type SessionSurface,
} from "./session-surface.js";

type TextBlock = { type: "text"; text: string };

function withUpgradeNotice(content: TextBlock[]): TextBlock[] {
  const notice = consumeUpgradeNotice();
  return notice ? [{ type: "text" as const, text: notice }, ...content] : content;
}

/**
 * Structured tail for an error a caller has to make a decision about (#799).
 * A bridge timeout is not a failed call: the editor may have finished it. The
 * prose says so, and this block says so in a form a client can branch on
 * without matching strings.
 */
function machineErrorBlock(e: unknown): TextBlock[] {
  if (!(e instanceof McpError) || !e.details) return [];
  return [{
    type: "text" as const,
    text: "MACHINE_ERROR=" + JSON.stringify({ code: e.code, ...e.details }),
  }];
}

/**
 * Turn an MCP request's progress token into a reporter the tools can call.
 *
 * Without this a long tool is a frozen line in the client UI: stderr from an
 * MCP server goes to a log file the user never opens, so the startup progress
 * bar printed there was invisible. `notifications/progress` is the one channel
 * clients render live, and it only exists when the caller supplied a token.
 */
function makeProgressReporter(extra: {
  sendNotification?: (n: never) => Promise<void>;
  _meta?: { progressToken?: string | number };
}): ProgressFn | undefined {
  const token = extra?._meta?.progressToken;
  // The SDK types sendNotification against its own ServerNotification union.
  // notifications/progress is a member of that union, but the params carry a
  // token whose type the compiler cannot narrow from here.
  const send = extra?.sendNotification as unknown as
    | ((n: { method: string; params: Record<string, unknown> }) => Promise<void>)
    | undefined;

  // Progress is opt-in per request: a client that wants it supplies a token.
  // When one never arrives, the server is silent by design and that is
  // indistinguishable from a client that discards what we send, so record
  // which it was. Debug level, so it costs nothing until someone is
  // diagnosing a call that looks frozen.
  debug("progress", token === undefined ? "no progressToken on this request - client did not ask for progress" : `progressToken present (${String(token)})`);

  if (token === undefined || typeof send !== "function") return undefined;

  return (update: ProgressUpdate): void => {
    // Fire and forget: a progress update must never fail the call it describes.
    void Promise.resolve(
      send({
        method: "notifications/progress",
        params: {
          progressToken: token,
          progress: update.progress,
          ...(update.total !== undefined ? { total: update.total } : {}),
          message: update.message,
        },
      }),
    ).catch(() => undefined);
  };
}

async function main() {
  // The guard pipeline is built first because every session wraps its own
  // bridge in it. The registry owns the sessions; nothing below keeps a
  // module-level bridge or project of its own.
  const guardRegistry = new GuardRegistry();
  const sessions = new SessionRegistry(guardRegistry);

  // Kick off the npm registry check in the background; the next tool response
  // injects the notice if a newer version is published.
  const { createRequire } = await import("node:module");
  const require = createRequire(import.meta.url);
  const pkg = require("../package.json") as { version: string };
  startVersionCheck(pkg.version);

  // ── Project init ─────────────────────────────────────────────────
  // Moved ahead of tool registration so plugin resolution can walk the
  // project's node_modules.
  //
  // #817: every positional argument is a project, and each becomes its own
  // session with its own bridge, port and lockfile. One argument behaves
  // exactly as it always has. A positional that fails to load is reported by
  // name rather than silently reducing the set, so a typo does not look like
  // a project that quietly refuses to connect.
  const projectArgs = process.argv.slice(2).filter((a) => !a.startsWith("-"));

  for (const arg of projectArgs) {
    try {
      const session = sessions.register({ projectPath: arg });
      console.error(
        `[ue-mcp] Project loaded: ${session.project.projectName} (engine ${session.project.engineAssociation ?? "unknown"})` +
          (projectArgs.length > 1 ? ` as editor '${session.name}' on port ${session.bridge.port}` : ""),
      );

      // Non-destructive attach - never overwrites local bridge source.
      // Source deployment is reserved for `ue-mcp init` / `ue-mcp deploy`.
      const result = attach(session.project);
      console.error(`[ue-mcp] ${attachSummary(result)}`);

      // #785: say loudly, once at startup, when the compiled plugin is older
      // than its source. Otherwise the only signal is an "Unknown method"
      // error later, which reads as "not implemented yet" and sends people
      // hand-authoring around handlers that already work.
      const freshness = checkPluginFreshness(session.project.projectPath);
      if (freshness.stale && freshness.message) {
        console.error(`[ue-mcp] WARNING: ${freshness.message}`);
      }
    } catch (e) {
      console.error(`[ue-mcp] Failed to initialize project '${arg}': ${e instanceof Error ? e.message : e}`);
    }
  }

  // No project argument, or every argument failed: keep one project-less
  // session on the legacy fixed port. That is the documented "attach to
  // whatever answers 9877" mode, and project(set_project) can bind it later.
  if (sessions.size === 0) sessions.register({});

  // Process-level construction that has one answer per transport (the context
  // strategy, the HTTP surface, the flow config source) reads the first
  // session's project, which for a single-editor server is the only one there
  // has ever been.
  const primary = sessions.active;
  const project = primary.project;
  const bridge = primary.bridge;

  // ── Per-session surfaces ─────────────────────────────────────────
  // Plugins and the Epic catalog are project-scoped: the `plugins:` list lives
  // in each project's ue-mcp.yml and the catalog is whatever that project's
  // editor reports. Both are built per session against a graph cloned from the
  // pristine declaration, so a second editor never inherits the first one's
  // plugins or toolsets (#817). At one editor this is one clone enriched from
  // one project, which is what the server did before.
  const surfaces: SessionSurface[] = [];
  const perSession = new Map<EditorSession, SessionLoad>();
  for (const session of sessions.list()) {
    const load = await buildSessionLoad(session, pkg.version, sessions.size > 1);
    perSession.set(session, load);
    surfaces.push(load.surface);
  }

  const primaryLoad = perSession.get(primary)!;
  const pluginLoad = primaryLoad.pluginLoad;
  const pluginRecords = primaryLoad.surface.pluginRecords;
  const configDir = project.projectDir ?? undefined;
  const activeTools = primaryLoad.surface.tools;

  // ── Context-seeding strategy (full | lean | micro) ───────────────
  // Applied AFTER plugin + Epic enrichment so lean/micro discovery covers the
  // injected actions too, and BEFORE the flow registry + MCP registration so
  // the gateway / catalog / describe surfaces are dispatchable and advertised.
  //   full  - every category tool advertised with its full action catalog
  //   lean  - trimmed category tools + a `catalog` discovery tool (names stay
  //           visible, descriptions/params on demand)
  //   micro - one `tools` gateway (list_categories / describe / call) fronts
  //           everything; smallest possible seed
  //
  // The strategy is process-level: there is one transport, so one advertised
  // shape. It comes from the first session, and any other session asking for
  // a different one is named rather than silently overridden.
  const contextStrategy = resolveContextStrategy(project.config.context?.strategy);
  const dissenting = surfaces
    .filter((s) => s.session !== primary)
    .filter((s) => resolveContextStrategy(s.session.project.config.context?.strategy) !== contextStrategy)
    .map((s) => s.session.name);
  if (dissenting.length > 0) {
    console.error(
      `[ue-mcp] Context strategy '${contextStrategy}' comes from '${primary.name}' and applies to the whole server; ` +
        `${dissenting.join(", ")} ask for a different one and it is not applied.`,
    );
  }
  const disabled = primaryLoad.surface.disabled;

  // Each session gets the strategy applied to its OWN graph, so its registry
  // dispatches only what that project actually provides.
  const loads = [...perSession.values()];
  for (const load of loads) {
    const enabled = load.surface.tools.filter((t) => !load.surface.disabled.has(t.name));
    if (contextStrategy === "micro") {
      const gateway = buildMicroGateway(enabled);
      load.advertisedTools = [gateway];
      // Keep every category task in the registry so flows still resolve.
      load.registryTools = [gateway, ...load.surface.tools];
    } else if (contextStrategy === "lean") {
      const leaned = applyLeanContext(load.surface.tools);
      load.advertisedTools = leaned.filter((t) => !load.surface.disabled.has(t.name));
      load.registryTools = leaned;
    } else {
      load.advertisedTools = enabled;
      load.registryTools = load.surface.tools;
    }
  }

  // What the client is advertised. At one editor this is that editor's list,
  // the same objects it always was. Beyond one it is the union, so an action
  // only one project has is still addressable there, and dispatch to a session
  // that lacks it is refused by name rather than falling through to a bridge
  // that cannot serve it. A category a session disabled stays advertised for
  // the others and is refused at dispatch for that one, naming its config.
  const advertisedTools: ToolDef[] = sessions.size > 1
    ? unionSurface(loads.map((l) => ({ ...l.surface, tools: l.advertisedTools }))).tools
    : primaryLoad.advertisedTools;

  // The union of every session's dispatchable graph. `search_tools`, the
  // execute_python gate and the feedback router all ask "what does this server
  // expose", and with a graph per session that answer no longer lives in the
  // module-level declaration they used to read.
  const dispatchUnion = unionSurface(loads.map((l) => ({ ...l.surface, tools: l.registryTools })));
  setLiveToolGraph(dispatchUnion.tools);
  const registryTools = primaryLoad.registryTools;
  if (contextStrategy !== "full") {
    console.error(`[ue-mcp] Context strategy: ${contextStrategy}`);
  }

  // Lazy flow accessor - reads ue-mcp.yml fresh each call so agents see
  // edits without a server restart. project(get_status) uses this so the
  // first call agents make in any session reveals the registered flows.
  // Reads the addressed session's project, so a flow declared in one project's
  // ue-mcp.yml is not reported as belonging to another's.
  const getFlows = (forSession?: EditorSession): Array<{ name: string; description?: string }> => {
    const load = perSession.get(forSession ?? primary) ?? primaryLoad;
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
  };

  const getPlugins = (forSession?: EditorSession): PluginInfo[] => {
    const target = forSession ?? primary;
    const load = perSession.get(target) ?? primaryLoad;
    return load.surface.pluginRecords.map((r) => toPluginInfo(r, target.project));
  };

  // Elicitation is only meaningful once the client has advertised support
  // during initialize. We lazily probe at call time so the function is bound
  // to whatever the live capabilities are, not a stale snapshot.
  const buildElicit = (mcp: McpServer): ElicitFn | undefined => {
    return async (params) => {
      const caps = mcp.server.getClientCapabilities();
      if (!caps?.elicitation) {
        // Surface a JSON-RPC-style error shape so callers can distinguish
        // "user declined" from "client has no UI for this".
        throw new McpError(
          ErrorCode.UNKNOWN_ACTION,
          "Connected MCP client did not advertise the `elicitation` capability - cannot obtain a deterministic user approval. Upgrade your client (Claude Code >= 2.1.76) or run the action from a client that supports MCP elicitation.",
        );
      }
      const result = await mcp.server.elicitInput(params);
      return result as Awaited<ReturnType<ElicitFn>>;
    };
  };

  // Each session already wraps its own raw bridge in the guard pipeline; the
  // guarded wrapper is what tools and tasks see, the raw bridge stays in scope
  // for connection lifecycle (connect / reconnect / lockfile). The guard
  // registry starts empty (pass-through) and is populated once the task
  // registry exists, below.
  const guardedBridge = primary.guarded;
  const ctx: ToolContext = {
    bridge: guardedBridge,
    project,
    session: primary,
    sessions,
    getFlows,
    getPlugins,
  };

  // Per-asset locking for concurrent agents. Opt-in; when off, withAssetLocks
  // is a passthrough. The registry itself lives in the C++ bridge.
  const lockingCfg = resolveLockingConfig(project.config.locking);
  if (lockingCfg.enabled) {
    console.error(`[ue-mcp] Per-asset locking enabled (TTL ${lockingCfg.ttlSeconds}s)`);
  }

  // ── Flow engine: one task registry per session ──────────────────
  // The registry is the dispatch layer, so it has to be built from the graph
  // the addressed session actually has. Sharing one registry is what made a
  // second editor dispatch the first editor's plugin tasks (#817).
  for (const load of loads) {
    const sessionRegistry = buildFlowRegistry(load.registryTools);
    for (const { name, ctor } of load.pluginLoad.taskRegistrations) {
      sessionRegistry.register(name, ctor);
    }
    for (const { classPath, ctor } of load.pluginLoad.classPathRegistrations) {
      sessionRegistry.registerClassPath(classPath, ctor);
    }
    load.registry = sessionRegistry;
  }
  const registry = primaryLoad.registry!;
  const taskCount = registry.listRegistered().length;

  // Populate the guard pipeline: any plugin-supplied `guard.<name>.<phase>` task
  // becomes a BridgeGuard. Each guard task runs with the RAW bridge in its
  // context so a guard cannot recurse through the pipeline. See flow/task-guards.ts.
  // Guards are discovered per session, from that session's own registry and
  // against that session's own raw bridge, so a guard declared by one project's
  // plugins cannot veto another project's calls.
  for (const load of loads) {
    const guardCtx: ToolContext = {
      bridge: load.surface.session.guarded,
      project: load.surface.session.project,
      session: load.surface.session,
      sessions,
      getFlows: () => getFlows(load.surface.session),
      getPlugins: () => getPlugins(load.surface.session),
    };
    for (const g of discoverTaskGuards(load.registry!, guardCtx, load.surface.session.bridge)) {
      load.surface.session.guards.register(g);
    }
  }
  for (const session of sessions.list()) {
    if (session.guards.size === 0) continue;
    const label = sessions.size > 1 ? `editor '${session.name}': ` : "";
    info(
      "guard",
      `${label}${session.guards.size} bridge guard(s) active: ${session.guards.list().map((g) => g.name).join(", ")}`,
    );
  }

  // ── Plugin knowledge → server instructions ──────────────────────
  // Attach per-category markdown to the AI-facing docs. Sized to the same
  // budget as SERVER_INSTRUCTIONS itself; deeper plugin docs remain
  // readable on demand via the file-reading surface.
  // The union across sessions: instructions are sent once at initialize and
  // cannot be renegotiated, so an editor whose plugins document a category
  // has to have that documented for the whole server or not at all.
  const knowledgeBlock = buildKnowledgeBlock(unionKnowledge(surfaces));
  const baseInstructions = contextStrategy === "micro"
    ? SERVER_INSTRUCTIONS_MICRO
    : contextStrategy === "lean"
      ? SERVER_INSTRUCTIONS_LEAN
      : SERVER_INSTRUCTIONS;
  const withKnowledge = knowledgeBlock
    ? `${baseInstructions}\n\n═══ PLUGIN KNOWLEDGE ═══\n${knowledgeBlock}`
    : baseInstructions;
  // Targeting is documented only when there is something to target, so a
  // single-editor client's initialize payload is unchanged.
  const serverInstructions = sessions.size > 1
    ? `${withKnowledge}\n\n${multiEditorInstructions(sessions.list().map((s) => s.name), sessions.active.name)}`
    : withKnowledge;

  const server = new McpServer({
    name: "ue-mcp",
    version: "0.6.4",
  }, {
    instructions: serverInstructions,
  });

  ctx.elicit = buildElicit(server);

  const tools = advertisedTools;

  // ── Per-call editor targeting (#817) ─────────────────────────────
  // The `editor` parameter exists only while this server drives more than one
  // editor: at one editor the advertised schema is byte-for-byte what it was
  // before sessions existed. Adding or dropping a session at runtime
  // re-advertises, so a client that honours tools/list_changed can target the
  // editor it just registered without a restart.
  const registeredTools = new Map<string, ReturnType<typeof server.tool>>();
  const targetable: ToolDef[] = [...tools];
  let targetingSignature = "";
  const syncEditorTargeting = (): void => {
    const names = sessions.list().map((s) => s.name);
    const signature = names.length > 1 ? names.join(", ") : "";
    if (signature === targetingSignature) return;
    targetingSignature = signature;
    for (const tool of targetable) {
      if (signature) {
        const outcome = injectEditorTarget(tool, names);
        if (!outcome.injected && outcome.reason) console.error(`[ue-mcp] ${outcome.reason}`);
      } else {
        removeEditorTarget(tool);
      }
      const registration = registeredTools.get(tool.name);
      if (registration) registration.update({ paramsSchema: tool.schema });
    }
  };

  /**
   * Route one call to the editor it addressed. `editor` is only a routing
   * instruction on a tool that had the parameter injected; on any other tool
   * it is the tool's own parameter and is left alone.
   */
  const routeCall = (
    tool: ToolDef,
    params: Record<string, unknown>,
  ): { session: EditorSession; params: Record<string, unknown> } => {
    if (!tool.injectedEditorParam) return { session: sessions.active, params };
    // The micro gateway carries every real parameter inside `args`, so a
    // target arrives there rather than at the top level. Read both, and strip
    // both, so the routing instruction cannot reach a bridge call either way.
    const nested = params.args && typeof params.args === "object"
      ? (params.args as Record<string, unknown>)
      : undefined;
    const target = params[EDITOR_TARGET_PARAM] ?? nested?.[EDITOR_TARGET_PARAM];
    const stripped = stripEditorTarget(params);
    if (nested) stripped.args = stripEditorTarget(nested);
    return { session: sessions.resolve(target), params: stripped };
  };

  // ── Register category tools - dispatched through the task registry ──
  for (const tool of tools) {
    const shape: Record<string, z.ZodType> = {};
    for (const [key, schema] of Object.entries(tool.schema)) {
      shape[key] = schema;
    }

    const registration = server.tool(tool.name, tool.description, shape, async (rawParams, extra) => {
      let routed: { session: EditorSession; params: Record<string, unknown> };
      try {
        routed = routeCall(tool, rawParams);
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        return {
          content: withUpgradeNotice([{ type: "text" as const, text: `Error [NOT_FOUND]: ${msg}` }]),
          isError: true,
        };
      }
      const session = routed.session;
      const params = routed.params;
      const action = params.action as string;
      const taskName = `${tool.name}.${action}`;
      const { action: _, ...taskParams } = params;
      const flowCtx: FlowContext = {
        bridge: session.guarded,
        project: session.project,
        session,
        sessions,
        getFlows: () => getFlows(session),
        getPlugins: () => getPlugins(session),
        elicit: ctx.elicit,
        onProgress: makeProgressReporter(extra),
        client: server.server.getClientVersion(),
      };

      // The addressed session's own registry, built from that project's graph.
      // A call for an action the session does not provide is refused here,
      // naming the editors that do, rather than reaching a bridge that would
      // answer "Unknown method" with no way to tell which editor was wrong.
      const sessionRegistry = perSession.get(session)?.registry ?? registry;
      const refusal = sessions.size > 1
        ? explainMissingAction(
            dispatchUnion,
            taskName,
            session.name,
            sessionRegistry.listRegistered().includes(taskName),
          )
        : null;
      if (refusal) {
        return {
          content: withUpgradeNotice([{ type: "text" as const, text: `Error [NOT_FOUND]: ${refusal}` }]),
          isError: true,
        };
      }

      try {
        const task = await sessionRegistry.create(taskName, flowCtx, taskParams);
        // Locks are acquired in the editor the call runs in, so they must be
        // taken on that session's bridge rather than the process default.
        const result = await withAssetLocks(
          session.bridge,
          lockingCfg,
          taskName,
          taskParams,
          () => task.run(),
          session.lockOwnerId,
        );

        if (!result.success) {
          const msg = result.error?.message ?? `Task ${taskName} failed`;
          return {
            content: withUpgradeNotice([
              { type: "text" as const, text: `Error [TASK_FAILED]: ${msg}` },
              ...machineErrorBlock(result.error),
            ]),
            isError: true,
          };
        }

        const stringify = (v: unknown) =>
          typeof v === "string" ? v : JSON.stringify(v, null, 2);

        // Preserve directive responses (execute_python workaround tracking).
        // Emit three blocks: (1) prose directive, (2) machine-readable JSON
        // so clients that strip prose still see the intent, (3) the actual
        // tool result. Block 2 is tagged with MACHINE_DIRECTIVE and a stable
        // JSON envelope.
        if (result.data?.__directive) {
          const blocks: Array<{ type: "text"; text: string }> = [
            { type: "text" as const, text: result.data.directive as string },
          ];
          if (result.data.machine) {
            blocks.push({
              type: "text" as const,
              text: "MACHINE_DIRECTIVE=" + JSON.stringify(result.data.machine),
            });
          }
          blocks.push({ type: "text" as const, text: stringify(result.data.result) });
          return { content: withUpgradeNotice(blocks) };
        }

        return {
          content: withUpgradeNotice([{ type: "text" as const, text: stringify(result.data) }]),
        };
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        const code = e instanceof McpError ? e.code : "UNKNOWN";
        return {
          content: withUpgradeNotice([
            { type: "text" as const, text: `Error [${code}]: ${msg}` },
            ...machineErrorBlock(e),
          ]),
          isError: true,
        };
      }
    });
    registeredTools.set(tool.name, registration);
  }

  // ── Load ue-mcp.yml and register flow tool ──────────────────────
  // Log initial load
  const initialLoad = loadFlowConfig(activeTools, configDir, {
    tasks: pluginLoad.taskDefs,
    flows: pluginLoad.flowDefs,
  });
  console.error(`[ue-mcp] ue-mcp.yml loaded - ${Object.keys(initialLoad.config.flows).length} flow(s), ${Object.keys(initialLoad.config.tasks).length} custom task(s)`);

  // Config is reloaded on every flow call - edit ue-mcp.yml without restarting.
  // Resolved from the addressed editor: a flow declared in one project's
  // ue-mcp.yml belongs to that project, and its steps have to dispatch through
  // that project's registry or a step naming an action only that project has
  // would fail as unknown.
  const loadFor = (target: ToolContext | undefined): SessionLoad =>
    (target?.session ? perSession.get(target.session) : undefined) ?? primaryLoad;
  const reloadConfigFor = (target?: ToolContext): FlowConfig => {
    const load = loadFor(target);
    return loadFlowConfig(load.surface.tools, load.configDir, {
      tasks: load.pluginLoad.taskDefs,
      flows: load.pluginLoad.flowDefs,
    }).config;
  };
  const flowTool = createFlowTool(
    (target) => loadFor(target).registry ?? registry,
    reloadConfigFor,
  );
  targetable.push(flowTool);
  const flowShape: Record<string, z.ZodType> = {};
  for (const [key, schema] of Object.entries(flowTool.schema)) {
    flowShape[key] = schema;
  }
  const flowRegistration = server.tool(flowTool.name, flowTool.description, flowShape, async (rawParams) => {
    try {
      // A flow addresses one editor for the whole run. `params` is forwarded
      // verbatim into every step's options, so a target left in there would
      // reach bridge.call on every unmapped action: read it as the run's
      // target and strip it from both places.
      const nested = rawParams.params && typeof rawParams.params === "object"
        ? (rawParams.params as Record<string, unknown>)
        : undefined;
      const target = flowTool.injectedEditorParam
        ? rawParams[EDITOR_TARGET_PARAM] ?? nested?.[EDITOR_TARGET_PARAM]
        : undefined;
      const session = flowTool.injectedEditorParam ? sessions.resolve(target) : sessions.active;
      const params = stripEditorTarget(rawParams);
      if (nested) params.params = stripEditorTarget(nested);

      const result = await flowTool.handler(sessionContext(ctx, session), params);
      const text = typeof result === "string" ? result : JSON.stringify(result, null, 2);
      return { content: withUpgradeNotice([{ type: "text" as const, text }]) };
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      return { content: withUpgradeNotice([{ type: "text" as const, text: `Error: ${msg}` }]), isError: true };
    }
  });
  registeredTools.set(flowTool.name, flowRegistration);

  // Re-advertise whenever the session set changes, and take the first pass now
  // that every tool is registered.
  sessions.onCountChanged = () => syncEditorTargeting();
  syncEditorTargeting();

  // ── Optional HTTP surface for flow.run (#144) ───────────────────
  // Off by default; opt-in via ue-mcp.yml `ue-mcp.http: { enabled: true, port: 7723 }`.
  // Binds to 127.0.0.1 only - do NOT expose to the network without adding auth.
  if (project.config.http?.enabled) {
    try {
      startFlowHttpServer(flowTool, ctx, {
        port: project.config.http.port,
        host: project.config.http.host,
      });
    } catch (e) {
      console.error(`[ue-mcp] Failed to start HTTP server: ${e instanceof Error ? e.message : e}`);
    }
  }

  // ── Bridge connections ───────────────────────────────────────────
  // One socket per session. A session whose editor is down is still
  // registered and still startable; only addressing it needs a live bridge.
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

  if (disabled.size > 0) {
    console.error(`[ue-mcp] Disabled categories: ${[...disabled].join(", ")}`);
  }
  const activePluginCount = pluginRecords.filter((r) => r.status === "active").length;
  const pluginNote = pluginRecords.length > 0
    ? `, ${activePluginCount}/${pluginRecords.length} plugin(s)`
    : "";
  console.error(`[ue-mcp] Registered ${tools.length + 1} tools, ${taskCount} tasks (flow engine)${pluginNote}`);

  const transport = new StdioServerTransport();
  await server.connect(transport);
}

/**
 * Everything one editor session needs to serve a call: its own tool graph,
 * its own plugin load, and (once the context strategy is known) its own
 * advertised list and task registry.
 */
interface SessionLoad {
  surface: SessionSurface;
  pluginLoad: Awaited<ReturnType<typeof loadPlugins>>;
  configDir: string | undefined;
  advertisedTools: ToolDef[];
  registryTools: ToolDef[];
  registry?: ReturnType<typeof buildFlowRegistry>;
}

/**
 * Build one session's surface: clone the pristine graph, load that project's
 * plugins into the clone, then enrich the clone from that project's Epic
 * catalog. Nothing here touches another session's graph or the declaration
 * they were all cloned from.
 */
async function buildSessionLoad(
  session: EditorSession,
  packageVersion: string,
  multi: boolean,
): Promise<SessionLoad> {
  const project = session.project;
  const configDir = project.projectDir ?? undefined;
  const label = multi ? `editor '${session.name}': ` : "";

  // ── Plugins ──────────────────────────────────────────────────────
  // Read the user's `plugins:` entries from ue-mcp.yml (best-effort - a
  // missing or invalid file means zero plugins, never a fatal error). Then
  // resolve, validate, and inject into target categories BEFORE the flow
  // registry is built so plugin tasks register cleanly.
  const pluginEntries = readPluginsEntries(configDir);
  const pluginLoad = await loadPlugins(
    baseGraphFor(ALL_TOOLS),
    pluginEntries,
    configDir,
    packageVersion,
    project.config.pluginConfig,
  );
  const tools = pluginLoad.tools;

  // ── Epic 5.8 native toolset surfacing (best-effort, startup) ─────
  // If this session's bridge is reachable now, pull Epic's live toolset
  // catalog and inject each tool as a first-class action into the matching
  // ue-mcp category (GAS tools into `gas`, Niagara into `niagara`, etc.).
  // This must run before the flow registry and MCP tool registration so the
  // injected actions are dispatchable and advertised. When the editor is not
  // up yet, the `epic` gateway still works; a server restart picks up
  // enrichment.
  const nativeCfg = project.config.nativeTools ?? {};
  if (nativeCfg.enabled === false) {
    console.error(`[ue-mcp] ${label}Native Epic tools disabled via ue-mcp.yml (nativeTools.enabled=false); epic gateway still available`);
  } else {
    try {
      // Source priority: live editor (most current, refreshes the cache) ->
      // project cache (last-seen) -> baked snapshot shipped with the package
      // (deterministic default so the surface appears on first cold startup
      // and matches the generated docs). First available wins. The cache is
      // already keyed by project directory, so each session reads and writes
      // its own.
      let catalog: EpicCatalog | null = null;
      let source = "";
      const bridge = session.bridge;
      if (!bridge.isConnected) {
        await bridge.connect(2000).catch(() => {});
      }
      if (bridge.isConnected) {
        catalog = (await bridge.call("epic_list_toolsets", { includeSchemas: true }, 20000)) as EpicCatalog;
        if (catalog?.toolsets?.length) {
          saveCatalogCache(configDir, catalog, project.engineAssociation);
          source = "live editor";
        }
      }
      if (!catalog?.toolsets?.length) {
        catalog = loadCatalogCache(configDir);
        if (catalog?.toolsets?.length) source = "project cache";
      }
      if (!catalog?.toolsets?.length) {
        catalog = loadBakedCatalog();
        if (catalog?.toolsets?.length) source = "baked snapshot";
      }
      if (catalog?.toolsets?.length) {
        const enriched = enrichToolsWithEpicCatalog(tools, catalog, {
          excludeCategories: nativeCfg.exclude,
        });
        if (enriched.injected > 0) {
          const summary = Object.entries(enriched.byCategory).map(([c, n]) => `${c}:${n}`).join(", ");
          console.error(`[ue-mcp] ${label}Epic 5.8 toolsets (${source}): surfaced ${enriched.injected} tools (${summary})`);
          if (enriched.createdCategories.length) {
            console.error(`[ue-mcp] ${label}Epic-only categories added: ${enriched.createdCategories.join(", ")}`);
          }
        }
      }
    } catch (e) {
      console.error(`[ue-mcp] ${label}Epic toolset enrichment skipped: ${e instanceof Error ? e.message : e}`);
    }
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

/**
 * Best-effort read of the `plugins:` array from ue-mcp.yml. Returns [] when
 * the file is missing, unreadable, or malformed - plugin failures are loud at
 * load time, not fatal here.
 */
function readPluginsEntries(configDir: string | undefined): PluginEntry[] {
  if (!configDir) return [];
  const configPath = path.join(configDir, "ue-mcp.yml");
  if (!fs.existsSync(configPath)) return [];
  try {
    const raw = yaml.load(fs.readFileSync(configPath, "utf-8")) as { plugins?: unknown } | null;
    if (!raw || !Array.isArray(raw.plugins)) return [];
    const out: PluginEntry[] = [];
    for (const entry of raw.plugins) {
      if (entry && typeof entry === "object" && typeof (entry as { name?: unknown }).name === "string") {
        const e = entry as { name: string; version?: unknown };
        out.push({
          name: e.name,
          version: typeof e.version === "string" ? e.version : undefined,
        });
      }
    }
    return out;
  } catch (e) {
    warn("plugin", `failed to parse plugins: from ue-mcp.yml - ${(e as Error).message}`);
    return [];
  }
}

function buildKnowledgeBlock(knowledgeByCategory: Record<string, string[]>): string {
  const lines: string[] = [];
  for (const [category, blobs] of Object.entries(knowledgeByCategory)) {
    if (blobs.length === 0) continue;
    lines.push(`── ${category} ──`);
    for (const blob of blobs) lines.push(blob.trim());
    lines.push("");
  }
  return lines.join("\n").trim();
}

function toPluginInfo(rec: PluginRecord, project: ProjectContext): PluginInfo {
  const uePluginPresent = rec.uePluginDependency
    ? isUePluginEnabled(project, rec.uePluginDependency)
    : undefined;
  return {
    name: rec.name,
    version: rec.version,
    actionPrefix: rec.actionPrefix,
    status: rec.status,
    statusReason: rec.statusReason,
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

function isUePluginEnabled(project: ProjectContext, name: string): boolean | undefined {
  if (!project.projectPath) return undefined;
  try {
    const raw = JSON.parse(fs.readFileSync(project.projectPath, "utf-8")) as {
      Plugins?: Array<{ Name?: string; Enabled?: boolean }>;
    };
    if (!raw.Plugins) return false;
    const entry = raw.Plugins.find((p) => p.Name === name);
    if (!entry) return false;
    return entry.Enabled !== false;
  } catch {
    return undefined;
  }
}

// Route subcommands
const subcmd = process.argv[2];
if (subcmd === "init") {
  process.argv.splice(2, 1);
  import("./init.js");
} else if (subcmd === "update") {
  process.argv.splice(2, 1);
  import("./update.js");
} else if (subcmd === "doctor") {
  process.argv.splice(2, 1);
  import("./doctor.js").then((m) => m.runDoctorCli());
} else if (subcmd === "deploy") {
  process.argv.splice(2, 1);
  import("./deploy-cli.js");
} else if (subcmd === "hook") {
  import("./hook-handler.js");
} else if (subcmd === "uninstall-hooks") {
  process.argv.splice(2, 1);
  import("./uninstall-hooks.js");
} else if (subcmd === "auth") {
  process.argv.splice(2, 1);
  // #620: invoked via the index.js bin, argv[1] is index.js so auth-cli's
  // own "am I the entry point" guard never fires. Call the export directly.
  import("./auth-cli.js").then((m) => m.runFeedbackAuthStep()).catch((e) => {
    console.error(`[ue-mcp] auth failed: ${e instanceof Error ? e.message : e}`);
    process.exit(1);
  });
} else if (subcmd === "login") {
  process.argv.splice(2, 1);
  import("./login-cli.js").then((m) => m.runLogin()).catch((e) => {
    console.error(`[ue-mcp] login failed: ${e instanceof Error ? e.message : e}`);
    process.exit(1);
  });
} else if (subcmd === "logout") {
  process.argv.splice(2, 1);
  import("./login-cli.js").then((m) => m.runLogout()).catch((e) => {
    console.error(`[ue-mcp] logout failed: ${e instanceof Error ? e.message : e}`);
    process.exit(1);
  });
} else if (subcmd === "feedback") {
  process.argv.splice(2, 1);
  import("./feedback-cli.js");
} else if (subcmd === "resolve") {
  import("./resolve.js");
} else if (subcmd === "build") {
  process.argv.splice(2, 1);
  import("./build-cli.js");
} else if (subcmd === "plugin") {
  process.argv.splice(2, 1);
  import("./plugin-cli.js");
} else if (subcmd === "context") {
  process.argv.splice(2, 1);
  import("./context-cli.js");
} else if (subcmd === "version" || subcmd === "--version" || subcmd === "-v") {
  const { createRequire } = await import("node:module");
  const require = createRequire(import.meta.url);
  const pkg = require("../package.json");
  console.log(pkg.version);
} else {
  main().catch((e) => {
    console.error(`[ue-mcp] Fatal error: ${e}`);
    process.exit(1);
  });
}
