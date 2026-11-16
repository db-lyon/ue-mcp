// @session per-session: Reports Epic's Unreal MCP server, Unreal Assistant and Unreal Canvas for the one ProjectContext it is handed.
/**
 * Epic's Unreal MCP server, Unreal Assistant and Unreal Canvas as one project
 * has them, read from files. init, doctor and project(get_status) all report
 * through this.
 */
import type { ProjectContext } from "../../config/project.js";
import { projectEngineVersion } from "../../editor/engine-root.js";
import { engineLookupFor, resolveEngineRoot } from "../../editor/engine-root.js";
import { resolveBridgeTarget } from "../../bridge/editor-target.js";
import { readUnrealMcpServer, type UnrealMcpServerStatus } from "./unreal-mcp-server.js";
import { readUnrealAssistant, type UnrealAssistantStatus } from "./unreal-assistant.js";
import { canvasPortReplacement, readUnrealCanvas, type UnrealCanvasStatus } from "./unreal-canvas.js";
import { deriveProjectPort } from "../../bridge/wire-contract.js";
import { resolveSetting } from "../../core/env.js";
import { FLOW_HTTP_PORT } from "../../core/flow-http-port.js";

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
  const flow = http?.enabled ? http.port ?? FLOW_HTTP_PORT : undefined;
  let liveBridge: number | undefined;
  try {
    const target = resolveBridgeTarget(project.projectDir);
    liveBridge = target.ok ? target.port : undefined;
  } catch {
    liveBridge = undefined;
  }
  return {
    unrealMcp: readUnrealMcpServer(inputs, { bridge: bridgePort, flow }),
    unrealAssistant: readUnrealAssistant(inputs),
    unrealCanvas: readUnrealCanvas(inputs.uprojectPath, { bridge: pinnedBridgePort ?? bridgePort, liveBridge, flow }),
  };
}

/** The bridge port a project uses, and what decided it. */
export interface BridgePort {
  port: number;
  /** What the project path derives, which is the port when nothing pins one. */
  derivedPort: number;
  /** "env" for UE_MCP_PORT, "config" for ue-mcp.bridge.port, null when derived. */
  pinnedBy: "env" | "config" | null;
  /**
   * A pinned port this project's Unreal Canvas Bridge claims, which the bridge
   * does not use; `port` is then the derived one (unreal-canvas.ts).
   */
  avoided?: { port: number; reason: string };
}

export function bridgePort(project: ProjectContext): BridgePort {
  const derivedPort = deriveProjectPort(project.projectDir!);
  const resolved = resolveSetting("bridgePort", { config: project.config.bridge?.port });
  const pinnedBy = resolved.source === "env" || resolved.source === "config" ? resolved.source : null;
  if (!pinnedBy) return { port: derivedPort, derivedPort, pinnedBy: null };
  const pinned = resolved.value!;
  const swap = canvasPortReplacement(project.projectPath, pinned, derivedPort);
  if (swap) return { port: swap.port, derivedPort, pinnedBy, avoided: { port: pinned, reason: swap.reason } };
  return { port: pinned, derivedPort, pinnedBy };
}

/** Epic's Unreal MCP server and Unreal Assistant for a project, counting the bridge port init gives it. */
export function projectEpicIntegrations(project: ProjectContext, engineRoot?: string | null): EpicIntegrations {
  const bridge = bridgePort(project);
  return epicIntegrations(project, bridge.port, engineRoot === undefined ? projectEngineRoot(project) : engineRoot, bridge.avoided?.port);
}
