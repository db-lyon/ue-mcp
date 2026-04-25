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
import { info } from "./log.js";
import { startVersionCheck, consumeUpgradeNotice } from "./version-check.js";
import { buildFlowRegistry } from "./flow/registry.js";
import { loadFlowConfig } from "./flow/loader.js";
import { createFlowTool } from "./flow/flow-tool.js";
import { startFlowHttpServer } from "./flow/http-server.js";
import type { FlowContext } from "./flow/context.js";
import type { FlowConfig } from "./flow/schema.js";

import { ALL_TOOLS } from "./tools.js";

type TextBlock = { type: "text"; text: string };

function withUpgradeNotice(content: TextBlock[]): TextBlock[] {
  const notice = consumeUpgradeNotice();
  return notice ? [{ type: "text" as const, text: notice }, ...content] : content;
}

async function main() {
  const bridge = new EditorBridge();
  const project = new ProjectContext();
  const ctx: ToolContext = { bridge, project };

  // Kick off the npm registry check in the background; the next tool response
  // injects the notice if a newer version is published.
  const { createRequire } = await import("node:module");
  const require = createRequire(import.meta.url);
  const pkg = require("../package.json") as { version: string };
  startVersionCheck(pkg.version);

  // ── Flow engine: task registry ──────────────────────────────────
  const registry = buildFlowRegistry(ALL_TOOLS);
  const taskCount = registry.listRegistered().length;

  const server = new McpServer({
    name: "ue-mcp",
    version: "0.6.4",
  }, {
    instructions: SERVER_INSTRUCTIONS,
  });

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

      try {
        const task = await registry.create(taskName, flowCtx, taskParams);
        const result = await task.run();

        if (!result.success) {
          const msg = result.error?.message ?? `Task ${taskName} failed`;
          return {
            content: withUpgradeNotice([{ type: "text" as const, text: `Error [TASK_FAILED]: ${msg}` }]),
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
          content: withUpgradeNotice([{ type: "text" as const, text: `Error [${code}]: ${msg}` }]),
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
      return { content: withUpgradeNotice([{ type: "text" as const, text }]) };
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      return { content: withUpgradeNotice([{ type: "text" as const, text: `Error: ${msg}` }]), isError: true };
    }
  });

  // ── Optional HTTP surface for flow.run (#144) ───────────────────
  // Off by default; opt-in via ".ue-mcp.json" { "http": { "enabled": true, "port": 7723 } }.
  // Binds to 127.0.0.1 only — do NOT expose to the network without adding auth.
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

  // ── Bridge connection ────────────────────────────────────────────
  try {
    await bridge.connect();
    info("bridge", "editor bridge connected - live mode active");
  } catch (e) {
    info("bridge", "editor not reachable - will retry in background", e);
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
