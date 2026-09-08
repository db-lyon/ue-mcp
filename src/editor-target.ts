/**
 * Which editor a lifecycle action is allowed to talk to.
 *
 * Every other tool call goes to whatever bridge the client is connected to.
 * Lifecycle actions are different: start, stop and restart act on a process,
 * and a request that lands on the wrong one closes somebody's other editor.
 * So they resolve their target from one place only - the port lockfile the
 * bridge publishes inside the project's own `Saved/UE_MCP_Bridge/` directory.
 *
 * There is deliberately no fallback port here (#819). The previous resolver
 * fell through to `UE_MCP_PORT` and then to the legacy fixed 9877, so a stop
 * aimed at a project whose lockfile was gone probed 9877, found whichever
 * editor happened to be pinned there, and shut that one down instead. A port
 * nobody published is not evidence about this project, and no amount of it
 * adds up to a safe target.
 *
 * The lockfile is a strictly better signal than any guess: the bridge writes it
 * from the editor whose project directory this is, whatever port it ended up
 * binding (derived, probed upward after a collision, or pinned), and removes it
 * on a clean exit.
 */

import * as fs from "node:fs";
import * as path from "node:path";

/** The path the bridge publishes its bound port to, for one project root. */
export function bridgeLockfilePath(projectDir: string): string {
  return path.join(projectDir, "Saved", "UE_MCP_Bridge", "port.json");
}

/** Where every live bridge publishes its own address, one file per process. */
export function bridgeInstancesDir(projectDir: string): string {
  return path.join(projectDir, "Saved", "UE_MCP_Bridge", "instances");
}

/**
 * One editor's own published address, from `instances/<pid>.json`.
 *
 * Unlike port.json this file cannot be taken away by anybody else: the process
 * that wrote it is the only one that ever deletes it, and its name is that
 * process's pid. That is what makes it the recovery path when port.json is
 * gone (#934).
 */
export interface BridgeInstanceRecord {
  port: number;
  pid: number;
  instanceId: string | null;
  /** "listening" or "bind-failed". A bind-failed record names no reachable port. */
  state: string | null;
  startedAt: string | null;
  recordPath: string;
  writtenAtMs: number;
}

/**
 * Every readable instance record for this project, newest first.
 *
 * Records are read, not trusted: the caller decides which pids are still alive.
 * A record whose process is gone is exactly the artefact that outlives a crash,
 * and treating it as an address would resurrect the bug the lockfile pid check
 * exists to prevent (#819).
 */
export function readBridgeInstanceRecords(projectDir: string): BridgeInstanceRecord[] {
  const dir = bridgeInstancesDir(projectDir);
  let names: string[];
  try {
    names = fs.readdirSync(dir);
  } catch {
    return [];
  }

  const records: BridgeInstanceRecord[] = [];
  for (const name of names) {
    if (!name.toLowerCase().endsWith(".json")) continue;
    const recordPath = path.join(dir, name);
    try {
      const stat = fs.statSync(recordPath);
      const parsed = JSON.parse(fs.readFileSync(recordPath, "utf-8")) as Record<string, unknown>;
      const pid = parsed.pid;
      const port = parsed.port;
      if (typeof pid !== "number" || !Number.isInteger(pid) || pid <= 0) continue;
      if (typeof port !== "number" || !Number.isInteger(port) || port <= 0 || port > 65535) continue;
      records.push({
        port,
        pid,
        instanceId: typeof parsed.instanceId === "string" ? parsed.instanceId : null,
        state: typeof parsed.state === "string" ? parsed.state : null,
        startedAt: typeof parsed.startedAt === "string" ? parsed.startedAt : null,
        recordPath,
        writtenAtMs: stat.mtimeMs,
      });
    } catch {
      // A record being written right now, or one from a build that wrote a
      // different shape. Neither is a reason to discard the others.
    }
  }
  return records.sort((a, b) => b.writtenAtMs - a.writtenAtMs);
}

export interface BridgeLockfile {
  port: number;
  /** The editor process that bound the port, or null on older plugin builds. */
  pid: number | null;
  startedAt: string | null;
  /** File mtime, used to tell this session's lockfile from a crashed one's. */
  writtenAtMs: number;
}

/** Read a project's bridge lockfile, or null when it is absent or malformed. */
export function readBridgeLockfileIn(projectDir: string): BridgeLockfile | null {
  const file = bridgeLockfilePath(projectDir);
  try {
    const stat = fs.statSync(file);
    const parsed = JSON.parse(fs.readFileSync(file, "utf-8")) as {
      port?: unknown;
      pid?: unknown;
      startedAt?: unknown;
    };
    if (typeof parsed.port !== "number" || !Number.isInteger(parsed.port) || parsed.port <= 0 || parsed.port > 65535) {
      return null;
    }
    return {
      port: parsed.port,
      pid: typeof parsed.pid === "number" && parsed.pid > 0 ? parsed.pid : null,
      startedAt: typeof parsed.startedAt === "string" ? parsed.startedAt : null,
      writtenAtMs: stat.mtimeMs,
    };
  } catch {
    return null;
  }
}

export type BridgeTarget =
  | {
      ok: true;
      port: number;
      pid: number | null;
      writtenAtMs: number;
      lockfilePath: string;
      /** Which published file this address came from. */
      source: "port.json" | "instance-record";
      /** The instance record used, when the shared lockfile could not answer. */
      recordPath?: string;
    }
  | {
      ok: false;
      /** The file that was checked, or null when no project is loaded. */
      lockfilePath: string | null;
      /** Names the lockfile path, so the reader can go and look at it. */
      reason: string;
    };

/**
 * A live editor's own published address for this project, newest first.
 *
 * #934: two editors of one project share a single `Saved/UE_MCP_Bridge/`, and
 * the one that quits removes port.json on its way out. Only the instance that
 * wrote it ever deletes it, so the survivor is left with no shared lockfile at
 * all even though it is listening perfectly well, and recovery used to mean
 * hand-writing port.json back. Its own `instances/<pid>.json` was on disk the
 * whole time and names its port, so recovery is a directory read.
 *
 * `alive` is injected so the fallback is testable without real pids.
 */
export function findLiveInstanceRecord(
  projectDir: string,
  alive: (pid: number) => boolean = isPidAlive,
): BridgeInstanceRecord | null {
  for (const record of readBridgeInstanceRecords(projectDir)) {
    // A bind-failed record exists to explain a failure, not to be dialled.
    if (record.state === "bind-failed") continue;
    if (!alive(record.pid)) continue;
    return record;
  }
  return null;
}

/**
 * The bridge endpoint belonging to `projectDir`, or a reason naming the exact
 * file that was checked. Never returns a port that this project did not
 * publish for itself.
 *
 * The shared lockfile is still the first answer, and callers still check the
 * pid it names for themselves. When there is no lockfile at all, a live
 * instance record for this project is a strictly better answer than a refusal:
 * the editor published it, for this project, and it names a port nobody else
 * can overwrite (#934).
 *
 * `isAlive` is injected so the fallback is testable without real pids.
 */
export function resolveBridgeTarget(
  projectDir?: string | null,
  isAlive: (pid: number) => boolean = isPidAlive,
): BridgeTarget {
  if (!projectDir) {
    return {
      ok: false,
      lockfilePath: null,
      reason:
        "No project is loaded, so there is no bridge lockfile to read and no editor this action could be about. " +
        "Load one with project(action='set_project').",
    };
  }

  const lockfilePath = bridgeLockfilePath(projectDir);
  const lockfile = readBridgeLockfileIn(projectDir);
  if (lockfile) {
    return {
      ok: true,
      port: lockfile.port,
      pid: lockfile.pid,
      writtenAtMs: lockfile.writtenAtMs,
      lockfilePath,
      source: "port.json",
    };
  }

  const record = findLiveInstanceRecord(projectDir, isAlive);
  if (record) {
    return {
      ok: true,
      port: record.port,
      pid: record.pid,
      writtenAtMs: record.writtenAtMs,
      lockfilePath,
      source: "instance-record",
      recordPath: record.recordPath,
    };
  }

  return {
    ok: false,
    lockfilePath,
    reason:
      `No bridge port published at ${lockfilePath}, and no live instance record under ${bridgeInstancesDir(projectDir)}. ` +
      "The editor writes those files while its bridge is listening and removes them when it exits, so either no " +
      "editor is running for this project or its bridge never started. " +
      "Lifecycle actions do not guess a port, because a guessed port reaches whichever editor happens to hold it.",
  };
}

/** A published bridge address that a live process is standing behind. */
export interface LiveBridgeAddress {
  port: number;
  /** The editor process holding it, or null on plugin builds that publish none. */
  pid: number | null;
  source: "port.json" | "instance-record";
  /** Set when port.json could not answer and an instance record did. */
  recordPath?: string;
}

/**
 * The port a LIVE editor of this project published (#934, D6).
 *
 * The data path (EditorBridge.connect) and the lifecycle path (stop / restart)
 * used to disagree about this in both directions, and both directions hurt:
 *
 *   - connect() took port.json on the strength of the port number alone, so a
 *     record a crashed editor left behind cleared the #818 pin refusal and the
 *     client dialled a port that may since belong to something else.
 *   - connect() had no instances/<pid>.json fallback, so with two editors of
 *     one project the one that quits deletes the shared port.json and the
 *     survivor becomes invisible to ordinary tool calls while editor(stop_editor)
 *     resolves it perfectly well. Two subsystems, one editor, opposite answers.
 *
 * A lockfile that names NO pid is still honoured: older plugin builds publish
 * none, and refusing them would refuse a healthy editor for having an old
 * binary. The pid check only ever discards a record whose named process is
 * demonstrably gone.
 *
 * `isAlive` is injected so this is testable without real pids.
 */
export function resolveLiveBridgeAddress(
  projectDir?: string | null,
  isAlive: (pid: number) => boolean = isPidAlive,
): LiveBridgeAddress | null {
  if (!projectDir) return null;
  const lockfile = readBridgeLockfileIn(projectDir);
  if (lockfile && (lockfile.pid === null || isAlive(lockfile.pid))) {
    return { port: lockfile.port, pid: lockfile.pid, source: "port.json" };
  }
  const record = findLiveInstanceRecord(projectDir, isAlive);
  if (record) {
    return { port: record.port, pid: record.pid, source: "instance-record", recordPath: record.recordPath };
  }
  return null;
}

/**
 * Filesystem timestamps can be coarse (2s on FAT), and the lockfile is written
 * moments after launch, so allow a little slack when deciding whether it
 * belongs to this session.
 */
const LOCKFILE_FRESHNESS_SLACK_MS = 2000;

/**
 * Is this lockfile from the launch that started at `notBeforeMs`?
 *
 * A crash leaves the lockfile behind, and its port can be reused by an
 * unrelated process later, so a wait that trusts an old lockfile can call an
 * editor ready when what answered was somebody else's.
 */
export function lockfileIsFromThisLaunch(writtenAtMs: number, notBeforeMs?: number): boolean {
  if (notBeforeMs === undefined) return true;
  return writtenAtMs >= notBeforeMs - LOCKFILE_FRESHNESS_SLACK_MS;
}

/**
 * Is this PID still around? Signal 0 delivers nothing and costs a syscall,
 * which is what makes it usable on a path that must not pay for a process
 * table query. EPERM means the process exists and is somebody else's.
 *
 * Liveness only: it says nothing about which project the process holds. Use it
 * to discard a lockfile a dead editor left behind, never to decide that a live
 * one is the right target.
 */
export function isPidAlive(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch (err) {
    return (err as NodeJS.ErrnoException)?.code === "EPERM";
  }
}
