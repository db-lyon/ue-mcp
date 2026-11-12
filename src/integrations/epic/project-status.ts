/**
 * Epic's Unreal MCP server, Unreal Assistant and Unreal Canvas as one project
 * has them, read from files. init, doctor and project(get_status) all report
 * through this.
 */
import type { ProjectContext } from "../../config/project.js";
import { projectEngineVersion } from "../../editor/deployer.js";
import { engineLookupFor, resolveEngineRoot } from "../../editor/engine-root.js";
import { resolveLiveBridgeAddress } from "../../bridge/editor-target.js";
import { readUnrealMcpServer, type UnrealMcpServerStatus } from "./unreal-mcp-server.js";
import { readUnrealAssistant, type UnrealAssistantStatus } from "./unreal-assistant.js";
import { readUnrealCanvas, type UnrealCanvasStatus } from "./unreal-canvas.js";

/** The engine tree this project builds and runs with, or null when none resolves. */
export function projectEngineRoot(project: ProjectContext): string | null {
  return resolveEngineRoot(engineLookupFor(project.projectPath, project.engineAssociation, project.config.editor));
}

export interface EpicIntegrations {
  /** Null when the project's engine has no Unreal MCP server. */
  unrealMcp: UnrealMcpServerStatus | null;
  unrealAssistant: UnrealAssistantStatus;
  unrealCanvas: UnrealCanvasStatus;
}

/**
 * `bridgePort` is the port ue-mcp's bridge uses for this project, so a
 * collision with Epic's server is reported; the flow HTTP port is read from
 * the project config when that surface is on. `pinnedBridgePort` is the port
 * a pin asked for when the bridge does not use it, which is what a Canvas
 * clash is reported against.
 */
export function epicIntegrations(
  project: ProjectContext,
  bridgePort: number,
  engineRoot: string | null = projectEngineRoot(project),
  pinnedBridgePort?: number,
): EpicIntegrations {
  const inputs = { uprojectPath: project.projectPath!, projectDir: project.projectDir!, engineVersion: projectEngineVersion(project), engineRoot };
  const http = project.config.http;
  const flow = http?.enabled ? http.port ?? 7723 : undefined;
  let liveBridge: number | undefined;
  try {
    liveBridge = resolveLiveBridgeAddress(project.projectDir)?.port;
  } catch {
    liveBridge = undefined;
  }
  return {
    unrealMcp: readUnrealMcpServer(inputs, { bridge: bridgePort, flow }),
    unrealAssistant: readUnrealAssistant(inputs),
    unrealCanvas: readUnrealCanvas(inputs.uprojectPath, { bridge: pinnedBridgePort ?? bridgePort, liveBridge, flow }),
  };
}
