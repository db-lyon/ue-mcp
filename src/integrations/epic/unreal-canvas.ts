/**
 * Unreal Canvas (UE 5.9, Engine/Plugins/Developer/UnrealCanvas) as one project
 * has it, read from files alone, and the two TCP ports it claims on loopback.
 *
 * The Bridge plugin (UnrealCanvasBridge) is not EnabledByDefault, so it runs
 * only when the .uproject, or a project plugin's dependency list, enables it.
 * When it does, two fixed ports belong to it:
 *
 * - 9877, the default of Scripts/webcam_server.py (`--port`, default 9877),
 *   which the Canvas sidecar starts for webcam capture. 9877 is also ue-mcp's
 *   legacy fixed bridge port.
 * - 9880, the viewport streamer's fallback (UnrealCanvasViewportStreamer.h,
 *   `Start(int32 Port = 9880, ...)`), which StartViewportStream binds whenever
 *   the editor has no per-instance stream port: in developer mode and under
 *   `-ModelContextProtocolPort=` (UnrealCanvasBridgeModule.cpp, EnsureMCPServer).
 *
 * The bridge port ue-mcp picks for such a project must be neither (the C++
 * bridge applies the same rule in FMCPBridgeServer::ResolveConfiguredPort).
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { uprojectPluginState } from "./unreal-mcp-server.js";

export const UNREAL_CANVAS_BRIDGE_PLUGIN = "UnrealCanvasBridge";

/** A port the Canvas Bridge claims, and what claims it. */
export interface CanvasReservedPort {
  port: number;
  use: string;
}

/** Keep in step with kCanvasReservedPorts in BridgePortConfig.cpp. */
export const CANVAS_RESERVED_PORTS: readonly CanvasReservedPort[] = [
  { port: 9877, use: "the Canvas webcam server (webcam_server.py)" },
  { port: 9880, use: "the Canvas viewport stream" },
];

/** What claims `port` when the Canvas Bridge is enabled, or undefined when nothing does. */
export function canvasReservedPort(port: number): CanvasReservedPort | undefined {
  return CANVAS_RESERVED_PORTS.find((r) => r.port === port);
}

export interface UnrealCanvasStatus {
  /** Whether the project enables UnrealCanvasBridge. */
  bridgeEnabled: boolean;
  /** "uproject", or the project plugin whose dependency list enables it; null when off. */
  enabledBy: string | null;
  reservedPorts: readonly CanvasReservedPort[];
  /** Ports ue-mcp uses that collide with a reserved one, as sentences. */
  portConflicts: string[];
}

/** Every .uplugin under the project's Plugins folder, at most three folders deep. */
function projectPluginFiles(projectDir: string): string[] {
  const found: string[] = [];
  const walk = (dir: string, depth: number) => {
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      const full = path.join(dir, e.name);
      if (e.isFile() && e.name.toLowerCase().endsWith(".uplugin")) found.push(full);
      else if (e.isDirectory() && depth < 3 && !/^(Binaries|Intermediate|Content|Source|Resources)$/i.test(e.name)) walk(full, depth + 1);
    }
  };
  walk(path.join(projectDir, "Plugins"), 1);
  return found;
}

/** The project plugin that enables `name` through its own dependency list, or null. */
function enablingProjectPlugin(projectDir: string, name: string): string | null {
  for (const file of projectPluginFiles(projectDir)) {
    try {
      const deps = JSON.parse(fs.readFileSync(file, "utf-8").replace(/^﻿/, ""))?.Plugins;
      if (!Array.isArray(deps)) continue;
      const hit = deps.find((d: { Name?: unknown; Enabled?: unknown }) =>
        typeof d?.Name === "string" && d.Name.toLowerCase() === name.toLowerCase() && d.Enabled !== false);
      if (hit) return path.basename(file, ".uplugin");
    } catch {
      // An unreadable descriptor enables nothing.
    }
  }
  return null;
}

/**
 * Whether the project enables the Canvas Bridge: the .uproject's own entry
 * decides when it lists the plugin, otherwise an enabled project plugin that
 * depends on it does.
 */
export function canvasBridgeEnabled(uprojectPath: string): { enabled: boolean; by: string | null } {
  const listed = uprojectPluginState(uprojectPath, UNREAL_CANVAS_BRIDGE_PLUGIN);
  if (listed !== null) return { enabled: listed, by: listed ? "uproject" : null };
  const by = enablingProjectPlugin(path.dirname(uprojectPath), UNREAL_CANVAS_BRIDGE_PLUGIN);
  return { enabled: by !== null, by };
}

/**
 * The Canvas Bridge's state for a project. `uePorts` are the ports ue-mcp
 * uses for it: the bridge port it resolved (and the port a live editor
 * actually bound, when that differs) and the flow HTTP port when on.
 */
export function readUnrealCanvas(
  uprojectPath: string,
  uePorts: { bridge?: number; liveBridge?: number; flow?: number } = {},
): UnrealCanvasStatus {
  const { enabled, by } = canvasBridgeEnabled(uprojectPath);
  const portConflicts: string[] = [];
  if (enabled) {
    const seen = new Set<string>();
    const add = (port: number | undefined, what: string) => {
      const reserved = port === undefined ? undefined : canvasReservedPort(port);
      if (!reserved) return;
      const line = `${what} is ${port}, which ${reserved.use} uses`;
      if (!seen.has(line)) portConflicts.push(line);
      seen.add(line);
    };
    add(uePorts.bridge, "ue-mcp's configured bridge port for this project");
    add(uePorts.liveBridge, "the port this project's running editor bound for the ue-mcp bridge");
    add(uePorts.flow, "ue-mcp's flow HTTP port");
  }
  return { bridgeEnabled: enabled, enabledBy: by, reservedPorts: CANVAS_RESERVED_PORTS, portConflicts };
}

/**
 * The bridge port to use instead of `port` for a project with the Canvas
 * Bridge enabled, or null when `port` is free to use. A reserved pin falls
 * back to the port derived from the project path, which is in the ephemeral
 * range and never reserved.
 */
export function canvasPortReplacement(
  uprojectPath: string | null,
  port: number,
  derivedPort: number,
): { port: number; reason: string } | null {
  if (!uprojectPath) return null;
  const reserved = canvasReservedPort(port);
  if (!reserved || !canvasBridgeEnabled(uprojectPath).enabled) return null;
  return {
    port: derivedPort,
    reason: `port ${port} is used by ${reserved.use}, and this project enables ${UNREAL_CANVAS_BRIDGE_PLUGIN}, so the bridge uses the per-project port ${derivedPort} instead`,
  };
}
