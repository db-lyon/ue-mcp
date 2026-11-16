// @session per-session: Reports the install and editor state of the one project it is handed.
/**
 * The install and editor state of one project, read-only: what `ue-mcp status`
 * prints and what the daemon serves to the in-editor UI and the installer.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { ProjectContext } from "../config/project.js";
import { packageVersion } from "../core/package-root.js";
import { isPidAlive, readBridgeInstanceRecords } from "../bridge/editor-target.js";
import { projectEngineVersion } from "../editor/engine-root.js";
import { inspectInstall, type InstallReport } from "../editor/install-check.js";
import { readInstallMarker, resolvePluginDir, type InstallMarker } from "../editor/install-marker.js";
import { clientId, detectMcpClients, readUeMcpEntry } from "./mcp-clients/mcp-client-config.js";

export interface ClientStatus {
  id: string;
  name: string;
  configPath: string;
  detected: boolean;
  /** The config has a ue-mcp entry. */
  configured: boolean;
  /** The exact version the entry pins, or null for @latest or no entry. */
  pinned: string | null;
}

export interface StatusReport {
  version: string;
  node: { version: string; execPath: string };
  project: { path: string; name: string; engine: string | null; engineAssociation: string | null };
  plugin: InstallReport["bridge"] & { dirSource: "instance-record" | "default" | "scan" | null; marker: InstallMarker | null };
  editor: {
    running: boolean;
    instances: Array<{ pid: number; port: number; state: string | null; alive: boolean; startedAt: string | null }>;
  };
  toolchain: InstallReport["toolchain"];
  engine: InstallReport["engine"];
  config: { path: string; exists: boolean };
  clients: ClientStatus[];
  ok: boolean;
  problems: InstallReport["problems"];
  nextSteps: string[];
}

export function collectStatus(uprojectPath: string, isAlive: (pid: number) => boolean = isPidAlive): StatusReport {
  const project = new ProjectContext();
  project.setProject(uprojectPath);
  const projectDir = project.projectDir!;
  const report = inspectInstall(project.projectPath!);
  const located = resolvePluginDir(projectDir, isAlive);

  const instances = readBridgeInstanceRecords(projectDir).map((r) => ({
    pid: r.pid,
    port: r.port,
    state: r.state,
    alive: isAlive(r.pid),
    startedAt: r.startedAt,
  }));

  const configPath = path.join(projectDir, "ue-mcp.yml");
  return {
    version: packageVersion(),
    node: { version: process.version, execPath: process.execPath },
    project: {
      path: project.projectPath!,
      name: project.projectName!,
      engine: projectEngineVersion(project),
      engineAssociation: project.engineAssociation,
    },
    plugin: {
      ...report.bridge,
      dirSource: located?.source ?? null,
      marker: located ? readInstallMarker(located.dir) : null,
    },
    editor: { running: instances.some((i) => i.alive && i.state !== "bind-failed"), instances },
    toolchain: report.toolchain,
    engine: report.engine,
    config: { path: configPath, exists: fs.existsSync(configPath) },
    clients: detectMcpClients(projectDir).map((c) => ({
      id: clientId(c.name),
      name: c.name,
      configPath: c.configPath,
      detected: c.detected,
      ...readUeMcpEntry(c),
    })),
    ok: report.ok,
    problems: report.problems,
    nextSteps: report.nextSteps,
  };
}
