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
    }
  | {
      ok: false;
      /** The file that was checked, or null when no project is loaded. */
      lockfilePath: string | null;
      /** Names the lockfile path, so the reader can go and look at it. */
      reason: string;
    };

/**
 * The bridge endpoint belonging to `projectDir`, or a reason naming the exact
 * file that was checked. Never returns a port that this project did not
 * publish for itself.
 */
export function resolveBridgeTarget(projectDir?: string | null): BridgeTarget {
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
  if (!lockfile) {
    return {
      ok: false,
      lockfilePath,
      reason:
        `No bridge port published at ${lockfilePath}. The editor writes that file while its bridge is listening ` +
        "and removes it when it exits, so either no editor is running for this project or its bridge never started. " +
        "Lifecycle actions do not guess a port, because a guessed port reaches whichever editor happens to hold it.",
    };
  }

  return {
    ok: true,
    port: lockfile.port,
    pid: lockfile.pid,
    writtenAtMs: lockfile.writtenAtMs,
    lockfilePath,
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
