/**
 * The machine report: this computer's Unreal projects, the ue-mcp state of
 * each, and every daemon running. One read-only JSON document, versioned by
 * MACHINE_REPORT_VERSION and additive within a version, for `ue-mcp projects`
 * and for any app that manages projects machine-wide. Tokens never appear.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { isPidAlive, readBridgeInstanceRecords } from "../bridge/editor-target.js";
import { readEnv } from "../core/env.js";
import { packageVersion } from "../core/package-root.js";
import { ProjectContext } from "../config/project.js";
import { getProjectExtensionVersion, getProjectList, updateProjectList } from "../config/user-state.js";
import { daemonDir, projectKey, type DaemonDiscovery } from "../daemon/discovery.js";
import { extensionsDir } from "../daemon/extensions.js";
import { installedCopies } from "../daemon/extension-versions.js";
import { projectEngineVersion } from "../editor/deployer.js";
import { installedBridge } from "../editor/install-marker.js";

/** Version of the MachineReport shape. Bump on a breaking change; add fields freely. */
export const MACHINE_REPORT_VERSION = 1;

export interface MachineDaemon {
  key: string;
  /** The project root as the daemon recorded it (normalized: lower case, forward slashes). */
  projectRoot: string;
  /** The project's .uproject: a listed project's own, else the one in the recorded folder where the file system finds it. */
  uproject: string | null;
  pid: number;
  port: number;
  version: string;
  startedAt: string;
  discovery: string;
  log: string;
}

export interface MachineProject {
  name: string;
  uproject: string;
  dir: string;
  key: string;
  source: "recent" | "added";
  /** When Unreal last opened it, as its recent list says (YYYY.MM.DD-HH.MM.SS), else null. */
  lastOpened: string | null;
  engine: { association: string | null; version: string | null };
  /** An editor with the bridge has this project open: installs and launches wait for it to close. */
  editor: { running: boolean; pids: number[] };
  ueMcp: { setUp: boolean; bridge: { version: string; kind: "source" | "binary" | null } | null; config: boolean };
  /** Daemon extensions this project records, by name: the version it loads. */
  extensions: Record<string, string>;
  /** The plugins in the project's own Plugins folder. */
  plugins: Array<{ name: string; version: string | null }>;
  daemon: { discovery: string; log: string; running: MachineDaemon | null };
}

export interface MachineReport {
  version: typeof MACHINE_REPORT_VERSION;
  /** The ue-mcp package that wrote the report. */
  ueMcp: string;
  /** The folder of daemon discovery files and logs. */
  daemonDir: string;
  projects: MachineProject[];
  daemons: MachineDaemon[];
}

/** Where Unreal keeps each version's user config. */
export function recentProjectsDir(): string {
  const set = readEnv("recentProjectsDir");
  if (set) return set;
  if (process.platform === "win32") return path.join(process.env.LOCALAPPDATA ?? path.join(os.homedir(), "AppData", "Local"), "UnrealEngine");
  if (process.platform === "darwin") return path.join(os.homedir(), "Library", "Preferences", "Unreal Engine");
  return path.join(os.homedir(), ".config", "Epic", "UnrealEngine");
}

/** Every project in the editors' recent lists, newest open first, one entry per project. */
export function recentProjects(root = recentProjectsDir()): Array<{ uproject: string; lastOpened: string }> {
  const found: Array<{ uproject: string; lastOpened: string }> = [];
  let versions: fs.Dirent[] = [];
  try {
    versions = fs.readdirSync(root, { withFileTypes: true });
  } catch {
    return [];
  }
  for (const v of versions) {
    if (!v.isDirectory()) continue;
    const config = path.join(root, v.name, "Saved", "Config");
    let platforms: string[] = [];
    try {
      platforms = fs.readdirSync(config).filter((p) => p.endsWith("Editor"));
    } catch {
      continue;
    }
    for (const p of platforms) {
      let text = "";
      try {
        text = fs.readFileSync(path.join(config, p, "EditorSettings.ini"), "utf-8");
      } catch {
        continue;
      }
      for (const m of text.matchAll(/^RecentlyOpenedProjectFiles=\(ProjectName="([^"]+)"(?:,LastOpenTime=([0-9.\-]+))?/gm)) {
        found.push({ uproject: path.resolve(m[1]), lastOpened: m[2] ?? "" });
      }
    }
  }
  found.sort((a, b) => b.lastOpened.localeCompare(a.lastOpened));
  const seen = new Set<string>();
  return found.filter((f) => {
    const key = projectKey(path.dirname(f.uproject));
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

const inside = (child: string, parent: string): boolean => {
  const rel = path.relative(parent, child);
  return rel !== "" && !rel.startsWith("..") && !path.isAbsolute(rel);
};

/**
 * Why a found project is left out, or null: a copy made by tooling. Its
 * .uproject is gone, or it sits under the temp folder or inside a folder
 * whose name starts with a dot (tests/.shards, .git, .cache).
 */
export function excludedBecause(uproject: string, tmp = os.tmpdir()): string | null {
  if (!fs.existsSync(uproject)) return "missing";
  const real = (p: string): string => {
    try {
      return fs.realpathSync.native(p);
    } catch {
      return path.resolve(p);
    }
  };
  if (inside(real(uproject), real(tmp))) return "temp";
  const parts = path.resolve(path.dirname(uproject)).split(/[\\/]+/);
  if (parts.some((p) => p.startsWith(".") && p !== "." && p !== "..")) return "hidden folder";
  return null;
}

/** The .uproject in a folder, with the folder's own spelling (a daemon records it lower case), or null. */
function uprojectIn(recorded: string): string | null {
  try {
    const dir = fs.realpathSync.native(recorded);
    const f = fs.readdirSync(dir).find((n) => n.toLowerCase().endsWith(".uproject"));
    return f ? path.join(dir, f) : null;
  } catch {
    return null;
  }
}

/** Every daemon whose discovery file names a live process. */
export function liveDaemons(isAlive: (pid: number) => boolean = isPidAlive): MachineDaemon[] {
  const dir = daemonDir();
  let files: string[] = [];
  try {
    files = fs.readdirSync(dir).filter((f) => /^[0-9a-f]{16}\.json$/.test(f));
  } catch {
    return [];
  }
  const out: MachineDaemon[] = [];
  for (const f of files) {
    const discovery = path.join(dir, f);
    let raw: Partial<DaemonDiscovery>;
    try {
      raw = JSON.parse(fs.readFileSync(discovery, "utf-8")) as Partial<DaemonDiscovery>;
    } catch {
      continue;
    }
    if (typeof raw.pid !== "number" || typeof raw.port !== "number" || typeof raw.projectRoot !== "string" || !isAlive(raw.pid)) continue;
    const key = f.slice(0, -".json".length);
    out.push({
      key,
      projectRoot: raw.projectRoot,
      uproject: uprojectIn(raw.projectRoot),
      pid: raw.pid,
      port: raw.port,
      version: typeof raw.version === "string" ? raw.version : "0.0.0",
      startedAt: typeof raw.startedAt === "string" ? raw.startedAt : "",
      discovery,
      log: path.join(dir, `${key}.log`),
    });
  }
  return out.sort((a, b) => b.startedAt.localeCompare(a.startedAt));
}

/** The plugins directly in Plugins/ or one folder down (Plugins/Marketplace/X). */
function projectPlugins(dir: string): Array<{ name: string; version: string | null }> {
  const out: Array<{ name: string; version: string | null }> = [];
  const visit = (folder: string, depth: number): void => {
    let entries: fs.Dirent[] = [];
    try {
      entries = fs.readdirSync(folder, { withFileTypes: true });
    } catch {
      return;
    }
    const descriptor = entries.find((e) => e.isFile() && e.name.toLowerCase().endsWith(".uplugin"));
    if (descriptor) {
      let version: string | null = null;
      try {
        const v = (JSON.parse(fs.readFileSync(path.join(folder, descriptor.name), "utf-8")) as { VersionName?: unknown }).VersionName;
        version = typeof v === "string" ? v : null;
      } catch {
        // A descriptor that does not parse still names a plugin.
      }
      out.push({ name: descriptor.name.slice(0, -".uplugin".length), version });
      return;
    }
    if (depth > 0) for (const e of entries) if (e.isDirectory()) visit(path.join(folder, e.name), depth - 1);
  };
  visit(path.join(dir, "Plugins"), 2);
  return out.sort((a, b) => a.name.localeCompare(b.name));
}

function describe(uproject: string, source: "recent" | "added", lastOpened: string | null, daemons: Map<string, MachineDaemon>, extensionNames: string[], isAlive: (pid: number) => boolean): MachineProject {
  const dir = path.dirname(uproject);
  const key = projectKey(dir);
  let association: string | null = null;
  let version: string | null = null;
  try {
    const project = new ProjectContext();
    project.setProject(uproject);
    association = project.engineAssociation;
    version = projectEngineVersion(project);
  } catch {
    // An unreadable .uproject is still a project the user has; its engine is unknown.
  }
  const bridge = installedBridge(dir);
  const pids = readBridgeInstanceRecords(dir).filter((r) => r.state !== "bind-failed" && isAlive(r.pid)).map((r) => r.pid);
  const extensions: Record<string, string> = {};
  for (const name of extensionNames) {
    const v = getProjectExtensionVersion(dir, name);
    if (v) extensions[name] = v;
  }
  return {
    name: path.basename(uproject, path.extname(uproject)),
    uproject,
    dir,
    key,
    source,
    lastOpened: lastOpened || null,
    engine: { association, version },
    editor: { running: pids.length > 0, pids },
    ueMcp: {
      setUp: !!bridge,
      bridge: bridge ? { version: bridge.version, kind: bridge.kind } : null,
      config: fs.existsSync(path.join(dir, "ue-mcp.yml")),
    },
    extensions,
    plugins: projectPlugins(dir),
    daemon: {
      discovery: path.join(daemonDir(), `${key}.json`),
      log: path.join(daemonDir(), `${key}.log`),
      running: daemons.get(key) ?? null,
    },
  };
}

/** The whole report: added projects first in the order added, then the recent ones newest first. */
export function machineReport(opts: { isAlive?: (pid: number) => boolean } = {}): MachineReport {
  const isAlive = opts.isAlive ?? isPidAlive;
  const daemons = liveDaemons(isAlive);
  const byKey = new Map(daemons.map((d) => [d.key, d]));
  const list = getProjectList();
  const hidden = new Set(list.hidden.map((p) => projectKey(path.dirname(p))));
  const extensionNames = [...installedCopies(extensionsDir()).keys()].sort();
  const seen = new Set<string>();
  const projects: MachineProject[] = [];
  const add = (uproject: string, source: "recent" | "added", lastOpened: string | null): void => {
    const key = projectKey(path.dirname(uproject));
    if (seen.has(key)) return;
    seen.add(key);
    projects.push(describe(uproject, source, lastOpened, byKey, extensionNames, isAlive));
  };
  for (const uproject of list.added) if (fs.existsSync(uproject)) add(path.resolve(uproject), "added", null);
  for (const r of recentProjects()) {
    if (hidden.has(projectKey(path.dirname(r.uproject))) || excludedBecause(r.uproject)) continue;
    add(r.uproject, "recent", r.lastOpened);
  }
  // A daemon records its root lower case, which only a listed project can turn back into its real path everywhere.
  for (const p of projects) if (p.daemon.running) p.daemon.running.uproject = p.uproject;
  return { version: MACHINE_REPORT_VERSION, ueMcp: packageVersion(), daemonDir: daemonDir(), projects, daemons };
}

/** Add a project to the machine's list, or show one that was hidden. */
export function addProject(uproject: string): string {
  const file = path.resolve(uproject);
  if (!file.toLowerCase().endsWith(".uproject") || !fs.existsSync(file)) throw new Error(`${file} is not a .uproject file`);
  const key = projectKey(path.dirname(file));
  const same = (p: string): boolean => projectKey(path.dirname(p)) === key;
  updateProjectList((list) => {
    list.hidden = list.hidden.filter((p) => !same(p));
    if (!list.added.some(same)) list.added.push(file);
  });
  return file;
}

/** Take a project off the machine's list: an added one is forgotten, a found one is hidden. */
export function removeProject(uproject: string): string {
  const file = path.resolve(uproject);
  const key = projectKey(path.dirname(file));
  const same = (p: string): boolean => projectKey(path.dirname(p)) === key;
  updateProjectList((list) => {
    list.added = list.added.filter((p) => !same(p));
    if (!list.hidden.some(same)) list.hidden.push(file);
  });
  return file;
}
