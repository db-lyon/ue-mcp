/**
 * Several editor processes holding one .uproject (#1150). get_status names
 * every holder and the one that answers; responses carry the answering pid
 * while more than one exists.
 */

import { editorOwnsProject, listEditorProcesses, sameProjectFile, type EditorProcess } from "./engine-observer.js";
import { isPidAlive, readBridgeInstanceRecords } from "./editor-target.js";

export interface ProjectHolder {
  pid: number;
  /** null when the pid is known only from its bridge instance record. */
  headless: boolean | null;
  /** True for the editor this session's bridge is connected to. */
  answering: boolean;
}

export interface ProjectHolders {
  count: number;
  answeringPid: number | null;
  editors: ProjectHolder[];
  warning: string;
}

/**
 * The editors holding `projectPath` open, headless ones included, or null
 * when there is at most one. Pure, so the rule is testable without a process
 * table. `recordPids` are live pids from this project's bridge instance
 * records: a process whose command line the probe could not read still holds
 * the project if it published an address in the project's own Saved dir.
 */
export function describeProjectHolders(
  processes: EditorProcess[],
  projectPath: string,
  answeringPid: number | null,
  recordPids: number[] = [],
): ProjectHolders | null {
  const byPid = new Map<number, ProjectHolder>();
  for (const proc of processes) {
    if (!editorOwnsProject(proc, projectPath)) continue;
    byPid.set(proc.pid, { pid: proc.pid, headless: proc.headless, answering: proc.pid === answeringPid });
  }
  for (const pid of recordPids) {
    if (byPid.has(pid)) continue;
    const proc = processes.find((p) => p.pid === pid);
    // A pid the probe saw with another project open reused a dead editor's pid.
    if (proc && proc.projectPath !== null) continue;
    byPid.set(pid, { pid, headless: proc ? proc.headless : null, answering: pid === answeringPid });
  }
  if (byPid.size < 2) return null;

  const editors = [...byPid.values()].sort((a, b) => a.pid - b.pid);
  const listed = editors
    .map((e) => `${e.pid}${e.headless ? " (headless)" : ""}${e.answering ? " (answering)" : ""}`)
    .join(", ");
  const answering = answeringPid !== null && byPid.has(answeringPid)
    ? `This session is connected to pid ${answeringPid}, and every call goes there.`
    : answeringPid !== null
      ? `This session is connected to pid ${answeringPid}, which is not among them.`
      : "No editor is connected, so which one a call would reach is not known.";
  return {
    count: editors.length,
    answeringPid,
    editors,
    warning:
      `${editors.length} editor processes hold ${projectPath} open: pids ${listed}. ${answering} ` +
      "A save from one can fail on a file lock held by another. Close the editors you are not driving.",
  };
}

export interface HolderProbeDeps {
  listProcesses?: () => Promise<EditorProcess[]>;
  readRecordPids?: (projectDir: string) => number[];
}

function liveRecordPids(projectDir: string): number[] {
  const pids = new Set<number>();
  for (const record of readBridgeInstanceRecords(projectDir)) {
    if (isPidAlive(record.pid)) pids.add(record.pid);
  }
  return [...pids];
}

/**
 * Detect several holders of one project, and remember the answer for
 * response attribution.
 *
 * The process probe costs seconds on Windows and get_status is polled, so it
 * runs only when the cheap signal already shows two editors: two live bridge
 * instance records, or a connected pid that differs from the only live one.
 */
export async function detectProjectHolders(
  projectDir: string | null | undefined,
  projectPath: string | null | undefined,
  answeringPid: number | null,
  deps: HolderProbeDeps = {},
): Promise<ProjectHolders | null> {
  if (!projectDir || !projectPath) return null;
  const recordPids = (deps.readRecordPids ?? liveRecordPids)(projectDir);
  const candidates = new Set(recordPids);
  if (answeringPid !== null) candidates.add(answeringPid);
  if (candidates.size < 2) {
    rememberProjectHolders(projectPath, null);
    return null;
  }
  let processes: EditorProcess[] = [];
  try {
    processes = await (deps.listProcesses ?? listEditorProcesses)();
  } catch {
    // The records alone still name the holders.
  }
  const holders = describeProjectHolders(processes, projectPath, answeringPid, recordPids);
  rememberProjectHolders(projectPath, holders);
  return holders;
}

const lastSeen = new Map<string, ProjectHolders>();

function keyOf(projectPath: string): string | undefined {
  for (const key of lastSeen.keys()) if (sameProjectFile(key, projectPath)) return key;
  return undefined;
}

/** Record the latest detection for a project; null clears it. */
export function rememberProjectHolders(projectPath: string, holders: ProjectHolders | null): void {
  const existing = keyOf(projectPath);
  if (existing !== undefined) lastSeen.delete(existing);
  if (holders) lastSeen.set(projectPath, holders);
}

/** The last detection that found several holders of this project, if any. */
export function contestedProject(projectPath: string | null | undefined): ProjectHolders | null {
  if (!projectPath) return null;
  const key = keyOf(projectPath);
  return key === undefined ? null : lastSeen.get(key) ?? null;
}
