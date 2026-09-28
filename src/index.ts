#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import type { z } from "zod";
import type { EditorSession } from "./sessions/session.js";
import { composeServerInstructions } from "./surface/context/instructions.js";
import { fullSurfaceDescription } from "./surface/context/lean-context.js";
import { envelopeInputSchema, envelopeShape, usesArgsEnvelope } from "./surface/context/call-envelope.js";
import {
  injectEditorTarget,
  removeEditorTarget,
  injectMigrateTarget,
  removeMigrateTarget,
} from "./surface/target-params.js";
import type { ToolDef } from "./core/types.js";
import { info, warn, error } from "./core/log.js";
import { startVersionCheck } from "./core/version-check.js";
import { createFlowTool } from "./flow/flow-tool.js";
import { startFlowHttpServer } from "./flow/http-server.js";
import { packageVersion } from "./core/package-root.js";
import { findCliCommand, runCliCommand } from "./cli/cli-commands.js";
import { UeMcpRuntime } from "./runtime/runtime.js";
import {
  buildElicit,
  dispatchCategoryCall,
  dispatchFlowCall,
  type CallExtra,
  type DispatchDeps,
} from "./dispatch/server-dispatch.js";

async function main() {
  // The next tool response carries the notice if a newer version is published.
  startVersionCheck(packageVersion());

  // The composition root: every editor (#817: one per positional argument),
  // its load, its dialog guard, and the settings that apply to the whole server.
  const runtime = await UeMcpRuntime.start({
    projectArgs: process.argv.slice(2).filter((a) => !a.startsWith("-")),
    packageVersion: packageVersion(),
  });
  const { sessions, loads } = runtime;
  const contextStrategy = runtime.settings.contextStrategy;
  const baseCtx = runtime.baseContext();
  const primaryLoad = loads.get(runtime.primary)!;
  const advertisedTools: ToolDef[] = await runtime.advertisedTools();
  if (contextStrategy !== "full") {
    info("context", `Context strategy: ${contextStrategy}`);
  }
  const dialogGuardFor = (session: EditorSession, canElicit = false) => runtime.dialogGuardFor(session, canElicit);

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
    lockingCfg: runtime.settings.locking,
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

  // ── Flow tool: ue-mcp.yml is reread whenever one of its files changes ──
  const initialConfig = primaryLoad.flowConfig.get();
  info("flow", `ue-mcp.yml loaded - ${Object.keys(initialConfig.flows).length} flow(s), ${Object.keys(initialConfig.tasks).length} custom task(s)`);

  // Resolved from the addressed editor: a flow declared in one project's
  // ue-mcp.yml runs through that project's registry, never the first one's (D1).
  const flowTool = createFlowTool(
    (target) => runtime.flowRegistryFor(target),
    (target) => runtime.flowConfigFor(target),
  );
  targetable.push(flowTool);
  const flowShape: Record<string, z.ZodType> = { ...flowTool.schema };
  const flowRegistration = server.tool(flowTool.name, flowTool.description, flowShape, ((rawParams: Record<string, unknown>, extra: CallExtra) =>
    dispatchFlowCall(deps, flowTool, baseCtx, rawParams, extra)) as never);
  registeredTools.set(flowTool.name, flowRegistration);

  // Re-advertise whenever the session set changes, and take the first pass now.
  sessions.onCountChanged = () => syncEditorTargeting();
  syncEditorTargeting();

  // ── Optional HTTP surface for flow.run (#144) ───────────────────
  // Opt-in via `ue-mcp.http`; binds to 127.0.0.1 only.
  const http = runtime.settings.http;
  if (http.enabled) {
    try {
      startFlowHttpServer(flowTool, baseCtx, { port: http.port, host: http.host });
    } catch (e) {
      error("http", "Failed to start HTTP server", e);
    }
  }

  // ── Bridge connections ───────────────────────────────────────────
  // One socket per session. A session whose editor is down stays registered.
  await runtime.connectAll();

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
