/**
 * Crash reporters a crashed editor of this project left behind.
 *
 * When an editor crashes on Windows, CrashReportClientEditor.exe stays up and
 * keeps the project's module binaries open, so the next UnrealBuildTool link
 * fails with LNK1104 although no editor is running. stop_editor ends the ones
 * that belong to this project, and only those: a crash reporter still serving
 * a live editor, or one tied to another project, is never touched.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { isPidAlive, readBridgeInstanceRecords, readBridgeLockfileIn } from "../bridge/editor-target.js";
import {
  findFileHolders,
  listCrashReporterProcesses,
  listEditorProcesses,
  type CrashReporterProcess,
} from "./engine-observer.js";

export interface CrashReporterMatch {
  pid: number;
  image: string;
  /** Why it was judged to belong to this project. */
  reason: string;
}

export interface CrashReporterFailure extends CrashReporterMatch {
  error: string;
}

export interface CrashReporterCleanup {
  ended: CrashReporterMatch[];
  failed: CrashReporterFailure[];
  /** Set when the process or file-holder probe could not run, so nothing was judged. */
  probeError?: string;
}

/** Everything the cleanup touches outside this module, injectable for tests. */
export interface CrashReporterDeps {
  platform: NodeJS.Platform;
  listCrashReporters: () => Promise<CrashReporterProcess[]>;
  /** Pids of every running UnrealEditor process, any project. */
  listLiveEditorPids: () => Promise<Set<number>>;
  /** Pids of the editors this project's bridge recorded, live or not. */
  listProjectEditorPids: (projectDir: string) => Set<number>;
  listModuleBinaries: (projectDir: string) => string[];
  findHolders: (files: string[]) => Promise<Map<number, string>>;
  /** End the process and resolve once it has exited; reject if it did not. */
  terminate: (pid: number) => Promise<void>;
}

/** The editor pid a crash reporter was launched to watch, from `-MONITOR=<pid>`. */
export function monitoredPid(commandLine: string): number | null {
  const m = /-MONITOR=(\d+)/i.exec(commandLine);
  return m ? Number(m[1]) : null;
}

function normalise(p: string): string {
  return p.replace(/\\/g, "/").replace(/\/+/g, "/").replace(/\/$/, "").toLowerCase();
}

/** Does this command line name a path inside `projectDir`? */
export function commandLineNamesDir(commandLine: string, projectDir: string): boolean {
  const cmd = normalise(commandLine);
  // Not path.resolve: the paths are Windows ones, and tests run on any host.
  const dir = normalise(projectDir);
  if (dir.length === 0) return false;
  for (let at = cmd.indexOf(dir); at >= 0; at = cmd.indexOf(dir, at + 1)) {
    const next = cmd[at + dir.length];
    if (next === undefined || next === "/" || /[\s"']/.test(next)) return true;
  }
  return false;
}

/**
 * Split crash reporters into the ones that belong to this project and the rest.
 * Pure, so the rule can be tested without a process table.
 *
 * A reporter watching an editor that is still running is left alone whatever
 * it names: it is serving that editor, not left over from a crash. Otherwise it
 * belongs to this project when it watched an editor this project's bridge
 * recorded, when its command line names a path inside the project directory,
 * or when it holds one of the project's binaries open.
 * `holders` may be null when that probe was not needed.
 */
export function selectProjectCrashReporters(
  reporters: CrashReporterProcess[],
  projectDir: string,
  liveEditorPids: Set<number>,
  holders: Map<number, string> | null,
  projectEditorPids: Set<number> = new Set(),
): CrashReporterMatch[] {
  const matches: CrashReporterMatch[] = [];
  for (const r of reporters) {
    const watched = monitoredPid(r.commandLine);
    if (watched !== null && liveEditorPids.has(watched)) continue;
    // The companion a crashed editor started names no project path, and may not have opened its binaries yet.
    if (watched !== null && projectEditorPids.has(watched)) {
      matches.push({ pid: r.pid, image: r.image, reason: `it watched editor pid ${watched} of this project, which has exited` });
      continue;
    }
    if (commandLineNamesDir(r.commandLine, projectDir)) {
      matches.push({ pid: r.pid, image: r.image, reason: `its command line names a path under ${projectDir}` });
      continue;
    }
    const held = holders?.get(r.pid);
    if (held) matches.push({ pid: r.pid, image: r.image, reason: `it holds ${held} open` });
  }
  return matches;
}

const SKIP_DIRS = new Set(["intermediate", "content", "source", "saved", "resources", "config", ".git", "node_modules"]);
const BINARY_EXT = /\.(dll|pdb)$/i;

/** Every module binary under the project's and its plugins' Binaries folders. */
export function listProjectModuleBinaries(projectDir: string): string[] {
  const found: string[] = [];
  const collect = (dir: string, depth: number): void => {
    if (depth > 4) return;
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      const full = path.join(dir, e.name);
      if (e.isDirectory()) collect(full, depth + 1);
      else if (BINARY_EXT.test(e.name)) found.push(full);
    }
  };
  const walk = (dir: string, depth: number): void => {
    if (depth > 6) return;
    let entries: fs.Dirent[];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      if (!e.isDirectory()) continue;
      const lower = e.name.toLowerCase();
      const full = path.join(dir, e.name);
      if (lower === "binaries") collect(full, 0);
      else if (!SKIP_DIRS.has(lower)) walk(full, depth + 1);
    }
  };
  collect(path.join(projectDir, "Binaries"), 0);
  walk(path.join(projectDir, "Plugins"), 0);
  return found;
}

/** The editor pids this project's bridge published in port.json and its instance records. */
export function listRecordedEditorPids(projectDir: string): Set<number> {
  const pids = new Set(readBridgeInstanceRecords(projectDir).map((r) => r.pid));
  const lockfilePid = readBridgeLockfileIn(projectDir)?.pid;
  if (lockfilePid) pids.add(lockfilePid);
  return pids;
}

/** End a process and wait for it to leave the process table. */
async function terminateAndWait(pid: number): Promise<void> {
  // lint-prose-allow: no-kill  only a crash reporter of this project whose editor is gone, never an editor
  process.kill(pid);
  for (let i = 0; i < 30; i++) {
    if (!isPidAlive(pid)) return;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`pid ${pid} was still running 3s after it was ended`);
}

const defaultDeps: CrashReporterDeps = {
  platform: process.platform,
  listCrashReporters: listCrashReporterProcesses,
  listLiveEditorPids: async () => new Set((await listEditorProcesses()).map((p) => p.pid)),
  listProjectEditorPids: listRecordedEditorPids,
  listModuleBinaries: listProjectModuleBinaries,
  findHolders: findFileHolders,
  terminate: terminateAndWait,
};

/**
 * Find and end every crash reporter that belongs to this project.
 *
 * Never throws: a stop must not turn into an error because this could not look.
 * A probe that failed is reported under `probeError` rather than as "none".
 */
export async function endProjectCrashReporters(
  projectDir: string | null | undefined,
  overrides: Partial<CrashReporterDeps> = {},
): Promise<CrashReporterCleanup> {
  const deps = { ...defaultDeps, ...overrides };
  const result: CrashReporterCleanup = { ended: [], failed: [] };
  if (deps.platform !== "win32" || !projectDir) return result;

  let matches: CrashReporterMatch[];
  try {
    const reporters = await deps.listCrashReporters();
    if (reporters.length === 0) return result;
    const live = await deps.listLiveEditorPids();
    const recorded = deps.listProjectEditorPids(projectDir);
    const byCommandLine = selectProjectCrashReporters(reporters, projectDir, live, null, recorded);
    // The file-holder probe is the expensive one, so it runs only for
    // reporters the command line did not already settle.
    const undecided = reporters.filter((r) => {
      const watched = monitoredPid(r.commandLine);
      return !byCommandLine.some((m) => m.pid === r.pid) && !(watched !== null && live.has(watched));
    });
    const holders = undecided.length > 0 ? await deps.findHolders(deps.listModuleBinaries(projectDir)) : null;
    matches = selectProjectCrashReporters(reporters, projectDir, live, holders, recorded);
  } catch (err) {
    return { ...result, probeError: err instanceof Error ? err.message : String(err) };
  }

  for (const m of matches) {
    try {
      await deps.terminate(m.pid);
      result.ended.push(m);
    } catch (err) {
      result.failed.push({ ...m, error: err instanceof Error ? err.message : String(err) });
    }
  }
  return result;
}

/** One sentence for a stop's message, or "" when there is nothing to say. */
export function describeCrashReporterCleanup(cleanup: CrashReporterCleanup): string {
  const parts: string[] = [];
  if (cleanup.ended.length > 0) {
    parts.push(
      `Ended ${cleanup.ended.length} crash reporter${cleanup.ended.length === 1 ? "" : "s"} a crashed editor of this ` +
        `project left running, which would have kept its binaries locked against the next build: ${
          cleanup.ended.map((m) => `${m.image} pid ${m.pid} (${m.reason})`).join("; ")}.`,
    );
  }
  if (cleanup.failed.length > 0) {
    parts.push(
      `Could not end ${cleanup.failed.map((m) => `${m.image} pid ${m.pid} (${m.reason}): ${m.error}`).join("; ")}. ` +
        "Close its window before building, or the link fails with LNK1104.",
    );
  }
  if (cleanup.probeError) {
    parts.push(`Could not check for a leftover crash reporter: ${cleanup.probeError}.`);
  }
  return parts.join(" ");
}

/** Whether a cleanup has anything worth putting in a result. */
export function crashReporterCleanupIsEmpty(cleanup: CrashReporterCleanup): boolean {
  return cleanup.ended.length === 0 && cleanup.failed.length === 0 && !cleanup.probeError;
}
