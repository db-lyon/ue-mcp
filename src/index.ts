#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { EditorBridge } from "./bridge.js";
import { ProjectContext } from "./project.js";
import { deploy, deploySummary } from "./deployer.js";
import { SERVER_INSTRUCTIONS } from "./instructions.js";
import { isDirectiveResponse, type ToolDef, type ToolContext } from "./types.js";
import { McpError } from "./errors.js";
import { buildFlowRegistry } from "./flow/registry.js";
import { loadFlowConfig } from "./flow/loader.js";
import { createFlowTool } from "./flow/flow-tool.js";
import type { FlowContext } from "./flow/context.js";
import type { FlowConfig } from "./flow/schema.js";

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
    version: "0.6.3",
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
            content: [{ type: "text" as const, text: `Error [TASK_FAILED]: ${msg}` }],
            isError: true,
          };
        }

        const stringify = (v: unknown) =>
          typeof v === "string" ? v : JSON.stringify(v, null, 2);

        // Preserve directive responses (execute_python workaround tracking)
        if (result.data?.__directive) {
          return {
            content: [
              { type: "text" as const, text: result.data.directive as string },
              { type: "text" as const, text: stringify(result.data.result) },
            ],
          };
        }

        return {
          content: [{ type: "text" as const, text: stringify(result.data) }],
        };
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        const code = e instanceof McpError ? e.code : "UNKNOWN";
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

      const result = deploy(project);
      console.error(`[ue-mcp] ${deploySummary(result)}`);
    } catch (e) {
      console.error(`[ue-mcp] Failed to initialize project: ${e instanceof Error ? e.message : e}`);
    }
  }

  // ── Load ue-mcp.yml and register flow tool ──────────────────────
  const flowLoaded = loadFlowConfig(ALL_TOOLS, project.projectDir ?? undefined);
  const flowConfig: FlowConfig = flowLoaded?.config ?? { tasks: {}, flows: {} };

  if (flowLoaded) {
    console.error(`[ue-mcp] ue-mcp.yml loaded — ${Object.keys(flowConfig.flows).length} flow(s), ${Object.keys(flowConfig.tasks).length} custom task(s)`);
  }

  const flowTool = createFlowTool(registry, flowConfig);
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
