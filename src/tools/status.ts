import * as fs from "node:fs";
import { z } from "zod";
import type { ToolDef, ToolContext } from "../types.js";
import { deploy, deploySummary } from "../deployer.js";

export const statusTools: ToolDef[] = [
  {
    name: "get_status",
    description:
      "Get the current status of the UE MCP server including operation mode, " +
      "loaded project info, engine version, and editor connection state. " +
      "Call this first to understand what capabilities are available.",
    schema: {},
    handler: async (ctx) => ({
      mode: ctx.bridge.isConnected ? "live" : "disconnected",
      editorConnected: ctx.bridge.isConnected,
      project: ctx.project.isLoaded
        ? {
            name: ctx.project.projectName,
            path: ctx.project.projectPath,
            contentDir: ctx.project.contentDir,
            engineAssociation: ctx.project.engineAssociation,
          }
        : null,
    }),
  },
  {
    name: "set_project",
    description:
      "Switch the Unreal Engine project. Normally the project is set automatically via the " +
      "command-line argument in mcp.json, so you do NOT need to call this. Only use it to " +
      "switch to a different project mid-session. Deploys the editor bridge plugin and connects.",
    schema: {
      projectPath: z
        .string()
        .describe("Absolute path to the .uproject file or directory containing it"),
    },
    handler: async (ctx, params) => {
      ctx.project.setProject(params.projectPath as string);
      const result = deploy(ctx.project);
      try {
        await ctx.bridge.connect();
      } catch {
        // editor might not be running yet
      }
      return {
        success: true,
        projectName: ctx.project.projectName,
        contentDir: ctx.project.contentDir,
        engineAssociation: ctx.project.engineAssociation,
        editorConnected: ctx.bridge.isConnected,
        bridgeSetup: deploySummary(result),
      };
    },
  },
  {
    name: "get_project_info",
    description:
      "Get detailed information from the .uproject file including plugins, target platforms, " +
      "modules, and engine association.",
    schema: {},
    handler: async (ctx) => {
      ctx.project.ensureLoaded();
      const raw = fs.readFileSync(ctx.project.projectPath!, "utf-8");
      const uproject = JSON.parse(raw);
      return {
        projectName: ctx.project.projectName,
        engineAssociation: ctx.project.engineAssociation,
        contentDir: ctx.project.contentDir,
        uprojectContents: uproject,
      };
    },
  },
];
