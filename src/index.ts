import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { EditorBridge } from "./bridge.js";
import { ProjectContext } from "./project.js";
import { deploy, deploySummary } from "./deployer.js";
import { SERVER_INSTRUCTIONS } from "./instructions.js";
import type { ToolDef, ToolContext } from "./types.js";

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
];

async function main() {
  const bridge = new EditorBridge();
  const project = new ProjectContext();
  const ctx: ToolContext = { bridge, project };

  const server = new McpServer({
    name: "ue-mcp",
    version: "0.3.0",
  }, {
    instructions: SERVER_INSTRUCTIONS,
  });

  for (const tool of ALL_TOOLS) {
    const shape: Record<string, z.ZodType> = {};
    for (const [key, schema] of Object.entries(tool.schema)) {
      shape[key] = schema;
    }

    server.tool(tool.name, tool.description, shape, async (params) => {
      try {
        const result = await tool.handler(ctx, params);
        return {
          content: [
            {
              type: "text" as const,
              text: typeof result === "string" ? result : JSON.stringify(result, null, 2),
            },
          ],
        };
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        return {
          content: [{ type: "text" as const, text: `Error: ${msg}` }],
          isError: true,
        };
      }
    });
  }

  const projectArg = process.argv.find((a) => !a.startsWith("-") && a !== process.argv[0] && a !== process.argv[1]);

  if (projectArg) {
    try {
      project.setProject(projectArg);
      console.error(`[ue-mcp] Project loaded: ${project.projectName} (engine ${project.engineAssociation ?? "unknown"})`);

      const result = deploy(project);
      console.error(`[ue-mcp] ${deploySummary(result)}`);

      try {
        await bridge.connect();
        console.error("[ue-mcp] Editor bridge connected — live mode active");
      } catch {
        console.error("[ue-mcp] Editor not reachable — will retry in background");
      }

      bridge.startReconnecting();
    } catch (e) {
      console.error(`[ue-mcp] Failed to initialize project: ${e instanceof Error ? e.message : e}`);
    }
  }

  console.error(`[ue-mcp] Registered ${ALL_TOOLS.length} tools (category mega-tools)`);

  const transport = new StdioServerTransport();
  await server.connect(transport);
}

main().catch((e) => {
  console.error(`[ue-mcp] Fatal error: ${e}`);
  process.exit(1);
});
