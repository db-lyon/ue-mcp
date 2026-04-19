#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { EditorBridge } from "./bridge.js";
import { ProjectContext } from "./project.js";
import { attach, attachSummary } from "./deployer.js";
import { SERVER_INSTRUCTIONS } from "./instructions.js";
import { isDirectiveResponse, type ToolDef, type ToolContext } from "./types.js";
import { McpError } from "./errors.js";
import { buildFlowRegistry } from "./flow/registry.js";
import { loadFlowConfig } from "./flow/loader.js";
import { createFlowTool } from "./flow/flow-tool.js";
import { startFlowHttpServer } from "./flow/http-server.js";
import type { FlowContext } from "./flow/context.js";
import type { FlowConfig } from "./flow/schema.js";

import * as path from "node:path";
import { OntologyRegistry, createHandlerRegistryProjector, createWorkaroundProjector, createPluginProjector, createEngineSymbolProjector, createInvocationProjector } from "./ontology/index.js";
import { getWorkarounds } from "./workaround-tracker.js";
import { getInvocations, pushInvocation } from "./invocation-tracker.js";
import { findEngineInstall } from "./deployer.js";
import { createOntologyTool } from "./tools/ontology.js";

import { projectTool } from "./tools/project.js";
import { assetTool } from "./tools/asset.js";
import { blueprintTool } from "./tools/blueprint.js";
import { levelTool } from "./tools/level.js";
import { materialTool } from "./tools/material.js";
import { animationTool } from "./tools/animation.js";
import { landscapeTool } from "./tools/landscape.js";
import { pcgTool } from "./tools/pcg.js";
import { foliageTool } from "./tools/foliage.js";
import { niagaraTool } from "./tools/niagara.js";
import { audioTool } from "./tools/audio.js";
import { widgetTool } from "./tools/widget.js";
import { editorTool } from "./tools/editor.js";
import { reflectionTool } from "./tools/reflection.js";
import { gameplayTool } from "./tools/gameplay.js";
import { gasTool } from "./tools/gas.js";
import { networkingTool } from "./tools/networking.js";
import { demoTool } from "./tools/demo.js";
import { feedbackTool } from "./tools/feedback.js";

const ALL_TOOLS: ToolDef[] = [
  projectTool,
  assetTool,
  blueprintTool,
  levelTool,
  materialTool,
  animationTool,
  landscapeTool,
  pcgTool,
  foliageTool,
  niagaraTool,
  audioTool,
  widgetTool,
  editorTool,
  reflectionTool,
  gameplayTool,
  gasTool,
  networkingTool,
  demoTool,
  feedbackTool,
];

async function main() {
  const bridge = new EditorBridge();
  const project = new ProjectContext();
  const ctx: ToolContext = { bridge, project };

  // ── Flow engine: task registry ──────────────────────────────────
  const registry = buildFlowRegistry(ALL_TOOLS);
  const taskCount = registry.listRegistered().length;

  const server = new McpServer({
    name: "ue-mcp",
    version: "0.6.4",
  }, {
    instructions: SERVER_INSTRUCTIONS,
  });

  // ── Ontology registry: projects live state into .kant fragments ──
  const packageRoot = path.dirname(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, "$1")));
  const resolveProjectedDir = () =>
    path.join(project.projectDir ?? process.cwd(), ".ue-mcp", "ontology", "projected");
  const ontologyRegistry = new OntologyRegistry(
    resolveProjectedDir,
    () => ({
      kernel: { priority: 0, paths: [path.join(packageRoot, "ontology", "kernel")] },
      projected: { priority: 1, paths: [resolveProjectedDir()] },
      repoLocal: {
        priority: 2,
        paths: project.projectDir ? [path.join(project.projectDir, ".ue-mcp")] : [],
      },
    }),
  );
  const ontologyTool = createOntologyTool(ontologyRegistry);
  ALL_TOOLS.push(ontologyTool);
  ontologyRegistry.register(
    createHandlerRegistryProjector(ALL_TOOLS),
    () => undefined,
  );
  ontologyRegistry.register(
    createWorkaroundProjector(getWorkarounds),
    () => undefined,
  );
  ontologyRegistry.register(
    createPluginProjector(),
    () => ({
      engineRoot: findEngineInstall(project.engineAssociation ?? null),
      projectDir: project.projectDir,
    }),
  );
  ontologyRegistry.register(
    createEngineSymbolProjector(),
    () => ({
      engineRoot: findEngineInstall(project.engineAssociation ?? null),
      trees: ["Runtime"] as const,
      includePlugins: false,
    }),
  );
  ontologyRegistry.register(
    createInvocationProjector(getInvocations),
    () => undefined,
  );

  // Prime the ontology cache so the dispatch-layer preflight can
  // consult it from the first tool call. We fire startup-subscribed
  // projectors only so expensive ones (EngineSymbolProjector) do not
  // block server ready-up. Agents can run ontology(project_by_event,
  // event="manual") to build the symbol index on demand.
  try {
    ontologyRegistry.projectByEvent("startup");
  } catch (e) {
    console.error(`[ue-mcp] ontology projection at startup failed: ${e instanceof Error ? e.message : e}`);
  }

  const disabled = new Set(project.config.disable ?? []);
  const tools = ALL_TOOLS.filter((t) => !disabled.has(t.name));

  // ── Register category tools — dispatched through the task registry ──
  for (const tool of tools) {
    const shape: Record<string, z.ZodType> = {};
    for (const [key, schema] of Object.entries(tool.schema)) {
      shape[key] = schema;
    }

    server.tool(tool.name, tool.description, shape, async (params) => {
      const action = params.action as string;
      const taskName = `${tool.name}.${action}`;
      const { action: _, ...taskParams } = params;
      const flowCtx: FlowContext = { bridge, project };
      const started = Date.now();

      try {
        // Ontology-backed preflight: if the action declared `requires`
        // and the resolved Plugin catalog says any of them are missing
        // or disabled, fail early with a structured error instead of
        // blowing up deep in the bridge.
        const pre = ontologyRegistry.checkRequires(tool.name, action);
        if (!pre.ok) {
          const parts: string[] = [];
          if (pre.missing.length > 0) parts.push(`missing plugins: ${pre.missing.join(", ")}`);
          if (pre.disabled.length > 0) parts.push(`disabled plugins: ${pre.disabled.join(", ")}`);
          const errText = `${tool.name}.${action} requires ${pre.declared.join(", ")}; ${parts.join("; ")}. Enable the plugin(s) in your project and restart the editor.`;
          pushInvocation({
            tool: tool.name,
            action,
            status: "requires_unmet",
            durationMs: Date.now() - started,
            timestamp: new Date().toISOString(),
            errorCode: "REQUIRES_UNMET",
            errorSnippet: errText,
          });
          return {
            content: [{ type: "text" as const, text: `Error [REQUIRES_UNMET]: ${errText}` }],
            isError: true,
          };
        }

        const task = await registry.create(taskName, flowCtx, taskParams);
        const result = await task.run();

        if (!result.success) {
          const msg = result.error?.message ?? `Task ${taskName} failed`;
          pushInvocation({
            tool: tool.name,
            action,
            status: "error",
            durationMs: Date.now() - started,
            timestamp: new Date().toISOString(),
            errorCode: "TASK_FAILED",
            errorSnippet: msg,
          });
          return {
            content: [{ type: "text" as const, text: `Error [TASK_FAILED]: ${msg}` }],
            isError: true,
          };
        }

        pushInvocation({
          tool: tool.name,
          action,
          status: "ok",
          durationMs: Date.now() - started,
          timestamp: new Date().toISOString(),
        });

        const stringify = (v: unknown) =>
          typeof v === "string" ? v : JSON.stringify(v, null, 2);

        // Auto-directive: declared approval="explicit" actions emit a
        // structured warning as a separate content block regardless of
        // whether the handler attached its own directive. Agents see
        // the approval policy at call time, not buried in docs.
        const approval = ontologyRegistry.resolveApproval(tool.name, action);
        const autoDirective = approval === "explicit"
          ? `[AGENT DIRECTIVE - MANDATORY]\n${tool.name}.${action} is declared approval="explicit". This action is recorded to /UE/Audit/Invocations. Confirm with the user before chaining further explicit actions.`
          : undefined;

        // Preserve directive responses (execute_python workaround tracking)
        if (result.data?.__directive) {
          const blocks: Array<{ type: "text"; text: string }> = [];
          if (autoDirective) blocks.push({ type: "text", text: autoDirective });
          blocks.push({ type: "text", text: result.data.directive as string });
          blocks.push({ type: "text", text: stringify(result.data.result) });
          return { content: blocks };
        }

        if (autoDirective) {
          return {
            content: [
              { type: "text" as const, text: autoDirective },
              { type: "text" as const, text: stringify(result.data) },
            ],
          };
        }

        return {
          content: [{ type: "text" as const, text: stringify(result.data) }],
        };
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        const code = e instanceof McpError ? e.code : "UNKNOWN";
        pushInvocation({
          tool: tool.name,
          action,
          status: "error",
          durationMs: Date.now() - started,
          timestamp: new Date().toISOString(),
          errorCode: code,
          errorSnippet: msg,
        });
        return {
          content: [{ type: "text" as const, text: `Error [${code}]: ${msg}` }],
          isError: true,
        };
      }
    });
  }

  // ── Project init ─────────────────────────────────────────────────
  const projectArg = process.argv.find((a) => !a.startsWith("-") && a !== process.argv[0] && a !== process.argv[1]);

  if (projectArg) {
    try {
      project.setProject(projectArg);
      console.error(`[ue-mcp] Project loaded: ${project.projectName} (engine ${project.engineAssociation ?? "unknown"})`);

      // Non-destructive attach — never overwrites local bridge source.
      // Source deployment is reserved for `ue-mcp init` / `ue-mcp update`.
      const result = attach(project);
      console.error(`[ue-mcp] ${attachSummary(result)}`);
    } catch (e) {
      console.error(`[ue-mcp] Failed to initialize project: ${e instanceof Error ? e.message : e}`);
    }
  }

  // ── Load ue-mcp.yml and register flow tool ──────────────────────
  const configDir = project.projectDir ?? undefined;

  // Log initial load
  const initialLoad = loadFlowConfig(ALL_TOOLS, configDir);
  console.error(`[ue-mcp] ue-mcp.yml loaded — ${Object.keys(initialLoad.config.flows).length} flow(s), ${Object.keys(initialLoad.config.tasks).length} custom task(s)`);

  // Config is reloaded on every flow call — edit ue-mcp.yml without restarting
  const reloadConfig = (): FlowConfig => loadFlowConfig(ALL_TOOLS, configDir).config;

  const flowTool = createFlowTool(registry, reloadConfig);
  const flowShape: Record<string, z.ZodType> = {};
  for (const [key, schema] of Object.entries(flowTool.schema)) {
    flowShape[key] = schema;
  }
  server.tool(flowTool.name, flowTool.description, flowShape, async (params) => {
    try {
      const result = await flowTool.handler(ctx, params);
      const text = typeof result === "string" ? result : JSON.stringify(result, null, 2);
      return { content: [{ type: "text" as const, text }] };
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      return { content: [{ type: "text" as const, text: `Error: ${msg}` }], isError: true };
    }
  });

  // ── Optional HTTP surface for flow.run (#144) ───────────────────
  // Off by default; opt-in via ".ue-mcp.json" { "http": { "enabled": true, "port": 7723 } }.
  // Binds to 127.0.0.1 only — do NOT expose to the network without adding auth.
  if (project.config.http?.enabled) {
    try {
      startFlowHttpServer(flowTool, ctx, reloadConfig, {
        port: project.config.http.port,
        host: project.config.http.host,
      });
    } catch (e) {
      console.error(`[ue-mcp] Failed to start HTTP server: ${e instanceof Error ? e.message : e}`);
    }
  }

  // ── Bridge connection ────────────────────────────────────────────
  try {
    await bridge.connect();
    console.error("[ue-mcp] Editor bridge connected — live mode active");
  } catch {
    console.error("[ue-mcp] Editor not reachable — will retry in background");
  }
  bridge.startReconnecting();

  if (disabled.size > 0) {
    console.error(`[ue-mcp] Disabled categories: ${[...disabled].join(", ")}`);
  }
  console.error(`[ue-mcp] Registered ${tools.length + 1} tools, ${taskCount} tasks (flow engine)`);

  const transport = new StdioServerTransport();
  await server.connect(transport);
}

// Route subcommands
const subcmd = process.argv[2];
if (subcmd === "init") {
  process.argv.splice(2, 1);
  import("./init.js");
} else if (subcmd === "update") {
  process.argv.splice(2, 1);
  import("./update.js");
} else if (subcmd === "hook") {
  import("./hook-handler.js");
} else if (subcmd === "resolve") {
  import("./resolve.js");
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
