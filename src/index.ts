#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import type { z } from "zod";
import { SessionRegistry, type EditorSession } from "./session.js";
import { ueMcpConfigRejections, describeConfigRejections } from "./project.js";
import { attach, attachSummary } from "./deployer.js";
import { composeServerInstructions } from "./instructions.js";
import { resolveContextStrategy, fullSurfaceDescription } from "./lean-context.js";
import { envelopeInputSchema, envelopeShape, usesArgsEnvelope } from "./call-envelope.js";
import {
  injectEditorTarget,
  removeEditorTarget,
  injectMigrateTarget,
  removeMigrateTarget,
} from "./target-params.js";
import type { ToolDef } from "./types.js";
import { DialogGuard, guardFor, sessionGuardDeps } from "./dialog-guard.js";
import { info, warn, error } from "./log.js";
import { startVersionCheck } from "./version-check.js";
import { GuardRegistry } from "./flow/guard.js";
import { loadFlowConfig } from "./flow/loader.js";
import { createFlowTool } from "./flow/flow-tool.js";
import { startFlowHttpServer } from "./flow/http-server.js";
import { resolveLockingConfig } from "./locking.js";
import { collapsingEnvWarnings } from "./session-env.js";
import { checkPluginFreshness } from "./plugin-freshness.js";
import { unionSurface } from "./session-surface.js";
import { packageVersion } from "./package-root.js";
import { findCliCommand, runCliCommand } from "./cli-commands.js";
import { SessionLoads } from "./session-load.js";
import {
  buildElicit,
  dispatchCategoryCall,
  dispatchFlowCall,
  type CallExtra,
  type DispatchDeps,
} from "./server-dispatch.js";

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

async function main() {
  // Every session wraps its own bridge in the guard pipeline, so it exists
  // first. The registry owns the sessions; nothing here keeps a bridge of its own.
  const sessions = new SessionRegistry(new GuardRegistry());

  // The next tool response carries the notice if a newer version is published.
  startVersionCheck(packageVersion());

  // #817: every positional argument is a project with its own session.
  registerSessions(sessions, process.argv.slice(2).filter((a) => !a.startsWith("-")));

  // Process-level choices (context strategy, HTTP surface, flow config source)
  // read the first session's project.
  const primary = sessions.active;
  const project = primary.project;
  // One transport, one advertised shape (full | lean | micro).
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

  const server = new McpServer({
    name: "ue-mcp",
    // Read from package.json, never written here.
    version: packageVersion(),
  }, {
    instructions: composeServerInstructions(
      contextStrategy,
      loads.surfaces,
      sessions.list().map((s) => s.name),
      sessions.active.name,
    ),
  });

  baseCtx.elicit = buildElicit(server);

  const deps: DispatchDeps = {
    sessions,
    loads,
    lockingCfg,
    dialogGuardFor,
    elicit: () => baseCtx.elicit,
    client: () => server.server.getClientVersion(),
  };

  // ── Per-call editor targeting (#817) ─────────────────────────────
  // `editor` exists only while this server drives more than one editor, so a
  // single-editor schema is byte-for-byte what it was before sessions existed.
  // Adding or dropping a session re-advertises.
  const registeredTools = new Map<string, ReturnType<typeof server.tool>>();
  const targetable: ToolDef[] = [...advertisedTools];
  let targetingSignature = "";
  const syncEditorTargeting = (): void => {
    const names = sessions.list().map((s) => s.name);
    const signature = names.length > 1 ? names.join(", ") : "";
    if (signature === targetingSignature) return;
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
      const registration = registeredTools.get(tool.name);
      if (!registration) continue;
      if (usesArgsEnvelope(tool)) {
        // The SDK rebuilds a stripping object from the raw shape; put the
        // pass-through one back so a flat call still reaches validation.
        registration.update({ paramsSchema: envelopeShape(tool) });
        registration.inputSchema = envelopeInputSchema(tool);
      } else {
        registration.update({ paramsSchema: tool.schema });
      }
    }
  };

  // ── Category tools, dispatched through each session's task registry ──
  for (const tool of advertisedTools) {
    const shape: Record<string, z.ZodType> = { ...tool.schema };
    // Advertised as `action` + `args` (#1172); the flat shape stays the
    // validation contract, applied in dispatch instead of by the SDK.
    const envelope = usesArgsEnvelope(tool);
    const description = envelope && contextStrategy === "full" ? fullSurfaceDescription(tool) : tool.description;
    const callback = (callArgs: Record<string, unknown>, extra: CallExtra) =>
      dispatchCategoryCall(deps, tool, envelope, callArgs, extra);
    const registration = envelope
      ? server.registerTool(tool.name, { description, inputSchema: envelopeInputSchema(tool) }, callback as never)
      : server.tool(tool.name, description, shape, callback as never);
    registeredTools.set(tool.name, registration);
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
  targetable.push(flowTool);
  const flowShape: Record<string, z.ZodType> = { ...flowTool.schema };
  const flowRegistration = server.tool(flowTool.name, flowTool.description, flowShape, (rawParams) =>
    dispatchFlowCall(deps, flowTool, baseCtx, rawParams) as never);
  registeredTools.set(flowTool.name, flowRegistration);

  // Re-advertise whenever the session set changes, and take the first pass now.
  sessions.onCountChanged = () => syncEditorTargeting();
  syncEditorTargeting();

  // ── Optional HTTP surface for flow.run (#144) ───────────────────
  // Opt-in via `ue-mcp.http`; binds to 127.0.0.1 only.
  if (project.config.http?.enabled) {
    try {
      startFlowHttpServer(flowTool, baseCtx, {
        port: project.config.http.port,
        host: project.config.http.host,
      });
    } catch (e) {
      error("http", "Failed to start HTTP server", e);
    }
  }

  // ── Bridge connections ───────────────────────────────────────────
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

  await server.connect(new StdioServerTransport());
}

// A first argument naming a command runs it; anything else starts the server.
const command = findCliCommand(process.argv[2]);
if (command) {
  void runCliCommand(command, process.argv.slice(3));
} else {
  main().catch((e) => {
    error("server", "Fatal error", e);
    process.exit(1);
  });
}
