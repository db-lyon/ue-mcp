/**
 * `ue-mcp status [project] [--json]` - the install and editor state of one
 * project, read-only. The in-editor UI and the installer read the JSON form to
 * decide what is missing; nothing here writes.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { ProjectContext } from "../config/project.js";
import { findUProject } from "../config/uproject-path.js";
import { packageVersion } from "../core/package-root.js";
import { isPidAlive, readBridgeInstanceRecords } from "../bridge/editor-target.js";
import { projectEngineVersion } from "../editor/deployer.js";
import { inspectInstall, type InstallReport } from "../editor/install-check.js";
import { readInstallMarker, resolvePluginDir, type InstallMarker } from "../editor/install-marker.js";
import { detectMcpClients, readUeMcpEntry } from "../integrations/claude-code/mcp-client-config.js";
import { clientId } from "./init-core.js";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import { BOLD, DIM, RESET, fail, ok, warn } from "./ui/ansi.js";

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

function printStatus(s: StatusReport): void {
  console.log("");
  console.log(`  ${BOLD}${s.project.name}${RESET}  ${DIM}UE ${s.project.engine ?? "?"}  ue-mcp ${s.version}${RESET}`);
  const p = s.plugin;
  if (p.deployed) ok(`bridge ${p.installedVersion ?? "?"} (${p.installKind ?? "unknown"})  ${DIM}${p.pluginDir}${RESET}`);
  else warn("bridge not installed");
  if (s.editor.running) ok(`editor running (${s.editor.instances.filter((i) => i.alive).map((i) => `pid ${i.pid} port ${i.port}`).join(", ")})`);
  else console.log(`  ${DIM}editor not running${RESET}`);
  for (const c of s.clients.filter((x) => x.configured)) {
    ok(`${c.name}: ${c.pinned ? `ue-mcp@${c.pinned}` : "ue-mcp@latest"}  ${DIM}${c.configPath}${RESET}`);
  }
  for (const problem of s.problems) fail(`${problem.what} ${problem.fix}`);
  console.log("");
}

/** Entry point for `ue-mcp status [project] [--json] [--editor <name-or-path>]`. */
export async function run(argv: string[]): Promise<number | void> {
  const json = argv.includes("--json");
  const out = (value: unknown): void => {
    process.stdout.write(JSON.stringify(value, null, 2) + "\n");
  };
  try {
    const target = takeEditorTarget(argv);
    const named = target.projectPath ?? target.rest.find((a) => !a.startsWith("-"));
    const uproject = named ?? findUProject(process.cwd());
    if (!uproject) {
      const message = "No .uproject found. Run from your project directory or pass the path.";
      if (json) out({ ok: false, error: message });
      else fail(message);
      return 2;
    }
    const status = collectStatus(uproject);
    if (json) out(status);
    else printStatus(status);
    return 0;
  } catch (e) {
    const message = e instanceof EditorFlagError || e instanceof Error ? e.message : String(e);
    if (json) out({ ok: false, error: message });
    else fail(message);
    return 1;
  }
}
