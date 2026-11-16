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

import {
  BRIDGE_STATE,
  bridgeInstancesDir,
  bridgeLockfilePath,
  INSTALL_KIND,
  RECORD_FIELDS,
  recordState,
  STATE_FILES,
  type BridgeState,
} from "./wire-contract.js";

export { bridgeInstancesDir, bridgeLockfilePath, bridgeStateDir } from "./wire-contract.js";

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
  /** A bind-failed record names no reachable port. Null for a state this client does not know. */
  state: BridgeState | null;
  startedAt: string | null;
  /** Where the running bridge plugin loaded from. Null on plugins that predate the field. */
  pluginDir: string | null;
  installKind: "source" | "binary" | null;
  pluginVersion: string | null;
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

  const F = RECORD_FIELDS.instance;
  const str = (v: unknown): string | null => (typeof v === "string" && v !== "" ? v : null);
  const records: BridgeInstanceRecord[] = [];
  for (const name of names) {
    if (!name.toLowerCase().endsWith(STATE_FILES.instanceRecordSuffix)) continue;
    const recordPath = path.join(dir, name);
    try {
      const stat = fs.statSync(recordPath);
      const parsed = JSON.parse(fs.readFileSync(recordPath, "utf-8")) as Record<string, unknown>;
      const pid = parsed[F.pid];
      const port = parsed[F.port];
      if (typeof pid !== "number" || !Number.isInteger(pid) || pid <= 0) continue;
      if (typeof port !== "number" || !Number.isInteger(port) || port <= 0 || port > 65535) continue;
      const installKind = parsed[F.installKind];
      records.push({
        port,
        pid,
        instanceId: typeof parsed[F.instanceId] === "string" ? (parsed[F.instanceId] as string) : null,
        state: recordState(parsed),
        startedAt: typeof parsed[F.startedAt] === "string" ? (parsed[F.startedAt] as string) : null,
        pluginDir: str(parsed[F.pluginDir]),
        installKind: installKind === INSTALL_KIND.source || installKind === INSTALL_KIND.binary ? installKind : null,
        pluginVersion: str(parsed[F.pluginVersion]),
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
    const F = RECORD_FIELDS.portLockfile;
    const parsed = JSON.parse(fs.readFileSync(file, "utf-8")) as Record<string, unknown>;
    const port = parsed[F.port];
    const pid = parsed[F.pid];
    if (typeof port !== "number" || !Number.isInteger(port) || port <= 0 || port > 65535) {
      return null;
    }
    return {
      port,
      pid: typeof pid === "number" && pid > 0 ? pid : null,
      startedAt: typeof parsed[F.startedAt] === "string" ? (parsed[F.startedAt] as string) : null,
      writtenAtMs: stat.mtimeMs,
    };
  } catch {
    return null;
  }
}

export type BridgeAddress =
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
      /** The port.json that was there but names a process that is gone: what a crash leaves behind. */
      deadLockfile?: BridgeLockfile;
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
    if (record.state === BRIDGE_STATE.bindFailed) continue;
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
 * One liveness rule for every caller (#934, D6): the data path (connect) and
 * the lifecycle path (start, stop, restart) used to read these files with
 * different rules and got opposite answers for one editor.
 *
 *   - port.json is the first answer while the process it names is alive. A
 *     lockfile that names no pid is honoured: older plugin builds publish none,
 *     and refusing them would refuse a healthy editor for an old binary. One
 *     whose process is gone was left by a crash, and its port may since belong
 *     to anything; it is returned as `deadLockfile` on the failure, never as an
 *     address.
 *   - Without a live port.json, a live instance record for this project names
 *     a port nobody else can overwrite. Two editors of one project share one
 *     port.json, and the one that quits removes it, so the survivor is found
 *     here.
 *
 * Callers that act on a process (stop) still confirm the pid holds this
 * project; liveness says nothing about which project a process has open.
 *
 * `isAlive` is injected so the rule is testable without real pids.
 */
export function resolveBridgeTarget(
  projectDir?: string | null,
  isAlive: (pid: number) => boolean = isPidAlive,
): BridgeAddress {
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
  if (lockfile && (lockfile.pid === null || isAlive(lockfile.pid))) {
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

  if (lockfile) {
    return {
      ok: false,
      lockfilePath,
      deadLockfile: lockfile,
      reason:
        `${lockfilePath} names port ${lockfile.port} and pid ${lockfile.pid}, which is no longer running, and there is no ` +
        `live instance record under ${bridgeInstancesDir(projectDir)}. A clean exit removes port.json, so the editor that ` +
        "wrote it crashed and no editor of this project is listening now. Start it with editor(action='start_editor').",
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
