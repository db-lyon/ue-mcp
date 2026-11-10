/**
 * Epic's Unreal MCP server and Unreal Assistant as one project has them, read
 * from files. init, doctor and project(get_status) all report through this.
 */
import type { ProjectContext } from "../../config/project.js";
import { projectEngineVersion } from "../../editor/deployer.js";
import { engineLookupFor, resolveEngineRoot } from "../../editor/engine-root.js";
import { readUnrealMcpServer, type UnrealMcpServerStatus } from "./unreal-mcp-server.js";
import { readUnrealAssistant, type UnrealAssistantStatus } from "./unreal-assistant.js";

/** The engine tree this project builds and runs with, or null when none resolves. */
export function projectEngineRoot(project: ProjectContext): string | null {
  return resolveEngineRoot(engineLookupFor(project.projectPath, project.engineAssociation, project.config.editor));
}

export interface EpicIntegrations {
  /** Null when the project's engine has no Unreal MCP server. */
  unrealMcp: UnrealMcpServerStatus | null;
  unrealAssistant: UnrealAssistantStatus;
}

/**
 * `bridgePort` is the port ue-mcp's bridge uses for this project, so a
 * collision with Epic's server is reported; the flow HTTP port is read from
 * the project config when that surface is on.
 */
export function epicIntegrations(project: ProjectContext, bridgePort: number, engineRoot: string | null = projectEngineRoot(project)): EpicIntegrations {
  const inputs = { uprojectPath: project.projectPath!, projectDir: project.projectDir!, engineVersion: projectEngineVersion(project), engineRoot };
  const http = project.config.http;
  return {
    unrealMcp: readUnrealMcpServer(inputs, { bridge: bridgePort, flow: http?.enabled ? http.port ?? 7723 : undefined }),
    unrealAssistant: readUnrealAssistant(inputs),
  };
}
