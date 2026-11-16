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
import { inspectInstall } from "../editor/install-check.js";
import { readInstallMarker, resolvePluginDir } from "../editor/install-marker.js";
import type { StatusReport } from "../contract/daemon.js";
import { clientId, detectMcpClients, readUeMcpEntry } from "./mcp-clients/mcp-client-config.js";

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
