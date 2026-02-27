import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { EditorBridge } from "./bridge.js";
import { ProjectContext } from "./project.js";
import { deploy, deploySummary } from "./deployer.js";
import { SERVER_INSTRUCTIONS } from "./instructions.js";
import type { ToolDef, ToolContext } from "./types.js";

import { statusTools } from "./tools/status.js";
import { configTools } from "./tools/config.js";
import { cppTools } from "./tools/cpp.js";
import { assetTools } from "./tools/assets.js";
import { blueprintTools } from "./tools/blueprint.js";
import { levelTools } from "./tools/level.js";
import { editorTools } from "./tools/editor.js";
import { materialTools } from "./tools/material.js";
import { animationTools } from "./tools/animation.js";
import { reflectionTools } from "./tools/reflection.js";
import { landscapeTools } from "./tools/landscape.js";
import { pcgTools } from "./tools/pcg.js";
import { foliageTools } from "./tools/foliage.js";
import { lightingTools } from "./tools/lighting.js";
import { niagaraTools } from "./tools/niagara.js";
import { audioTools } from "./tools/audio.js";
import { sequencerTools } from "./tools/sequencer.js";
import { navigationTools } from "./tools/navigation.js";
import { physicsTools } from "./tools/physics.js";
import { skeletonTools } from "./tools/skeleton.js";
import { widgetTools } from "./tools/widget.js";
import { volumeTools } from "./tools/volume.js";
import { splineTools } from "./tools/spline.js";
import { textureTools } from "./tools/texture.js";
import { importTools } from "./tools/import.js";
import { inputTools } from "./tools/input.js";
import { behaviorTreeTools } from "./tools/behaviortree.js";
import { performanceTools } from "./tools/performance.js";
import { dataTableTools } from "./tools/datatable.js";
import { demoTools } from "./tools/demo.js";

const ALL_TOOLS: ToolDef[] = [
  ...statusTools,
  ...configTools,
  ...cppTools,
  ...assetTools,
  ...blueprintTools,
  ...levelTools,
  ...editorTools,
  ...materialTools,
  ...animationTools,
  ...reflectionTools,
  ...landscapeTools,
  ...pcgTools,
  ...foliageTools,
  ...lightingTools,
  ...niagaraTools,
  ...audioTools,
  ...sequencerTools,
  ...navigationTools,
  ...physicsTools,
  ...skeletonTools,
  ...widgetTools,
  ...volumeTools,
  ...splineTools,
  ...textureTools,
  ...importTools,
  ...inputTools,
  ...behaviorTreeTools,
  ...performanceTools,
  ...dataTableTools,
  ...demoTools,
];

async function main() {
  const bridge = new EditorBridge();
  const project = new ProjectContext();
  const ctx: ToolContext = { bridge, project };

  const server = new McpServer({
    name: "ue-mcp",
    version: "0.2.0",
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

  console.error(`[ue-mcp] Registered ${ALL_TOOLS.length} tools`);

  const transport = new StdioServerTransport();
  await server.connect(transport);
}

main().catch((e) => {
  console.error(`[ue-mcp] Fatal error: ${e}`);
  process.exit(1);
});
